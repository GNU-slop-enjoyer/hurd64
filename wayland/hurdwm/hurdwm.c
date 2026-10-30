/* hurdwm: a tiling Wayland compositor for Hurd64, on wlroots.
 *
 *   hurdwm [-k keys] [-s command]      (run as root from the console)
 *
 * wlroots renders the scene in software (pixman) into an off-screen
 * output of the headless backend; every frame the damaged part is copied
 * to the screen the firmware set up (GNU Mach's "fb" device), so no
 * graphics driver is needed. Keys and the mouse come from tuxkbd (USB) or,
 * with -k, keys from a tty or fifo. A click focuses the window under the
 * pointer and goes on to it, as does the wheel. Clients connect over the usual Wayland
 * socket in $XDG_RUNTIME_DIR.
 *
 * Windows tile: the first one fills the left half, the others share the
 * right half; one window alone gets the whole screen under the bar.
 *   Super+Enter    terminal (foot, or -s command)
 *   Super+J/K      next/previous window   Super+Tab  next
 *   Super+Shift+Enter  focused window to the big tile
 *   Super+F        full screen            Super+Q    close window
 *   Super+D        run a program (type its name, Enter)
 *   Super+P        screenshot (~/hurdwm-N.ppm)
 *   Super+Shift+E or Super+Esc   quit
 * With -k, Ctrl+] then a key stands for Super+key (n = Enter).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <linux/input-event-codes.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "screen.h"
#include "keys.h"
#include "bar.h"

#define BORDER 2
#define GAP 4

struct server {
	struct wl_display *display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_scene_tree *windows, *top;
	struct wlr_scene_rect *background;
	struct wlr_output_layout *layout;
	struct wlr_output *output;
	struct wlr_xdg_shell *xdg_shell;
	struct wlr_seat *seat;
	struct wlr_keyboard keyboard;
	struct wlr_cursor *cursor;
	struct wlr_scene_buffer *arrow;
	uint32_t buttons;		/* held down */
	struct wl_list toplevels;	/* tiling order, the first is the big tile */
	struct toplevel *focused;
	struct bar bar;
	struct screen screen;
	const char *terminal;
	int width, height;
	bool launching;		/* Super+D: typing a command into the bar */
	char command[256];

	struct wl_listener new_output, frame;
	struct wl_listener new_toplevel, new_popup;
	struct wl_listener key, modifiers;
	struct wl_listener new_decoration;
	struct wl_event_source *clock_timer, *keys_source;
};

struct toplevel {
	struct wl_list link;
	struct server *server;
	struct wlr_xdg_toplevel *xdg;
	struct wlr_scene_tree *frame, *content;
	struct wlr_scene_rect *border;
	bool mapped, fullscreen;
	struct wlr_xdg_toplevel_decoration_v1 *decoration;
	struct wl_listener decoration_destroy;
	struct wl_listener map, unmap, commit, destroy, request_fullscreen, set_title;
};

static const float c_desk[4] = { 0x2d / 255.0f, 0x32 / 255.0f, 0x36 / 255.0f, 1 };
static const float c_border[4] = { 0x3a / 255.0f, 0x3e / 255.0f, 0x42 / 255.0f, 1 };
static const float c_focus[4] = { 0x44 / 255.0f, 0x62 / 255.0f, 0x7c / 255.0f, 1 };

/* the bar */

static void update_bar(struct server *s)
{
	const char *titles[BAR_MAX_TITLES];
	int n = 0, focus = -1;
	struct toplevel *t;

	wl_list_for_each(t, &s->toplevels, link) {
		if (!t->mapped || n == BAR_MAX_TITLES)
			continue;
		if (t == s->focused)
			focus = n;
		const char *title = t->xdg->title && *t->xdg->title ? t->xdg->title
			: t->xdg->app_id ? t->xdg->app_id : "window";
		titles[n++] = title;
	}
	bar_draw(&s->bar, titles, n, focus,
		 n ? NULL : "Super+Enter opens a terminal, Super+D runs a program",
		 s->launching ? s->command : NULL);
}

static int clock_tick(void *data)
{
	struct server *s = data;

	if (bar_clock_changed(&s->bar))
		update_bar(s);
	wl_event_source_timer_update(s->clock_timer, 1000);
	return 0;
}

/* tiling */

