/* first match in keys[] wins */

#define MODKEY WLR_MODIFIER_LOGO
#define ALT_MODKEY WLR_MODIFIER_ALT

#define TAGKEYS(KEY,SKEY,TAG) \
	{ MODKEY,                    KEY,            view,            {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_CTRL,  KEY,            toggleview,      {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_SHIFT, SKEY,           tag,             {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_SHIFT,SKEY,toggletag, {.ui = 1 << TAG} }

#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

static const char *termcmd[] = { "hurd96-term", NULL };
static const char *clockcmd[] ={ "hurd96-term", "-a", "clock", "tty-clock", "-sc", "-C", "7", NULL };
static const char *wlaunch[] = { "dmenu_run", NULL };
static const char *btop[]    = { "hurd96-term", "htop", NULL };
static const char *telegram[]= { "hurd96-term", "nano", NULL };
static const char *browser[] = { "hurd96-term", "w3m", "https://www.gnu.org/software/hurd/", NULL };
static const char *tunes[]   = { "9cynth", NULL };
static const char *volupcmd[]   = { "wpctl", "set-volume", "-l", "1.0", "@DEFAULT_AUDIO_SINK@", "5%+", NULL };
static const char *voldowncmd[] = { "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "5%-", NULL };
static const char *volmutecmd[] = { "wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle", NULL };
static const char *brupcmd[]    = { "brightnessctl", "set", "10%+", NULL };
static const char *brdowncmd[]  = { "brightnessctl", "set", "10%-", NULL };

static const Key keys[] = {
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_f,		  spawn,	    {.v = browser} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_t,		  spawn,	    {.v = telegram} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_b,		  spawn,	    {.v = btop} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_d,		  spawn,	    {.v = clockcmd} },
	{ MODKEY,                    XKB_KEY_m,           spawn,            {.v = tunes} },
	{ MODKEY,                    XKB_KEY_r,           spawn,            {.v = wlaunch} },
	{ MODKEY,                    XKB_KEY_z,		  zoomfit,	    {0} },
	{ MODKEY,                    XKB_KEY_q,           spawn,            {.v = termcmd} },
	{ MODKEY,                    XKB_KEY_Tab,         focusstackcenter,       {.i = +1} },
	{ MODKEY,                    XKB_KEY_k,           focusstackcenter,       {.i = -1} },
	{ MODKEY,                    XKB_KEY_c,           killclient,       {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_f,           togglefullscreen, {0} },  /* dead. firefox has it */
	{ MODKEY,                    XKB_KEY_0,           view,             {.ui = ~0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_parenright,  tag,              {.ui = ~0} },
	{ MODKEY,                    XKB_KEY_comma,       focusmon,         {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY,                    XKB_KEY_period,      focusmon,         {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_less,        tagmon,           {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_greater,     tagmon,           {.i = WLR_DIRECTION_RIGHT} },
	TAGKEYS(          XKB_KEY_1, XKB_KEY_exclam,                        0),
	TAGKEYS(          XKB_KEY_2, XKB_KEY_at,                            1),
	TAGKEYS(          XKB_KEY_3, XKB_KEY_numbersign,                    2),
	TAGKEYS(          XKB_KEY_4, XKB_KEY_dollar,                        3),
	TAGKEYS(          XKB_KEY_5, XKB_KEY_percent,                       4),
	TAGKEYS(          XKB_KEY_6, XKB_KEY_asciicircum,                   5),
	TAGKEYS(          XKB_KEY_7, XKB_KEY_ampersand,                     6),
	TAGKEYS(          XKB_KEY_8, XKB_KEY_asterisk,                      7),
	TAGKEYS(          XKB_KEY_9, XKB_KEY_parenleft,                     8),
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_q,           quit,             {0} },
	{ MODKEY,                    XKB_KEY_b,           togglebar,        {0} },
	{ MODKEY|WLR_MODIFIER_CTRL,  XKB_KEY_b,           barcyclepos,      {.i = +1} },
	{ MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_SHIFT, XKB_KEY_b, barcyclepos, {.i = -1} },
	{ MODKEY|ALT_MODKEY,         XKB_KEY_b,           togglebarfloat,   {0} },
	{ MODKEY|ALT_MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_b, togglebarlayer,  {0} },

	/* focusdir 0 up 1 left 2 down 3 right. movecanvas 0 left 1 right 2 up 3 down */
	{ MODKEY,                    XKB_KEY_w,           focusdir,         {.ui = 0} },
	{ MODKEY,                    XKB_KEY_a,           focusdir,         {.ui = 1} },
	{ MODKEY,                    XKB_KEY_s,           focusdir,         {.ui = 2} },
	{ MODKEY,                    XKB_KEY_d,           focusdir,         {.ui = 3} },
	{ MODKEY,                    XKB_KEY_Home,         homecanvas,       {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Left,         movecanvas,       {.i = 0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Right,        movecanvas,       {.i = 1} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Up,           movecanvas,       {.i = 2} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Down,         movecanvas,       {.i = 3} },
	{ MODKEY,		     XKB_KEY_Tab,            centerwindow,     {0} },  /* dead too */

	{ MODKEY, XKB_KEY_equal, zoom,      {.f =  +0.25f} },
	{ MODKEY, XKB_KEY_minus, zoom,      {.f = -0.25f} },

	{ 0, XKB_KEY_XF86AudioRaiseVolume,  barspawn, {.v = volupcmd} },
	{ 0, XKB_KEY_XF86AudioLowerVolume,  barspawn, {.v = voldowncmd} },
	{ 0, XKB_KEY_XF86AudioMute,         barspawn, {.v = volmutecmd} },
	{ 0, XKB_KEY_XF86MonBrightnessUp,   barspawn, {.v = brupcmd} },
	{ 0, XKB_KEY_XF86MonBrightnessDown, barspawn, {.v = brdowncmd} },

	{ 0, XKB_KEY_Print, spawn, SHCMD("mkdir -p ~/Pictures && r=$(slurp) && grim -g \"$r\" ~/Pictures/$(date +%Y-%m-%d_%H-%M-%S).png") },

	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_Terminate_Server, quit, {0} },
#define CHVT(n) { WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_XF86Switch_VT_##n, chvt, {.ui = (n)} }
	CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
	CHVT(7), CHVT(8), CHVT(9), CHVT(10), CHVT(11), CHVT(12),
};

static const Button buttons[] = {
	{ ClkClient, MODKEY, BTN_LEFT,   moveresize,     {.ui = CurMove} },
	{ ClkClient, MODKEY, BTN_RIGHT,  moveresize,     {.ui = CurResize} },
	{ ClkClient, MODKEY|WLR_MODIFIER_SHIFT, BTN_LEFT, movecanvasmouse, {.f = 1} },
	{ ClkRoot, MODKEY|WLR_MODIFIER_SHIFT, BTN_LEFT, movecanvasmouse, {.f = 1} },
	{ ClkRoot, 0, BTN_LEFT, movecanvasmouse, {.f = 1} },
	{ ClkRoot, 0, BTN_RIGHT, riomenu, {0} },
	{ ClkTagBar, 0,         BTN_LEFT,   view,       {0} },
	{ ClkTagBar, 0,         BTN_RIGHT,  toggleview, {0} },
	{ ClkTagBar, MODKEY,    BTN_LEFT,   tag,        {0} },
	{ ClkTagBar, MODKEY,    BTN_RIGHT,  toggletag,  {0} },
	{ ClkStatus, 0,         BTN_MIDDLE, spawn,      {.v = termcmd} },
	{ ClkStatus, 0,         BTN_LEFT,   togglerioclock, {0} },
	{ ClkClient, MODKEY, SCROLL_UP,   zoom, {.f = +0.25f} },
	{ ClkClient, MODKEY, SCROLL_DOWN, zoom, {.f = -0.25f} },

	{ ClkRoot, MODKEY, SCROLL_UP,   zoom, {.f = +0.25f} },
	{ ClkRoot, MODKEY, SCROLL_DOWN, zoom, {.f = -0.25f} },

	{ ClkRoot, 0, SCROLL_UP,   zoom, {.f = +0.25f} },
	{ ClkRoot, 0, SCROLL_DOWN, zoom, {.f = -0.25f} },
};

static const TabletButton tabletbuttons[] = {
	{ 0,            0,                     NULL,       {0} }
};

static const TabletPadButton tabletpadbuttons[] = {
	{ 0,            0,                     NULL,       {0} }
};
