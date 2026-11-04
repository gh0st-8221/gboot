#include <efi.h>
#include <efilib.h>
#include "gb.h"

#define CONF_PATH1 "\\EFI\\gboot\\gboot.conf"
#define CONF_PATH2 "\\gboot.conf"

__attribute__((weak)) void *memset(void *d, int c, __SIZE_TYPE__ n)
{
	unsigned char *p = d;
	while (n--)
		*p++ = (unsigned char)c;
	return d;
}

__attribute__((weak)) void *memcpy(void *d, const void *s, __SIZE_TYPE__ n)
{
	unsigned char *p = d;
	const unsigned char *q = s;
	while (n--)
		*p++ = *q++;
	return d;
}

__attribute__((weak)) void *memmove(void *d, const void *s, __SIZE_TYPE__ n)
{
	unsigned char *p = d;
	const unsigned char *q = s;
	if (p < q) {
		while (n--)
			*p++ = *q++;
	} else {
		p += n;
		q += n;
		while (n--)
			*--p = *--q;
	}
	return d;
}

void *gb_alloc(size_t n)
{
	return AllocateZeroPool(n ? n : 1);
}

void gb_free(void *p)
{
	if (p)
		FreePool(p);
}

static EFI_HANDLE image, vol;
static EFI_FILE_HANDLE root;
static struct conf cfg;
static EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
static struct fb back;

static void a2w(CHAR16 *d, const char *s, UINTN max)
{
	UINTN i = 0;
	while (s[i] && i + 1 < max) {
		d[i] = s[i] == '/' ? '\\' : (unsigned char)s[i];
		i++;
	}
	d[i] = 0;
}

static UINT8 *read_file(const char *path, UINTN *len)
{
	CHAR16 wp[PATH_LEN + 16];
	EFI_FILE_HANDLE f;
	EFI_FILE_INFO *fi;
	UINT8 *buf;
	UINTN sz, rd;

	a2w(wp, path, PATH_LEN + 16);
	if (EFI_ERROR(uefi_call_wrapper(root->Open, 5, root, &f, wp, EFI_FILE_MODE_READ, 0)))
		return NULL;
	fi = LibFileInfo(f);
	if (!fi) {
		uefi_call_wrapper(f->Close, 1, f);
		return NULL;
	}
	sz = fi->FileSize;
	FreePool(fi);
	if (sz > (128u << 20)) {
		uefi_call_wrapper(f->Close, 1, f);
		return NULL;
	}
	buf = AllocatePool(sz + 1);
	if (!buf) {
		uefi_call_wrapper(f->Close, 1, f);
		return NULL;
	}
	rd = sz;
	if (EFI_ERROR(uefi_call_wrapper(f->Read, 3, f, &rd, buf))) {
		FreePool(buf);
		uefi_call_wrapper(f->Close, 1, f);
		return NULL;
	}
	uefi_call_wrapper(f->Close, 1, f);
	buf[rd] = 0;
	*len = rd;
	return buf;
}

static const char *fail(const char *what, EFI_STATUS st)
{
	static char msg[128];
	char n[12];
	gb_cpy(msg, what, sizeof msg);
	gb_cat(msg, " (EFI error ", sizeof msg);
	gb_utoa(n, (unsigned)(st & 0xff));
	gb_cat(msg, n, sizeof msg);
	gb_cat(msg, ")", sizeof msg);
	return msg;
}

static int gfx_init(void)
{
	EFI_GUID g = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	UINTN n;

	gop = NULL;
	if (EFI_ERROR(uefi_call_wrapper(BS->HandleProtocol, 3, ST->ConsoleOutHandle, &g, (VOID **)&gop)) || !gop)
		if (EFI_ERROR(uefi_call_wrapper(BS->LocateProtocol, 3, &g, NULL, (VOID **)&gop)) || !gop)
			return 0;
	back.w = gop->Mode->Info->HorizontalResolution;
	back.h = gop->Mode->Info->VerticalResolution;
	if (back.w < 320 || back.h < 200 || back.w > 16384 || back.h > 16384)
		return 0;
	n = (UINTN)back.w * back.h * 4;
	back.px = AllocatePool(n);
	if (!back.px)
		return 0;
	uefi_call_wrapper(ST->ConOut->EnableCursor, 2, ST->ConOut, FALSE);
	return 1;
}

static void gfx_present(void)
{
	uefi_call_wrapper(gop->Blt, 10, gop, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)back.px,
			  EfiBltBufferToVideo, 0, 0, 0, 0, (UINTN)back.w, (UINTN)back.h, 0);
}

static void gfx_draw(const struct ui *u)
{
	fb_fill(&back, 0x000000);
	ui_draw(&back, u);
	gfx_present();
}

static void txt_row(int row, const char *s, UINTN attr)
{
	CHAR16 w[80];
	int i = 0, j = 0;
	w[i++] = ' ';
	while (s[j] && i < 70) {
		unsigned char ch = s[j++];
		w[i++] = (ch >= 32 && ch < 127) ? ch : '?';
	}
	while (i < 72)
		w[i++] = ' ';
	w[i] = 0;
	uefi_call_wrapper(ST->ConOut->SetAttribute, 2, ST->ConOut, attr);
	uefi_call_wrapper(ST->ConOut->SetCursorPosition, 3, ST->ConOut, 2, row);
	uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, w);
}

