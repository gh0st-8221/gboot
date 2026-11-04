#include "gb.h"

static int is_sp(char c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

static int to_int(const char *s, int *out)
{
	int neg = 0, v = 0, any = 0;
	if (*s == '-') {
		neg = 1;
		s++;
	}
	while (*s >= '0' && *s <= '9') {
		v = v * 10 + (*s - '0');
		if (v > 1000000)
			v = 1000000;
		s++;
		any = 1;
	}
	if (!any)
		return 0;
	*out = neg ? -v : v;
	return 1;
}

void conf_defaults(struct conf *c)
{
	char *p = (char *)c;
	size_t i;
	for (i = 0; i < sizeof *c; i++)
		p[i] = 0;
	c->timeout = 5;
	gb_cpy(c->title, "gboot", sizeof c->title);
}

static void line(struct conf *c, struct entry **cur, char *k, char *v)
{
	int n;
	struct entry *e = *cur;

	if (gb_streq(k, "entry")) {
		if (c->nent < MAX_ENT) {
			e = &c->ent[c->nent++];
			gb_cpy(e->name, v, sizeof e->name);
			*cur = e;
		} else {
			*cur = 0;
		}
	} else if (gb_streq(k, "timeout")) {
		if (to_int(v, &n))
			c->timeout = n;
	} else if (gb_streq(k, "default")) {
		if (to_int(v, &n) && n >= 1)
			c->def = n - 1;
	} else if (gb_streq(k, "scale")) {
		if (to_int(v, &n) && n >= 0 && n <= 8)
			c->scale = n;
	} else if (gb_streq(k, "title")) {
		gb_cpy(c->title, v, sizeof c->title);
	} else if (!e) {
		return;
	} else if (gb_streq(k, "kernel") || gb_streq(k, "efi") || gb_streq(k, "linux")) {
		gb_cpy(e->kernel, v, sizeof e->kernel);
	} else if (gb_streq(k, "initrd")) {
		if (e->ninitrd < MAX_INITRD)
			gb_cpy(e->initrd[e->ninitrd++], v, PATH_LEN);
	} else if (gb_streq(k, "options")) {
		gb_cpy(e->options, v, sizeof e->options);
	} else if (gb_streq(k, "firmware")) {
		e->action = A_FIRMWARE;
	} else if (gb_streq(k, "reboot")) {
		e->action = A_REBOOT;
	} else if (gb_streq(k, "shutdown")) {
		e->action = A_SHUTDOWN;
	}
}

void conf_parse(struct conf *c, char *buf, size_t len)
{
	struct entry *cur = 0;
	size_t i = 0;

	buf[len] = 0;
	while (i < len) {
		char *s = buf + i, *k, *v, *e;
		while (i < len && buf[i] != '\n')
			i++;
		buf[i] = 0;
		i++;
		while (is_sp(*s))
			s++;
		if (!*s || *s == '#')
			continue;
		e = s + gb_strlen(s);
		while (e > s && is_sp(e[-1]))
			*--e = 0;
		k = s;
		while (*s && !is_sp(*s))
			s++;
		if (*s)
			*s++ = 0;
		while (is_sp(*s))
			s++;
		v = s;
		line(c, &cur, k, v);
	}
	if (c->def >= c->nent)
		c->def = 0;
}
