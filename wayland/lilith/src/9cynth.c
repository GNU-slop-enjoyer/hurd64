/*
 * 9cynth. acme for music with a bit of cynthiune
 * songs live in ~/Music/Songs and every other dir in ~/Music is a playlist.
 * b1 selects b2 runs a word b3 plays or opens.
 */
#define _GNU_SOURCE
#include <cairo.h>
#include <pango/pangocairo.h>
#include <glib.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>
#include <linux/input-event-codes.h>
#include "xdg-shell-client-protocol.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#ifndef __GNU__
#include <alsa/asoundlib.h>
#endif
#include <stdatomic.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <unistd.h>

#define nelem(x) (int)(sizeof(x)/sizeof((x)[0]))

typedef struct Rect {
	int x, y, w, h;
} Rect;

enum {
	Ctagbg  = 0xEAFFFF,	/* tags */
	Ctaghi  = 0x9EEEEE,
	Cbodybg = 0xFFFFEA,	/* bodies */
	Cbodyhi = 0xEEEE9E,
	Cscroll = 0x99994C,
	Cbutton = 0x8888CC,
	Cmod    = 0x000099,	/* dirty box */
	Cborder = 0x55AAAA,
	Cblack  = 0x000000,
	Cwhite  = 0xFFFFFF,
	Cgrey   = 0x777777,
	Cred    = 0xAA0000,	/* b2 */
	Cgreen  = 0x006600,	/* b3 */
};

typedef struct Meta Meta;
struct Meta {
	char *path;		/* realpath and the songtab key */
	char *title, *artist, *album;
	double dur;		/* -1 if we dunno */
	int scanned;		/* 0 not yet 1 fine -1 broken */
};

typedef struct Ent {
	Meta *m;
	char *rel;
	char *name;		/* for files with no tags */
	char *key;
	char sel;
} Ent;

enum { Sfile, Stitle, Sartist, Salbum, Stime, Nsort };
static const char *sortname[Nsort] = { "file", "title", "artist", "album", "time" };

typedef struct List {
	char *name;
	char *dir;
	Ent *e;
	int n, cap;
	int *view;		/* into e after look and sort */
	int nview;
	char look[256];
	int sort;
	int top;
	int anchor;		/* for shift click */
} List;

static GMutex metalk;
static GHashTable *songtab;
static GAsyncQueue *scanq;
static atomic_int unscanned;

static List **lists;
static int nlists;
static int curlist;
static int dirscroll;
static char musicdir[PATH_MAX];

enum { Stopped, Playing, Paused };
enum { Roff, Rall, Rone };
static const char *repname[] = { "off", "all", "one" };

/* threads poke the main loop with these */
enum { EVend, EVerr, EVmeta, EVnoaudio };
typedef struct Ev {
	int type, code;
	char *s;
} Ev;
static GAsyncQueue *evq;
static int evfd = -1;

#define RATE 48000
#define RINGCAP (1<<18)	/* floats. stereo interleaved */

static struct {
	float ring[RINGCAP];
	atomic_size_t rd, wr;
	atomic_llong played;	/* frames given to the card since base */
	atomic_llong latency;
	_Atomic double base;
	atomic_int volume;
	atomic_int eof, endposted, gen;
	atomic_int out;
	atomic_int devflush;
	atomic_int flushgen;
	GMutex outlk;		/* rd wr vs flushring */

	GMutex lk;
	GCond cv;
	char *loadpath;		/* decoder orders. hold lk */
	int loadgen;
	double seekto;
	int stopreq, quit;
	atomic_int busy;	/* new order. stop writing */

	int state;		/* main thread */
	double dur;
	Meta *cur;

	unsigned char *art;	/* ARTSZ squared bgra. hold lk */
	int artgen;
} P;

#define ARTSZ 160

static Meta **queue;
static int nqueue;
static int *order;
static int qpos = -1;
static char qfrom[256];
static int shuffle, repeat;
static int duds;		/* broken tracks in a row */
static cairo_surface_t *coverpic;
static int coverseen = -1;

enum { Tmain, Tlcol, Trcol, Tdir, Tnp, Tlist, Ntag };
typedef struct Tag {
	char s[512];
	int q;			/* cursor in bytes */
	Rect r;
	int box;
} Tag;
static Tag tags[Ntag];

enum { Bprev, Bplay, Bstop, Bnext, Bpause, Brepeat, Bshuffle, Nbtn };

static cairo_surface_t *canvas;
static PangoContext *pctx;
static PangoLayout *lay, *biglay;
static char *fontname = "PxPlus IBM VGA 8x16, DejaVu Sans Mono 16px";
static int W = 1000, H = 640;
static int lh, tagh, bigh;
static const int sbw = 12;
static int colx;
static Rect rdirbody, rnpbody, rlistbody;
static Rect rbanner, rseek, rbtn[Nbtn], rvol, rmsg;
static int dirty = 1;
static int alive = 1;
static char msg[512];
static int ptrx, ptry;

enum { Mshift = 1, Mctrl = 2 };
static int mods;

enum { Gnone, Gsweep, Gselect, Gdrag, Gscroll, Gseek, Gbutton, Gvol };
static struct {
	int kind, button, tag, q0, q1;
	int row, moved, x0, y0, btn;
	double seekpos;
	int droprow;
} G;

static void gripe(const char *fmt, ...) G_GNUC_PRINTF(1, 2);
static void gripe(const char *fmt, ...)
{
	va_list a;
	va_start(a, fmt);
	vsnprintf(msg, sizeof msg, fmt, a);
	va_end(a);
	dirty = 1;
}

static void
tellmain(int type, int code, char *s)
{
	Ev *e;
	uint64_t one;

	e = g_new0(Ev, 1);
	e->type = type;
	e->code = code;
	e->s = s;
	g_async_queue_push(evq, e);
	one = 1;
	if(write(evfd, &one, sizeof one) < 0){
		/* whatever. it gets picked up next pass */
	}
}

static char *
dictget(AVFormatContext *fc, int si, const char *k)
{
	AVDictionaryEntry *e;

	e = av_dict_get(fc->metadata, k, NULL, 0);
	if((e == NULL || e->value[0] == 0) && si >= 0)
		e = av_dict_get(fc->streams[si]->metadata, k, NULL, 0);
	if(e == NULL || e->value[0] == 0)
		return NULL;
	return g_strstrip(g_strdup(e->value));
}

