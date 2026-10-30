/* times are seconds. beziers are css cubic-bezier x1 y1 x2 y2 and y over 1 overshoots */

/* tag switch. style 0 slides the view 1 fades the old windows 2 plays close then open */
#define animatetag   1
#define tagstyle     2
#define tagduration  0.4
#define tagbezier    ".28,.67,.28,1.04"

#define TAG_KEEP_VIEW 1  /* 0 gives every tag its own canvas spot */

/* throw a window and it keeps sliding */
#define animateinertia    1
#define inertiatau        0.4
#define inertiagain       0.6
#define inertiamaxspeed   2400  /* px/s */
#define inertiaminstart   150
#define inertiaminspeed   20
#define inertiawindowms   100  /* how far back we look for the throw speed */
#define inertiastalems    100

#define animatecanvas  1
#define canvasduration 0.7
#define canvasbezier   ".28,.67,.28,1.04"

#define animateclose   1
#define closeduration  0.4
#define closebezier    ".28,.67,.28,1.04"
#define closescale     0.8
#define closealpha     0.0

#define animateopen    1
#define openduration   0.4
#define openbezier     ".28,.67,.28,1.04"
#define openscale      0.8
#define openalpha      0.0

#define ZOOMMIN 0.3
#define ZOOMMAX 1
#define animatezoom 1
#define zoomduration 0.55
#define zoombezier ".22,1.3,.36,1.0"
#define dragbyzoom 0  /* 1 divides canvas drags by the zoom */
