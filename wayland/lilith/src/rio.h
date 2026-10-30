/*
 * rio's button 3 menu. New Resize Move Delete Hide and then
 * whatever got hidden. hold and release on an item or click
 * once and click again. the sight cursor picks a window and
 * clicking the bare canvas with it gives up.
 */

enum { Mnew, Mresize, Mmove, Mdelete, Mhide, Nmenu };
enum { Sweepnone, Sweepnew, Sweepreshape };

static const char *menu3str[Nmenu] = { "New", "Resize", "Move", "Delete", "Hide" };

#define NHIDE 32
#define NLAB  20  /* same as rio shortlabel */

static struct wlr_scene_buffer *menusb;
static int menuup, stuck, msel = -1, lasthit;
static int mnx, mny, mnw, mnh, itemht, nitem;
static double downx, downy;
static uint32_t downms;
static Client *hidden[NHIDE];
static int nhidden;
static char hidlab[NHIDE][NLAB*4 + 1];

static int picking = -1;
static int sweeping = Sweepnone;
static Client *victim;
static int eatup;

static const char *
menulab(int i)
{
	return i < Nmenu ? menu3str[i] : hidlab[i - Nmenu];
}

static void
shortlabel(char *d, const char *s)
{
	const char *p = s ? s : "", *q;
	int n;

	for (n = 0; *p && n < NLAB; n++) {
		q = p;
		bar_nextcp(&q);
		memcpy(d, p, (size_t)(q - p));
		d += q - p;
		p = q;
	}
	*d = '\0';
}

static void
gathhidden(void)
{
	Client *c;

	nhidden = 0;
	wl_list_for_each(c, &clients, link) {
		if (!c->riohidden || nhidden >= NHIDE)
			continue;
		hidden[nhidden] = c;
		shortlabel(hidlab[nhidden], client_get_title(c));
		if (!hidlab[nhidden][0])
			shortlabel(hidlab[nhidden], client_get_appid(c));
		nhidden++;
	}
}

static void
drawmenu(void)
{
	BarBuffer *bb;
	int i, y, b = riomenu_borderpx;
	uint32_t fg;

	if (!(bb = bar_newbuf(mnw, mnh, 0)))
		return;
	bar_rect(bb->image, 0, 0, mnw, mnh, riomenu_bordercolor);
	bar_rect(bb->image, b, b, mnw - 2*b, mnh - 2*b, riomenu_bgcolor);
	for (i = 0; i < nitem; i++) {
		fg = riomenu_fgcolor;
		y = b + i*itemht;
		if (i == msel) {
			bar_rect(bb->image, b, y, mnw - 2*b, itemht, riomenu_selbgcolor);
			fg = riomenu_selfgcolor;
		}
		if (i == Nmenu)
			bar_rect(bb->image, b, y, mnw - 2*b, 1, riomenu_bordercolor);
		bar_text_center(bb->image, b, y, mnw - 2*b, itemht, menulab(i), fg);
	}
	wlr_scene_buffer_set_buffer(menusb, &bb->base);
	wlr_buffer_drop(&bb->base);
}

static int
menuitem(double cx, double cy)
{
	int x = (int)cx - mnx, y = (int)cy - mny - riomenu_borderpx;

	if (x < 0 || x >= mnw || y < 0 || y >= nitem*itemht)
		return -1;
	return y / itemht;
}

static void
menudown(void)
{
	menuup = stuck = 0;
	msel = -1;
	if (menusb)
		wlr_scene_node_set_enabled(&menusb->node, 0);
	cursor_mode = CurNormal;
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
}

void
riomenu(const Arg *arg)
{
	struct wlr_box mb;
	int i, tw, wid = 0;

	if (!bar_font || !selmon) {
		spawn(&(Arg){.v = termcmd});
		return;
	}
	if (!menusb && !(menusb = wlr_scene_buffer_create(layers[LyrBlock], NULL)))
		return;

	gathhidden();
	nitem = Nmenu + nhidden;
	for (i = 0; i < nitem; i++)
		if ((tw = bar_textw(menulab(i), strlen(menulab(i)))) > wid)
			wid = tw;
	itemht = bar_font->height + 2*riomenu_vpad;
	mnw = wid + 2*riomenu_hpad + 2*riomenu_borderpx;
	mnh = nitem*itemht + 2*riomenu_borderpx;
	if (lasthit >= nitem)
		lasthit = 0;

	/* last pick lands under the pointer like menuhit does */
	mnx = (int)cursor->x - mnw/2;
	mny = (int)cursor->y - riomenu_borderpx - lasthit*itemht - itemht/2;
	mb = selmon->m;
	mnx = MAX(mb.x, MIN(mnx, mb.x + mb.width - mnw));
	mny = MAX(mb.y, MIN(mny, mb.y + mb.height - mnh));

	menuup = 1;
	stuck = 0;
	downx = cursor->x;
	downy = cursor->y;
	downms = (uint32_t)bar_now_ms();
	msel = menuitem(cursor->x, cursor->y);
	cursor_mode = CurNormal;
	wlr_seat_pointer_clear_focus(seat);
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
	wlr_scene_node_set_position(&menusb->node, mnx, mny);
	wlr_scene_node_raise_to_top(&menusb->node);
	wlr_scene_node_set_enabled(&menusb->node, 1);
	drawmenu();
}

