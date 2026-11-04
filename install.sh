#!/bin/sh
set -e
[ "$(id -u)" = 0 ] || exec sudo -E "$0" "$@"
[ -d /sys/firmware/efi ] || { echo "system is not booted in UEFI mode"; exit 1; }
command -v efibootmgr >/dev/null || { echo "install efibootmgr"; exit 1; }
here=$(cd "$(dirname "$0")" && pwd)
efi=${EFI:-$here/gbootx64.efi}
[ -f "$efi" ] || efi=$here/dist/gbootx64.efi
[ -f "$efi" ] || { echo "gbootx64.efi not found, run make first"; exit 1; }
esp=${ESP:-}
if [ -z "$esp" ]; then
	for d in /boot /efi /boot/efi; do
		if [ "$(findmnt -no FSTYPE "$d" 2>/dev/null)" = vfat ]; then
			esp=$d
			break
		fi
	done
fi
[ -n "$esp" ] || { echo "ESP not found, run: ESP=/path/to/esp $0"; exit 1; }
src=$(findmnt -no SOURCE "$esp")
dev=$(basename "$src")
part=$(cat "/sys/class/block/$dev/partition")
disk=/dev/$(lsblk -no PKNAME "$src" | head -n1)
echo "ESP: $esp ($src), disk $disk, partition $part"

mkdir -p "$esp/EFI/gboot"
cp "$efi" "$esp/EFI/gboot/gbootx64.efi"

if [ -f "$esp/EFI/gboot/gboot.conf" ]; then
	echo "keeping existing $esp/EFI/gboot/gboot.conf"
else
	opts=$(sed -e 's/BOOT_IMAGE=[^ ]* *//' -e 's/initrd=[^ ]* *//g' /proc/cmdline)
	ucode=""
	[ -f "$esp/intel-ucode.img" ] && ucode="${ucode}initrd /intel-ucode.img
"
	[ -f "$esp/amd-ucode.img" ] && ucode="${ucode}initrd /amd-ucode.img
"
	img=""
	[ -f "$esp/initramfs-linux.img" ] && img="initrd /initramfs-linux.img
"
	[ -f "$esp/initramfs-ginit.img" ] && img="initrd /initramfs-ginit.img
"
	{
		printf 'timeout 3\ndefault 1\ntitle gboot\n\n'
		n=0
		for pass in plain ginit; do
			for f in "$esp"/EFI/Linux/*.efi; do
				[ -f "$f" ] || continue
				b=${f##*/}
				case "$b" in *ginit*) k=ginit ;; *) k=plain ;; esac
				[ "$k" = "$pass" ] || continue
				printf 'entry %s\nefi /EFI/Linux/%s\n\n' "${b%.efi}" "$b"
				n=$((n+1))
			done
		done
		if [ "$n" = 0 ]; then
			if [ -f "$esp/vmlinuz-linux" ]; then
				printf 'entry Arch Linux\nkernel /vmlinuz-linux\n%s%soptions %s\n\n' "$ucode" "$img" "$opts"
				if [ -x /usr/local/bin/ginit ]; then
					printf 'entry Arch Linux (ginit)\nkernel /vmlinuz-linux\n%s%soptions %s init=/usr/local/bin/ginit\n\n' "$ucode" "$img" "$opts"
				fi
			else
				echo "warning: no UKI in $esp/EFI/Linux and no $esp/vmlinuz-linux, no Linux entry written" >&2
			fi
		fi
		if [ -f "$esp/EFI/Microsoft/Boot/bootmgfw.efi" ]; then
			printf 'entry Windows\nefi /EFI/Microsoft/Boot/bootmgfw.efi\n\n'
		fi
		printf 'entry Firmware setup\nfirmware\n\nentry Reboot\nreboot\n\nentry Shutdown\nshutdown\n'
	} > "$esp/EFI/gboot/gboot.conf"
	echo "wrote $esp/EFI/gboot/gboot.conf"
fi

cur=$(efibootmgr | awk '$2=="gboot"{print substr($1,5,4)}' | head -n1)
if [ -n "$cur" ]; then
	echo
	echo "gboot updated, boot entry $cur already exists and is left as is."
	exit 0
fi
old=$(efibootmgr | sed -n 's/^BootOrder: *//p')
efibootmgr -c -d "$disk" -p "$part" -L gboot -l '\EFI\gboot\gbootx64.efi' >/dev/null
num=$(efibootmgr | awk '$2=="gboot"{print substr($1,5,4)}' | tail -n1)
[ -n "$old" ] && efibootmgr -o "$old" >/dev/null
efibootmgr -n "$num" >/dev/null

echo
echo "Boot entry $num created. Boot order is unchanged, gboot will start ONCE on next reboot."
echo "If it works and you want it as default:  efibootmgr -o $num,$old"
echo "To remove it:  efibootmgr -B -b $num ; rm -r $esp/EFI/gboot"
if [ -r /sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c ] && [ "$(od -An -t u1 -j4 -N1 /sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c | tr -d ' ')" = 1 ]; then
	echo "WARNING: Secure Boot is ON, unsigned gboot will be refused. Disable it or sign the file."
fi
