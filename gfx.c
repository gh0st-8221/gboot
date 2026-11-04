#include "gb.h"
#include "font.h"

#define BLACK 0x000000
#define WHITE 0xffffff

void fb_fill(struct fb *fb, uint32_t c)
{
	size_t i, n = (size_t)fb->w * fb->h;
	for (i = 0; i < n; i++)
		fb->px[i] = c;
}

static void rect(struct fb *fb, int x, int y, int w, int h, uint32_t c)
{
	int i, j;
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x + w > fb->w)
		w = fb->w - x;
	if (y + h > fb->h)
		h = fb->h - y;
	for (j = 0; j < h; j++) {
		uint32_t *p = fb->px + (size_t)(y + j) * fb->w + x;
		for (i = 0; i < w; i++)
			p[i] = c;
	}
}

static const unsigned char *glyph(uint32_t cp)
{
	int lo = 0, hi = FONT_N - 1;
	while (lo <= hi) {
		int m = (lo + hi) / 2;
		if (font_cp[m] == cp)
			return font_bits[m];
		if (font_cp[m] < cp)
			lo = m + 1;
		else
			hi = m - 1;
	}
	return cp == '?' ? font_bits[0] : glyph('?');
}

static uint32_t u8next(const char **sp)
{
	const unsigned char *s = (const unsigned char *)*sp;
	uint32_t c = s[0];
	int n = 0;
	if (c < 0x80) {
		n = 1;
	} else if ((c & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80) {
		c = ((c & 0x1f) << 6) | (s[1] & 0x3f);
		n = 2;
	} else if ((c & 0xf0) == 0xe0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80) {
		c = ((c & 0x0f) << 12) | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f);
		n = 3;
	} else if ((c & 0xf8) == 0xf0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80 &&
		   (s[3] & 0xc0) == 0x80) {
		c = '?';
		n = 4;
	} else {
		c = '?';
		n = 1;
	}
	*sp += n;
	return c;
}

static int u8len(const char *s)
{
	int n = 0;
	while (*s) {
		u8next(&s);
		n++;
	}
	return n;
}

static const char *u8skip(const char *s, int n)
{
	while (*s && n-- > 0)
		u8next(&s);
	return s;
}

static void text(struct fb *fb, int x, int y, const char *s, uint32_t col, int sc, int maxc)
{
	while (*s && maxc-- > 0) {
		const unsigned char *g = glyph(u8next(&s));
		int r, c;
		for (r = 0; r < 16; r++)
			for (c = 0; c < 8; c++)
				if (g[r] & (0x80 >> c))
					rect(fb, x + c * sc, y + r * sc, sc, sc, col);
		x += 8 * sc;
	}
}

void ui_draw(struct fb *fb, const struct ui *u)
{
	const struct conf *c = u->c;
	int s = c->scale ? c->scale : fb->h >= 1800 ? 3 : fb->h >= 900 ? 2 : 1;
	int cw = 8 * s, ch = 16 * s, rh = ch + 8 * s, pad = 2 * cw, ln = ch + 2 * s;
	int maxc = 40, i, cols, pw, ph, px, py, y, lx, room;

	for (i = 0; i < c->nent; i++) {
		int l = u8len(c->ent[i].name) + 2;
		if (l > maxc)
			maxc = l;
	}
	if (u8len(c->title) > maxc)
		maxc = u8len(c->title);
	if (maxc > 60)
		maxc = 60;
	cols = maxc + 4;
	pw = cols * cw + pad;
	if (pw > fb->w - 2 * cw)
		pw = fb->w - 2 * cw;
	ph = pad / 2 + rh + c->nent * rh + pad / 2;
	if (u->editing || u->msg)
		ph += ln + pad / 2;
	px = (fb->w - pw) / 2;
	py = (fb->h - ph) / 2;
	if (py < 0)
		py = 0;
	room = (pw - pad) / cw;

	rect(fb, px, py, pw, ph, BLACK);
	rect(fb, px, py, pw, 2 * s, WHITE);
	rect(fb, px, py + ph - 2 * s, pw, 2 * s, WHITE);
	rect(fb, px, py, 2 * s, ph, WHITE);
	rect(fb, px + pw - 2 * s, py, 2 * s, ph, WHITE);

	y = py + pad / 2;
	text(fb, px + (pw - u8len(c->title) * cw) / 2, y, c->title, WHITE, s, 1 << 20);
	rect(fb, px + pad / 2, y + ch + 3 * s, pw - pad, s, WHITE);
	y += rh;
	lx = px + pad / 2;

	for (i = 0; i < c->nent; i++, y += rh) {
		int sel = i == u->sel;
		if (sel) {
			rect(fb, lx, y, pw - pad, rh, WHITE);
			text(fb, lx + cw / 2, y + (rh - ch) / 2, ">", BLACK, s, 1);
		}
		text(fb, lx + cw * 2, y + (rh - ch) / 2, c->ent[i].name, sel ? BLACK : WHITE, s, room - 3);
	}

	y += pad / 2;
	if (u->editing) {
		int l = u8len(u->edit);
		const char *e = u->edit;
		if (l > room - 3)
			e = u8skip(e, l - (room - 3));
		text(fb, lx, y, "$", WHITE, s, 1);
		text(fb, lx + cw * 2, y, e, WHITE, s, room - 2);
		text(fb, lx + cw * 2 + u8len(e) * cw, y, "_", WHITE, s, 1);
	} else if (u->msg) {
		text(fb, lx, y, u->msg, WHITE, s, room);
	}
}
