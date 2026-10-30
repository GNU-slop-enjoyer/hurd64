/* wl-ping: a Wayland server and a client in two processes. The client
 * binds wl_shm (server side: wl_display_init_shm), creates a pool from a
 * shared file, which passes a descriptor over the socket, and does a
 * roundtrip. Exit 0 if all of that worked. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-server.h>

static struct wl_shm *shm;

static void global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t v)
{
	if (!strcmp(iface, "wl_shm"))
		shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
}
static void global_remove(void *d, struct wl_registry *r, uint32_t name) {}
static const struct wl_registry_listener reg = { global, global_remove };

static int client(const char *sock)
{
	struct wl_display *d = wl_display_connect(sock);
	if (!d) {
		perror("client: connect");
		return 1;
	}
	struct wl_registry *r = wl_display_get_registry(d);
	wl_registry_add_listener(r, &reg, NULL);
	wl_display_roundtrip(d);
	if (!shm) {
		fprintf(stderr, "client: no wl_shm\n");
		return 1;
	}
	char path[] = "/tmp/wl-ping-XXXXXX";
	int fd = mkstemp(path);
	unlink(path);
	if (fd < 0 || ftruncate(fd, 4096) < 0)
		return 1;
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, 4096);
	struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, 16, 16, 64, WL_SHM_FORMAT_XRGB8888);
	if (wl_display_roundtrip(d) < 0) {
		fprintf(stderr, "client: roundtrip with a buffer failed\n");
		return 1;
	}
	wl_buffer_destroy(b);
	wl_shm_pool_destroy(pool);
	wl_display_roundtrip(d);
	printf("client: connected, got wl_shm, made a buffer from a passed fd\n");
	wl_display_disconnect(d);
	return 0;
}

static int done;
static void client_gone(struct wl_listener *l, void *data) { done = 1; }
static struct wl_listener gone = { .notify = client_gone };
static void client_created(struct wl_listener *l, void *data)
{
	wl_client_add_destroy_listener(data, &gone);
}
static struct wl_listener created = { .notify = client_created };

int main(void)
{
	struct wl_display *d = wl_display_create();
	const char *sock = wl_display_add_socket_auto(d);
	if (!sock) {
		perror("server: socket");
		return 1;
	}
	wl_display_init_shm(d);
	wl_display_add_client_created_listener(d, &created);
	pid_t pid = fork();
	if (pid == 0)
		_exit(client(sock));
	struct wl_event_loop *loop = wl_display_get_event_loop(d);
	for (int i = 0; i < 500 && !done; i++) {
		wl_event_loop_dispatch(loop, 20);
		wl_display_flush_clients(d);
	}
	int st = 1;
	waitpid(pid, &st, 0);
	wl_display_destroy(d);
	printf("server: %s\n", done && WIFEXITED(st) && WEXITSTATUS(st) == 0 ? "ok" : "FAIL");
	return !(done && WIFEXITED(st) && WEXITSTATUS(st) == 0);
}
