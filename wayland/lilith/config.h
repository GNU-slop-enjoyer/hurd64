/* the real settings are in config/ */

#define COLOR(hex)    { ((hex >> 24) & 0xFF) / 255.0f, \
                        ((hex >> 16) & 0xFF) / 255.0f, \
                        ((hex >> 8) & 0xFF) / 255.0f, \
                        (hex & 0xFF) / 255.0f }

#define TAGCOUNT (9)  /* bartags and TAGKEYS have to agree */

static int log_level = WLR_SILENT;

#include "config/config_appearance.h"
#include "config/config_animation.h"
#include "config/config_bar.h"
#include "config/config_input.h"

/* app_id title tags floating monitor */
static const Rule rules[] = {
	{ NULL,               NULL,       0,            0,           -1 },
};

#define CENTER_NEW_WINDOWS 0

/* name scale transform x y resx resy refresh mode adaptive */
static const MonitorRule monrules[] = {
	{ NULL, 1, WL_OUTPUT_TRANSFORM_NORMAL, -1, -1, 0, 0, 0.0f, 0, 0 },
};

#include "config/config_keys.h"