static void
hidewin(Client *c)
{
	if (!c || c->riohidden || client_is_unmanaged(c))
		return;
	c->riohidden = c->tags ? c->tags : 1;
	c->tags = 0;
	focusclient(focustop(selmon), 1);
	arrange(c->mon ? c->mon : selmon);
	printstatus();
}

static void
unhidewin(Client *c)
{
	uint32_t t;

	if (!c || !c->riohidden || !selmon)
		return;
	t = selmon->tagset[selmon->seltags];
	c->riohidden = 0;
	if (c->mon != selmon)
		setmon(c, selmon, t);
	c->tags = t;
	arrange(selmon);
	focusclient(c, 1);
	printstatus();
}

static void
startsweep(int how)
{
	spawnregion_active = 1;
	has_pending_spawn_geo = 0;
	sweeping = how;
	wlr_seat_pointer_clear_focus(seat);
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "crosshair");
}

static void
menuhit(int i)
{
	if (i < 0)
		return;
	lasthit = i;
	if (i >= Nmenu) {
		unhidewin(hidden[i - Nmenu]);
		return;
	}
	if (i == Mnew) {
		startsweep(Sweepnew);
		return;
	}
	picking = i;
	wlr_seat_pointer_clear_focus(seat);
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "sight");
}

static int
riomotion(void)
{
	int s;

	if (menuup) {
		if ((s = menuitem(cursor->x, cursor->y)) != msel) {
			msel = s;
			drawmenu();
		}
		return 1;
	}
	if (picking >= 0) {
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "sight");
		return 1;
	}
	return 0;
}

static int
riopress(void)
{
	Client *c;
	int what;

	if (menuup)
		return 1;
	if (picking < 0)
		return 0;

	what = picking;
	picking = -1;
	xytonode(cursor->x, cursor->y, NULL, &c, NULL, NULL, NULL);
	if (!c || client_is_unmanaged(c))
		goto out;
	switch (what) {
	case Mmove:
		/* the normal drag takes it from here */
		cursor_mode = CurNormal;
		focusclient(c, 1);
		moveresize(&(Arg){.ui = CurMove});
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "plan9-box");
		return 1;
	case Mresize:
		victim = c;
		eatup = 1;
		cursor_mode = CurNormal;
		startsweep(Sweepreshape);
		return 1;
	case Mdelete:
		client_send_close(c);
		break;
	case Mhide:
		hidewin(c);
		break;
	}
out:
	eatup = 1;
	cursor_mode = CurNormal;
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
	return 1;
}

static int
riorelease(void)
{
	int i;

	if (eatup) {
		eatup = 0;
		cursor_mode = CurNormal;
		return 1;
	}
	if (!menuup)
		return 0;

	/* a click that didnt move leaves it up */
	if (!stuck && fabs(cursor->x - downx) < 4 && fabs(cursor->y - downy) < 4
			&& (uint32_t)bar_now_ms() - downms < 300) {
		stuck = 1;
		cursor_mode = CurNormal;
		return 1;
	}
	i = menuitem(cursor->x, cursor->y);
	menudown();
	menuhit(i);
	return 1;
}

/* spawnregion_finish calls this first. 1 means it was ours */
static int
riosweptout(int ok, struct wlr_box r)
{
	int how = sweeping;
	Client *c = victim;

	if (how == Sweepnone)
		return 0;
	sweeping = Sweepnone;
	victim = NULL;
	if (!ok)
		return 1;
	if (how == Sweepnew) {
		pending_spawn_geo = r;
		has_pending_spawn_geo = 1;
		spawn(&(Arg){.v = termcmd});
	} else if (c) {
		has_pending_spawn_geo = 0;
		setfloating(c, 1);
		resize(c, r, 0);
		focusclient(c, 1);
	}
	return 1;
}

static void
rioforget(Client *c)
{
	if (c == victim) {
		victim = NULL;
		if (sweeping == Sweepreshape)
			spawnregion_finish();
	}
	c->riohidden = 0;
}
