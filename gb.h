#ifndef GB_H
#define GB_H

#include <stddef.h>
#include <stdint.h>

#define MAX_ENT 16
#define MAX_INITRD 6
#define PATH_LEN 192
#define OPT_LEN 512
#define NAME_LEN 64

enum { A_BOOT, A_FIRMWARE, A_REBOOT, A_SHUTDOWN };

struct entry {
	char name[NAME_LEN];
	int action;
	char kernel[PATH_LEN];
	char initrd[MAX_INITRD][PATH_LEN];
	int ninitrd;
	char options[OPT_LEN];
};

struct conf {
	int timeout;
	int def;
	int scale;
	char title[NAME_LEN];
	struct entry ent[MAX_ENT];
	int nent;
};

struct fb {
	uint32_t *px;
	int w, h;
};

struct ui {
	const struct conf *c;
	int sel;
	int editing;
	const char *edit;
	const char *msg;
};

void *gb_alloc(size_t n);
void gb_free(void *p);

void conf_defaults(struct conf *c);
void conf_parse(struct conf *c, char *buf, size_t len);

void fb_fill(struct fb *fb, uint32_t c);
void ui_draw(struct fb *fb, const struct ui *u);

static inline size_t gb_strlen(const char *s)
{
	size_t n = 0;
	while (s[n])
		n++;
	return n;
}

static inline int gb_streq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static inline void gb_cpy(char *d, const char *s, size_t n)
{
	size_t i = 0;
	if (!n)
		return;
	while (i + 1 < n && s[i]) {
		d[i] = s[i];
		i++;
	}
	d[i] = 0;
}

static inline void gb_cat(char *d, const char *s, size_t n)
{
	size_t l = gb_strlen(d);
	if (l + 1 < n)
		gb_cpy(d + l, s, n - l);
}

static inline void gb_utoa(char *d, unsigned v)
{
	char t[12];
	int i = 0, j = 0;
	do {
		t[i++] = '0' + v % 10;
		v /= 10;
	} while (v);
	while (i)
		d[j++] = t[--i];
	d[j] = 0;
}

#endif