static void arrange(struct server *s)
{
	int n = 0;
	struct toplevel *t, *full = NULL;

	wl_list_for_each(t, &s->toplevels, link) {
		if (!t->mapped)
			continue;
		n++;
		if (t->fullscreen)
			full = t;
	}
	int ax = 0, ay = BAR_HEIGHT, aw = s->width, ah = s->height - BAR_HEIGHT, i = 0;
	wl_list_for_each(t, &s->toplevels, link) {
		if (!t->mapped)
			continue;
		int x, y, w, h;
		if (full) {
			wlr_scene_node_set_enabled(&t->frame->node, t == full);
			if (t != full)
				continue;
			x = 0, y = 0, w = s->width, h = s->height;
		} else {
			wlr_scene_node_set_enabled(&t->frame->node, true);
			if (n == 1) {
				x = ax, y = ay, w = aw, h = ah;
			} else if (i == 0) {
				x = ax, y = ay, w = aw / 2, h = ah;
			} else {
				int rows = n - 1, rh = ah / rows;
				x = ax + aw / 2, y = ay + (i - 1) * rh, w = aw - aw / 2;
				h = i == n - 1 ? ah - (i - 1) * rh : rh;
			}
			x += GAP, y += GAP, w -= 2 * GAP, h -= 2 * GAP;
		}
		int b = full ? 0 : BORDER;
		wlr_scene_node_set_position(&t->frame->node, x, y);
		wlr_scene_rect_set_size(t->border, w, h);
		wlr_scene_node_set_position(&t->content->node, b, b);
		wlr_xdg_toplevel_set_size(t->xdg, w - 2 * b, h - 2 * b);
		i++;
	}
	wlr_scene_node_set_enabled(&s->bar.node->node, !full);
	update_bar(s);
}

static void focus(struct server *s, struct toplevel *t)
{
	if (s->focused == t && t)
		return;
	if (s->focused) {
		wlr_xdg_toplevel_set_activated(s->focused->xdg, false);
		wlr_scene_rect_set_color(s->focused->border, c_border);
	}
	s->focused = t;
	if (!t) {
		wlr_seat_keyboard_notify_clear_focus(s->seat);
		update_bar(s);
		return;
	}
	wlr_xdg_toplevel_set_activated(t->xdg, true);
	wlr_scene_rect_set_color(t->border, c_focus);
	wlr_scene_node_raise_to_top(&t->frame->node);
	wlr_seat_keyboard_notify_enter(s->seat, t->xdg->base->surface,
				       s->keyboard.keycodes, s->keyboard.num_keycodes,
				       &s->keyboard.modifiers);
	update_bar(s);
}

/* the next (dir 1) or previous (-1) mapped window after the focused one */
static struct toplevel *neighbour(struct server *s, int dir)
{
	struct toplevel *t, *list[64];
	int n = 0, at = 0;

	wl_list_for_each(t, &s->toplevels, link)
		if (t->mapped && n < 64) {
			if (t == s->focused)
				at = n;
			list[n++] = t;
		}
	if (!n)
		return NULL;
	return list[(at + dir + n) % n];
}

/* windows */

static void toplevel_map(struct wl_listener *l, void *data)
{
	struct toplevel *t = wl_container_of(l, t, map);

	t->mapped = true;
	arrange(t->server);
	focus(t->server, t);
}

static void toplevel_unmap(struct wl_listener *l, void *data)
{
	struct toplevel *t = wl_container_of(l, t, unmap);
	struct server *s = t->server;

	t->mapped = false;
	if (s->focused == t) {
		s->focused = NULL;
		struct toplevel *o;
		struct toplevel *next = NULL;
		wl_list_for_each(o, &s->toplevels, link)
			if (o->mapped) {
				next = o;
				break;
			}
		focus(s, next);
	}
	arrange(s);
}

