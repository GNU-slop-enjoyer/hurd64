/* LKL host side for GNU/Hurd: one PCI device, its interrupt, and DMA.
 *
 * LKL (Linux as a library) runs Linux drivers in this process. On Linux
 * hosts it reaches the hardware through VFIO; here it goes through
 * libpciaccess (config space, BARs) and GNU Mach (interrupts, physical
 * memory):
 *
 *  - kernel RAM is one physically contiguous wired block, so a DMA address
 *    is the block's physical start plus the offset into it. LKL maps that
 *    RAM where it wants it through the "mem" device.
 *  - BARs are mapped straight into the address space; ioremap() of a BAR
 *    returns the mapping and readl()/writel() become plain loads/stores.
 *  - INTx arrives as a Mach message (device_intr_register); it is passed to
 *    LKL and the line stays masked until the device stops asserting it.
 *    GNU Mach has no MSI.
 *
 * The device is chosen with "lkl_pci=BUS:DEV.FN" (or DOMAIN:BUS:DEV.FN) on
 * the LKL command line.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <hurd.h>
#include <mach.h>
#include <mach/gnumach.h>
#include <device/device.h>
#include <device/intr.h>
#include <pciaccess.h>

#include <lkl_host.h>
#include "iomem.h"

#define PCI_COMMAND		0x04
#define PCI_COMMAND_INTX_DISABLE 0x400
#define PCI_STATUS		0x06
#define PCI_STATUS_INTERRUPT	0x08
#define PCI_INTERRUPT_LINE	0x3c

static mach_port_t host_priv, master_device, mem_dev;

/* kernel RAM: virtual start (where it was first allocated), physical start */
static vm_address_t ram_va;
static uint64_t ram_pa;
static unsigned long ram_size;
/* where LKL put it (shmem_mmap at offset 0) */
static uintptr_t ram_lkl;

/* BARs mapped straight in; everything else goes through LKL's iomem table */
#define MAX_DIRECT 8
static struct { uintptr_t start; size_t size; } direct[MAX_DIRECT];
static int ndirect;

struct lkl_pci_dev {
	struct pci_device *pdev;
	int irq;		/* LKL's number */
	int line;		/* the hardware line */
	mach_port_t port;
	pthread_t thread;
};

static int privileged(void)
{
	if (host_priv != MACH_PORT_NULL)
		return 0;
	if (get_privileged_ports(&host_priv, &master_device))
		return -1;
	if (device_open(master_device, D_READ | D_WRITE, "mem", &mem_dev))
		return -1;
	return 0;
}

/* map physical memory at addr (fixed) or anywhere (addr == 0) */
static void *map_phys(void *addr, uint64_t pa, size_t size, vm_prot_t prot)
{
	mach_port_t pager;
	vm_address_t va = (vm_address_t)addr;

	if (device_map(mem_dev, prot, pa, size, &pager, 0))
		return NULL;
	kern_return_t kr = vm_map(mach_task_self(), &va, size, 0, addr == NULL,
				  pager, 0, 0, prot, prot, VM_INHERIT_NONE);
	mach_port_deallocate(mach_task_self(), pager);
	return kr ? NULL : (void *)va;
}

/* LKL kernel RAM ("shared memory" in LKL's MMU mode) */

static void hurd_shmem_init(unsigned long size)
{
	rpc_phys_addr_t pa;

	if (privileged()) {
		lkl_printf("lkl_hurd: no privileged ports (run as root)\n");
		abort();
	}
	if (vm_allocate_contiguous(host_priv, mach_task_self(), &ram_va, &pa,
				   size, 0, ~(rpc_phys_addr_t)0, 4096)) {
		lkl_printf("lkl_hurd: cannot get %lu MiB of contiguous memory\n",
			   size >> 20);
		abort();
	}
	ram_pa = pa;
	ram_size = size;
}

static void *hurd_shmem_mmap(void *addr, unsigned long pg_off, unsigned long size,
		      enum lkl_prot prot)
{
	vm_prot_t p = 0;

