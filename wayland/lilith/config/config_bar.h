/* colors are 0xRRGGBBAA. sizes in px */

enum { BarTop, BarBottom, BarLeft, BarRight };
enum { BarRotCW, BarRotCCW, BarUpright };
enum { BarAlignStart, BarAlignCenter, BarAlignEnd };
/* BarModSpacer eats the free space */
enum { BarModTags, BarModStatus, BarModSpacer,
       BarModVolume, BarModBrightness, BarModBattery,
       BarModCpu, BarModRam, BarModTemp, BarModLayout, BarModCount };
enum { BarDecoNone, BarDecoInner, BarDecoOuter, BarDecoBoth, BarDecoBorder };
enum { BarOccNone, BarOccDwm, BarOccDot, BarOccLine };
enum { BarAnimSlide, BarAnimFade };
enum { BarHideSlide, BarHideFade, BarHideBoth };
/* these carry on from SchemeNorm SchemeSel SchemeUrg in lilith.c */
enum { SchemeOcc = SchemeUrg + 1, SchemeBar, SchemeStatus, SchemeSep, SchemeBorder,
       SchemeVolume, SchemeMuted, SchemeBrightness,
       SchemeBattery, SchemeBatChg, SchemeBatLow,
       SchemeCpu, SchemeCpuWarn, SchemeRam, SchemeRamWarn,
       SchemeTemp, SchemeTempWarn, SchemeLayout, SchemeCount };

static const int showbar         = 1;
static const int barpos          = BarBottom;
static const int barreserve      = 1;  /* 0 floats it over the windows */
static const int barabovewindows = 1;
static const int barmargin[4]    = { 0, 0, 0, 0 };  /* top right bottom left */
static const int bargap          = 0;
static const int barthickness    = 0;  /* 0 means font height plus padding */
static const int barlength       = 0;
static const int barlengthpct    = 0;  /* 0 is the whole edge */
static const int baralign        = BarAlignCenter;
static const int barvertmode     = BarRotCW;

static const int barborderpx     = 1;
static const int barpad          = 0;
/* fontconfig patterns. put an icon font last if you use the ICO_ glyphs */
static const char *barfonts[]    = { "PxPlus IBM VGA 8x16:pixelsize=16", "monospace:pixelsize=16" };
static const int barvpad         = 2;
static const int barlrpad        = 8;

static const int barorder[]      = { BarModTags, BarModSpacer, BarModCpu, BarModRam,
	BarModVolume, BarModBattery, BarModStatus };
static const int barmodulegap    = 8;
static const int barsepwidth     = 1;
static const int barseppct       = 100;
static const char *barseptext    = "";
static const int barcontentalign = BarAlignStart;

static const char *bartags[TAGCOUNT] = { " 1 ", " 2 ", " 3 ", " 4 ", " 5 ", " 6 ", " 7 ", " 8 ", " 9 " };
static const int bartaggap       = 0;
static const int bartagminw      = 0;
static const int bartaginset     = 0;
static const int bartaghideempty = 0;
static const int bartagdeco      = BarDecoBorder;
static const int bartagline      = 1;
static const int bartagdecoall   = 0;
static const int baroccstyle     = BarOccDwm;
static const int baroccsize      = 0;
static const int baroccscheme    = 1;

static const int baranimtag      = 0;
static const int baranimstyle    = BarAnimSlide;
static const double baranimdur   = 0.25;
static const char *baranimbezier = ".28,.67,.28,1.04";

static const int barhideanim        = 1;
static const int barhidestyle       = BarHideBoth;
static const double barhidedur      = 0.35;
static const double barshowdur      = 0.35;
static const char *barhidebezier    = ".28,.67,.28,1.04";
static const char *barshowbezier    = ".28,.67,.28,1.04";

static const int barstatusallmons = 0;
#define barstatusstdin  0  /* dwm style status on stdin */
static const char *barclockfmt   = "%Y/%m/%d %a %H:%M:%S";

