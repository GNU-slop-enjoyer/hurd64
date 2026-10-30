static const int sloppyfocus               = 1;
static const int bypass_surface_visibility = 0;  /* 1 lets hidden surfaces inhibit idle too */
static const float rootcolor[]             = COLOR(0x777777ff);  /* rio grey */

/* wallpaper. empty path means none and the canvas stays rootcolor */
static const char *wallpaper_path              = "";
static const int wallpaper_mode                = 1;  /* 1 zooms and pans with the canvas 0 is a plain fitted image */
static const float wallpaper_bgcolor[]         = COLOR(0x777777ff);
static const int wallpaper_fit                 = 0;  /* mode 0: 0 cover 1 contain anything else stretches */
static const double wallpaper_zoom_strength    = 1.0;
static const double wallpaper_pan_factor       = 0.3;  /* below 1 gives parallax */
static const int wallpaper_edge                = 1;  /* never let rootcolor peek out at the edges */

/* borders. inner then outer */
static const unsigned int innerborderpx            = 1;
static const float focused_inner_border_color[]    = COLOR(0x55aaaaff);
static const float urgent_inner_border_color[]     = COLOR(0x000099ff);
static const float unfocused_inner_border_color[]  = COLOR(0x9eeeeeff);

static const unsigned int outerborderpx            = 3;
static const float focused_outer_border_color[]    = COLOR(0x55aaaaff);  /* DGreygreen */
static const float urgent_outer_border_color[]     = COLOR(0x000099ff);
static const float unfocused_outer_border_color[]  = COLOR(0x9eeeeeff);  /* DPalegreygreen */

#define WINDOW_GAP 0  /* snap gap. negative overlaps the borders */

/* the box you drag on the empty canvas. 0 hidden 1 filled 2 outlined */
#define SPAWNREGION_STYLE 2
#define SPAWNREGION_COLOR COLOR(0x55aaaa50)
static const unsigned int spawnregion_innerborderpx  = 1;
static const unsigned int spawnregion_outerborderpx  = 3;
static const float spawnregion_inner_border_color[]  = COLOR(0x55aaaaff);
static const float spawnregion_outer_border_color[]  = COLOR(0x55aaaaff);

static const float fullscreen_bg[] = {0.0f, 0.0f, 0.0f, 1.0f};

/* b3 menu on the canvas (src/rio.h). this is 9front black.
 * old plan 9 green is bg eaffea fg 000000 selbg 448844 selfg eaffea border 88cc88 */
static const uint32_t riomenu_bgcolor     = 0x000000ff;
static const uint32_t riomenu_fgcolor     = 0xffffffff;
static const uint32_t riomenu_selbgcolor  = 0xffffffff;
static const uint32_t riomenu_selfgcolor  = 0x000000ff;
static const uint32_t riomenu_bordercolor = 0x000000ff;
static const int riomenu_borderpx         = 2;
static const int riomenu_hpad             = 6;
static const int riomenu_vpad             = 1;

/* stats(1) and clock(1) in the top left corner (src/riostats.h).
 * click the bar clock to get the clock */
static const int riostats_show      = 1;
static const int riostats_w         = 160;
static const int riostats_h         = 116;
static const int rioclock_w         = 112;
static const int riogadget_x        = 0;
static const int riogadget_y        = 0;
static const int riogadget_borderpx = 4;
static const uint32_t riogadget_bordercolor = 0x9eeeeeff;
static const int riostats_interval  = 1000;  /* ms. graphs move a pixel per tick */
