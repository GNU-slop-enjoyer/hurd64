# lilith-fork
(i cant write README's well)

a wlroots wayland compositor. theres no tiling and no rules engine. windows
float on an endless canvas wherever you drop them and you can pan and zoom the
canvas so you can spread stuff out and zoom out to see all of it like a
whiteboard. this fork dresses it up as rio and also a music player 9cynth.


## what it does

 floating windows on an endless canvas. drag them anywhere and drag the empty
 canvas to pan. throw a window and it slides for a bit
 two borders per window (inner and outer) each with its own width and color
 for focused and urgent and unfocused
 scroll on the canvas to zoom. the tag and open and close animations follow
 the zoom so nothing looks off
 zoomfit (win+z) zooms out until every window fits. mostly for screenshots
 windows snap to each other and to the screen edges while you drag
 its own status bar with tags and clock and volume and brightness and battery
 and cpu and ram and temperature and keyboard layout. put it on any edge and
 make it see through or not. it hides with an animation and needs no daemon
 since fcft and pixman draw it
 spawn region. drag a box on the empty canvas and the next window opens in
 it. SPAWNREGION_STYLE in config_appearance.h makes the box an outline or a
 fill or nothing
 every tag shares one canvas view unless you flip `TAG_KEEP_VIEW` in
 config_animation.h
 keyboard layouts with an indicator in the bar. graphics tablets work and
 the pen maps to the window under it. xwayland is there for old stuff

## the rio bits

right click the empty canvas for rio's menu. New Resize Move Delete Hide and
then any hidden windows. hold and let go on an item or click once and click
again.

 New: sweep a box with any button and a terminal opens in it
 Resize: click a window with the sight cursor then sweep its new shape
 Move: press on a window and drag
 Delete: click a window to close it
 Hide: click a window and it goes into the menu until you pick it again

a stats(1) window sits in the top left corner and graphs load and memory and
interrupts and system time and context switches. click the clock on the bar
to get a clock(1) next to it. both sit under every window and clicks go
straight through them.

## 9cynth

acme but for music with cynthiune's player panel and song table stuck on it
is a normal wayland client in src/9cynth.c and builds with lilith win+m
starts it

songs go in ~/Music/Songs and subfolders are fine. every other folder
~/Music/<name> is a playlist and is usually just symlinks into Songs. it
plays offline with libav and pulse (pipewire-pulse) or alsa.

the mouse works like acme

 b1 selects. drag for a range and shift or ctrl to extend or toggle.
 double click plays. drag selected songs onto a playlist on the left to add
 them
 b2 runs the word under it in any tag. sweep to run a phrase like
 Add chill or New roadtrip or Look beatles or Vol 40
 b3 plays the song under it. in a tag it opens the playlist with that name
 or looks the word up
 on scroll bars b1 goes up and b3 down and b2 jumps. the wheel over the
 player is volume and over the seek bar it seeks

typing goes into whatever tag the mouse is over so you can just write
commands there.

the commands are Play Pause Stop Next Prev Seek+ Seek- Shuffle Repeat Vol+
Vol- Vol n Get (rescan) Look words (Look alone clears it) Sort how (file
title artist album or time. clicking a column header does it too) New name
Add name Remove Delete name Snarf (copies artist - title) and Exit.
Remove and Delete only ever unlink symlinks so your real files are safe.

keys when the mouse isnt over a tag. space plays or pauses and enter plays the
selection. left and right seek 5s. n p s are next prev stop. + and - change
volume. z is shuffle and r is repeat. up and down move the selection and tab
goes to the next playlist. delete is Remove and ctrl-q quits.

flags are -f font for a pango font and -m musicdir and -x command to
run something at start. NINECYNTH_AUDIO=null plays into nothing in real time
which is handy for testing. volume and shuffle and repeat and the last list
are saved in ~/.config/9cynth/state.

## building

i have these names for the packages adjust for your distro:


base-devel wlroots0.19 libinput libxkbcommon libdrm fcft pixman
xorg-server-xwayland xcb-util-wm


9cynth also wants pango cairo glib ffmpeg libpulse and alsa-lib.

the easy way is the 9front style installer that comes prebuilt:


git clone https://codeberg.org/Yuas/lilith-fork

cd lilith

inst/start


it goes checkdeps then compile then install. checkdeps only lists what you
dont have yet so install that from another window

## config

all of it is in config/

 config_appearance.h borders and gaps and wallpaper and the spawn region and the rio colors
 config_animation.h tag switch and open and close and zoom animations and inertia
 config_bar.h the bar. where it goes and what modules and colors and font
 config_input.h keyboard layouts and touchpad and tablet
 config_keys.h commands and every key and mouse binding

colors are 0xRRGGBBAA hex. run make after editing and restart.

lilith doesnt draw a wallpaper by default and leaves that to swaybg or
whatever you run. set wallpaper_path in config_appearance.h if you want one
that zooms with the canvas.

## some keys (config_keys.h has them all)

 win+q terminal. win+c close. win+shift+q quit
 win+r launcher. win+m 9cynth. win+tab centers a window and focuses it
 win+1..9 tags. win+home resets the canvas
 win+left drag moves a window and win+right drag resizes it. scroll zooms
 win+w a s d focus by direction. win+shift+arrows pan the canvas
 win+b hides the bar. win+ctrl+b moves it to the next edge

## credits

lilith is by ohto121. the LICENSE files say who wrote which parts.

like fr ohto is a goated man check his github out https://github.com/ohto121/lilith
his README.md lowkey prettier then mine 