static void toplevel_commit(struct wl_listener *l, void *data)
{
	struct toplevel *t = wl_container_of(l, t, commit);

	if (t->xdg->base->initial_commit) {
		if (t->decoration)
			wlr_xdg_toplevel_decoration_v1_set_mode(t->decoration,
				WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
		/* tiled: no client-side decorations, sizes come from the layout */
		wlr_xdg_toplevel_set_tiled(t->xdg, WLR_EDGE_TOP | WLR_EDGE_BOTTOM |
					   WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
		wlr_xdg_toplevel_set_size(t->xdg, 0, 0);
	}
}

static void toplevel_title(struct wl_listener *l, void *data)
{
	struct toplevel *t = wl_container_of(l, t, set_title);

	update_bar(t->server);
}

static void toplevel_fullscreen(struct wl_listener *l, void *data)
{
	struct toplevel *t = wl_container_of(l, t, request_fullscreen);

	t->fullscreen = t->xdg->requested.fullscreen;
	wlr_xdg_toplevel_set_fullscreen(t->xdg, t->fullscreen);
	if (t->mapped)
		arrange(t->server);
}

static void toplevel_destroy(struct wl_listener *l, void *data)
{
	struct toplevel *t = wl_container_of(l, t, destroy);

	wl_list_remove(&t->map.link);
	wl_list_remove(&t->unmap.link);
	wl_list_remove(&t->commit.link);
	wl_list_remove(&t->destroy.link);
	wl_list_remove(&t->request_fullscreen.link);
	wl_list_remove(&t->set_title.link);
	if (t->decoration)
		wl_list_remove(&t->decoration_destroy.link);
	wl_list_remove(&t->link);
	wlr_scene_node_destroy(&t->frame->node);
	if (t->server->focused == t)
		t->server->focused = NULL;
	update_bar(t->server);
	free(t);
}

static void new_toplevel(struct wl_listener *l, void *data)
{
	struct server *s = wl_container_of(l, s, new_toplevel);
	struct wlr_xdg_toplevel *xdg = data;
	struct toplevel *t = calloc(1, sizeof *t);

	t->server = s;
	t->xdg = xdg;
	t->frame = wlr_scene_tree_create(s->windows);
	t->border = wlr_scene_rect_create(t->frame, 0, 0, c_border);
	t->content = wlr_scene_xdg_surface_create(t->frame, xdg->base);
	t->frame->node.data = t;
	xdg->base->data = t->content;
	wl_list_insert(s->toplevels.prev, &t->link);

	t->map.notify = toplevel_map;
	wl_signal_add(&xdg->base->surface->events.map, &t->map);
	t->unmap.notify = toplevel_unmap;
	wl_signal_add(&xdg->base->surface->events.unmap, &t->unmap);
	t->commit.notify = toplevel_commit;
	wl_signal_add(&xdg->base->surface->events.commit, &t->commit);
	t->destroy.notify = toplevel_destroy;
	wl_signal_add(&xdg->events.destroy, &t->destroy);
	t->request_fullscreen.notify = toplevel_fullscreen;
	wl_signal_add(&xdg->events.request_fullscreen, &t->request_fullscreen);
	t->set_title.notify = toplevel_title;
	wl_signal_add(&xdg->events.set_title, &t->set_title);
}

struct popup {
	struct wlr_xdg_popup *xdg;
	struct wl_listener commit, destroy;
};

static void popup_commit(struct wl_listener *l, void *data)
{
	struct popup *p = wl_container_of(l, p, commit);

	if (p->xdg->base->initial_commit)
		wlr_xdg_surface_schedule_configure(p->xdg->base);
}

static void popup_destroy(struct wl_listener *l, void *data)
{
	struct popup *p = wl_container_of(l, p, destroy);

	wl_list_remove(&p->commit.link);
	wl_list_remove(&p->destroy.link);
	free(p);
}

static void new_popup(struct wl_listener *l, void *data)
{
	struct wlr_xdg_popup *xdg = data;
	struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xdg->parent);

	if (!parent || !parent->data)
		return;
	struct popup *p = calloc(1, sizeof *p);
	p->xdg = xdg;
	xdg->base->data = wlr_scene_xdg_surface_create(parent->data, xdg->base);
	p->commit.notify = popup_commit;
	wl_signal_add(&xdg->base->surface->events.commit, &p->commit);
	p->destroy.notify = popup_destroy;
	wl_signal_add(&xdg->events.destroy, &p->destroy);
}

static void decoration_destroy(struct wl_listener *l, void *data)
{
	struct toplevel *t = wl_container_of(l, t, decoration_destroy);

	wl_list_remove(&t->decoration_destroy.link);
	t->decoration = NULL;
}

/* server-side decorations: we draw the borders, clients draw nothing. The
   mode can only be sent once the surface is set up (its first commit). */
static void new_decoration(struct wl_listener *l, void *data)
{
	struct server *s = wl_container_of(l, s, new_decoration);
	struct wlr_xdg_toplevel_decoration_v1 *d = data;
	struct toplevel *t;

	wl_list_for_each(t, &s->toplevels, link) {
		if (t->xdg != d->toplevel)
			continue;
		t->decoration = d;
		t->decoration_destroy.notify = decoration_destroy;
		wl_signal_add(&d->events.destroy, &t->decoration_destroy);
		if (d->toplevel->base->initialized)
			wlr_xdg_toplevel_decoration_v1_set_mode(d,
				WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
		return;
	}
}

/* output: render, then copy to the screen */

static void output_frame(struct wl_listener *l, void *data)
{
	struct server *s = wl_container_of(l, s, frame);
	struct wlr_scene_output *so = wlr_scene_get_scene_output(s->scene, s->output);
	struct wlr_output_state st;

	wlr_output_state_init(&st);
	if (wlr_scene_output_build_state(so, &st, NULL) && (st.committed & WLR_OUTPUT_STATE_BUFFER)) {
		void *px;
		uint32_t fmt;
		size_t stride;
		if (wlr_buffer_begin_data_ptr_access(st.buffer, WLR_BUFFER_DATA_PTR_ACCESS_READ,
						     &px, &fmt, &stride)) {
			const pixman_region32_t *damage =
				(st.committed & WLR_OUTPUT_STATE_DAMAGE) ? &st.damage : NULL;
			screen_put(&s->screen, px, stride, damage);
			wlr_buffer_end_data_ptr_access(st.buffer);
		}
		wlr_output_commit_state(s->output, &st);
	}
	wlr_output_state_finish(&st);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(so, &now);
}

static void new_output(struct wl_listener *l, void *data)
{
	struct server *s = wl_container_of(l, s, new_output);
	struct wlr_output *o = data;
	struct wlr_output_state st;

	if (s->output)
		return;
	wlr_output_init_render(o, s->allocator, s->renderer);
	wlr_output_state_init(&st);
	wlr_output_state_set_enabled(&st, true);
	wlr_output_state_set_custom_mode(&st, s->width, s->height, 60000);
	wlr_output_commit_state(o, &st);
	wlr_output_state_finish(&st);
	s->output = o;
	s->frame.notify = output_frame;
	wl_signal_add(&o->events.frame, &s->frame);
	wlr_output_layout_add_auto(s->layout, o);
	wlr_scene_output_create(s->scene, o);
}

/* keys */

static void spawn(const char *cmd)
{
	pid_t pid = fork();

	if (pid == 0) {
		setsid();
		signal(SIGPIPE, SIG_DFL);
		execl("/bin/sh", "/bin/sh", "-c", cmd, (char *)NULL);
		_exit(127);
	}
}

static int screenshot_n;

static bool binding(struct server *s, xkb_keysym_t sym, uint32_t mods)
{
	bool shift = mods & WLR_MODIFIER_SHIFT;

	switch (sym) {
	case XKB_KEY_Return:
		if (shift) {
			if (s->focused) {	/* to the big tile */
				wl_list_remove(&s->focused->link);
				wl_list_insert(&s->toplevels, &s->focused->link);
				arrange(s);
			}
		} else {
			spawn(s->terminal);
		}
		return true;
	case XKB_KEY_j: case XKB_KEY_Tab:
		focus(s, neighbour(s, 1));
		return true;
	case XKB_KEY_k: case XKB_KEY_ISO_Left_Tab:
		focus(s, neighbour(s, -1));
		return true;
	case XKB_KEY_q:
		if (s->focused)
			wlr_xdg_toplevel_send_close(s->focused->xdg);
		return true;
	case XKB_KEY_f:
		if (s->focused) {
			s->focused->fullscreen = !s->focused->fullscreen;
			wlr_xdg_toplevel_set_fullscreen(s->focused->xdg, s->focused->fullscreen);
			arrange(s);
		}
		return true;
	case XKB_KEY_d:
		s->launching = true;
		s->command[0] = 0;
		update_bar(s);
		return true;
	case XKB_KEY_p: {
		char path[512];
		const char *home = getenv("HOME");
		snprintf(path, sizeof path, "%s/hurdwm-%d.ppm", home ? home : "/tmp", ++screenshot_n);
		screen_save(&s->screen, path);
		return true;
	}
	case XKB_KEY_E: case XKB_KEY_e:
		if (!shift)
			return false;
		/* fall through */
	case XKB_KEY_Escape:
		wl_display_terminate(s->display);
		return true;
	}
	return false;
}

/* a key while the launcher is open: edit the command, Enter runs it */
static void launcher_key(struct server *s, xkb_keycode_t code)
{
	xkb_keysym_t sym = xkb_state_key_get_one_sym(s->keyboard.xkb_state, code);
	size_t len = strlen(s->command);

	if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
		if (len)
			spawn(s->command);
		s->launching = false;
	} else if (sym == XKB_KEY_Escape) {
		s->launching = false;
	} else if (sym == XKB_KEY_BackSpace) {
		if (len)
			s->command[len - 1] = 0;
	} else {
		char utf8[8];
		int n = xkb_state_key_get_utf8(s->keyboard.xkb_state, code, utf8, sizeof utf8);
		/* the bar font has ASCII */
		if (n == 1 && utf8[0] >= 0x20 && utf8[0] < 0x7f && len + 1 < sizeof s->command) {
			s->command[len] = utf8[0];
			s->command[len + 1] = 0;
		}
	}
	update_bar(s);
}

static void keyboard_key(struct wl_listener *l, void *data)
{
	struct server *s = wl_container_of(l, s, key);
	struct wlr_keyboard_key_event *ev = data;
	uint32_t mods = wlr_keyboard_get_modifiers(&s->keyboard);

	if (s->launching) {
		if (ev->state == WL_KEYBOARD_KEY_STATE_PRESSED)
			launcher_key(s, ev->keycode + 8);
		return;
	}
	if ((mods & WLR_MODIFIER_LOGO) && ev->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		const xkb_keysym_t *syms;
		int n = xkb_state_key_get_syms(s->keyboard.xkb_state, ev->keycode + 8, &syms);
		for (int i = 0; i < n; i++)
			if (binding(s, syms[i], mods))
				return;
	}
	wlr_seat_set_keyboard(s->seat, &s->keyboard);
	wlr_seat_keyboard_notify_key(s->seat, ev->time_msec, ev->keycode, ev->state);
}

static void keyboard_modifiers(struct wl_listener *l, void *data)
{
	struct server *s = wl_container_of(l, s, modifiers);

	wlr_seat_set_keyboard(s->seat, &s->keyboard);
	wlr_seat_keyboard_notify_modifiers(s->seat, &s->keyboard.modifiers);
}

static uint32_t now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* the window (and surface) under the pointer */
static struct toplevel *toplevel_at(struct server *s, double x, double y,
				    struct wlr_surface **surface, double *sx, double *sy)
{
	struct wlr_scene_node *node = wlr_scene_node_at(&s->windows->node, x, y, sx, sy);

	*surface = NULL;
	if (!node || node->type != WLR_SCENE_NODE_BUFFER)
		return NULL;
	struct wlr_scene_surface *ss =
		wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node));
	if (ss)
		*surface = ss->surface;
	for (struct wlr_scene_tree *t = node->parent; t; t = t->node.parent)
		if (t->node.data && t->node.parent == s->windows)
			return t->node.data;
	return NULL;
}

