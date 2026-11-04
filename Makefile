CC ?= cc
INC ?= /usr/include/efi
LIBDIR ?= /usr/lib
CRT0 ?= $(LIBDIR)/crt0-efi-x86_64.o
LDS ?= $(LIBDIR)/elf_x86_64_efi.lds
LIBPATH ?= -L$(LIBDIR)
OBJCOPY ?= objcopy
LIBGCC := $(shell $(CC) -print-libgcc-file-name)

CFLAGS += -I$(INC) -I$(INC)/x86_64 -I$(INC)/protocol -DCONFIG_x86_64 -std=c11 \
	-DGNU_EFI_USE_MS_ABI -maccumulate-outgoing-args -mno-red-zone -mno-avx \
	-fPIC -fPIE -fshort-wchar -ffreestanding -fno-stack-protector \
	-fno-strict-aliasing -fno-merge-all-constants -fno-tree-loop-distribute-patterns \
	-O2 -Wall -Wextra

OBJS = main.o conf.o gfx.o

all: gbootx64.efi

%.o: %.c gb.h font.h
	$(CC) $(CFLAGS) -c $< -o $@

gboot.so: $(OBJS)
	$(LD) -nostdlib --warn-common --no-undefined --fatal-warnings --build-id=sha1 \
		-z norelro -z nocombreloc $(LIBPATH) $(CRT0) -pie -Bsymbolic \
		--no-dynamic-linker $(OBJS) -o $@ -T $(LDS) -lefi -lgnuefi $(LIBGCC)

gbootx64.efi: gboot.so
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .rodata -j .rel \
		-j .rela -j .rel.* -j .rela.* -j .rel* -j .rela* -j .areloc -j .reloc \
		-O efi-app-x86_64 $< $@

test/host: test/host.c conf.c gfx.c gb.h font.h
	$(CC) -O1 -Wall -Wextra -o $@ test/host.c conf.c gfx.c

clean:
	rm -f $(OBJS) gboot.so gbootx64.efi test/host

.PHONY: all clean
