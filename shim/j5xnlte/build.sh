#!/bin/sh
# Build the j5xnlte lk2nd loader and wrap it in a boot.img v0 (kernel +
# ramdisk, no dt_size) with the DTB appended to the kernel image, for
# `fastboot boot`. No flash here: trials never write the BOOT partition.
set -eu
cd "$(dirname "$0")"
mkdir -p out
: "${CC:=aarch64-linux-gnu-gcc}"
: "${READELF:=aarch64-linux-gnu-readelf}"
: "${NM:=aarch64-linux-gnu-nm}"
: "${OBJCOPY:=aarch64-linux-gnu-objcopy}"
: "${DTC:=dtc}"
CFLAGS="-O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -nostdlib -fno-asynchronous-unwind-tables -mgeneral-regs-only -mstrict-align -march=armv8-a -Wall -Wextra -Werror"
# DIAG=1 builds the entry probe, DIAG=2 the lk2nd.pass-simplefb display
# probe, DIAG=3 the PSCI CPU_ON smc probe instead of the full loader (see
# loader.c: J5XNLTE_DIAG, J5XNLTE_DIAG_FB, J5XNLTE_DIAG_SMC).
case "${DIAG:-}" in
1) CFLAGS="$CFLAGS -DJ5XNLTE_DIAG" ;;
2) CFLAGS="$CFLAGS -DJ5XNLTE_DIAG_FB" ;;
3) CFLAGS="$CFLAGS -DJ5XNLTE_DIAG_SMC" ;;
esac
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
# Minimal DTB passed to the booted image (see dtb.dts): lk2nd's
# `fastboot boot` fails with "dtb not found" unless it can find one in the
# received image. Its copy_dtb() only parses legacy qcdt dt.img tables, and
# the bootimg v2 --dtb field is ignored, so the working path is
# dev_tree_appended(): it reads a u32 DTB offset at kernel+0x2C
# (DTB_OFFSET, platform/msm_shared/include/dev_tree.h) — reserved res3 in
# the arm64 header — accepts a single non-skales DTB at exactly that offset,
# copies it to ABOOT_FORCE_TAGS_ADDR (DDR_START+0x1E00000), patches /chosen
# (cmdline, kaslr-seed, linux,initrd-start/end) and passes it in x0.
"$DTC" -I dts -O dtb -o out/dtb.dtb dtb.dts
python3 - out/Image out/dtb.dtb out/blob <<'EOF'
import struct, sys
image, dtb = bytearray(open(sys.argv[1], 'rb').read()), open(sys.argv[2], 'rb').read()
# 8-align the DTB so lk2nd uses it in place instead of copying from the
# received image buffer.
off = (len(image) + 7) & ~7
image += b'\0' * (off - len(image))
image[0x2c:0x30] = struct.pack('<I', off)
open(sys.argv[3], 'wb').write(bytes(image) + dtb)
EOF
# Header version 0: dt_size field is 0, so lk2nd skips its bogus copy_dtb()
# qcdt-table parse and goes straight to dev_tree_appended(). Its forced
# arm64 addresses (ABOOT_IGNORE_BOOT_HEADER_ADDRS in project/msm8916.mk)
# override all offsets given here. The cmdline arg makes lk2nd publish its
# continuous-splash framebuffer as /chosen/simple-framebuffer (boot_type
# ARM64 only; the full loader ignores it).
mkbootimg --header_version 0 --kernel out/blob --ramdisk out/ramdisk.bin \
	--base 0 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
	--tags_offset 0x100 --pagesize 4096 \
	--cmdline lk2nd.pass-simplefb \
	-o out/j5xnlte-boot.img
ls -l out/Image out/j5xnlte-boot.img