static const int barsysinterval  = 2000;  /* ms */
static const int barvolinterval  = 5000;
static const int barkickms       = 150;  /* wait this long after barspawn before rereading */
static const char *barvolcmd[]   = { "wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@", NULL };
static const char *barbatname    = "";  /* empty picks the first one */
static const char *barblname     = "";
static const char *bartemppath   = "";
static const int barbatwarn      = 20;
static const int barcpuwarn      = 85;
static const int barramwarn      = 85;
static const int bartempwarn     = 80;
static const char *barlayoutnames[] = { "us" };

/* font awesome */
#define ICO_VOL_MUTE  "\xef\x80\xa6"
#define ICO_VOL_LOW   "\xef\x80\xa7"
#define ICO_VOL_HIGH  "\xef\x80\xa8"
#define ICO_BRI       "\xef\x86\x85"
#define ICO_BAT_CHG   "\xef\x83\xa7"
#define ICO_BAT_0     "\xef\x89\x84"
#define ICO_BAT_1     "\xef\x89\x83"
#define ICO_BAT_2     "\xef\x89\x82"
#define ICO_BAT_3     "\xef\x89\x81"
#define ICO_BAT_4     "\xef\x89\x80"
#define ICO_CPU       "\xef\x8b\x9b"
#define ICO_RAM       "\xef\x87\x80"
#define ICO_TEMP      "\xef\x8b\x89"
#define ICO_LAYOUT    "\xef\x84\x9c"

/* %i icon %v number %s text %u used %t total */
static const char *barfmt_vol    = "vol %v%%";
static const char *barfmt_bri    = "%v%%";
static const char *barfmt_bat    = "bat %v%%";
static const char *barfmt_cpu    = "cpu %v%%";
static const char *barfmt_ram    = "mem %u/%tG";
static const char *barfmt_temp   = "%v°C";
static const char *barfmt_layout = "%s";

static const char *baricons_vol[3] = { ICO_VOL_MUTE, ICO_VOL_LOW, ICO_VOL_HIGH };
static const char *baricon_bri     = ICO_BRI;
static const char *baricons_bat[6] = { ICO_BAT_CHG, ICO_BAT_0, ICO_BAT_1, ICO_BAT_2, ICO_BAT_3, ICO_BAT_4 };
static const char *baricon_cpu     = ICO_CPU;
static const char *baricon_ram     = ICO_RAM;
static const char *baricon_temp    = ICO_TEMP;
static const char *baricon_layout  = ICO_LAYOUT;

/* fg bg accent. accent 0 means use fg */
static const uint32_t barcolors[SchemeCount][3] = {
	[SchemeNorm]      = { 0x000000ff, 0x00000000, 0 },
	[SchemeSel]       = { 0x000000ff, 0x9eeeeeff, 0x55aaaaff },
	[SchemeUrg]       = { 0xffffffff, 0x000099ff, 0 },
	[SchemeOcc]       = { 0x000000ff, 0x00000000, 0x55aaaaff },
	[SchemeBar]       = { 0x000000ff, 0xeaffffff, 0 },  /* acme tag */
	[SchemeStatus]    = { 0x000000ff, 0x00000000, 0 },
	[SchemeSep]       = { 0x8888ccff, 0x8888ccff, 0 },
	[SchemeBorder]    = { 0,          0x8888ccff, 0 },
	[SchemeVolume]    = { 0x000000ff, 0x00000000, 0 },
	[SchemeMuted]     = { 0x777777ff, 0x00000000, 0 },
	[SchemeBrightness]= { 0x000000ff, 0x00000000, 0 },
	[SchemeBattery]   = { 0x000000ff, 0x00000000, 0 },
	[SchemeBatChg]    = { 0x000000ff, 0x00000000, 0 },
	[SchemeBatLow]    = { 0xffffffff, 0xaa0000ff, 0 },
	[SchemeCpu]       = { 0x000000ff, 0x00000000, 0 },
	[SchemeCpuWarn]   = { 0xffffffff, 0xaa0000ff, 0 },
	[SchemeRam]       = { 0x000000ff, 0x00000000, 0 },
	[SchemeRamWarn]   = { 0xffffffff, 0xaa0000ff, 0 },
	[SchemeTemp]      = { 0x000000ff, 0x00000000, 0 },
	[SchemeTempWarn]  = { 0xffffffff, 0xaa0000ff, 0 },
	[SchemeLayout]    = { 0x000000ff, 0x00000000, 0 },
};
