/*
 * stats(1) and clock(1) sitting in the corner of the first monitor.
 * graphs are l load m mem i interrupts s system cpu c context switches.
 * linux has no syscall counter so s is sys time instead.
 * they live in LyrBottom and have no surface so xytonode skips them
 * and clicks fall through to the canvas.
 */

enum { Gload, Gmem, Gintr, Gsys, Gctxt, Ngraph };

#define NHIST 1024
#define PIE   3.14159265358979323846

static const char *glab[Ngraph] = { "l", "m", "i", "s", "c" };
/* from stats colinit. back fill peak */
static const uint32_t gcol[Ngraph][3] = {
	{ 0xffeaeaff, 0xffaaaaff, 0xbb5d5dff },
	{ 0xeaffffff, 0x9eeeeeff, 0x8888ccff },
	{ 0xffffeaff, 0xeeee9eff, 0x99994cff },
	{ 0xeaffeaff, 0x88cc88ff, 0x448844ff },
	{ 0xbfeaffff, 0x00aaffff, 0x0088ccff },
};
/* scale starts here and doubles when something pokes over */
static const double gmin[Ngraph] = { 1.0, 100.0, 1000.0, 100.0, 1000.0 };

static struct wlr_scene_buffer *statsb, *clocksb;
static struct wl_event_source *ticker;
static double hist[Ngraph][NHIST];
static int hhead, nhist;
static unsigned long long ointr, octxt, otot, osys;
static uint32_t oms;
static int warm;
static char sysname[96];
static int clockon, lastmin = -1;

static Monitor *
statsmon(void)
{
	Monitor *m;

	wl_list_for_each(m, &mons, link)
		if (m->wlr_output->enabled && m->m.width > 0)
			return m;
	return NULL;
}

static void
sample(void)
{
	static char buf[65536];  /* the intr line is huge on big boxes */
	char *p;
	unsigned long long u = 0, n = 0, s = 0, idle = 0, io = 0, irq = 0, sirq = 0, st = 0;
	unsigned long long intr = 0, ctxt = 0, tot, sys, mt = 0, ma = 0;
	double v[Ngraph] = {0}, dt;
	uint32_t now = (uint32_t)bar_now_ms();
	int i;

	if (bar_slurp("/proc/loadavg", buf, sizeof(buf)) > 0)
		v[Gload] = strtod(buf, NULL);

	if (bar_slurp("/proc/meminfo", buf, sizeof(buf)) > 0) {
		if ((p = strstr(buf, "MemTotal:")))
			mt = strtoull(p + 9, NULL, 10);
		if ((p = strstr(buf, "MemAvailable:")))
			ma = strtoull(p + 13, NULL, 10);
		else if ((p = strstr(buf, "MemFree:")))
			ma = strtoull(p + 8, NULL, 10);	/* the Hurd's procfs */
		if (mt)
			v[Gmem] = 100.0 * (double)(mt - MIN(ma, mt)) / (double)mt;
	}

	if (bar_slurp("/proc/stat", buf, sizeof(buf)) <= 0)
		return;
	sscanf(buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
			&u, &n, &s, &idle, &io, &irq, &sirq, &st);
	if ((p = strstr(buf, "\nintr ")))
		intr = strtoull(p + 6, NULL, 10);
	if ((p = strstr(buf, "\nctxt ")))
		ctxt = strtoull(p + 6, NULL, 10);
	tot = u + n + s + idle + io + irq + sirq + st;
	sys = s + irq + sirq;

	if (warm) {
		dt = (double)(now - oms) / 1000.0;
		if (dt <= 0.0)
			dt = 1.0;
		v[Gintr] = (double)(intr - ointr) / dt;
		v[Gctxt] = (double)(ctxt - octxt) / dt;
		if (tot > otot)
			v[Gsys] = 100.0 * (double)(sys - osys) / (double)(tot - otot);
	}
	ointr = intr;
	octxt = ctxt;
	otot = tot;
	osys = sys;
	oms = now;
	if (!warm) {
		warm = 1;
		return;
	}

	for (i = 0; i < Ngraph; i++)
		hist[i][hhead] = v[i];
	hhead = (hhead + 1) % NHIST;
	if (nhist < NHIST)
		nhist++;
}