	if (pg_off + size > ram_size)
		return NULL;
	if (prot & LKL_PROT_READ)
		p |= VM_PROT_READ;
	if (prot & LKL_PROT_WRITE)
		p |= VM_PROT_WRITE;
	if (prot & LKL_PROT_EXEC)
		p |= VM_PROT_EXECUTE;
	void *r = map_phys(addr, ram_pa + pg_off, size, p);
	if (r && pg_off == 0 && !ram_lkl)
		ram_lkl = (uintptr_t)r;
	return r;
}

/* I/O memory */

static int is_direct(const volatile void *addr, size_t len)
{
	uintptr_t a = (uintptr_t)addr;

	for (int i = 0; i < ndirect; i++)
		if (a >= direct[i].start && a - direct[i].start + len <= direct[i].size)
			return 1;
	return 0;
}

static void *hurd_ioremap(long addr, int size)
{
	if (is_direct((void *)addr, size))
		return (void *)addr;
	return lkl_ioremap(addr, size);
}

static int hurd_iomem_access(const volatile void *addr, void *val, int size, int write)
{
	if (!is_direct(addr, size))
		return lkl_iomem_access(addr, val, size, write);
	switch (size) {
	case 1:
		if (write) *(volatile uint8_t *)addr = *(uint8_t *)val;
		else *(uint8_t *)val = *(volatile uint8_t *)addr;
		break;
	case 2:
		if (write) *(volatile uint16_t *)addr = *(uint16_t *)val;
		else *(uint16_t *)val = *(volatile uint16_t *)addr;
		break;
	case 4:
		if (write) *(volatile uint32_t *)addr = *(uint32_t *)val;
		else *(uint32_t *)val = *(volatile uint32_t *)addr;
		break;
	case 8:
		if (write) *(volatile uint64_t *)addr = *(uint64_t *)val;
		else *(uint64_t *)val = *(volatile uint64_t *)addr;
		break;
	default:
		return -1;
	}
	return 0;
}

/* PCI */

static struct lkl_pci_dev *hurd_pci_add(const char *name, void *kernel_ram,
					unsigned long size)
{
	unsigned dom = 0, bus, dev, fn;
	struct lkl_pci_dev *d;

	if (sscanf(name, "%x:%x:%x.%x", &dom, &bus, &dev, &fn) != 4) {
		dom = 0;
		if (sscanf(name, "%x:%x.%x", &bus, &dev, &fn) != 3) {
			lkl_printf("lkl_hurd: bad PCI address '%s'\n", name);
			return NULL;
		}
	}
	if (privileged() || pci_system_init()) {
		lkl_printf("lkl_hurd: no PCI access\n");
		return NULL;
	}
	struct pci_device *pdev = pci_device_find_by_slot(dom, bus, dev, fn);
	if (!pdev || pci_device_probe(pdev)) {
		lkl_printf("lkl_hurd: no device at %s\n", name);
		return NULL;
	}
	d = calloc(1, sizeof *d);
	if (!d)
		return NULL;
	d->pdev = pdev;
	d->port = MACH_PORT_NULL;
	lkl_printf("lkl_hurd: %s is %04x:%04x, kernel RAM at physical 0x%llx\n",
		   name, pdev->vendor_id, pdev->device_id, (unsigned long long)ram_pa);
	return d;
}

static int hurd_pci_read(struct lkl_pci_dev *d, int where, int size, void *val)
{
	pciaddr_t got = 0;

	if (pci_device_cfg_read(d->pdev, val, where, size, &got))
		return -1;
	return got;
}

static int hurd_pci_write(struct lkl_pci_dev *d, int where, int size, void *val)
{
	pciaddr_t got = 0;

	if (pci_device_cfg_write(d->pdev, val, where, size, &got))
		return -1;
	return got;
}