static void txt_draw(const struct ui *u)
{
	UINTN base = EFI_WHITE | EFI_BACKGROUND_BLACK;
	UINTN sel = EFI_BLACK | EFI_BACKGROUND_LIGHTGRAY;
	char buf[160];
	int i, row = 4;

	uefi_call_wrapper(ST->ConOut->SetAttribute, 2, ST->ConOut, base);
	uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);
	txt_row(1, cfg.title, base);
	for (i = 0; i < cfg.nent; i++, row++)
		txt_row(row, cfg.ent[i].name, i == u->sel ? sel : base);
	row++;
	if (u->editing) {
		buf[0] = '$';
		buf[1] = ' ';
		buf[2] = 0;
		gb_cat(buf, u->edit, sizeof buf);
		gb_cat(buf, "_", sizeof buf);
		txt_row(row, buf, base);
	} else if (u->msg) {
		txt_row(row, u->msg, base);
	}
}

static const char *do_firmware(void)
{
	EFI_GUID gv = EFI_GLOBAL_VARIABLE;
	UINT64 sup = 0, ind = 0;
	UINTN sz = sizeof sup;
	EFI_STATUS st;

	st = uefi_call_wrapper(RT->GetVariable, 5, L"OsIndicationsSupported", &gv, NULL, &sz, &sup);
	if (EFI_ERROR(st) || !(sup & EFI_OS_INDICATIONS_BOOT_TO_FW_UI))
		return "Firmware does not support reboot to setup";
	sz = sizeof ind;
	if (EFI_ERROR(uefi_call_wrapper(RT->GetVariable, 5, L"OsIndications", &gv, NULL, &sz, &ind)))
		ind = 0;
	ind |= EFI_OS_INDICATIONS_BOOT_TO_FW_UI;
	st = uefi_call_wrapper(RT->SetVariable, 5, L"OsIndications", &gv,
			       EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS |
				       EFI_VARIABLE_RUNTIME_ACCESS,
			       sizeof ind, &ind);
	if (EFI_ERROR(st))
		return fail("Cannot set OsIndications", st);
	uefi_call_wrapper(RT->ResetSystem, 4, EfiResetCold, EFI_SUCCESS, 0, NULL);
	return "Reset failed";
}

static const char *boot_image(const struct entry *e, const char *opts)
{
	static char o[2048];
	static CHAR16 wo[2048];
	CHAR16 wk[PATH_LEN];
	EFI_HANDLE h;
	EFI_STATUS st;
	EFI_LOADED_IMAGE *li;
	EFI_DEVICE_PATH *dp;
	UINTN xs = 0, i;
	int k;

	if (!e->kernel[0])
		return "Entry has no kernel or efi path";
	gb_cpy(o, opts, sizeof o);
	for (k = 0; k < e->ninitrd; k++) {
		char t[PATH_LEN];
		size_t j;
		gb_cat(o, " initrd=", sizeof o);
		gb_cpy(t, e->initrd[k], sizeof t);
		for (j = 0; t[j]; j++)
			if (t[j] == '/')
				t[j] = '\\';
		gb_cat(o, t, sizeof o);
	}
	for (i = 0; o[i] && i + 1 < 2048; i++)
		wo[i] = (unsigned char)o[i];
	wo[i] = 0;
	a2w(wk, e->kernel, PATH_LEN);
	dp = FileDevicePath(vol, wk);
	if (!dp)
		return "Cannot build device path";
	st = uefi_call_wrapper(BS->LoadImage, 6, FALSE, image, dp, NULL, 0, &h);
	FreePool(dp);
	if (EFI_ERROR(st))
		return fail("Cannot load image", st);
	st = uefi_call_wrapper(BS->HandleProtocol, 3, h, &LoadedImageProtocol, (VOID **)&li);
	if (!EFI_ERROR(st) && wo[0]) {
		li->LoadOptions = wo;
		li->LoadOptionsSize = (UINT32)((i + 1) * sizeof(CHAR16));
	}
	if (back.px) {
		fb_fill(&back, 0);
		gfx_present();
	}
	st = uefi_call_wrapper(BS->StartImage, 3, h, &xs, NULL);
	uefi_call_wrapper(BS->UnloadImage, 1, h);
	if (st == EFI_SUCCESS)
		return NULL;
	return fail("Boot failed", st);
}

static const char *launch(const struct entry *e, const char *opts)
{
	if (e->action == A_FIRMWARE)
		return do_firmware();
	if (e->action == A_REBOOT || e->action == A_SHUTDOWN) {
		uefi_call_wrapper(RT->ResetSystem, 4, e->action == A_REBOOT ? EfiResetCold : EfiResetShutdown,
				  EFI_SUCCESS, 0, NULL);
		return "Reset failed";
	}
	return boot_image(e, opts);
}