static void pointer_update(struct server *s)
{
	struct wlr_surface *surface;
	double sx, sy;

	wlr_scene_node_set_position(&s->arrow->node, s->cursor->x, s->cursor->y);
	toplevel_at(s, s->cursor->x, s->cursor->y, &surface, &sx, &sy);
	if (surface) {
		wlr_seat_pointer_notify_enter(s->seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(s->seat, now_ms(), sx, sy);
	} else if (!s->buttons) {
		wlr_seat_pointer_clear_focus(s->seat);
	}
	wlr_seat_pointer_notify_frame(s->seat);
}

static void mouse_motion(void *data, int dx, int dy)
{
	struct server *s = data;

	wlr_cursor_move(s->cursor, NULL, dx, dy);
	pointer_update(s);
}

static void mouse_button(void *data, int button, bool pressed)
{
	struct server *s = data;
	static const uint32_t codes[] = { BTN_LEFT, BTN_MIDDLE, BTN_RIGHT };

	if (button < 0 || button > 2)
		return;
	if (pressed) {
		s->buttons |= 1u << button;
		struct wlr_surface *surface;
		double sx, sy;
		struct toplevel *t = toplevel_at(s, s->cursor->x, s->cursor->y, &surface, &sx, &sy);
		if (t)
			focus(s, t);
	} else {
		s->buttons &= ~(1u << button);
	}
	wlr_seat_pointer_notify_button(s->seat, now_ms(), codes[button],
		pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
	wlr_seat_pointer_notify_frame(s->seat);
}

static void mouse_wheel(void *data, int steps)
{
	struct server *s = data;

	/* away from the user scrolls up: a negative vertical axis value */
	wlr_seat_pointer_notify_axis(s->seat, now_ms(), WL_POINTER_AXIS_VERTICAL_SCROLL,
		-15.0 * steps, -120 * steps, WL_POINTER_AXIS_SOURCE_WHEEL,
		WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
	wlr_seat_pointer_notify_frame(s->seat);
}

/* keys.c hands us Linux key codes */
static void key_event(void *data, uint32_t code, bool pressed)
{
	struct server *s = data;
	struct wlr_keyboard_key_event ev = {
		.time_msec = now_ms(), .keycode = code, .update_state = true,
		.state = pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED,
	};

	wlr_keyboard_notify_key(&s->keyboard, &ev);
}

static int keys_readable(int fd, uint32_t mask, void *data)
{
	struct server *s = data;

	static const struct input_fns fns = { key_event, mouse_motion, mouse_button, mouse_wheel };

	if (keys_read(fd, &fns, s) < 0) {
		wlr_log(WLR_ERROR, "the keyboard went away");
		wl_event_source_remove(s->keys_source);
		s->keys_source = NULL;
	}
	return 0;
}

static const struct wlr_keyboard_impl keyboard_impl = { .name = "hurd64-keyboard" };

static int quit_signal(int sig, void *data)
{
	wl_display_terminate(data);
	return 0;
}

static void reap(int sig)
{
	int e = errno;

	while (waitpid(-1, NULL, WNOHANG) > 0)
		;
	errno = e;
}

int main(int argc, char **argv)
{
	struct server s = { 0 };
	const char *keys_path = NULL;
	int opt;

	s.terminal = "foot";
	while ((opt = getopt(argc, argv, "k:s:")) != -1) {
		if (opt == 'k')
			keys_path = optarg;
		else if (opt == 's')
			s.terminal = optarg;
		else {
			fprintf(stderr, "usage: hurdwm [-k keys] [-s terminal command]\n");
			return 2;
		}
	}
	wlr_log_init(WLR_INFO, NULL);

	/* a place for the Wayland socket */
	if (!getenv("XDG_RUNTIME_DIR")) {
		char dir[64];
		snprintf(dir, sizeof dir, "/run/user/%d", getuid());
		mkdir("/run/user", 0755);
		mkdir(dir, 0700);
		setenv("XDG_RUNTIME_DIR", dir, 1);
	}
	/* foot wants a UTF-8 locale; one that is not installed is no use */
	if (!setlocale(LC_ALL, "") || (!getenv("LANG") && !getenv("LC_ALL"))) {
		unsetenv("LC_ALL");
		unsetenv("LC_CTYPE");
		setenv("LANG", "C.UTF-8", 1);
	}

	if (screen_open(&s.screen) < 0)
		return 1;
	s.width = s.screen.width;
	s.height = s.screen.height;
	/* foot's default font is for small screens */
	if (!strcmp(s.terminal, "foot") && s.width >= 2560)
		s.terminal = "foot --font=monospace:size=15";
	else if (!strcmp(s.terminal, "foot") && s.width >= 1600)
		s.terminal = "foot --font=monospace:size=11";

	int kfd = keys_open(keys_path);
	if (kfd < 0)
		fprintf(stderr, "hurdwm: no keyboard (is tuxkbd running?)\n");

	s.display = wl_display_create();
	struct wl_event_loop *loop = wl_display_get_event_loop(s.display);
	s.backend = wlr_headless_backend_create(loop);
	s.renderer = wlr_renderer_autocreate(s.backend);
	if (!s.backend || !s.renderer) {
		fprintf(stderr, "hurdwm: no wlroots backend or renderer\n");
		return 1;
	}
	wlr_renderer_init_wl_display(s.renderer, s.display);
	s.allocator = wlr_allocator_autocreate(s.backend, s.renderer);
	wlr_compositor_create(s.display, 5, s.renderer);
	wlr_subcompositor_create(s.display);
	wlr_data_device_manager_create(s.display);

	s.layout = wlr_output_layout_create(s.display);
	s.scene = wlr_scene_create();
	s.background = wlr_scene_rect_create(&s.scene->tree, s.width, s.height, c_desk);
	s.windows = wlr_scene_tree_create(&s.scene->tree);
	s.top = wlr_scene_tree_create(&s.scene->tree);
	wlr_scene_attach_output_layout(s.scene, s.layout);
	bar_init(&s.bar, s.top, s.width);
	wl_list_init(&s.toplevels);

	s.new_output.notify = new_output;
	wl_signal_add(&s.backend->events.new_output, &s.new_output);
	s.xdg_shell = wlr_xdg_shell_create(s.display, 3);
	s.new_toplevel.notify = new_toplevel;
	wl_signal_add(&s.xdg_shell->events.new_toplevel, &s.new_toplevel);
	s.new_popup.notify = new_popup;
	wl_signal_add(&s.xdg_shell->events.new_popup, &s.new_popup);
	struct wlr_xdg_decoration_manager_v1 *deco = wlr_xdg_decoration_manager_v1_create(s.display);
	s.new_decoration.notify = new_decoration;
	wl_signal_add(&deco->events.new_toplevel_decoration, &s.new_decoration);

	s.seat = wlr_seat_create(s.display, "seat0");
	wlr_keyboard_init(&s.keyboard, &keyboard_impl, "hurd64-keyboard");
	struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	/* XKB_DEFAULT_LAYOUT etc. in the environment pick the layout */
	struct xkb_keymap *km = xkb_keymap_new_from_names(ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!km) {
		fprintf(stderr, "hurdwm: no keymap (is xkb-data installed?)\n");
		return 1;
	}
	wlr_keyboard_set_keymap(&s.keyboard, km);
	xkb_keymap_unref(km);
	xkb_context_unref(ctx);
	wlr_keyboard_set_repeat_info(&s.keyboard, 25, 600);
	s.key.notify = keyboard_key;
	wl_signal_add(&s.keyboard.events.key, &s.key);
	s.modifiers.notify = keyboard_modifiers;
	wl_signal_add(&s.keyboard.events.modifiers, &s.modifiers);
	wlr_seat_set_keyboard(s.seat, &s.keyboard);
	wlr_seat_set_capabilities(s.seat, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);
	s.cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(s.cursor, s.layout);
	s.arrow = bar_arrow(s.top);
	if (kfd >= 0)
		s.keys_source = wl_event_loop_add_fd(loop, kfd, WL_EVENT_READABLE, keys_readable, &s);

	const char *sock = wl_display_add_socket_auto(s.display);
	if (!sock) {
		fprintf(stderr, "hurdwm: no Wayland socket in %s\n", getenv("XDG_RUNTIME_DIR"));
		return 1;
	}
	if (!wlr_backend_start(s.backend)) {
		fprintf(stderr, "hurdwm: backend did not start\n");
		return 1;
	}
	wlr_headless_add_output(s.backend, s.width, s.height);
	setenv("WAYLAND_DISPLAY", sock, 1);
	fprintf(stderr, "hurdwm: %dx%d, WAYLAND_DISPLAY=%s\n", s.width, s.height, sock);

	signal(SIGCHLD, reap);
	wl_event_loop_add_signal(loop, SIGTERM, quit_signal, s.display);
	wl_event_loop_add_signal(loop, SIGINT, quit_signal, s.display);
	s.clock_timer = wl_event_loop_add_timer(loop, clock_tick, &s);
	wl_event_source_timer_update(s.clock_timer, 1000);
	update_bar(&s);

	wlr_cursor_warp(s.cursor, NULL, s.width / 2, s.height / 2);
	wlr_scene_node_set_position(&s.arrow->node, s.width / 2, s.height / 2);
	spawn(s.terminal);
	wl_display_run(s.display);

	wl_display_destroy_clients(s.display);
	wlr_scene_node_destroy(&s.scene->tree.node);
	wl_display_destroy(s.display);
	keys_close();
	screen_close(&s.screen);
	return 0;
}
