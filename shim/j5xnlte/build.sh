#!/bin/sh
# Build the j5xnlte lk2nd loader and wrap it in a boot.img v2 for
# `fastboot boot`. No flash here: trials never write the BOOT partition.
set -eu
cd "$(dirname "$0")"
mkdir -p out
: "${CC:=aarch64-linux-gnu-gcc}"
: "${READELF:=aarch64-linux-gnu-readelf}"
: "${NM:=aarch64-linux-gnu-nm}"
: "${OBJCOPY:=aarch64-linux-gnu-objcopy}"
CFLAGS="-O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -nostdlib -fno-asynchronous-unwind-tables -mgeneral-regs-only -mstrict-align -march=armv8-a -Wall -Wextra -Werror"
"$CC" $CFLAGS -c start.S -o out/start.o
"$CC" $CFLAGS -c loader.c -o out/loader.o
"$CC" $CFLAGS -c ../fdt.c -o out/fdt.o
# The image runs wherever lk2nd puts it with no relocation pass: only the
# header's image_size (start.o, linked at 0) may be an absolute address.
abs=$("$READELF" -r out/loader.o out/fdt.o | grep -c ABS64 || true)
[ "$abs" = 0 ] || { echo "error: $abs absolute relocations in C objects" >&2; exit 1; }
"$CC" $CFLAGS -static -T j5xnlte.ld -Wl,--build-id=none -Wl,--no-warn-rwx-segments -Wl,-N \
	out/start.o out/loader.o out/fdt.o -o out/shim.elf
"$OBJCOPY" -O binary out/shim.elf out/Image
# lk2nd copies the file; pad to image_size so .bss/stacks exist as zeroed bytes.
size=$("$NM" out/shim.elf | awk '$3=="_end"{print strtonum("0x"$1)}')
truncate -s "$size" out/Image
# Ramdisk: an xnu payload (tools/mkpayload.py) if given as $1, else an empty
# newc cpio (a zero-size ramdisk can be rejected) -> diagnostics.
if [ -n "${1:-}" ]; then
	cp "$1" out/ramdisk.bin
else
	(cd out && rm -rf rd && mkdir rd && cd rd && find . | cpio -o -H newc --quiet | gzip -9 > ../ramdisk.bin)
fi
# No --dtb: lk2nd passes its own DTB in x0. No --cmdline: the payload's
# command line comes from tools/mkpayload.py. Header version 0 (kernel +
# ramdisk only): the installed mkbootimg requires a non-empty --dtb for v2,
# and lk2nd ignores the boot image's DTB in favour of its own.
mkbootimg --header_version 0 --kernel out/Image --ramdisk out/ramdisk.bin \
	--base 0 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
	--tags_offset 0x100 --pagesize 4096 \
	-o out/j5xnlte-boot.img
ls -l out/Image out/j5xnlte-boot.img