static gpointer
scanner(gpointer u)
{
	Meta *m;
	AVFormatContext *fc;
	char *t, *a, *al;
	double d;
	int si, ok;

	(void)u;
	for(;;){
		m = g_async_queue_pop(scanq);
		fc = NULL;
		t = a = al = NULL;
		d = -1;
		ok = -1;
		if(avformat_open_input(&fc, m->path, NULL, NULL) == 0){
			avformat_find_stream_info(fc, NULL);
			si = av_find_best_stream(fc, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
			if(si >= 0){
				ok = 1;
				t = dictget(fc, si, "title");
				a = dictget(fc, si, "artist");
				if(a == NULL)
					a = dictget(fc, si, "album_artist");
				al = dictget(fc, si, "album");
				if(fc->duration != AV_NOPTS_VALUE && fc->duration > 0)
					d = fc->duration / (double)AV_TIME_BASE;
			}
			avformat_close_input(&fc);
		}
		g_mutex_lock(&metalk);
		m->title = t;
		m->artist = a;
		m->album = al;
		m->dur = d;
		m->scanned = ok;
		g_mutex_unlock(&metalk);
		if(!atomic_exchange(&unscanned, 1))
			tellmain(EVmeta, 0, NULL);
	}
	return NULL;
}

static Meta *
getmeta(const char *path)
{
	char *rp, *key;
	Meta *m;

	rp = realpath(path, NULL);
	key = g_strdup(rp ? rp : path);
	free(rp);
	g_mutex_lock(&metalk);
	m = g_hash_table_lookup(songtab, key);
	if(m == NULL){
		m = g_new0(Meta, 1);
		m->path = key;
		m->dur = -1;
		g_hash_table_insert(songtab, key, m);
		g_async_queue_push(scanq, m);
	}else
		g_free(key);
	g_mutex_unlock(&metalk);
	return m;
}

static int
isaudio(const char *name)
{
	static const char *ext[] = {
		"mp3", "flac", "ogg", "oga", "opus", "m4a", "aac", "wav", "wave",
		"aif", "aiff", "wma", "ape", "wv", "mka", "mpc", "alac", "mp2",
		"ac3", "dsf", "tta", "spx", "webm", "mp4",
	};
	const char *dot;
	int i;

	dot = strrchr(name, '.');
	if(dot == NULL)
		return 0;
	for(i = 0; i < nelem(ext); i++)
		if(g_ascii_strcasecmp(dot+1, ext[i]) == 0)
			return 1;
	return 0;
}

static void
addent(List *l, Meta *m, char *rel)
{
	Ent *e;
	char *b, *dot;

	if(l->n == l->cap){
		l->cap = l->cap ? 2*l->cap : 64;
		l->e = g_renew(Ent, l->e, l->cap);
	}
	e = &l->e[l->n++];
	memset(e, 0, sizeof *e);
	e->m = m;
	e->rel = rel;
	b = g_path_get_basename(rel);
	if((dot = strrchr(b, '.')) != NULL && dot != b)
		*dot = 0;
	e->name = b;
	e->key = g_utf8_collate_key_for_filename(rel, -1);
}

static void
walk(List *l, const char *dir, const char *rel, int depth)
{
	DIR *d;
	struct dirent *de;
	struct stat st;
	char *p, *r;

	if((d = opendir(dir)) == NULL)
		return;
	while((de = readdir(d)) != NULL){
		if(de->d_name[0] == '.')
			continue;
		p = g_build_filename(dir, de->d_name, NULL);
		r = rel ? g_build_filename(rel, de->d_name, NULL) : g_strdup(de->d_name);
		if(stat(p, &st) == 0 && S_ISDIR(st.st_mode) && depth < 8){
			walk(l, p, r, depth+1);
			g_free(r);
		}else if(stat(p, &st) == 0 && S_ISREG(st.st_mode) && isaudio(de->d_name))
			addent(l, getmeta(p), r);
		else
			g_free(r);
		g_free(p);
	}
	closedir(d);
}

static int
entcmp(const void *a, const void *b)
{
	return strcmp(((Ent*)a)->key, ((Ent*)b)->key);
}

static List *
loadlist(const char *name)
{
	List *l;

	l = g_new0(List, 1);
	l->name = g_strdup(name);
	l->dir = g_build_filename(musicdir, name, NULL);
	walk(l, l->dir, NULL, 0);
	if(l->n > 0)
		qsort(l->e, l->n, sizeof l->e[0], entcmp);
	return l;
}

static void
freelist(List *l)
{
	int i;

	for(i = 0; i < l->n; i++){
		g_free(l->e[i].rel);
		g_free(l->e[i].name);
		g_free(l->e[i].key);
	}
	g_free(l->e);
	g_free(l->view);
	g_free(l->name);
	g_free(l->dir);
	g_free(l);
}

static const char *
enttitle(Ent *e)
{
	return e->m->title ? e->m->title : e->name;
}

static List *sortlist;

static int
viewcmp(const void *a, const void *b)
{
	Ent *x, *y;
	const char *s, *t;
	int r;

	x = &sortlist->e[*(const int*)a];
	y = &sortlist->e[*(const int*)b];
	r = 0;
	switch(sortlist->sort){
	case Stitle:
		r = g_utf8_collate(enttitle(x), enttitle(y));
		break;
	case Sartist:
	case Salbum:
		s = sortlist->sort == Sartist ? x->m->artist : x->m->album;
		t = sortlist->sort == Sartist ? y->m->artist : y->m->album;
		/* no tags go last */
		if(s == NULL || t == NULL)
			r = (s == NULL) - (t == NULL);
		else
			r = g_utf8_collate(s, t);
		break;
	case Stime:
		r = (x->m->dur > y->m->dur) - (x->m->dur < y->m->dur);
		break;
	}
	if(r == 0)
		r = strcmp(x->key, y->key);
	return r;
}

static int
matches(Ent *e, char **words)
{
	char *hay, *f;
	int i, ok;

	hay = g_strjoin("\n", e->rel, e->m->title ? e->m->title : "",
		e->m->artist ? e->m->artist : "", e->m->album ? e->m->album : "", NULL);
	f = g_utf8_casefold(hay, -1);
	ok = 1;
	for(i = 0; words[i] && ok; i++)
		if(words[i][0] && strstr(f, words[i]) == NULL)
			ok = 0;
	g_free(f);
	g_free(hay);
	return ok;
}

/* hold metalk */
static void
buildview(List *l)
{
	char *f, **words;
	int i, vis;

	g_free(l->view);
	l->view = g_new(int, l->n + 1);
	l->nview = 0;
	f = g_utf8_casefold(l->look, -1);
	words = g_strsplit_set(f, " \t", -1);
	for(i = 0; i < l->n; i++)
		if(l->look[0] == 0 || matches(&l->e[i], words))
			l->view[l->nview++] = i;
	g_strfreev(words);
	g_free(f);
	sortlist = l;
	if(l->nview > 1)
		qsort(l->view, l->nview, sizeof(int), viewcmp);
	vis = rlistbody.h > 0 ? (rlistbody.h - lh - 1) / lh : 1;
	if(l->top > l->nview - vis)
		l->top = l->nview - vis;
	if(l->top < 0)
		l->top = 0;
	if(l->anchor >= l->nview)
		l->anchor = 0;
}

static List *
current(void)
{
	return nlists > 0 ? lists[curlist] : NULL;
}

static void setlisttag(void);

static int
findlist(const char *name)
{
	int i;

	for(i = 0; i < nlists; i++)
		if(strcmp(lists[i]->name, name) == 0)
			return i;
	return -1;
}

static int
dircmp(const void *a, const void *b)
{
	return g_utf8_collate(*(char**)a, *(char**)b);
}

static void
rescan(void)
{
	DIR *d;
	struct dirent *de;
	struct stat st;
	GPtrArray *names;
	char *p, *keep, look[256];
	List *old;
	int i, sort, top;

	keep = g_strdup(nlists ? lists[curlist]->name : "Songs");
	look[0] = 0;
	sort = Sfile;
	top = 0;
	if((old = current()) != NULL){
		strcpy(look, old->look);
		sort = old->sort;
		top = old->top;
	}
	for(i = 0; i < nlists; i++)
		freelist(lists[i]);
	g_free(lists);

	names = g_ptr_array_new_with_free_func(g_free);
	if((d = opendir(musicdir)) != NULL){
		while((de = readdir(d)) != NULL){
			if(de->d_name[0] == '.' || strcmp(de->d_name, "Songs") == 0)
				continue;
			p = g_build_filename(musicdir, de->d_name, NULL);
			if(stat(p, &st) == 0 && S_ISDIR(st.st_mode))
				g_ptr_array_add(names, g_strdup(de->d_name));
			g_free(p);
		}
		closedir(d);
	}
	g_ptr_array_sort(names, dircmp);

	nlists = names->len + 1;
	lists = g_new(List*, nlists);
	lists[0] = loadlist("Songs");
	for(i = 0; i < (int)names->len; i++)
		lists[i+1] = loadlist(names->pdata[i]);
	g_ptr_array_free(names, TRUE);

	curlist = findlist(keep);
	if(curlist < 0)
		curlist = 0;
	else{
		strcpy(lists[curlist]->look, look);
		lists[curlist]->sort = sort;
		lists[curlist]->top = top;
	}
	g_free(keep);
	g_mutex_lock(&metalk);
	for(i = 0; i < nlists; i++)
		buildview(lists[i]);
	g_mutex_unlock(&metalk);
	setlisttag();
	dirty = 1;
}

static pa_simple *pa;
#ifdef __GNU__
#define pcm NULL
#else
static snd_pcm_t *pcm;
#endif
static int nullout;	/* NINECYNTH_AUDIO=null plays to nowhere */

static int
audioopen(void)
{
	pa_sample_spec ss;
	pa_buffer_attr ba;
	int err;

	if(getenv("NINECYNTH_AUDIO") && strcmp(getenv("NINECYNTH_AUDIO"), "null") == 0)
		return nullout = 1;
	ss.format = PA_SAMPLE_FLOAT32NE;
	ss.rate = RATE;
	ss.channels = 2;
	ba.maxlength = (uint32_t)-1;
	ba.tlength = RATE/10 * 2 * sizeof(float);	/* 100ms or pause feels laggy */
	ba.prebuf = (uint32_t)-1;
	ba.minreq = (uint32_t)-1;
	ba.fragsize = (uint32_t)-1;
	pa = pa_simple_new(NULL, "9cynth", PA_STREAM_PLAYBACK, NULL, "music", &ss, NULL, &ba, &err);
	if(pa)
		return 1;
#ifdef __GNU__
	return 0;
#else
	if(snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0){
		pcm = NULL;
		return 0;
	}
	if(snd_pcm_set_params(pcm, SND_PCM_FORMAT_FLOAT, SND_PCM_ACCESS_RW_INTERLEAVED,
	   2, RATE, 1, 100000) < 0){
		snd_pcm_close(pcm);
		pcm = NULL;
		return 0;
	}
	return 1;
#endif
}

static void
audioclose(void)
{
	if(pa)
		pa_simple_free(pa);
	pa = NULL;
#ifndef __GNU__
	if(pcm)
		snd_pcm_close(pcm);
	pcm = NULL;
#endif
}

static int
audiowrite(float *buf, size_t frames)
{
#ifndef __GNU__
	snd_pcm_sframes_t r, d;
#endif
	pa_usec_t us;
	int err;

	if(nullout){
		g_usleep(frames * 1000000 / RATE);
		return 0;
	}
	if(pa){
		if(pa_simple_write(pa, buf, frames*2*sizeof(float), &err) < 0)
			return -1;
		us = pa_simple_get_latency(pa, &err);
		atomic_store(&P.latency, (long long)(us * RATE / 1000000));
		return 0;
	}
#ifndef __GNU__
	while(frames > 0){
		r = snd_pcm_writei(pcm, buf, frames);
		if(r < 0 && snd_pcm_recover(pcm, r, 1) < 0)
			return -1;
		if(r < 0)
			continue;
		buf += 2*r;
		frames -= r;
	}
	if(snd_pcm_delay(pcm, &d) == 0)
		atomic_store(&P.latency, (long long)d);
#endif
	return 0;
}

static void
audiodrop(void)
{
	int err;

	if(pa)
		pa_simple_flush(pa, &err);
#ifndef __GNU__
	if(pcm){
		snd_pcm_drop(pcm);
		snd_pcm_prepare(pcm);
	}
#endif
	atomic_store(&P.latency, 0);
}

static gpointer
outthread(gpointer u)
{
	static float buf[2048];
	size_t rd, wr, n, i;
	float v;
	int fg, failed;

	(void)u;
	failed = 0;
	for(;;){
		if(atomic_exchange(&P.devflush, 0))
			audiodrop();
		g_mutex_lock(&P.outlk);
		rd = atomic_load(&P.rd);
		wr = atomic_load(&P.wr);
		n = 0;
		if(atomic_load(&P.out) && wr != rd){
			n = MIN(wr - rd, (size_t)nelem(buf)) & ~(size_t)1;
			for(i = 0; i < n; i++)
				buf[i] = P.ring[(rd+i) & (RINGCAP-1)];
			atomic_store(&P.rd, rd + n);
		}
		fg = atomic_load(&P.flushgen);
		if(n == 0 && atomic_load(&P.out) && atomic_load(&P.eof) && wr == rd
		&& !atomic_exchange(&P.endposted, 1))
			tellmain(EVend, atomic_load(&P.gen), NULL);
		g_mutex_unlock(&P.outlk);
		if(n == 0){
			g_usleep(5000);
			continue;
		}
		if(pa == NULL && pcm == NULL && !nullout && !audioopen()){
			if(!failed)
				tellmain(EVnoaudio, 0, NULL);
			failed = 1;
			atomic_store(&P.out, 0);
			continue;
		}
		failed = 0;
		v = atomic_load(&P.volume) / 100.0f;
		v *= v;
		for(i = 0; i < n; i++)
			buf[i] *= v;
		if(audiowrite(buf, n/2) < 0){
			/* server died. reopen next time */
			audioclose();
			continue;
		}
		g_mutex_lock(&P.outlk);
		if(atomic_load(&P.flushgen) == fg)
			atomic_fetch_add(&P.played, n/2);
		g_mutex_unlock(&P.outlk);
	}
	return NULL;
}

static void
flushring(double base)
{
	g_mutex_lock(&P.outlk);
	atomic_store(&P.rd, 0);
	atomic_store(&P.wr, 0);
	atomic_store(&P.played, 0);
	atomic_store(&P.latency, 0);
	atomic_store(&P.base, base);
	atomic_store(&P.eof, 0);
	atomic_store(&P.endposted, 0);
	atomic_fetch_add(&P.flushgen, 1);
	atomic_store(&P.devflush, 1);
	g_mutex_unlock(&P.outlk);
}

/* 0 means an order came in and we bailed */
static int
writering(const float *s, size_t n)
{
	size_t wr, space, k, i;

	while(n > 0){
		if(atomic_load(&P.busy))
			return 0;
		wr = atomic_load(&P.wr);
		space = RINGCAP - (wr - atomic_load(&P.rd));
		if(space < 2){
			g_usleep(5000);
			continue;
		}
		k = n < space ? n : space;
		k &= ~(size_t)1;
		for(i = 0; i < k; i++)
			P.ring[(wr+i) & (RINGCAP-1)] = s[i];
		atomic_store(&P.wr, wr + k);
		s += k;
		n -= k;
	}
	return 1;
}

static unsigned char *
decodeart(AVFormatContext *fc, int si, AVPacket *pkt)
{
	const AVCodec *c;
	AVCodecContext *cc;
	AVFrame *f;
	struct SwsContext *sws;
	unsigned char *buf;
	uint8_t *dst[1];
	int stride[1];

	buf = NULL;
	if((c = avcodec_find_decoder(fc->streams[si]->codecpar->codec_id)) == NULL)
		return NULL;
	cc = avcodec_alloc_context3(c);
	f = av_frame_alloc();
	if(avcodec_parameters_to_context(cc, fc->streams[si]->codecpar) < 0
	|| avcodec_open2(cc, c, NULL) < 0)
		goto out;
	if(avcodec_send_packet(cc, pkt) < 0)
		goto out;
	avcodec_send_packet(cc, NULL);
	if(avcodec_receive_frame(cc, f) < 0 || f->width <= 0 || f->height <= 0)
		goto out;
	sws = sws_getContext(f->width, f->height, f->format, ARTSZ, ARTSZ,
		AV_PIX_FMT_BGRA, SWS_BICUBIC, NULL, NULL, NULL);
	if(sws == NULL)
		goto out;
	buf = g_malloc(ARTSZ*ARTSZ*4);
	dst[0] = buf;
	stride[0] = ARTSZ*4;
	sws_scale(sws, (const uint8_t * const*)f->data, f->linesize, 0, f->height, dst, stride);
	sws_freeContext(sws);
out:
	av_frame_free(&f);
	avcodec_free_context(&cc);
	return buf;
}

/* embedded art or else cover.jpg and co next to the file */
static unsigned char *
findart(AVFormatContext *fc, const char *path)
{
	static const char *names[] = {
		"cover.jpg", "cover.png", "Cover.jpg", "folder.jpg", "Folder.jpg",
		"front.jpg", "Front.jpg", "cover.jpeg", "album.jpg", "AlbumArt.jpg",
	};
	AVFormatContext *ic;
	AVPacket *pkt;
	unsigned char *art;
	char *dir, *p;
	int i;

	for(i = 0; i < (int)fc->nb_streams; i++)
		if(fc->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC)
			if((art = decodeart(fc, i, &fc->streams[i]->attached_pic)) != NULL)
				return art;
	art = NULL;
	dir = g_path_get_dirname(path);
	pkt = av_packet_alloc();
	for(i = 0; i < nelem(names) && art == NULL; i++){
		p = g_build_filename(dir, names[i], NULL);
		ic = NULL;
		if(access(p, R_OK) == 0 && avformat_open_input(&ic, p, NULL, NULL) == 0){
			if(ic->nb_streams > 0 && av_read_frame(ic, pkt) == 0){
				art = decodeart(ic, pkt->stream_index, pkt);
				av_packet_unref(pkt);
			}
			avformat_close_input(&ic);
		}
		g_free(p);
	}
	av_packet_free(&pkt);
	g_free(dir);
	return art;
}

static gpointer
decoder(gpointer u)
{
	AVFormatContext *fc;
	AVCodecContext *cc;
	const AVCodec *codec;
	SwrContext *swr;
	AVPacket *pkt;
	AVFrame *fr;
	AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
	float *tmp;
	char *load;
	unsigned char *art;
	double seek;
	int si, stop, gen, r, n, tmpcap;

	(void)u;
	fc = NULL;
	cc = NULL;
	swr = NULL;
	si = -1;
	tmp = NULL;
	tmpcap = 0;
	pkt = av_packet_alloc();
	fr = av_frame_alloc();
	for(;;){
		g_mutex_lock(&P.lk);
		while(!P.quit && P.loadpath == NULL && P.seekto < 0 && !P.stopreq
		&& (fc == NULL || atomic_load(&P.eof)
		   || RINGCAP - (atomic_load(&P.wr) - atomic_load(&P.rd)) < 16384))
			g_cond_wait_until(&P.cv, &P.lk, g_get_monotonic_time() + 20*G_TIME_SPAN_MILLISECOND);
		if(P.quit){
			g_mutex_unlock(&P.lk);
			break;
		}
		load = P.loadpath;
		P.loadpath = NULL;
		gen = P.loadgen;
		seek = P.seekto;
		P.seekto = -1;
		stop = P.stopreq;
		P.stopreq = 0;
		atomic_store(&P.busy, 0);
		g_mutex_unlock(&P.lk);

		if(stop || load){
			swr_free(&swr);
			avcodec_free_context(&cc);
			avformat_close_input(&fc);
			flushring(0);
		}
		if(load){
			r = avformat_open_input(&fc, load, NULL, NULL);
			if(r == 0)
				r = avformat_find_stream_info(fc, NULL);
			if(r >= 0)
				r = si = av_find_best_stream(fc, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
			if(r >= 0){
				cc = avcodec_alloc_context3(codec);
				r = avcodec_parameters_to_context(cc, fc->streams[si]->codecpar);
				if(r >= 0)
					r = avcodec_open2(cc, codec, NULL);
			}
			if(r >= 0){
				if(cc->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
					av_channel_layout_default(&cc->ch_layout, cc->ch_layout.nb_channels);
				r = swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, RATE,
					&cc->ch_layout, cc->sample_fmt, cc->sample_rate, 0, NULL);
				if(r >= 0)
					r = swr_init(swr);
			}
			if(r < 0){
				char e[256];
				av_strerror(r, e, sizeof e);
				swr_free(&swr);
				avcodec_free_context(&cc);
				avformat_close_input(&fc);
				tellmain(EVerr, gen, g_strdup_printf("%s: %s", load, e));
				g_free(load);
				continue;
			}
			atomic_store(&P.gen, gen);
			art = findart(fc, load);
			g_mutex_lock(&P.lk);
			g_free(P.art);
			P.art = art;
			P.artgen++;
			g_mutex_unlock(&P.lk);
			tellmain(EVmeta, 0, NULL);
			g_free(load);
		}
		if(seek >= 0 && fc != NULL){
			avformat_seek_file(fc, -1, INT64_MIN, (int64_t)(seek*AV_TIME_BASE), INT64_MAX, 0);
			avcodec_flush_buffers(cc);
			swr_close(swr);
			swr_init(swr);
			flushring(seek);
		}
		if(fc == NULL || atomic_load(&P.eof))
			continue;

		r = av_read_frame(fc, pkt);
		if(r < 0)
			avcodec_send_packet(cc, NULL);
		else if(pkt->stream_index == si)
			avcodec_send_packet(cc, pkt);
		av_packet_unref(pkt);
		while(avcodec_receive_frame(cc, fr) == 0){
			n = swr_get_out_samples(swr, fr->nb_samples);
			if(n > tmpcap){
				tmpcap = n;
				tmp = g_renew(float, tmp, 2*tmpcap);
			}
			n = swr_convert(swr, (uint8_t**)&tmp, tmpcap,
				(const uint8_t**)fr->extended_data, fr->nb_samples);
			av_frame_unref(fr);
			if(n > 0 && !writering(tmp, 2*n))
				break;
		}
		if(r < 0 && !atomic_load(&P.busy))
			atomic_store(&P.eof, 1);
	}
	swr_free(&swr);
	avcodec_free_context(&cc);
	avformat_close_input(&fc);
	av_packet_free(&pkt);
	av_frame_free(&fr);
	g_free(tmp);
	return NULL;
}

static void
command(const char *load, double seek, int stop)
{
	g_mutex_lock(&P.lk);
	if(load){
		g_free(P.loadpath);
		P.loadpath = g_strdup(load);
		P.loadgen++;
		P.seekto = -1;
	}
	if(seek >= 0)
		P.seekto = seek;
	if(stop){
		g_free(P.loadpath);
		P.loadpath = NULL;
		P.stopreq = 1;
	}
	atomic_store(&P.busy, 1);
	g_cond_signal(&P.cv);
	g_mutex_unlock(&P.lk);
}

static double
position(void)
{
	long long f;

	if(P.state == Stopped)
		return 0;
	f = atomic_load(&P.played) - atomic_load(&P.latency);
	if(f < 0)
		f = 0;
	return atomic_load(&P.base) + f / (double)RATE;
}

static void settitle(void);

static void
playpos(int pos)
{
	if(pos < 0 || pos >= nqueue)
		return;
	qpos = pos;
	P.cur = queue[order[pos]];
	P.dur = P.cur->dur;
	P.state = Playing;
	command(P.cur->path, -1, 0);
	atomic_store(&P.out, 1);
	settitle();
	dirty = 1;
}

static void
makeorder(int first)
{
	int i, j, t;

	g_free(order);
	order = g_new(int, nqueue + 1);
	for(i = 0; i < nqueue; i++)
		order[i] = i;
	if(!shuffle || nqueue == 0)
		return;
	for(i = nqueue-1; i > 0; i--){
		j = g_random_int_range(0, i+1);
		t = order[i], order[i] = order[j], order[j] = t;
	}
	/* clicked song goes first */
	for(i = 0; i < nqueue; i++)
		if(order[i] == first){
			t = order[0], order[0] = order[i], order[i] = t;
			break;
		}
}

/* whatever is in view becomes the queue */
static void
playfrom(List *l, int row)
{
	int i;

	if(l == NULL || l->nview == 0)
		return;
	if(row < 0 || row >= l->nview)
		row = 0;
	g_free(queue);
	nqueue = l->nview;
	queue = g_new(Meta*, nqueue);
	for(i = 0; i < nqueue; i++)
		queue[i] = l->e[l->view[i]].m;
	g_strlcpy(qfrom, l->name, sizeof qfrom);
	makeorder(row);
	duds = 0;
	msg[0] = 0;
	if(shuffle)
		playpos(0);
	else
		playpos(row);
}

static void
stop(void)
{
	if(P.state == Stopped)
		return;
	command(NULL, -1, 1);
	atomic_store(&P.out, 0);
	P.state = Stopped;
	settitle();
	dirty = 1;
}

static void
next(int automatic)
{
	if(nqueue == 0)
		return;
	if(automatic && repeat == Rone){
		playpos(qpos);
		return;
	}
	if(qpos+1 < nqueue)
		playpos(qpos+1);
	else if(repeat != Roff){
		if(shuffle)
			makeorder(-1);
		playpos(0);
	}else if(automatic){
		stop();
		qpos = 0;
	}else
		gripe("end of %s", qfrom);
}

static void
prev(void)
{
	if(nqueue == 0)
		return;
	if(P.state != Stopped && position() > 3){
		command(NULL, 0, 0);
		return;
	}
	if(qpos > 0)
		playpos(qpos-1);
	else if(repeat != Roff)
		playpos(nqueue-1);
	else
		playpos(0);
}

static void
pause_(void)
{
	if(P.state == Playing){
		atomic_store(&P.out, 0);
		P.state = Paused;
	}else if(P.state == Paused){
		atomic_store(&P.out, 1);
		P.state = Playing;
	}
	dirty = 1;
}

static void playselection(void);

static void
play(void)
{
	if(P.state == Paused)
		pause_();
	else if(P.state == Stopped){
		if(nqueue > 0 && qpos >= 0)
			playpos(qpos);
		else
			playselection();
	}
}

static void
setshuffle(int on)
{
	int cur, i;

	cur = qpos >= 0 && nqueue > 0 ? order[qpos] : 0;
	shuffle = on;
	if(nqueue > 0){
		makeorder(cur);
		qpos = 0;
		for(i = 0; i < nqueue; i++)
			if(order[i] == cur)
				qpos = i;
	}
	gripe("shuffle %s", shuffle ? "on" : "off");
}

static void
seekto(double p)
{
	if(P.state == Stopped)
		return;
	if(p < 0)
		p = 0;
	if(P.dur > 0 && p > P.dur)
		p = P.dur - 0.5;
	command(NULL, p, 0);
	atomic_store(&P.base, p);
	atomic_store(&P.played, 0);
	atomic_store(&P.latency, 0);
	dirty = 1;
}

static void
seekrel(double d)
{
	seekto(position() + d);
}

static void
setvolume(int v)
{
	if(v < 0)
		v = 0;
	if(v > 100)
		v = 100;
	atomic_store(&P.volume, v);
	dirty = 1;
}

static int
visrows(void)
{
	int n;

	n = (rlistbody.h - lh - 1) / lh;
	return n < 1 ? 1 : n;
}

static void
clearsel(List *l)
{
	int i;

	for(i = 0; i < l->n; i++)
		l->e[i].sel = 0;
}

static int
firstsel(List *l)
{
	int i;

	for(i = 0; i < l->nview; i++)
		if(l->e[l->view[i]].sel)
			return i;
	return -1;
}

static void
showrow(List *l, int row)
{
	if(row < l->top)
		l->top = row;
	else if(row >= l->top + visrows())
		l->top = row - visrows() + 1;
	if(l->top < 0)
		l->top = 0;
}

static void
selectrange(List *l, int a, int b)
{
	int i, t;

	if(a > b)
		t = a, a = b, b = t;
	clearsel(l);
	for(i = a; i <= b && i < l->nview; i++)
		if(i >= 0)
			l->e[l->view[i]].sel = 1;
}

static void
playselection(void)
{
	List *l;

	if((l = current()) == NULL)
		return;
	playfrom(l, firstsel(l));
}

static int
okname(const char *name)
{
	if(name == NULL || name[0] == 0 || name[0] == '.' || strchr(name, '/')){
		gripe("bad playlist name '%s'", name ? name : "");
		return 0;
	}
	return 1;
}

static void
newlist(const char *name)
{
	char *p;
	int r;

	if(!okname(name))
		return;
	p = g_build_filename(musicdir, name, NULL);
	r = mkdir(p, 0755);
	rescan();
	if(r < 0)
		gripe("New %s: %s", name, strerror(errno));
	else
		gripe("new playlist %s", name);
	g_free(p);
}

static int
addto(const char *name, List *from)
{
	char *dir, *base, *link, *dot, *stem, buf[PATH_MAX];
	struct stat st;
	ssize_t n;
	int i, k, added, dup;

	if(!okname(name))
		return 0;
	if(strcmp(name, "Songs") == 0){
		gripe("Songs is the library. copy files in there yourself");
		return 0;
	}
	if(from == NULL || firstsel(from) < 0){
		gripe("Add what? select some songs");
		return 0;
	}
	dir = g_build_filename(musicdir, name, NULL);
	if(stat(dir, &st) < 0)
		mkdir(dir, 0755);
	added = dup = 0;
	for(i = 0; i < from->n; i++){
		if(!from->e[i].sel)
			continue;
		base = g_path_get_basename(from->e[i].rel);
		link = g_build_filename(dir, base, NULL);
		for(k = 2; lstat(link, &st) == 0; k++){
			n = readlink(link, buf, sizeof buf - 1);
			if(n > 0){
				buf[n] = 0;
				if(strcmp(buf, from->e[i].m->path) == 0)
					break;
			}
			g_free(link);
			stem = g_strdup(base);
			dot = strrchr(stem, '.');
			if(dot)
				*dot++ = 0;
			link = g_strdup_printf("%s/%s (%d)%s%s", dir, stem, k, dot ? "." : "", dot ? dot : "");
			g_free(stem);
		}
		if(lstat(link, &st) == 0)
			dup++;
		else if(symlink(from->e[i].m->path, link) == 0)
			added++;
		g_free(link);
		g_free(base);
	}
	g_free(dir);
	rescan();
	gripe("added %d to %s%s", added, name, dup ? " (some were in there already)" : "");
	return added;
}

static void
removesel(void)
{
	List *l;
	struct stat st;
	char *p, *name;
	int i, gone, kept;

	if((l = current()) == NULL)
		return;
	if(strcmp(l->name, "Songs") == 0){
		gripe("not deleting real files from Songs. use rm");
		return;
	}
	gone = kept = 0;
	for(i = 0; i < l->n; i++){
		if(!l->e[i].sel)
			continue;
		p = g_build_filename(l->dir, l->e[i].rel, NULL);
		if(lstat(p, &st) == 0 && S_ISLNK(st.st_mode) && unlink(p) == 0)
			gone++;
		else
			kept++;
		g_free(p);
	}
	name = g_strdup(l->name);
	rescan();
	if(gone == 0 && kept == 0)
		gripe("Remove what? select some songs");
	else
		gripe("removed %d from %s%s", gone, name, kept ? ". left the real files alone" : "");
	g_free(name);
}

static void
deletelist(const char *name)
{
	DIR *d;
	struct dirent *de;
	struct stat st;
	char *dir, *p;
	int real;

	if(!okname(name))
		return;
	if(strcmp(name, "Songs") == 0){
		gripe("not deleting Songs");
		return;
	}
	dir = g_build_filename(musicdir, name, NULL);
	if((d = opendir(dir)) == NULL){
		gripe("Delete %s: %s", name, strerror(errno));
		g_free(dir);
		return;
	}
	real = 0;
	while((de = readdir(d)) != NULL){
		if(strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
			continue;
		p = g_build_filename(dir, de->d_name, NULL);
		if(lstat(p, &st) < 0 || !S_ISLNK(st.st_mode))
			real++;
		g_free(p);
	}
	if(real){
		gripe("%s has real files in it. delete it yourself", name);
		closedir(d);
		g_free(dir);
		return;
	}
	rewinddir(d);
	while((de = readdir(d)) != NULL){
		if(strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
			continue;
		p = g_build_filename(dir, de->d_name, NULL);
		unlink(p);
		g_free(p);
	}
	closedir(d);
	if(rmdir(dir) < 0)
		gripe("Delete %s: %s", name, strerror(errno));
	else
		gripe("deleted playlist %s", name);
	g_free(dir);
	rescan();
}

static void
settag(int t, const char *s)
{
	g_strlcpy(tags[t].s, s, sizeof tags[t].s);
	tags[t].q = strlen(tags[t].s);
}

static void
setlisttag(void)
{
	List *l;
	char *s;

	if((l = current()) == NULL)
		return;
	s = g_strdup_printf("%s/ Play Add Remove Sort Get | Look %s", l->dir, l->look);
	settag(Tlist, s);
	g_free(s);
}

static void
openlist(int i)
{
	if(i < 0 || i >= nlists)
		return;
	curlist = i;
	setlisttag();
	dirty = 1;
}

static void
look(const char *s)
{
	List *l;

	if((l = current()) == NULL)
		return;
	g_strlcpy(l->look, s ? s : "", sizeof l->look);
	l->top = 0;
	g_mutex_lock(&metalk);
	buildview(l);
	g_mutex_unlock(&metalk);
	if(l->look[0])
		gripe("%d of %d match '%s'", l->nview, l->n, l->look);
	else
		gripe("%d songs", l->n);
}

static void
setsort(int s)
{
	List *l;

	if((l = current()) == NULL)
		return;
	l->sort = s;
	g_mutex_lock(&metalk);
	buildview(l);
	g_mutex_unlock(&metalk);
	gripe("sorted by %s", sortname[s]);
}

static void snarf(const char *s);

static void
execute(int tag, const char *s)
{
	char **av, *cmd, *arg;
	int i, n;

	av = g_strsplit_set(s, " \t\n", -1);
	n = 0;
	for(i = 0; av[i]; i++)
		if(av[i][0])
			av[n++] = av[i];
		else
			g_free(av[i]);
	av[n] = NULL;
	if(n == 0){
		g_free(av);
		return;
	}
	cmd = av[0];
	arg = n > 1 ? g_strjoinv(" ", av+1) : NULL;

	if(strcmp(cmd, "Exit") == 0)
		alive = 0;
	else if(strcmp(cmd, "Get") == 0){
		rescan();
		gripe("%d songs %d playlists", lists[0]->n, nlists-1);
	}else if(strcmp(cmd, "Play") == 0){
		if(tag == Tlist)
			playselection();
		else
			play();
	}else if(strcmp(cmd, "Pause") == 0)
		pause_();
	else if(strcmp(cmd, "Stop") == 0)
		stop();
	else if(strcmp(cmd, "Next") == 0)
		next(0);
	else if(strcmp(cmd, "Prev") == 0)
		prev();
	else if(strcmp(cmd, "Seek+") == 0)
		seekrel(arg ? atof(arg) : 10);
	else if(strcmp(cmd, "Seek-") == 0)
		seekrel(-(arg ? atof(arg) : 10));
	else if(strcmp(cmd, "Shuffle") == 0)
		setshuffle(!shuffle);
	else if(strcmp(cmd, "Repeat") == 0){
		repeat = (repeat+1) % 3;
		gripe("repeat %s", repname[repeat]);
	}else if(strcmp(cmd, "Vol+") == 0)
		setvolume(atomic_load(&P.volume) + (arg ? atoi(arg) : 5));
	else if(strcmp(cmd, "Vol-") == 0)
		setvolume(atomic_load(&P.volume) - (arg ? atoi(arg) : 5));
	else if(strcmp(cmd, "Vol") == 0 && arg)
		setvolume(atoi(arg));
	else if(strcmp(cmd, "New") == 0){
		if(arg)
			newlist(arg);
		else
			gripe("type a name after New and b2 sweep both");
	}else if(strcmp(cmd, "Delete") == 0){
		if(arg)
			deletelist(arg);
		else
			gripe("b2 sweep Delete name");
	}else if(strcmp(cmd, "Add") == 0){
		if(arg)
			addto(arg, current());
		else
			gripe("b2 sweep Add name or drag songs onto a playlist");
	}else if(strcmp(cmd, "Remove") == 0)
		removesel();
	else if(strcmp(cmd, "Sort") == 0){
		if(arg){
			for(i = 0; i < Nsort; i++)
				if(g_ascii_strcasecmp(arg, sortname[i]) == 0)
					break;
			if(i < Nsort)
				setsort(i);
			else
				gripe("Sort file title artist album or time");
		}else if(current())
			setsort((current()->sort+1) % Nsort);
	}else if(strcmp(cmd, "Look") == 0)
		look(arg);
	else if(strcmp(cmd, "Snarf") == 0){
		if(P.cur && P.state != Stopped){
			char *t;
			g_mutex_lock(&metalk);
			if(P.cur->title)
				t = g_strdup_printf("%s%s%s", P.cur->artist ? P.cur->artist : "",
					P.cur->artist ? " - " : "", P.cur->title);
			else
				t = g_strdup(P.cur->path);
			g_mutex_unlock(&metalk);
			snarf(t);
			gripe("snarfed: %s", t);
			g_free(t);
		}else
			gripe("Snarf: nothing playing");
	}else if(strcmp(cmd, "|") == 0 || cmd[0] == '/' || cmd[0] == '+')
		;
	else
		gripe("%s: unknown command", cmd);
	g_free(arg);
	g_strfreev(av);
	dirty = 1;
}

/* b3 in a tag opens that playlist or does Look */
static void
lookword(const char *w)
{
	int i;

	if(w[0] == 0 || w[0] == '/' || strcmp(w, "|") == 0)
		return;
	if((i = findlist(w)) >= 0){
		openlist(i);
		return;
	}
	look(w);
	setlisttag();
}

static void
setc(cairo_t *cr, uint32_t c)
{
	cairo_set_source_rgb(cr, ((c>>16)&255)/255.0, ((c>>8)&255)/255.0, (c&255)/255.0);
}

static void
fillr(cairo_t *cr, double x, double y, double w, double h, uint32_t c)
{
	setc(cr, c);
	cairo_rectangle(cr, x, y, w, h);
	cairo_fill(cr);
}

static void
boxr(cairo_t *cr, double x, double y, double w, double h, uint32_t c)
{
	setc(cr, c);
	cairo_set_line_width(cr, 1);
	cairo_rectangle(cr, x+0.5, y+0.5, w-1, h-1);
	cairo_stroke(cr);
}

static void
hline(cairo_t *cr, double x, double y, double w, uint32_t c)
{
	fillr(cr, x, y, w, 1, c);
}

static int
textw(PangoLayout *l, const char *s)
{
	int w, h;

	pango_layout_set_width(l, -1);
	pango_layout_set_text(l, s, -1);
	pango_layout_get_pixel_size(l, &w, &h);
	return w;
}

/* maxw > 0 cuts it with ... */
static int
text(cairo_t *cr, PangoLayout *l, const char *s, double x, double y, int maxw, uint32_t c)
{
	int w, h;

	pango_layout_set_text(l, s, -1);
	if(maxw > 0){
		pango_layout_set_width(l, maxw*PANGO_SCALE);
		pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
	}else
		pango_layout_set_width(l, -1);
	setc(cr, c);
	cairo_move_to(cr, x, y);
	pango_cairo_show_layout(cr, l);
	pango_layout_get_pixel_size(l, &w, &h);
	pango_layout_set_width(l, -1);
	return w;
}

static int
rtext(cairo_t *cr, PangoLayout *l, const char *s, double right, double y, uint32_t c)
{
	int w;

	w = textw(l, s);
	text(cr, l, s, right - w, y, 0, c);
	return w;
}

static void
fmttime(char *buf, size_t n, double t)
{
	int s;

	if(t < 0){
		snprintf(buf, n, "-:--");
		return;
	}
	s = (int)t;
	if(s >= 3600)
		snprintf(buf, n, "%d:%02d:%02d", s/3600, s/60%60, s%60);
	else
		snprintf(buf, n, "%d:%02d", s/60, s%60);
}

static int
tagtextx(int t)
{
	return tags[t].r.x + (tags[t].box ? sbw + 4 : 4);
}

static int
tagindex(int t, int x)
{
	int idx, trail;
	const char *p;

	pango_layout_set_width(lay, -1);
	pango_layout_set_text(lay, tags[t].s, -1);
	pango_layout_xy_to_index(lay, (x - tagtextx(t))*PANGO_SCALE, 0, &idx, &trail);
	p = tags[t].s + idx;
	while(trail-- > 0 && *p)
		p = g_utf8_next_char(p);
	idx = p - tags[t].s;
	if(x < tagtextx(t))
		idx = 0;
	return idx;
}

static int
tagx(int t, int idx)
{
	PangoRectangle r;

	pango_layout_set_width(lay, -1);
	pango_layout_set_text(lay, tags[t].s, -1);
	pango_layout_index_to_pos(lay, idx, &r);
	return tagtextx(t) + PANGO_PIXELS(r.x);
}

static void
tick(cairo_t *cr, int x, int y, int h)
{
	fillr(cr, x, y+1, 1, h-2, Cblack);
	fillr(cr, x-1, y+1, 3, 3, Cblack);
	fillr(cr, x-1, y+h-4, 3, 3, Cblack);
}

static void
drawtag(cairo_t *cr, int t, uint32_t boxc, int filled)
{
	Tag *g;
	int x0, x1, y;

	g = &tags[t];
	fillr(cr, g->r.x, g->r.y, g->r.w, g->r.h, Ctagbg);
	y = g->r.y + (g->r.h - lh)/2;
	if(g->box){
		if(filled)
			fillr(cr, g->r.x+2, g->r.y+2, sbw-4, g->r.h-4, boxc);
		else{
			fillr(cr, g->r.x+2, g->r.y+2, sbw-4, g->r.h-4, Ctagbg);
			boxr(cr, g->r.x+2, g->r.y+2, sbw-4, g->r.h-4, boxc);
		}
	}
	if(G.kind == Gsweep && G.tag == t){
		x0 = tagx(t, MIN(G.q0, G.q1));
		x1 = tagx(t, MAX(G.q0, G.q1));
		if(x1 == x0)
			x1 = x0 + 1;
		fillr(cr, x0, g->r.y+1, x1-x0, g->r.h-2, G.button == 2 ? Cred : Cgreen);
	}
	cairo_save(cr);
	cairo_rectangle(cr, g->r.x, g->r.y, g->r.w, g->r.h);
	cairo_clip(cr);
	text(cr, lay, g->s, tagtextx(t), y, 0, Cblack);
	if(G.kind == Gsweep && G.tag == t){
		/* swept text goes white like acme */
		x0 = tagx(t, MIN(G.q0, G.q1));
		x1 = tagx(t, MAX(G.q0, G.q1));
		cairo_save(cr);
		cairo_rectangle(cr, x0, g->r.y, x1-x0, g->r.h);
		cairo_clip(cr);
		text(cr, lay, g->s, tagtextx(t), y, 0, Cwhite);
		cairo_restore(cr);
	}else
		tick(cr, tagx(t, g->q), g->r.y, g->r.h);
	cairo_restore(cr);
}

static void
drawscroll(cairo_t *cr, Rect b, int top, int vis, int total)
{
	double y0, y1;

	fillr(cr, b.x, b.y, sbw, b.h, Cscroll);
	if(total <= 0){
		y0 = 0;
		y1 = b.h;
	}else{
		y0 = b.h * top / (double)total;
		y1 = b.h * MIN(top + vis, total) / (double)total;
	}
	if(y1 - y0 < 2)
		y1 = y0 + 2;
	fillr(cr, b.x, b.y + y0, sbw-1, y1 - y0, Cbodybg);
	fillr(cr, b.x + sbw - 1, b.y, 1, b.h, Cblack);
}

static void
playmark(cairo_t *cr, double x, double y, int paused, uint32_t c)
{
	double s;

	s = lh - 6;
	setc(cr, c);
	if(paused){
		cairo_rectangle(cr, x+1, y+3, s/3, s);
		cairo_rectangle(cr, x+1+2*s/3, y+3, s/3, s);
	}else{
		cairo_move_to(cr, x+1, y+3);
		cairo_line_to(cr, x+1+s*0.8, y+3+s/2);
		cairo_line_to(cr, x+1, y+3+s);
		cairo_close_path(cr);
	}
	cairo_fill(cr);
}

static void
drawdir(cairo_t *cr)
{
	Rect b;
	List *l;
	char n[32];
	int i, y, x, vis, playing;

	b = rdirbody;
	fillr(cr, b.x, b.y, b.w, b.h, Cbodybg);
	vis = b.h / lh;
	drawscroll(cr, b, dirscroll, vis, nlists);
	x = b.x + sbw + 4;
	cairo_save(cr);
	cairo_rectangle(cr, b.x, b.y, b.w, b.h);
	cairo_clip(cr);
	for(i = dirscroll; i < nlists && (i-dirscroll)*lh < b.h; i++){
		l = lists[i];
		y = b.y + (i-dirscroll)*lh;
		if(i == curlist)
			fillr(cr, b.x+sbw, y, b.w-sbw, lh, Cbodyhi);
		if(G.kind == Gdrag && G.droprow == i)
			fillr(cr, b.x+sbw, y, b.w-sbw, lh, Ctaghi);
		playing = P.state != Stopped && strcmp(l->name, qfrom) == 0;
		if(playing)
			playmark(cr, x, y, P.state == Paused, Cmod);
		text(cr, lay, l->name, x + lh, y, b.w - sbw - lh - 70, playing ? Cmod : Cblack);
		text(cr, lay, "/", x + lh + textw(lay, l->name), y, 0, playing ? Cmod : Cblack);
		snprintf(n, sizeof n, "%d", l->n);
		rtext(cr, lay, n, b.x + b.w - 6, y, Cgrey);
	}
	cairo_restore(cr);
}

static void
columns(int *tx, int *tw, int *aw, int *alw, int *timew)
{
	int rw;

	*tx = rlistbody.x + sbw + 4 + lh;
	*timew = textw(lay, "88:88:88");
	rw = rlistbody.x + rlistbody.w - *tx - *timew - 12;
	*tw = rw;
	*aw = *alw = 0;
	if(rw > 520){
		*tw = rw * 0.42;
		*aw = rw * 0.30;
		*alw = rw - *tw - *aw;
	}else if(rw > 260){
		*tw = rw * 0.6;
		*aw = rw - *tw;
	}
}

static void
drawlist(cairo_t *cr)
{
	Rect b;
	List *l;
	Ent *e;
	Meta *m;
	char tm[32];
	const char *hdr;
	int i, y, x, tx, timew, tw, aw, alw, vis, row, isplay;
	uint32_t c;

	b = rlistbody;
	fillr(cr, b.x, b.y, b.w, b.h, Cbodybg);
	l = current();
	vis = visrows();
	drawscroll(cr, b, l ? l->top : 0, vis, l ? l->nview : 0);
	if(l == NULL)
		return;

	x = b.x + sbw;
	columns(&tx, &tw, &aw, &alw, &timew);

	/* cynthiune header. click to sort */
	fillr(cr, x, b.y, b.w - sbw, lh, Ctagbg);
	hline(cr, x, b.y + lh, b.w - sbw, Cbutton);
	{
		struct { const char *s; int x, w, sort; } h[] = {
			{ "Song", tx, tw, Stitle },
			{ "Artist", tx + tw, aw, Sartist },
			{ "Album", tx + tw + aw, alw, Salbum },
		};
		for(i = 0; i < nelem(h); i++){
			if(h[i].w <= 0)
				continue;
			if(i > 0)
				fillr(cr, h[i].x - 6, b.y, 1, lh, Cbutton);
			text(cr, lay, h[i].s, h[i].x, b.y, h[i].w - 8, l->sort == h[i].sort ? Cmod : Cblack);
			if(l->sort == h[i].sort)
				hline(cr, h[i].x, b.y + lh - 2, textw(lay, h[i].s), Cmod);
		}
		fillr(cr, b.x + b.w - timew - 10, b.y, 1, lh, Cbutton);
		hdr = "Time";
		rtext(cr, lay, hdr, b.x + b.w - 6, b.y, l->sort == Stime ? Cmod : Cblack);
		if(l->sort == Stime)
			hline(cr, b.x + b.w - 6 - textw(lay, hdr), b.y + lh - 2, textw(lay, hdr), Cmod);
		if(l->sort == Sfile)
			text(cr, lay, "·", x + 4, b.y, 0, Cmod);
	}

	cairo_save(cr);
	cairo_rectangle(cr, x, b.y + lh + 1, b.w - sbw, b.h - lh - 1);
	cairo_clip(cr);
	for(row = l->top; row < l->nview && row - l->top < vis + 1; row++){
		e = &l->e[l->view[row]];
		m = e->m;
		y = b.y + lh + 1 + (row - l->top)*lh;
		isplay = P.state != Stopped && m == P.cur;
		if(e->sel)
			fillr(cr, x, y, b.w - sbw, lh, Cbodyhi);
		c = m->scanned < 0 ? Cgrey : isplay ? Cmod : Cblack;
		if(isplay)
			playmark(cr, x + 4, y, P.state == Paused, Cmod);
		text(cr, lay, enttitle(e), tx, y, tw - 8, c);
		if(aw > 0 && m->artist)
			text(cr, lay, m->artist, tx + tw, y, aw - 8, c);
		if(alw > 0 && m->album)
			text(cr, lay, m->album, tx + tw + aw, y, alw - 8, c);
		fmttime(tm, sizeof tm, m->dur);
		rtext(cr, lay, tm, b.x + b.w - 6, y, c);
	}
	if(l->nview == 0){
		y = b.y + lh + 1 + lh/2;
		if(l->look[0])
			text(cr, lay, "nothing matches; B2 on Look alone shows everything", tx, y, 0, Cgrey);
		else if(strcmp(l->name, "Songs") == 0){
			char *s = g_strdup_printf("no songs yet: put music files in %s/", l->dir);
			text(cr, lay, s, tx, y, b.w - sbw - lh - 8, Cgrey);
			g_free(s);
		}else
			text(cr, lay, "empty playlist: drag songs here from Songs, or sweep 'Add name'", tx, y, b.w - sbw - lh - 8, Cgrey);
	}
	cairo_restore(cr);
}

/* hand drawn. fonts get these wrong */
static void
glyph(cairo_t *cr, int which, Rect r, uint32_t c)
{
	double cx, cy, s, t;

	cx = r.x + r.w/2.0;
	cy = r.y + r.h/2.0;
	s = MIN(r.w, r.h) * 0.30;
	setc(cr, c);
	cairo_set_line_width(cr, 1.5);
	switch(which){
	case Bplay:
		cairo_move_to(cr, cx - s*0.7, cy - s);
		cairo_line_to(cr, cx + s, cy);
		cairo_line_to(cr, cx - s*0.7, cy + s);
		cairo_close_path(cr);
		cairo_fill(cr);
		break;
	case Bstop:
		cairo_rectangle(cr, cx - s*0.8, cy - s*0.8, 1.6*s, 1.6*s);
		cairo_fill(cr);
		break;
	case Bpause:
		cairo_rectangle(cr, cx - s*0.8, cy - s, s*0.55, 2*s);
		cairo_rectangle(cr, cx + s*0.25, cy - s, s*0.55, 2*s);
		cairo_fill(cr);
		break;
	case Bprev:
	case Bnext:
		t = which == Bnext ? 1 : -1;
		cairo_rectangle(cr, cx + t*s - (t > 0 ? 0 : 2), cy - s, 2, 2*s);
		cairo_fill(cr);
		cairo_move_to(cr, cx - t*s*1.1, cy - s);
		cairo_line_to(cr, cx - t*s*0.05, cy);
		cairo_line_to(cr, cx - t*s*1.1, cy + s);
		cairo_close_path(cr);
		cairo_move_to(cr, cx - t*s*0.1, cy - s);
		cairo_line_to(cr, cx + t*s*0.95, cy);
		cairo_line_to(cr, cx - t*s*0.1, cy + s);
		cairo_close_path(cr);
		cairo_fill(cr);
		break;
	case Brepeat:
		cairo_new_path(cr);
		cairo_arc(cr, cx - s*0.5, cy, s*0.7, M_PI/2, 3*M_PI/2);
		cairo_line_to(cr, cx + s*0.5, cy - s*0.7);
		cairo_arc(cr, cx + s*0.5, cy, s*0.7, -M_PI/2, M_PI/2);
		cairo_line_to(cr, cx - s*0.1, cy + s*0.7);
		cairo_stroke(cr);
		cairo_move_to(cr, cx + s*0.1, cy - s*1.15);
		cairo_line_to(cr, cx + s*0.55, cy - s*0.7);
		cairo_line_to(cr, cx + s*0.1, cy - s*0.25);
		cairo_stroke(cr);
		if(repeat == Rone){
			pango_layout_set_text(lay, "1", -1);
			cairo_move_to(cr, cx - textw(lay, "1")/2.0, cy - lh/2.0);
			pango_cairo_show_layout(cr, lay);
		}
		break;
	case Bshuffle:
		cairo_move_to(cr, cx - s*1.1, cy - s*0.7);
		cairo_curve_to(cr, cx, cy - s*0.7, cx, cy + s*0.7, cx + s*0.9, cy + s*0.7);
		cairo_move_to(cr, cx - s*1.1, cy + s*0.7);
		cairo_curve_to(cr, cx, cy + s*0.7, cx, cy - s*0.7, cx + s*0.9, cy - s*0.7);
		cairo_stroke(cr);
		for(t = -1; t <= 1; t += 2){
			cairo_move_to(cr, cx + s*0.55, cy + t*s*0.7 - s*0.35);
			cairo_line_to(cr, cx + s*1.0, cy + t*s*0.7);
			cairo_line_to(cr, cx + s*0.55, cy + t*s*0.7 + s*0.35);
			cairo_stroke(cr);
		}
		break;
	}
}

static int
inrect(Rect r, int x, int y)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void
drawplayer(cairo_t *cr)
{
	Rect b, r;
	char t0[32], t1[32], tm[400];
	const char *title, *artist, *album;
	double pos, frac;
	int i, s, tx, tw, w, on, pressed, x;

	b = rnpbody;
	fillr(cr, b.x, b.y, b.w, b.h, Cbodybg);

	r = rbanner;
	fillr(cr, r.x, r.y, r.w, r.h, Ctagbg);
	boxr(cr, r.x, r.y, r.w, r.h, Cborder);
	s = r.h - 8;
	if(coverpic && P.state != Stopped){
		cairo_save(cr);
		cairo_translate(cr, r.x + 4, r.y + 4);
		cairo_scale(cr, s / (double)ARTSZ, s / (double)ARTSZ);
		cairo_set_source_surface(cr, coverpic, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint(cr);
		cairo_restore(cr);
	}else{
		fillr(cr, r.x + 4, r.y + 4, s, s, Cbodybg);
		w = textw(biglay, "♫");
		text(cr, biglay, "♫", r.x + 4 + (s - w)/2, r.y + 4 + (s - bigh)/2, 0, Cbutton);
	}
	boxr(cr, r.x + 4, r.y + 4, s, s, Cblack);
	tx = r.x + 4 + s + 10;
	tw = r.x + r.w - tx - 8;
	if(P.state != Stopped && P.cur){
		g_mutex_lock(&metalk);
		title = P.cur->title ? P.cur->title : NULL;
		artist = P.cur->artist ? P.cur->artist : "";
		album = P.cur->album ? P.cur->album : "";
		if(title == NULL){
			char *bn = g_path_get_basename(P.cur->path);
			text(cr, biglay, bn, tx, r.y + 4, tw, Cblack);
			g_free(bn);
		}else
			text(cr, biglay, title, tx, r.y + 4, tw, Cblack);
		text(cr, lay, artist, tx, r.y + 4 + bigh + 2, tw, Cblack);
		if(P.dur <= 0)
			P.dur = P.cur->dur;
		pos = G.kind == Gseek ? G.seekpos : position();
		fmttime(t0, sizeof t0, pos);
		fmttime(t1, sizeof t1, P.dur);
		snprintf(tm, sizeof tm, "%s / %s", t0, t1);
		w = textw(lay, tm);
		text(cr, lay, album, tx, r.y + 4 + bigh + 2 + lh, tw - w - 12, Cgrey);
		rtext(cr, lay, tm, r.x + r.w - 8, r.y + 4 + bigh + 2 + lh, Cblack);
		g_mutex_unlock(&metalk);
	}else{
		text(cr, biglay, "9cynth", tx, r.y + 4, tw, Cblack);
		text(cr, lay, "a free and romantic music player,", tx, r.y + 4 + bigh + 2, tw, Cblack);
		text(cr, lay, "after acme and Cynthiune", tx, r.y + 4 + bigh + 2 + lh, tw, Cgrey);
		pos = 0;
	}

	r = rseek;
	fillr(cr, r.x, r.y, r.w, r.h, Cbodybg);
	frac = P.state != Stopped && P.dur > 0 ? pos / P.dur : 0;
	frac = CLAMP(frac, 0, 1);
	fillr(cr, r.x, r.y, (r.w) * frac, r.h, Cscroll);
	boxr(cr, r.x, r.y, r.w, r.h, Cblack);
	x = r.x + (r.w - 8) * frac;
	fillr(cr, x, r.y - 3, 8, r.h + 6, Cbutton);
	boxr(cr, x, r.y - 3, 8, r.h + 6, Cblack);

	for(i = 0; i < Nbtn; i++){
		r = rbtn[i];
		on = (i == Bplay && P.state == Playing) || (i == Bpause && P.state == Paused)
			|| (i == Bstop && P.state == Stopped)
			|| (i == Brepeat && repeat != Roff) || (i == Bshuffle && shuffle);
		pressed = G.kind == Gbutton && G.btn == i && inrect(r, ptrx, ptry);
		fillr(cr, r.x, r.y, r.w, r.h, pressed ? Cbutton : on ? Ctaghi : Ctagbg);
		boxr(cr, r.x, r.y, r.w, r.h, Cblack);
		glyph(cr, i, r, (i == Brepeat || i == Bshuffle) ? (on ? Cmod : Cgrey) : Cblack);
	}

	r = rvol;
	for(i = 0; i < 10; i++){
		double bw = r.w / 10.0, bh = r.h * (i+1) / 10.0;
		double bx = r.x + i*bw, by = r.y + r.h - bh;
		if(atomic_load(&P.volume) > i*10)
			fillr(cr, bx + 1, by, bw - 2, bh, Cmod);
		else
			boxr(cr, bx + 1, by, bw - 2, bh, Cbutton);
	}

	/* poor mans +Errors */
	r = rmsg;
	if(msg[0])
		text(cr, lay, msg, r.x, r.y, r.w, Cmod);
	else if(P.state != Stopped){
		snprintf(tm, sizeof tm, "%s %d/%d in %s", P.state == Paused ? "paused," : "playing", qpos+1, nqueue, qfrom);
		text(cr, lay, tm, r.x, r.y, r.w, Cgrey);
	}
}

static void
layout(void)
{
	int y, ly, npH, x, i, bw;

	tagh = lh + 2;
	colx = W * 0.36;
	if(colx < 330)
		colx = MIN(330, W/2);

	tags[Tmain].r = (Rect){ 0, 0, W, tagh };
	y = tagh + 1;
	tags[Tlcol].r = (Rect){ 0, y, colx, tagh };
	tags[Trcol].r = (Rect){ colx + 1, y, W - colx - 1, tagh };
	y += tagh + 1;

	/* left side */
	bigh = lh * 2;
	rbanner.h = bigh + 2*lh + 10;
	npH = 6 + rbanner.h + 10 + 10 + 10 + (lh + 10) + 8 + lh + 6;
	ly = H - npH - tagh - 1;
	if(ly < y + tagh + 1 + 2*lh)
		ly = y + tagh + 1 + 2*lh;
	tags[Tdir].r = (Rect){ 0, y, colx, tagh };
	rdirbody = (Rect){ 0, y + tagh + 1, colx, ly - (y + tagh + 1) - 1 };
	tags[Tnp].r = (Rect){ 0, ly, colx, tagh };
	rnpbody = (Rect){ 0, ly + tagh + 1, colx, H - (ly + tagh + 1) };

	rbanner.x = 6;
	rbanner.y = rnpbody.y + 6;
	rbanner.w = colx - 12;
	rseek = (Rect){ 6 + 4, rbanner.y + rbanner.h + 10, colx - 12 - 8, 10 };
	y = rseek.y + rseek.h + 10;
	x = 6;
	bw = lh + 14;
	for(i = 0; i < Nbtn; i++){
		if(i == Brepeat)
			x += 10;
		rbtn[i] = (Rect){ x, y, bw, lh + 10 };
		x += bw + (i == Bpause ? 0 : 3);
	}
	rvol = (Rect){ x + 10, y + 2, colx - 6 - (x + 10), lh + 6 };
	if(rvol.w > 90){
		rvol.x += rvol.w - 90;
		rvol.w = 90;
	}
	rmsg = (Rect){ 8, y + lh + 10 + 8, colx - 16, lh };

	/* right side */
	y = tags[Trcol].r.y + tagh + 1;
	tags[Tlist].r = (Rect){ colx + 1, y, W - colx - 1, tagh };
	rlistbody = (Rect){ colx + 1, y + tagh + 1, W - colx - 1, H - (y + tagh + 1) };
}

static void
drawall(cairo_t *cr)
{
	List *l;
	char st[128];
	int ql;

	layout();
	fillr(cr, 0, 0, W, H, Cblack);
	g_mutex_lock(&metalk);
	drawtag(cr, Tmain, Cbutton, 1);
	snprintf(st, sizeof st, "vol %d  shuffle %s  repeat %s ", atomic_load(&P.volume),
		shuffle ? "on" : "off", repname[repeat]);
	if(textw(lay, tags[Tmain].s) + textw(lay, st) + 30 < W){
		fillr(cr, W - textw(lay, st) - 8, 0, textw(lay, st) + 8, tagh, Ctagbg);
		rtext(cr, lay, st, W - 4, 1, Cgrey);
	}
	drawtag(cr, Tlcol, Cbutton, 1);
	drawtag(cr, Trcol, Cbutton, 1);
	drawtag(cr, Tdir, Cbutton, 0);
	drawdir(cr);
	drawtag(cr, Tnp, Cbutton, P.state != Stopped);
	l = current();
	ql = P.state != Stopped && l && strcmp(l->name, qfrom) == 0;
	drawtag(cr, Tlist, ql ? Cmod : Cbutton, ql);
	drawlist(cr);
	g_mutex_unlock(&metalk);
	drawplayer(cr);
	fillr(cr, colx, tagh + 1, 1, H, Cblack);
}

static int
tagat(int x, int y)
{
	int i;

	for(i = 0; i < Ntag; i++)
		if(inrect(tags[i].r, x, y))
			return i;
	return -1;
}

static int
listrow(int y)
{
	List *l;
	int r;

	if((l = current()) == NULL || y < rlistbody.y + lh + 1)
		return -1;
	r = l->top + (y - rlistbody.y - lh - 1) / lh;
	return r < l->nview ? r : -1;
}

static int
dirrow(int y)
{
	int r;

	r = dirscroll + (y - rdirbody.y) / lh;
	return r >= 0 && r < nlists ? r : -1;
}

static void
wordat(Tag *g, int q, int *q0, int *q1)
{
	int a, b;

	a = b = q;
	while(a > 0 && !g_ascii_isspace(g->s[a-1]))
		a--;
	while(g->s[b] && !g_ascii_isspace(g->s[b]))
		b++;
	*q0 = a;
	*q1 = b;
}

static void
scrolllist(int *top, int n, int vis, int d)
{
	*top += d;
	if(*top > n - vis)
		*top = n - vis;
	if(*top < 0)
		*top = 0;
	dirty = 1;
}

/* b1 up b3 down by how far from the top. b2 jumps */
static void
scrollclick(int button, int y, Rect b, int *top, int n, int vis, int rowy)
{
	int lines;

	lines = MAX(1, (y - rowy) / lh);
	if(button == 1)
		scrolllist(top, n, vis, -lines);
	else if(button == 3)
		scrolllist(top, n, vis, lines);
	else
		scrolllist(top, n, vis, (int)((y - b.y) / (double)b.h * n) - *top);
}

static double
seekat(int x)
{
	double f;

	f = (x - rseek.x) / (double)rseek.w;
	return CLAMP(f, 0, 1) * (P.dur > 0 ? P.dur : 0);
}

static void
volat(int x)
{
	setvolume((int)((x - rvol.x) * 100.0 / rvol.w + 5) / 10 * 10);
}

static void
buttonpress(int x, int y, int button, int clicks)
{
	List *l;
	Rect sr;
	int t, row, i, tx, tw, aw, alw, timew;

	memset(&G, 0, sizeof G);
	G.droprow = -1;
	G.x0 = x;
	G.y0 = y;
	G.button = button;
	l = current();

	if((t = tagat(x, y)) >= 0){
		G.tag = t;
		G.q0 = G.q1 = tagindex(t, x);
		if(button == 1)
			tags[t].q = G.q0;
		else
			G.kind = Gsweep;
		dirty = 1;
		return;
	}
	if(inrect(rlistbody, x, y) && l){
		if(x < rlistbody.x + sbw){
			G.kind = Gscroll;
			scrollclick(button, y, rlistbody, &l->top, l->nview, visrows(), rlistbody.y + lh + 1);
			return;
		}
		if(y < rlistbody.y + lh){
			columns(&tx, &tw, &aw, &alw, &timew);
			if(x >= rlistbody.x + rlistbody.w - timew - 10)
				setsort(Stime);
			else if(x < tx)
				setsort(Sfile);
			else if(x < tx + tw)
				setsort(Stitle);
			else if(x < tx + tw + aw)
				setsort(Sartist);
			else
				setsort(Salbum);
			return;
		}
		row = listrow(y);
		if(button != 1){
			if(row >= 0){
				clearsel(l);
				l->e[l->view[row]].sel = 1;
				l->anchor = row;
				playfrom(l, row);
			}
			return;
		}
		if(row < 0){
			clearsel(l);
			dirty = 1;
			return;
		}
		if(clicks >= 2){
			playfrom(l, row);
			return;
		}
		G.row = row;
		if(mods & Mshift)
			selectrange(l, l->anchor, row);
		else if(mods & Mctrl){
			l->e[l->view[row]].sel ^= 1;
			l->anchor = row;
		}else if(l->e[l->view[row]].sel){
			/* maybe a drag onto a playlist */
			G.kind = Gdrag;
			G.moved = 0;
		}else{
			clearsel(l);
			l->e[l->view[row]].sel = 1;
			l->anchor = row;
			G.kind = Gselect;
		}
		dirty = 1;
		return;
	}
	if(inrect(rdirbody, x, y)){
		if(x < rdirbody.x + sbw){
			G.kind = Gscroll;
			scrollclick(button, y, rdirbody, &dirscroll, nlists, rdirbody.h / lh, rdirbody.y);
			return;
		}
		if((row = dirrow(y)) >= 0){
			openlist(row);
			if(button != 1)
				playfrom(lists[row], -1);
		}
		return;
	}
	if(inrect(rnpbody, x, y)){
		sr = rseek;
		sr.y -= 4;
		sr.h += 8;
		if(inrect(sr, x, y) && P.state != Stopped){
			G.kind = Gseek;
			G.seekpos = seekat(x);
			dirty = 1;
			return;
		}
		for(i = 0; i < Nbtn; i++)
			if(inrect(rbtn[i], x, y)){
				G.kind = Gbutton;
				G.btn = i;
				dirty = 1;
				return;
			}
		if(inrect(rvol, x, y)){
			G.kind = Gvol;
			volat(x);
		}
	}
}

static void
motion(int x, int y)
{
	List *l;
	int row;

	ptrx = x;
	ptry = y;
	l = current();
	switch(G.kind){
	case Gsweep:
		if(y >= tags[G.tag].r.y && y < tags[G.tag].r.y + tagh)
			G.q1 = tagindex(G.tag, x);
		dirty = 1;
		break;
	case Gselect:
		if(l && (row = listrow(CLAMP(y, rlistbody.y + lh + 1, rlistbody.y + rlistbody.h - 1))) >= 0){
			selectrange(l, l->anchor, row);
			if(y >= rlistbody.y + rlistbody.h - lh)
				scrolllist(&l->top, l->nview, visrows(), 1);
			else if(y < rlistbody.y + lh + 1)
				scrolllist(&l->top, l->nview, visrows(), -1);
			dirty = 1;
		}
		break;
	case Gdrag:
		if(abs(x - G.x0) + abs(y - G.y0) > 4)
			G.moved = 1;
		G.droprow = inrect(rdirbody, x, y) && x >= sbw ? dirrow(y) : -1;
		if(G.droprow == 0 || (G.droprow >= 0 && G.droprow == curlist))
			G.droprow = -1;
		dirty = 1;
		break;
	case Gseek:
		G.seekpos = seekat(x);
		dirty = 1;
		break;
	case Gvol:
		volat(x);
		break;
	case Gbutton:
		dirty = 1;
		break;
	}
}

static void
pressbutton(int i)
{
	switch(i){
	case Bprev:	prev(); break;
	case Bplay:	play(); break;
	case Bstop:	stop(); break;
	case Bnext:	next(0); break;
	case Bpause:	pause_(); break;
	case Brepeat:	execute(Tnp, "Repeat"); break;
	case Bshuffle:	setshuffle(!shuffle); break;
	}
}

static void
buttonrelease(int x, int y)
{
	List *l;
	Tag *g;
	char *s;
	int a, b;

	l = current();
	switch(G.kind){
	case Gsweep:
		g = &tags[G.tag];
		a = MIN(G.q0, G.q1);
		b = MAX(G.q0, G.q1);
		if(a == b)
			wordat(g, a, &a, &b);
		s = g_strndup(g->s + a, b - a);
		G.kind = Gnone;
		if(G.button == 2)
			execute(G.tag, s);
		else
			lookword(g_strstrip(s));
		g_free(s);
		break;
	case Gdrag:
		if(G.droprow > 0 && l)
			addto(lists[G.droprow]->name, l);
		else if(!G.moved && l && G.row < l->nview){
			clearsel(l);
			l->e[l->view[G.row]].sel = 1;
			l->anchor = G.row;
		}
		break;
	case Gseek:
		seekto(G.seekpos);
		break;
	case Gbutton:
		if(inrect(rbtn[G.btn], x, y))
			pressbutton(G.btn);
		break;
	}
	G.kind = Gnone;
	dirty = 1;
}

/* dy > 0 means down */
static void
wheel(int dy)
{
	List *l;
	Rect sr;

	if(inrect(rlistbody, ptrx, ptry) && (l = current()))
		scrolllist(&l->top, l->nview, visrows(), 3*dy);
	else if(inrect(rdirbody, ptrx, ptry))
		scrolllist(&dirscroll, nlists, rdirbody.h / lh, dy);
	else if(inrect(rnpbody, ptrx, ptry)){
		sr = rseek;
		sr.y -= 6;
		sr.h += 12;
		if(inrect(sr, ptrx, ptry))
			seekrel(-5*dy);
		else
			setvolume(atomic_load(&P.volume) - 5*dy);
	}
}

static void
taginsert(Tag *g, const char *s)
{
	size_t n, len;

	n = strlen(s);
	len = strlen(g->s);
	if(len + n >= sizeof g->s)
		return;
	memmove(g->s + g->q + n, g->s + g->q, len - g->q + 1);
	memcpy(g->s + g->q, s, n);
	g->q += n;
	dirty = 1;
}

static void
tagdelete(Tag *g, int a, int b)
{
	if(a < 0)
		a = 0;
	if(b > (int)strlen(g->s))
		b = strlen(g->s);
	if(a >= b)
		return;
	memmove(g->s + a, g->s + b, strlen(g->s + b) + 1);
	g->q = a;
	dirty = 1;
}

/* keys go to the tag under the mouse */
static int
tagkey(xkb_keysym_t k, const char *utf8)
{
	Tag *g;
	int t, a;
	const char *p;

	if((t = tagat(ptrx, ptry)) < 0)
		return 0;
	g = &tags[t];
	if(mods & Mctrl){
		switch(k){
		case XKB_KEY_a:
			g->q = 0;
			dirty = 1;
			return 1;
		case XKB_KEY_e:
			g->q = strlen(g->s);
			dirty = 1;
			return 1;
		case XKB_KEY_u:
			tagdelete(g, 0, g->q);
			return 1;
		case XKB_KEY_w:
			a = g->q;
			while(a > 0 && g_ascii_isspace(g->s[a-1]))
				a--;
			while(a > 0 && !g_ascii_isspace(g->s[a-1]))
				a--;
			tagdelete(g, a, g->q);
			return 1;
		}
		return 0;
	}
	switch(k){
	case XKB_KEY_BackSpace:
		if(g->q > 0){
			p = g_utf8_prev_char(g->s + g->q);
			tagdelete(g, p - g->s, g->q);
		}
		return 1;
	case XKB_KEY_Delete:
		if(g->s[g->q]){
			p = g_utf8_next_char(g->s + g->q);
			tagdelete(g, g->q, p - g->s);
		}
		return 1;
	case XKB_KEY_Left:
		if(g->q > 0)
			g->q = g_utf8_prev_char(g->s + g->q) - g->s;
		dirty = 1;
		return 1;
	case XKB_KEY_Right:
		if(g->s[g->q])
			g->q = g_utf8_next_char(g->s + g->q) - g->s;
		dirty = 1;
		return 1;
	case XKB_KEY_Home:
		g->q = 0;
		dirty = 1;
		return 1;
	case XKB_KEY_End:
		g->q = strlen(g->s);
		dirty = 1;
		return 1;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
	case XKB_KEY_Escape:
	case XKB_KEY_Tab:
		return 1;
	}
	if((unsigned char)utf8[0] >= ' ' && utf8[0] != 0x7f)
		taginsert(g, utf8);
	return 1;
}

static void
keypress(xkb_keysym_t k, const char *utf8)
{
	List *l;
	int r, i;

	if(tagkey(k, utf8))
		return;
	l = current();
	switch(k){
	case XKB_KEY_space:
	case XKB_KEY_XF86AudioPlay:
		if(P.state == Stopped)
			play();
		else
			pause_();
		break;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		playselection();
		break;
	case XKB_KEY_Left:
		seekrel(-5);
		break;
	case XKB_KEY_Right:
		seekrel(5);
		break;
	case XKB_KEY_Up:
	case XKB_KEY_Down:
		if(l == NULL || l->nview == 0)
			break;
		r = firstsel(l);
		if(r < 0)
			r = k == XKB_KEY_Up ? l->nview : -1;
		r += k == XKB_KEY_Up ? -1 : 1;
		r = CLAMP(r, 0, l->nview - 1);
		if(mods & Mshift)
			selectrange(l, l->anchor, r);
		else{
			clearsel(l);
			l->e[l->view[r]].sel = 1;
			l->anchor = r;
		}
		showrow(l, r);
		dirty = 1;
		break;
	case XKB_KEY_Page_Up:
	case XKB_KEY_Page_Down:
		if(l)
			scrolllist(&l->top, l->nview, visrows(), (k == XKB_KEY_Page_Up ? -1 : 1) * visrows());
		break;
	case XKB_KEY_Home:
		if(l)
			scrolllist(&l->top, l->nview, visrows(), -l->nview);
		break;
	case XKB_KEY_End:
		if(l)
			scrolllist(&l->top, l->nview, visrows(), l->nview);
		break;
	case XKB_KEY_plus:
	case XKB_KEY_equal:
	case XKB_KEY_KP_Add:
		setvolume(atomic_load(&P.volume) + 5);
		break;
	case XKB_KEY_minus:
	case XKB_KEY_KP_Subtract:
		setvolume(atomic_load(&P.volume) - 5);
		break;
	case XKB_KEY_XF86AudioNext:
	case XKB_KEY_n:
		next(0);
		break;
	case XKB_KEY_XF86AudioPrev:
	case XKB_KEY_p:
		prev();
		break;
	case XKB_KEY_XF86AudioStop:
	case XKB_KEY_s:
		stop();
		break;
	case XKB_KEY_z:
		setshuffle(!shuffle);
		break;
	case XKB_KEY_r:
		execute(Tnp, "Repeat");
		break;
	case XKB_KEY_Delete:
		removesel();
		break;
	case XKB_KEY_Escape:
		if(l){
			clearsel(l);
			dirty = 1;
		}
		break;
	case XKB_KEY_a:
		if((mods & Mctrl) && l){
			for(i = 0; i < l->nview; i++)
				l->e[l->view[i]].sel = 1;
			dirty = 1;
		}
		break;
	case XKB_KEY_q:
		if(mods & Mctrl)
			alive = 0;
		break;
	case XKB_KEY_Tab:
	case XKB_KEY_ISO_Left_Tab:
		if(nlists > 0)
			openlist((curlist + ((mods & Mshift) ? nlists-1 : 1)) % nlists);
		break;
	}
}

static void
appevent(Ev *e)
{
	switch(e->type){
	case EVmeta:
		atomic_store(&unscanned, 0);
		if(current() && (current()->sort != Sfile || current()->look[0])){
			g_mutex_lock(&metalk);
			buildview(current());
			g_mutex_unlock(&metalk);
		}
		if(P.cur && P.dur <= 0)
			P.dur = P.cur->dur;
		settitle();
		break;
	case EVend:
		if(e->code == atomic_load(&P.gen) && P.state == Playing){
			duds = 0;
			next(1);
		}
		break;
	case EVerr:
		gripe("can't play %s", e->s);
		if(e->code == P.loadgen && P.state == Playing){
			if(++duds < nqueue)
				next(1);
			else
				stop();
		}
		break;
	case EVnoaudio:
		stop();
		gripe("no sound. is pulseaudio running?");
		break;
	}
	g_free(e->s);
	g_free(e);
	dirty = 1;
}

static void
updateart(void)
{
	int y;

	g_mutex_lock(&P.lk);
	if(P.artgen != coverseen){
		coverseen = P.artgen;
		if(coverpic)
			cairo_surface_destroy(coverpic);
		coverpic = NULL;
		if(P.art){
			coverpic = cairo_image_surface_create(CAIRO_FORMAT_RGB24, ARTSZ, ARTSZ);
			cairo_surface_flush(coverpic);
			for(y = 0; y < ARTSZ; y++)
				memcpy(cairo_image_surface_get_data(coverpic) + y*cairo_image_surface_get_stride(coverpic),
					P.art + y*ARTSZ*4, ARTSZ*4);
			cairo_surface_mark_dirty(coverpic);
		}
		dirty = 1;
	}
	g_mutex_unlock(&P.lk);
}

static struct wl_display *dpy;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wmbase;
static struct wl_seat *seat;
static struct wl_pointer *pointer;
static struct wl_keyboard *keyboard;
static struct wl_data_device_manager *ddm;
static struct wl_data_device *ddev;
static struct wl_data_source *dsrc;
static char *snarfbuf;
static struct wl_surface *surface, *cursurf;
static struct xdg_surface *xsurface;
static struct xdg_toplevel *toplevel;
static struct wl_cursor_theme *curtheme;
static struct wl_cursor *cursor;
static struct xkb_context *xkb;
static struct xkb_keymap *keymap;
static struct xkb_state *xkbstate;
static int configured;
static int pendw, pendh;
static uint32_t lastserial;
static int repfd = -1;
static int repdelay = 400, reprate = 25;
static uint32_t repkey;
static int axisacc;
static uint32_t lastclick;
static int clicks, lastbutton, lastclickx, lastclicky;

typedef struct Buf {
	struct wl_buffer *b;
	cairo_surface_t *cs;
	void *data;
	size_t size;
	int w, h, busy;
} Buf;
static Buf bufs[2];

static void
bufrelease(void *d, struct wl_buffer *b)
{
	((Buf*)d)->busy = 0;
}
static const struct wl_buffer_listener buflistener = { bufrelease };

static void
freebuf(Buf *b)
{
	if(b->b == NULL)
		return;
	cairo_surface_destroy(b->cs);
	wl_buffer_destroy(b->b);
	munmap(b->data, b->size);
	memset(b, 0, sizeof *b);
}

static Buf *
getbuf(void)
{
	struct wl_shm_pool *pool;
	Buf *b;
	int i, fd, stride;

	b = NULL;
	for(i = 0; i < nelem(bufs); i++)
		if(!bufs[i].busy && bufs[i].b && bufs[i].w == W && bufs[i].h == H)
			return &bufs[i];
	for(i = 0; i < nelem(bufs); i++)
		if(!bufs[i].busy){
			b = &bufs[i];
			break;
		}
	if(b == NULL)
		return NULL;
	freebuf(b);
	stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, W);
	b->size = (size_t)stride * H;
#ifdef __GNU__
	{
		char name[32];
		snprintf(name, sizeof name, "/9cynth-%d-%d", (int)getpid(), (int)(b - bufs));
		if((fd = shm_open(name, O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC, 0600)) < 0)
			return NULL;
		shm_unlink(name);
	}
#else
	if((fd = memfd_create("9cynth", MFD_CLOEXEC)) < 0)
		return NULL;
#endif
	if(ftruncate(fd, b->size) < 0){
		close(fd);
		return NULL;
	}
	b->data = mmap(NULL, b->size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if(b->data == MAP_FAILED){
		close(fd);
		b->data = NULL;
		return NULL;
	}
	pool = wl_shm_create_pool(shm, fd, b->size);
	b->b = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	wl_buffer_add_listener(b->b, &buflistener, b);
	b->cs = cairo_image_surface_create_for_data(b->data, CAIRO_FORMAT_RGB24, W, H, stride);
	b->w = W;
	b->h = H;
	return b;
}

/* 0 if both buffers are busy */
static int
draw(void)
{
	cairo_t *cr;
	Buf *b;

	if(!configured || (b = getbuf()) == NULL)
		return 0;
	cr = cairo_create(b->cs);
	drawall(cr);
	cairo_destroy(cr);
	cairo_surface_flush(b->cs);
	canvas = b->cs;
	wl_surface_attach(surface, b->b, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, W, H);
	wl_surface_commit(surface);
	b->busy = 1;
	return 1;
}

static void
settitle(void)
{
	char *t;

	if(toplevel == NULL)
		return;
	if(P.state != Stopped && P.cur){
		g_mutex_lock(&metalk);
		t = g_strdup_printf("9cynth: %s", P.cur->title ? P.cur->title : P.cur->path);
		g_mutex_unlock(&metalk);
		xdg_toplevel_set_title(toplevel, t);
		g_free(t);
	}else
		xdg_toplevel_set_title(toplevel, "9cynth");
}

static void
dsrctarget(void *d, struct wl_data_source *s, const char *mime)
{
}

static void
dsrcsend(void *d, struct wl_data_source *s, const char *mime, int fd)
{
	size_t n, off;
	ssize_t r;

	n = snarfbuf ? strlen(snarfbuf) : 0;
	for(off = 0; off < n; off += r)
		if((r = write(fd, snarfbuf + off, n - off)) <= 0)
			break;
	close(fd);
}

static void
dsrccancelled(void *d, struct wl_data_source *s)
{
	wl_data_source_destroy(s);
	if(dsrc == s)
		dsrc = NULL;
}

static const struct wl_data_source_listener dsrclistener = {
	.target = dsrctarget,
	.send = dsrcsend,
	.cancelled = dsrccancelled,
};

static void
snarf(const char *s)
{
	g_free(snarfbuf);
	snarfbuf = g_strdup(s);
	if(ddm == NULL || ddev == NULL)
		return;
	if(dsrc)
		wl_data_source_destroy(dsrc);
	dsrc = wl_data_device_manager_create_data_source(ddm);
	wl_data_source_add_listener(dsrc, &dsrclistener, NULL);
	wl_data_source_offer(dsrc, "text/plain;charset=utf-8");
	wl_data_source_offer(dsrc, "text/plain");
	wl_data_source_offer(dsrc, "UTF8_STRING");
	wl_data_source_offer(dsrc, "TEXT");
	wl_data_device_set_selection(ddev, dsrc, lastserial);
}

static void
ptrenter(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y)
{
	struct wl_cursor_image *img;

	ptrx = wl_fixed_to_int(x);
	ptry = wl_fixed_to_int(y);
	if(cursor && cursurf){
		img = cursor->images[0];
		wl_surface_attach(cursurf, wl_cursor_image_get_buffer(img), 0, 0);
		wl_surface_damage(cursurf, 0, 0, img->width, img->height);
		wl_surface_commit(cursurf);
		wl_pointer_set_cursor(p, serial, cursurf, img->hotspot_x, img->hotspot_y);
	}
	dirty = 1;
}

static void
ptrleave(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s)
{
	ptrx = ptry = -1;
	dirty = 1;
}

static void
ptrmotion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
	motion(wl_fixed_to_int(x), wl_fixed_to_int(y));
}

static void
ptrbutton(void *d, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t button, uint32_t state)
{
	int b;

	lastserial = serial;
	switch(button){
	case BTN_LEFT:   b = 1; break;
	case BTN_MIDDLE: b = 2; break;
	case BTN_RIGHT:  b = 3; break;
	default: return;
	}
	t = g_get_monotonic_time() / 1000;
	if(state == WL_POINTER_BUTTON_STATE_PRESSED){
		if(b == lastbutton && t - lastclick < 400
		&& abs(ptrx - lastclickx) <= 3 && abs(ptry - lastclicky) <= 3)
			clicks++;
		else
			clicks = 1;
		lastclick = t;
		lastbutton = b;
		lastclickx = ptrx;
		lastclicky = ptry;
		buttonpress(ptrx, ptry, b, clicks);
	}else
		buttonrelease(ptrx, ptry);
}

static void
ptraxis(void *d, struct wl_pointer *p, uint32_t t, uint32_t axis, wl_fixed_t v)
{
	if(axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
		return;
	axisacc += wl_fixed_to_int(v);
	while(axisacc >= 10){
		wheel(1);
		axisacc -= 10;
	}
	while(axisacc <= -10){
		wheel(-1);
		axisacc += 10;
	}
}

static void ptrframe(void *d, struct wl_pointer *p) {}
static void ptraxissource(void *d, struct wl_pointer *p, uint32_t s) {}
static void ptraxisstop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) { axisacc = 0; }
static void ptraxisdiscrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v) {}

static const struct wl_pointer_listener ptrlistener = {
	.enter = ptrenter,
	.leave = ptrleave,
	.motion = ptrmotion,
	.button = ptrbutton,
	.axis = ptraxis,
	.frame = ptrframe,
	.axis_source = ptraxissource,
	.axis_stop = ptraxisstop,
	.axis_discrete = ptraxisdiscrete,
};

static void
kbkeymap(void *d, struct wl_keyboard *k, uint32_t fmt, int fd, uint32_t size)
{
	char *map;

	if(fmt != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1){
		close(fd);
		return;
	}
	map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if(map == MAP_FAILED)
		return;
	if(xkbstate)
		xkb_state_unref(xkbstate);
	if(keymap)
		xkb_keymap_unref(keymap);
	keymap = xkb_keymap_new_from_string(xkb, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map, size);
	xkbstate = keymap ? xkb_state_new(keymap) : NULL;
}

static void
kbenter(void *d, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s, struct wl_array *keys)
{
}

static void
disarmrepeat(void)
{
	struct itimerspec its = {0};

	repkey = 0;
	timerfd_settime(repfd, 0, &its, NULL);
}

static void
kbleave(void *d, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s)
{
	disarmrepeat();
}

static void
dokey(uint32_t key)
{
	xkb_keysym_t sym;
	char utf8[64];

	if(xkbstate == NULL)
		return;
	sym = xkb_state_key_get_one_sym(xkbstate, key + 8);
	utf8[0] = 0;
	if(!(mods & Mctrl))
		xkb_state_key_get_utf8(xkbstate, key + 8, utf8, sizeof utf8);
	keypress(sym, utf8);
}

static void
kbkey(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t t, uint32_t key, uint32_t state)
{
	struct itimerspec its = {0};

	lastserial = serial;
	if(state == WL_KEYBOARD_KEY_STATE_PRESSED){
		dokey(key);
		if(keymap && xkb_keymap_key_repeats(keymap, key + 8) && reprate > 0){
			repkey = key;
			its.it_value.tv_sec = repdelay / 1000;
			its.it_value.tv_nsec = (repdelay % 1000) * 1000000L;
			its.it_interval.tv_nsec = 1000000000L / reprate;
			timerfd_settime(repfd, 0, &its, NULL);
		}
	}else if(key == repkey)
		disarmrepeat();
}

static void
kbmods(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t group)
{
	if(xkbstate == NULL)
		return;
	xkb_state_update_mask(xkbstate, dep, lat, lock, 0, 0, group);
	mods = 0;
	if(xkb_state_mod_name_is_active(xkbstate, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0)
		mods |= Mshift;
	if(xkb_state_mod_name_is_active(xkbstate, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0)
		mods |= Mctrl;
}

static void
kbrepeat(void *d, struct wl_keyboard *k, int32_t rate, int32_t delay)
{
	reprate = rate;
	repdelay = delay;
}

static const struct wl_keyboard_listener kblistener = {
	.keymap = kbkeymap,
	.enter = kbenter,
	.leave = kbleave,
	.key = kbkey,
	.modifiers = kbmods,
	.repeat_info = kbrepeat,
};

static void
seatcaps(void *d, struct wl_seat *s, uint32_t caps)
{
	if((caps & WL_SEAT_CAPABILITY_POINTER) && pointer == NULL){
		pointer = wl_seat_get_pointer(s);
		wl_pointer_add_listener(pointer, &ptrlistener, NULL);
	}
	if((caps & WL_SEAT_CAPABILITY_KEYBOARD) && keyboard == NULL){
		keyboard = wl_seat_get_keyboard(s);
		wl_keyboard_add_listener(keyboard, &kblistener, NULL);
	}
}

static void seatname(void *d, struct wl_seat *s, const char *name) {}

static const struct wl_seat_listener seatlistener = { seatcaps, seatname };

static void
wmping(void *d, struct xdg_wm_base *b, uint32_t serial)
{
	xdg_wm_base_pong(b, serial);
}
static const struct xdg_wm_base_listener wmlistener = { wmping };

static void
xsconfigure(void *d, struct xdg_surface *s, uint32_t serial)
{
	xdg_surface_ack_configure(s, serial);
	if(pendw > 0 && pendh > 0){
		W = pendw;
		H = pendh;
	}
	configured = 1;
	dirty = 1;
}
static const struct xdg_surface_listener xslistener = { xsconfigure };

static void
tlconfigure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *states)
{
	pendw = w;
	pendh = h;
}

static void
tlclose(void *d, struct xdg_toplevel *t)
{
	alive = 0;
}

static void tlbounds(void *d, struct xdg_toplevel *t, int32_t w, int32_t h) {}
static void tlcaps(void *d, struct xdg_toplevel *t, struct wl_array *caps) {}

static const struct xdg_toplevel_listener tllistener = {
	.configure = tlconfigure,
	.close = tlclose,
	.configure_bounds = tlbounds,
	.wm_capabilities = tlcaps,
};

static void
global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t version)
{
	if(strcmp(iface, wl_compositor_interface.name) == 0)
		compositor = wl_registry_bind(r, name, &wl_compositor_interface, MIN(version, 4));
	else if(strcmp(iface, wl_shm_interface.name) == 0)
		shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	else if(strcmp(iface, xdg_wm_base_interface.name) == 0){
		wmbase = wl_registry_bind(r, name, &xdg_wm_base_interface, MIN(version, 5));
		xdg_wm_base_add_listener(wmbase, &wmlistener, NULL);
	}else if(strcmp(iface, wl_seat_interface.name) == 0 && seat == NULL){
		seat = wl_registry_bind(r, name, &wl_seat_interface, MIN(version, 5));
		wl_seat_add_listener(seat, &seatlistener, NULL);
	}else if(strcmp(iface, wl_data_device_manager_interface.name) == 0)
		ddm = wl_registry_bind(r, name, &wl_data_device_manager_interface, MIN(version, 3));
}

static void globalremove(void *d, struct wl_registry *r, uint32_t name) {}

static const struct wl_registry_listener reglistener = { global, globalremove };

static int
wlinit(void)
{
	const char *theme, *sz;

	if((dpy = wl_display_connect(NULL)) == NULL){
		fprintf(stderr, "9cynth: can't connect to a wayland display\n");
		return 0;
	}
	wl_registry_add_listener(wl_display_get_registry(dpy), &reglistener, NULL);
	wl_display_roundtrip(dpy);
	if(compositor == NULL || shm == NULL || wmbase == NULL){
		fprintf(stderr, "9cynth: compositor has no wl_compositor or wl_shm or xdg_wm_base\n");
		return 0;
	}
	if(ddm && seat)
		ddev = wl_data_device_manager_get_data_device(ddm, seat);
	xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

	theme = getenv("XCURSOR_THEME");
	sz = getenv("XCURSOR_SIZE");
	curtheme = wl_cursor_theme_load(theme, sz && atoi(sz) > 0 ? atoi(sz) : 16, shm);
	if(curtheme){
		cursor = wl_cursor_theme_get_cursor(curtheme, "left_ptr");
		if(cursor == NULL)
			cursor = wl_cursor_theme_get_cursor(curtheme, "default");
	}
	cursurf = wl_compositor_create_surface(compositor);

	surface = wl_compositor_create_surface(compositor);
	xsurface = xdg_wm_base_get_xdg_surface(wmbase, surface);
	xdg_surface_add_listener(xsurface, &xslistener, NULL);
	toplevel = xdg_surface_get_toplevel(xsurface);
	xdg_toplevel_add_listener(toplevel, &tllistener, NULL);
	xdg_toplevel_set_app_id(toplevel, "9cynth");
	xdg_toplevel_set_title(toplevel, "9cynth");
	xdg_toplevel_set_min_size(toplevel, 480, 360);
	wl_surface_commit(surface);
	wl_display_roundtrip(dpy);
	return 1;
}

static char *
statepath(void)
{
	return g_build_filename(g_get_user_config_dir(), "9cynth", "state", NULL);
}

static void
loadstate(void)
{
	char *p, *buf, **lines, *v;
	int i;

	p = statepath();
	if(g_file_get_contents(p, &buf, NULL, NULL)){
		lines = g_strsplit(buf, "\n", -1);
		for(i = 0; lines[i]; i++){
			if((v = strchr(lines[i], ' ')) == NULL)
				continue;
			*v++ = 0;
			if(strcmp(lines[i], "volume") == 0)
				setvolume(atoi(v));
			else if(strcmp(lines[i], "shuffle") == 0)
				shuffle = atoi(v) != 0;
			else if(strcmp(lines[i], "repeat") == 0)
				repeat = CLAMP(atoi(v), 0, 2);
			else if(strcmp(lines[i], "list") == 0 && findlist(v) >= 0)
				openlist(findlist(v));
		}
		g_strfreev(lines);
		g_free(buf);
	}
	g_free(p);
}

static void
savestate(void)
{
	char *p, *d, *s;

	p = statepath();
	d = g_path_get_dirname(p);
	g_mkdir_with_parents(d, 0755);
	s = g_strdup_printf("volume %d\nshuffle %d\nrepeat %d\nlist %s\n",
		atomic_load(&P.volume), shuffle, repeat, current() ? current()->name : "Songs");
	g_file_set_contents(p, s, -1, NULL);
	g_free(s);
	g_free(d);
	g_free(p);
}

static void
usage(void)
{
	fprintf(stderr, "usage: 9cynth [-f font] [-m musicdir] [-x command]...\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	PangoFontDescription *fd;
	cairo_font_options_t *fo;
	GPtrArray *cmds;
	struct pollfd pfd[3];
	Ev *e;
	char *shot, *songs, *s;
	gint64 shotat;
	uint64_t junk;
	int i, c, w, lastsec, needdraw;

	shot = NULL;
	shotat = 0;
	cmds = g_ptr_array_new();
	snprintf(musicdir, sizeof musicdir, "%s/Music", g_get_home_dir());
	while((c = getopt(argc, argv, "f:m:x:s:")) != -1){
		switch(c){
		case 'f': fontname = optarg; break;
		case 'm': g_strlcpy(musicdir, optarg, sizeof musicdir); break;
		case 'x': g_ptr_array_add(cmds, optarg); break;
		case 's': shot = optarg; break;	/* png after 2s then quit. for testing */
		default: usage();
		}
	}
	if(optind != argc)
		usage();
	songs = g_build_filename(musicdir, "Songs", NULL);
	g_mkdir_with_parents(songs, 0755);
	g_free(songs);

	av_log_set_level(AV_LOG_QUIET);
	evq = g_async_queue_new();
	evfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	repfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	if(!wlinit())
		return 1;

	pctx = pango_font_map_create_context(pango_cairo_font_map_get_default());
	fo = cairo_font_options_create();
	cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_FULL);
	cairo_font_options_set_hint_metrics(fo, CAIRO_HINT_METRICS_ON);
	if(strstr(fontname, "PxPlus") || strstr(fontname, "VGA") || strstr(fontname, "Gallant"))
		cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_NONE);
	else
		cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_GRAY);
	pango_cairo_context_set_font_options(pctx, fo);
	cairo_font_options_destroy(fo);
	fd = pango_font_description_from_string(fontname);
	lay = pango_layout_new(pctx);
	pango_layout_set_font_description(lay, fd);
	pango_layout_set_single_paragraph_mode(lay, TRUE);
	pango_layout_set_text(lay, "Mg", -1);
	pango_layout_get_pixel_size(lay, &w, &lh);
	if(pango_font_description_get_size_is_absolute(fd))
		pango_font_description_set_absolute_size(fd, pango_font_description_get_size(fd) * 2);
	else
		pango_font_description_set_size(fd, pango_font_description_get_size(fd) * 2);
	biglay = pango_layout_new(pctx);
	pango_layout_set_font_description(biglay, fd);
	pango_layout_set_single_paragraph_mode(biglay, TRUE);
	pango_font_description_free(fd);

	settag(Tmain, "Get Shuffle Repeat Vol- Vol+ Snarf Exit ");
	settag(Tlcol, "New Delete ");
	settag(Trcol, "Prev Play Pause Stop Next ");
	s = g_strdup_printf("%s/ Get | Look ", musicdir);
	settag(Tdir, s);
	g_free(s);
	settag(Tnp, "+Playing Seek- Seek+ Snarf ");
	for(i = Tlcol; i < Ntag; i++)
		tags[i].box = 1;

	songtab = g_hash_table_new(g_str_hash, g_str_equal);
	scanq = g_async_queue_new();
	g_thread_new("scan", scanner, NULL);

	atomic_store(&P.volume, 70);
	P.seekto = -1;
	g_thread_new("decode", decoder, NULL);
	g_thread_new("output", outthread, NULL);

	layout();
	rescan();
	loadstate();
	if(msg[0] == 0)
		gripe("%d songs %d playlists. b2 runs a word b3 plays a song", lists[0]->n, nlists-1);
	for(i = 0; i < (int)cmds->len; i++)
		execute(Tmain, cmds->pdata[i]);
	if(shot)
		shotat = g_get_monotonic_time() + 2*G_TIME_SPAN_SECOND;

	lastsec = -1;
	needdraw = 0;
	while(alive){
		while(wl_display_prepare_read(dpy) != 0)
			wl_display_dispatch_pending(dpy);
		wl_display_flush(dpy);
		pfd[0] = (struct pollfd){ wl_display_get_fd(dpy), POLLIN, 0 };
		pfd[1] = (struct pollfd){ evfd, POLLIN, 0 };
		pfd[2] = (struct pollfd){ repfd, POLLIN, 0 };
		if(poll(pfd, 3, P.state == Playing || shot ? 200 : 1000) > 0 && (pfd[0].revents & POLLIN))
			wl_display_read_events(dpy);
		else
			wl_display_cancel_read(dpy);
		if(wl_display_dispatch_pending(dpy) < 0){
			fprintf(stderr, "9cynth: lost the display\n");
			break;
		}
		if(pfd[1].revents & POLLIN){
			if(read(evfd, &junk, sizeof junk) < 0){
				/* someone else read it. fine */
			}
			while((e = g_async_queue_try_pop(evq)) != NULL)
				appevent(e);
		}
		if((pfd[2].revents & POLLIN) && read(repfd, &junk, sizeof junk) > 0 && repkey)
			dokey(repkey);
		if(P.state == Playing && (int)position() != lastsec){
			lastsec = position();
			dirty = 1;
		}
		updateart();
		if(dirty || needdraw){
			dirty = 0;
			needdraw = !draw();
		}
		if(shot && g_get_monotonic_time() >= shotat && canvas){
			cairo_surface_write_to_png(canvas, shot);
			alive = 0;
		}
	}
	savestate();
	g_mutex_lock(&P.lk);
	P.quit = 1;
	g_cond_signal(&P.cv);
	g_mutex_unlock(&P.lk);
	atomic_store(&P.out, 0);
	wl_display_disconnect(dpy);
	return 0;
}
