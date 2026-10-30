static const int tabletmaptosurface = 1;  /* pen maps to the window under it */
static const double tablet_area[4]  = { 0.4, 0.4, 0.4, 0.4 };  /* x y w h as fractions */

/* more layouts go like .layout = "us,ru" */
static const struct xkb_rule_names xkb_rules = {
	/* hurd96: XKB_DEFAULT_LAYOUT (set by desk) picks it */
#ifndef __GNU__
	.layout = "us",
#endif
};

static const int repeat_rate = 25;
static const int repeat_delay = 600;

/* touchpad */
#ifndef __GNU__
static const int tap_to_click = 1;
static const int tap_and_drag = 1;
static const int drag_lock = 1;
static const int natural_scrolling = 0;
static const int disable_while_typing = 1;
static const int left_handed = 0;
static const int middle_button_emulation = 0;
static const enum libinput_config_scroll_method scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;
static const enum libinput_config_click_method click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
static const uint32_t send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
static const enum libinput_config_accel_profile accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
static const double accel_speed = 0;  /* -1 to 1 */
static const enum libinput_config_tap_button_map button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;
#endif