/* k samples back. 0 is the newest */
static double
ago(int g, int k)
{
	return hist[g][(hhead - 1 - k + NHIST) % NHIST];
}

static void
graph(pixman_image_t *img, int g, int x, int y, int w, int h)
{
	double sc = gmin[g], top = 0.0;
	int k, n = MIN(nhist, w), py, ty;

	bar_rect(img, x, y, w, h, gcol[g][0]);
	if (h < 2)
		return;
	for (k = 0; k < n; k++)
		top = MAX(top, ago(g, k));
	while (top > sc)
		sc *= 2.0;
	for (k = 0; k < n; k++) {
		py = (int)(ago(g, k) / sc * (double)(h - 2) + 0.5);
		ty = y + h - 2 - MIN(py, h - 2);
		bar_rect(img, x + w - 1 - k, ty, 1, y + h - ty, gcol[g][1]);
		bar_rect(img, x + w - 1 - k, ty, 1, 2, gcol[g][2]);
	}
}

static void
drawstats(void)
{
	BarBuffer *bb;
	int b = riogadget_borderpx, w = riostats_w, h = riostats_h;
	int ix, iy, iw, ih, th, lw, rh, i, y, y1;

	if (!statsb || !bar_font || w <= 2*b + 8 || h <= 2*b + 8)
		return;
	if (!(bb = bar_newbuf(w, h, 0)))
		return;
	ix = iy = b;
	iw = w - 2*b;
	ih = h - 2*b;
	bar_rect(bb->image, 0, 0, w, h, riogadget_bordercolor);
	bar_rect(bb->image, ix, iy, iw, ih, 0xffffffff);

	th = bar_font->height + 2;
	lw = bar_textw("m", 1) + 4;
	bar_run(bb->image, ix + lw + 1 + 4, iy, th, sysname, strlen(sysname),
			0x000000ff, ix + iw);
	bar_rect(bb->image, ix, iy + th, iw, 1, 0x000000ff);

	rh = (ih - th) / Ngraph;
	for (i = 0; i < Ngraph; i++) {
		y = iy + th + i*rh;
		y1 = i == Ngraph - 1 ? iy + ih : y + rh;
		if (i > 0)
			bar_rect(bb->image, ix, y, iw, 1, 0x000000ff);
		bar_text_center(bb->image, ix, y + 1, lw, y1 - y - 1, glab[i], 0x000000ff);
		graph(bb->image, i, ix + lw + 1, y + 1, iw - lw - 1, y1 - y - 1);
	}
	bar_rect(bb->image, ix + lw, iy + th, 1, ih - th, 0x000000ff);

	wlr_scene_buffer_set_buffer(statsb, &bb->base);
	wlr_buffer_drop(&bb->base);
}

static void
blob(pixman_image_t *img, int cx, int cy, int r, uint32_t col)
{
	int dx, dy;

	for (dy = -r; dy <= r; dy++)
		for (dx = -r; dx <= r; dx++)
			if (dx*dx + dy*dy <= r*r + 1)
				bar_rect(img, cx + dx, cy + dy, 1, 1, col);
}

/* 3px thick like draw(2) line with radius 1 */
static void
fatline(pixman_image_t *img, int x0, int y0, int x1, int y1, uint32_t col)
{
	int i, x, y, n = MAX(abs(x1 - x0), abs(y1 - y0));

	for (i = 0; i <= n; i++) {
		x = n ? x0 + (x1 - x0)*i/n : x0;
		y = n ? y0 + (y1 - y0)*i/n : y0;
		bar_rect(img, x - 1, y - 1, 3, 3, col);
	}
}