static void menu(const char *msg)
{
	static char ed[OPT_LEN];
	struct ui u = {&cfg, cfg.def, 0, ed, msg};
	int have_gfx = gfx_init();
	int period = 100, rem = cfg.timeout > 0 ? cfg.timeout * 1000 : -1;
	int dirty = 1;
	EFI_EVENT tev, evs[2];
	UINTN idx;
	EFI_INPUT_KEY k;

	uefi_call_wrapper(BS->CreateEvent, 5, EVT_TIMER, TPL_APPLICATION, NULL, NULL, &tev);
	uefi_call_wrapper(BS->SetTimer, 3, tev, TimerPeriodic, (UINT64)period * 10000);
	evs[0] = ST->ConIn->WaitForKey;
	evs[1] = tev;

	for (;;) {
		if (dirty) {
			if (have_gfx)
				gfx_draw(&u);
			else
				txt_draw(&u);
			dirty = 0;
		}
		uefi_call_wrapper(BS->WaitForEvent, 3, 2, evs, &idx);
		if (idx == 1) {
			if (rem >= 0) {
				rem -= period;
				if (rem <= 0) {
					u.msg = launch(&cfg.ent[u.sel], cfg.ent[u.sel].options);
					rem = -1;
					dirty = 1;
				}
			}
			continue;
		}
		if (EFI_ERROR(uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &k)))
			continue;
		rem = -1;
		u.msg = NULL;
		dirty = 1;
		if (u.editing) {
			size_t l = gb_strlen(ed);
			if (k.ScanCode == SCAN_ESC) {
				u.editing = 0;
			} else if (k.UnicodeChar == CHAR_CARRIAGE_RETURN || k.UnicodeChar == CHAR_LINEFEED) {
				u.editing = 0;
				u.msg = launch(&cfg.ent[u.sel], ed);
			} else if (k.UnicodeChar == CHAR_BACKSPACE) {
				if (l)
					ed[l - 1] = 0;
			} else if (k.UnicodeChar >= 32 && k.UnicodeChar < 127 && l + 1 < OPT_LEN) {
				ed[l] = (char)k.UnicodeChar;
				ed[l + 1] = 0;
			}
			continue;
		}
		if (k.ScanCode == SCAN_UP || k.UnicodeChar == 'k') {
			u.sel = u.sel > 0 ? u.sel - 1 : cfg.nent - 1;
		} else if (k.ScanCode == SCAN_DOWN || k.UnicodeChar == 'j') {
			u.sel = u.sel + 1 < cfg.nent ? u.sel + 1 : 0;
		} else if (k.ScanCode == SCAN_HOME) {
			u.sel = 0;
		} else if (k.ScanCode == SCAN_END) {
			u.sel = cfg.nent - 1;
		} else if (k.UnicodeChar == CHAR_CARRIAGE_RETURN || k.UnicodeChar == CHAR_LINEFEED) {
			u.msg = launch(&cfg.ent[u.sel], cfg.ent[u.sel].options);
		} else if (k.UnicodeChar == 'e' || k.UnicodeChar == 'E') {
			if (cfg.ent[u.sel].action == A_BOOT) {
				gb_cpy(ed, cfg.ent[u.sel].options, sizeof ed);
				u.editing = 1;
			}
		}
	}
}

static void add_builtin(const char *name, int action)
{
	struct entry *e = &cfg.ent[cfg.nent++];
	gb_cpy(e->name, name, sizeof e->name);
	e->action = action;
}

EFI_STATUS efi_main(EFI_HANDLE img, EFI_SYSTEM_TABLE *systab)
{
	EFI_LOADED_IMAGE *li;
	UINT8 *d;
	UINTN n;
	const char *msg = NULL;

	InitializeLib(img, systab);
	image = img;
	uefi_call_wrapper(BS->SetWatchdogTimer, 4, 0, 0, 0, NULL);
	conf_defaults(&cfg);
	if (EFI_ERROR(uefi_call_wrapper(BS->HandleProtocol, 3, img, &LoadedImageProtocol, (VOID **)&li)))
		return EFI_LOAD_ERROR;
	vol = li->DeviceHandle;
	root = LibOpenRoot(vol);
	d = root ? read_file(CONF_PATH1, &n) : NULL;
	if (!d && root)
		d = read_file(CONF_PATH2, &n);
	if (d) {
		conf_parse(&cfg, (char *)d, n);
		FreePool(d);
		if (!cfg.nent)
			msg = "gboot.conf has no entries";
	} else {
		msg = "gboot.conf not found";
	}
	if (!cfg.nent) {
		add_builtin("Firmware setup", A_FIRMWARE);
		add_builtin("Reboot", A_REBOOT);
		add_builtin("Shutdown", A_SHUTDOWN);
		cfg.timeout = -1;
		cfg.def = 0;
	}
	if (cfg.timeout == 0) {
		if (EFI_ERROR(uefi_call_wrapper(BS->CheckEvent, 1, ST->ConIn->WaitForKey))) {
			msg = launch(&cfg.ent[cfg.def], cfg.ent[cfg.def].options);
		} else {
			EFI_INPUT_KEY k;
			uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &k);
		}
	}
	menu(msg);
	return EFI_SUCCESS;
}
