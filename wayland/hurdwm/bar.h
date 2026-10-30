/* hurdwm's top bar: name, the windows, the time. Drawn with the console
 * font into a buffer of its own, shown as a scene node. */
#ifndef BAR_H
#define BAR_H
#include <wlr/types/wlr_scene.h>

#define BAR_HEIGHT (24 * bar_scale)
#define BAR_MAX_TITLES 16

/* 2 on big screens (2560 pixels wide and more), else 1 */
extern int bar_scale;

struct bar {
	struct wlr_scene_buffer *node;
	int width;
	char clock[16];
};

void bar_init(struct bar *b, struct wlr_scene_tree *parent, int width);
/* titles of the windows, the focused one (or -1), a hint when empty; with
   prompt set the bar shows that instead (the launcher) */
void bar_draw(struct bar *b, const char **titles, int n, int focused, const char *hint,
	      const char *prompt);
/* the mouse pointer: an arrow, hot spot at its top left */
struct wlr_scene_buffer *bar_arrow(struct wlr_scene_tree *parent);
/* whether the minute changed since the last draw */
int bar_clock_changed(struct bar *b);

#endif
