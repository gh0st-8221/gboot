#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../gb.h"

void *gb_alloc(size_t n) { return calloc(1, n ? n : 1); }
void gb_free(void *p) { free(p); }

int main(int argc, char **argv)
{
	static struct conf c;
	static char buf[65536];
	struct fb fb;
	FILE *f = fopen(argv[2], "rb"), *o;
	size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
	int W = atoi(argv[3]), H = atoi(argv[4]), sel = atoi(argv[5]);
	struct ui u = {&c, sel, 0, "", 0};

	conf_defaults(&c);
	conf_parse(&c, buf, n);
	if (argc > 6) {
		u.editing = 1;
		u.edit = argv[6];
	}
	fb.w = W;
	fb.h = H;
	fb.px = malloc((size_t)W * H * 4);
	fb_fill(&fb, 0);
	ui_draw(&fb, &u);
	o = fopen(argv[1], "wb");
	fprintf(o, "P6\n%d %d\n255\n", W, H);
	for (size_t i = 0; i < (size_t)W * H; i++) {
		fputc(fb.px[i] >> 16, o);
		fputc(fb.px[i] >> 8, o);
		fputc(fb.px[i], o);
	}
	fclose(o);
	return 0;
}