static void
drawclock(void)
{
	BarBuffer *bb;
	int b = riogadget_borderpx, w = rioclock_w, h = riostats_h;
	int cx, cy, r, i;
	double a;
	time_t t = time(NULL);
	struct tm tm;

	if (!clocksb || w <= 2*b + 8 || h <= 2*b + 8)
		return;
	if (!(bb = bar_newbuf(w, h, 0)))
		return;
	localtime_r(&t, &tm);
	lastmin = tm.tm_min;

	bar_rect(bb->image, 0, 0, w, h, riogadget_bordercolor);
	bar_rect(bb->image, b, b, w - 2*b, h - 2*b, 0xeaffffff);
	cx = w/2;
	cy = h/2;
	r = MIN(w, h)/2 - b;

	/* same as clock(1). dots at r-8 then minute hand 3r/4 and hour hand r/2 */
	for (i = 0; i < 12; i++) {
		a = i * PIE / 6.0;
		blob(bb->image, cx + (int)lround((r - 8) * cos(a)),
				cy - (int)lround((r - 8) * sin(a)), 2, 0x0000ffff);
	}
	a = (90.0 - tm.tm_min * 6.0) * PIE / 180.0;
	fatline(bb->image, cx, cy, cx + (int)lround(r * 3 / 4 * cos(a)),
			cy - (int)lround(r * 3 / 4 * sin(a)), 0x0000bbff);
	a = (90.0 - ((tm.tm_hour % 12) * 5 + tm.tm_min / 12) * 6.0) * PIE / 180.0;
	fatline(bb->image, cx, cy, cx + (int)lround(r / 2 * cos(a)),
			cy - (int)lround(r / 2 * sin(a)), 0x000055ff);

	wlr_scene_buffer_set_buffer(clocksb, &bb->base);
	wlr_buffer_drop(&bb->base);
}

static void
riostats_layout(void)
{
	Monitor *m = statsmon();
	int x, y;

	if (!statsb)
		return;
	if (!m) {
		wlr_scene_node_set_enabled(&statsb->node, 0);
		wlr_scene_node_set_enabled(&clocksb->node, 0);
		return;
	}
	x = m->m.x + riogadget_x;
	y = m->m.y + riogadget_y;
	wlr_scene_node_set_position(&statsb->node, x, y);
	wlr_scene_node_set_enabled(&statsb->node, riostats_show);
	wlr_scene_node_set_position(&clocksb->node, x + (riostats_show ? riostats_w : 0), y);
	wlr_scene_node_set_enabled(&clocksb->node, clockon);
}

static int
tick(void *data)
{
	time_t t = time(NULL);
	struct tm tm;

	if (riostats_show) {
		sample();
		drawstats();
	}
	localtime_r(&t, &tm);
	if (clockon && tm.tm_min != lastmin)
		drawclock();
	wl_event_source_timer_update(ticker, riostats_interval > 0 ? riostats_interval : 1000);
	return 0;
}

void
togglerioclock(const Arg *arg)
{
	if (!clocksb)
		return;
	clockon = !clockon;
	if (clockon)
		drawclock();
	riostats_layout();
}

static void
riostats_init(void)
{
	char host[64] = "";
	long ncpu = sysconf(_SC_NPROCESSORS_ONLN);

	if (!bar_font)
		return;
	gethostname(host, sizeof(host) - 1);
	snprintf(sysname, sizeof(sysname), "%s(%ld)", host, ncpu > 0 ? ncpu : 1);

	statsb = wlr_scene_buffer_create(layers[LyrBottom], NULL);
	clocksb = wlr_scene_buffer_create(layers[LyrBottom], NULL);
	wlr_scene_node_lower_to_bottom(&clocksb->node);
	wlr_scene_node_lower_to_bottom(&statsb->node);
	sample();
	drawstats();
	riostats_layout();
	ticker = wl_event_loop_add_timer(event_loop, tick, NULL);
	wl_event_source_timer_update(ticker, riostats_interval > 0 ? riostats_interval : 1000);
}

static void
riostats_cleanup(void)
{
	if (ticker)
		wl_event_source_remove(ticker);
	ticker = NULL;
	if (statsb)
		wlr_scene_node_destroy(&statsb->node);
	if (clocksb)
		wlr_scene_node_destroy(&clocksb->node);
	statsb = clocksb = NULL;
}