static void *hurd_resource_alloc(struct lkl_pci_dev *d, unsigned long size,
				 int idx)
{
	void *p = NULL;

	if (idx < 0 || idx >= 6 || ndirect == MAX_DIRECT)
		return NULL;
	struct pci_mem_region *r = &d->pdev->regions[idx];
	if (r->is_IO || !r->size || size > r->size)
		return NULL;
	if (pci_device_map_range(d->pdev, r->base_addr, size,
				 PCI_DEV_MAP_FLAG_WRITABLE, &p))
		return NULL;
	direct[ndirect].start = (uintptr_t)p;
	direct[ndirect].size = size;
	ndirect++;
	lkl_printf("lkl_hurd: BAR%d 0x%llx (%lu KiB) mapped\n", idx,
		   (unsigned long long)r->base_addr, size >> 10);
	return p;
}

static unsigned long long hurd_map_page(struct lkl_pci_dev *d, void *vaddr,
					unsigned long size)
{
	uintptr_t v = (uintptr_t)vaddr;

	if (ram_lkl && v >= ram_lkl && v - ram_lkl + size <= ram_size)
		return ram_pa + (v - ram_lkl);
	if (v >= ram_va && v - ram_va + size <= ram_size)
		return ram_pa + (v - ram_va);
	lkl_printf("lkl_hurd: DMA from %p is outside kernel RAM\n", vaddr);
	return 0;
}

static void hurd_unmap_page(struct lkl_pci_dev *d, unsigned long long dma,
			    unsigned long size)
{
}

static int intx_pending(struct lkl_pci_dev *d)
{
	uint16_t status = 0;

	hurd_pci_read(d, PCI_STATUS, 2, &status);
	return status & PCI_STATUS_INTERRUPT;
}

static int intr_server(mach_msg_header_t *in, mach_msg_header_t *out);
static struct lkl_pci_dev *the_dev;	/* LKL's PCI root bus holds one device */

static void *intr_thread(void *arg)
{
	struct lkl_pci_dev *d = arg;

	/* this thread only waits; LKL runs the handler on its own threads */
	mach_msg_server(intr_server, 0, d->port);
	return NULL;
}

static int intr_server(mach_msg_header_t *in, mach_msg_header_t *out)
{
	mach_intr_notification_t *n = (mach_intr_notification_t *)in;
	struct lkl_pci_dev *d = the_dev;
	const struct timespec ms = { 0, 1000 * 1000 };

	((mig_reply_header_t *)out)->RetCode = MIG_NO_REPLY;
	if (in->msgh_id != MACH_INTR_NOTIFY || n->line != d->line)
		return 0;
	/* level triggered: keep handing it to Linux until the device lets go
	   of the line, then unmask it */
	do {
		lkl_trigger_irq(d->irq);
		nanosleep(&ms, NULL);
	} while (intx_pending(d));
	device_intr_enable(master_device, d->line, TRUE);
	return 1;
}

static int hurd_irq_init(struct lkl_pci_dev *d, int irq)
{
	uint8_t line = 0xff;
	uint16_t cmd;

	hurd_pci_read(d, PCI_INTERRUPT_LINE, 1, &line);
	if (line == 0 || line == 0xff) {
		lkl_printf("lkl_hurd: device has no interrupt line\n");
		return -1;
	}
	d->irq = irq;
	d->line = line;
	the_dev = d;
	if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &d->port))
		return -1;
	if (device_intr_register(master_device, line, 0, 0x04000000, d->port,
				 MACH_MSG_TYPE_MAKE_SEND)) {
		lkl_printf("lkl_hurd: cannot register irq %d\n", line);
		return -1;
	}
	/* make sure INTx is not disabled at the device */
	hurd_pci_read(d, PCI_COMMAND, 2, &cmd);
	if (cmd & PCI_COMMAND_INTX_DISABLE) {
		cmd &= ~PCI_COMMAND_INTX_DISABLE;
		hurd_pci_write(d, PCI_COMMAND, 2, &cmd);
	}
	if (pthread_create(&d->thread, NULL, intr_thread, d))
		return -1;
	device_intr_enable(master_device, line, TRUE);
	lkl_printf("lkl_hurd: irq %d -> LKL irq %d\n", line, irq);
	return 0;
}

static int hurd_msi_init(struct lkl_pci_dev *d, int type, int nvec, int *irqs)
{
	return -1;
}

static void hurd_msi_teardown(struct lkl_pci_dev *d, int type)
{
}

static void hurd_pci_remove(struct lkl_pci_dev *d)
{
	/* the interrupt thread and mappings live as long as the process */
}

static struct lkl_dev_pci_ops hurd_pci_ops = {
	.add = hurd_pci_add,
	.remove = hurd_pci_remove,
	.irq_init = hurd_irq_init,
	.msi_init = hurd_msi_init,
	.msi_teardown = hurd_msi_teardown,
	.read = hurd_pci_read,
	.write = hurd_pci_write,
	.resource_alloc = hurd_resource_alloc,
	.map_page = hurd_map_page,
	.unmap_page = hurd_unmap_page,
};

/* Timers. The Hurd's glibc has no timer_create(): one thread per timer
   sleeps until its deadline. LKL only ever uses one or two. */

struct hurd_timer {
	void (*fn)(void);
	pthread_mutex_t lock;
	pthread_cond_t cond;
	struct timespec when;
	int armed, dead;
	pthread_t thread;
};

static void *timer_thread(void *arg)
{
	struct hurd_timer *t = arg;

	pthread_mutex_lock(&t->lock);
	while (!t->dead) {
		if (!t->armed) {
			pthread_cond_wait(&t->cond, &t->lock);
			continue;
		}
		if (pthread_cond_timedwait(&t->cond, &t->lock, &t->when) != ETIMEDOUT)
			continue;	/* re-armed, freed or woken early */
		t->armed = 0;
		pthread_mutex_unlock(&t->lock);
		t->fn();
		pthread_mutex_lock(&t->lock);
	}
	pthread_mutex_unlock(&t->lock);
	pthread_mutex_destroy(&t->lock);
	pthread_cond_destroy(&t->cond);
	free(t);
	return NULL;
}

static void *hurd_timer_alloc(void (*fn)(void))
{
	struct hurd_timer *t = calloc(1, sizeof *t);

	if (!t)
		return NULL;
	t->fn = fn;
	pthread_mutex_init(&t->lock, NULL);
	pthread_cond_init(&t->cond, NULL);
	if (pthread_create(&t->thread, NULL, timer_thread, t)) {
		free(t);
		return NULL;
	}
	pthread_detach(t->thread);
	return t;
}

static int hurd_timer_set_oneshot(void *timer, unsigned long ns)
{
	struct hurd_timer *t = timer;
	struct timespec now;

	/* pthread_cond_timedwait counts in CLOCK_REALTIME */
	clock_gettime(CLOCK_REALTIME, &now);
	pthread_mutex_lock(&t->lock);
	t->when.tv_sec = now.tv_sec + ns / 1000000000;
	t->when.tv_nsec = now.tv_nsec + ns % 1000000000;
	if (t->when.tv_nsec >= 1000000000) {
		t->when.tv_sec++;
		t->when.tv_nsec -= 1000000000;
	}
	t->armed = 1;
	pthread_cond_signal(&t->cond);
	pthread_mutex_unlock(&t->lock);
	return 0;
}

static void hurd_timer_free(void *timer)
{
	struct hurd_timer *t = timer;

	pthread_mutex_lock(&t->lock);
	t->dead = 1;
	pthread_cond_signal(&t->cond);
	pthread_mutex_unlock(&t->lock);
}

/* posix-host.c fills lkl_host_ops for POSIX; swap in the Hurd parts */
__attribute__((constructor)) static void hurd_host_ops(void)
{
	lkl_host_ops.timer_alloc = hurd_timer_alloc;
	lkl_host_ops.timer_set_oneshot = hurd_timer_set_oneshot;
	lkl_host_ops.timer_free = hurd_timer_free;
	lkl_host_ops.shmem_init = hurd_shmem_init;
	lkl_host_ops.shmem_mmap = hurd_shmem_mmap;
	lkl_host_ops.ioremap = hurd_ioremap;
	lkl_host_ops.iomem_access = hurd_iomem_access;
	lkl_host_ops.pci_ops = &hurd_pci_ops;
}
