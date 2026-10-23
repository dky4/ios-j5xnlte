# j5xnlte boot shim

Minimal lk2nd loader for `MACHINE_CONFIG=J5XNLTE`. It plays iBoot for xnu:
lk2nd enters it at EL1 (MMU and D-cache off, x0 = the boot image's appended
DTB copy at `0x81e00000`, patched by lk2nd with the initrd ranges), and it
boots the SWEETXNU payload that `tools/mkpayload.py` packed into the boot
image's ramdisk. The flow follows `shim/a5xelte/xnu_boot.c` (4 KiB-aligned
ADT and boot_args pages) and the donor `shim/loader.c` (payload format,
`/chosen` initrd), minus everything the J5 does not have: no console, no
framebuffer, no UFS probe.

lk2nd's arm64 entry goes through the TZ milestone call (`scm_elexec_call`:
`el1_x0` = DTB, `el1_elr` = entry), so the image is entered at EL1 under the
stock Samsung TrustZone firmware.

## Files

- `start.S` — arm64 Linux Image header (flags `0xa`: LE, 4 KiB pages, place
  anywhere) and entry. No vectors: an unexpected exception wedges.
- `loader.c` — payload loader: validates the payload, copies segments to
  `phys_base + (vmaddr - virt_base)`, places the ADT and `boot_args` behind
  the kernel, and jumps to the entry point with x0 = boot_args (physical).
- `dtb.dts` — minimal arm64 DTB appended to the kernel image; lk2nd's
  `update_device_tree` requires `/memory` and `/chosen` nodes and patches
  `/chosen` with the initrd ranges, cmdline and kaslr-seed. XNU gets its
  device tree from the payload's ADT, not from this DTB.
- `j5xnlte.ld` — link script; the image is linked at 0 and runs wherever
  lk2nd puts it.
- `fdt.c`/`fdt.h` are shared with the donor shim (`../fdt.c`).

## Boot image layout

lk2nd's `fastboot boot` fails with "dtb not found" unless it finds a DTB in
the received image: its `copy_dtb()` only parses legacy qcdt dt.img tables
and the bootimg v2 `--dtb` field is ignored, so the working path is
`dev_tree_appended()` — it reads a u32 DTB offset at kernel+`0x2C`
(`DTB_OFFSET`, reserved `res3` in the arm64 header), accepts a single
non-skales DTB at exactly that offset, copies it to
`ABOOT_FORCE_TAGS_ADDR` (`DDR_START+0x1E00000` = `0x81e00000`), patches
`/chosen` and passes it in x0. `build.sh` therefore appends `dtb.dtb`
(8-aligned) to the Image and publishes the offset at header `0x2C`. Header
version 0 (`dt_size` = 0) skips lk2nd's bogus qcdt-table parse; lk2nd's
forced arm64 addresses (`ABOOT_IGNORE_BOOT_HEADER_ADDRS` in
`project/msm8916.mk`) override all offsets in the header. lk2nd itself runs
near the top of RAM (`MEMBASE` `0x8F600000`), above the payload's kernel
window.

## Build and test

```sh
shim/j5xnlte/build.sh payload.bin   # payload from tools/mkpayload.py
fastboot boot shim/j5xnlte/out/j5xnlte-boot.img
```

Without a payload argument the ramdisk is an empty cpio, so a boot reaches
the shim's failure path and tests the markers. `DIAG=1 build.sh` builds the
entry/PS_HOLD probe instead of the full loader. Trials are always
`fastboot boot`; never `fastboot flash boot` (that would overwrite lk2nd's
512 KiB-offset boot slot). lk2nd is installed once with
`heimdall flash --BOOT lk2nd.img`.

## Failure observables

There is no console on the J5, and **post-reset readback is dead**
(device-proven 2026-10-23: IMEM and scratch DRAM are both wiped by the boot
ROM/SBL on every reset — do not retry reads after a reset). Failures are
observable as a powerdown with a stage-dependent delay; the in-session
marker word at `0x92000000` (`0x4a355800 | stage`; 32 MB into lk2nd's
`SCRATCH_ADDR 0x90000000`, which lk2nd maps in full) is only readable while
fastboot is up:

| Marker | Meaning | Powerdown delay |
| --- | --- | --- |
| `0x4a355800` | shim entry reached (stage 0; also present if XNU wedges before its own marker) | no reset from the shim |
| `0x4a355801` | no initrd in `/chosen`, or initrd outside DRAM | ~5 s |
| `0x4a355802` | bad payload magic/version/header | ~10 s |
| `0x4a355803` | payload placement or segment out of bounds | ~15 s |
| `0x4a355804` | ramdisk overlaps the kernel window | ~20 s |

After the delay the shim drops `PS_HOLD` (`0x4ab000`): the PM8916 default
PSHOLD action powers the phone off. This is device-proven and safe; the
phone boots back on a power press (it auto-boots while charging). PSCI
`SYSTEM_RESET` from the TZ-milestone EL1 state wedged a dedicated probe
(delay elapsed, then no powerdown), so it stays out of the reset path.

Success stashes the in-boot mailbox at `0x92000000` (magic `0x584e5531`,
fb base/stride/depth — written and consumed in ONE boot, so the reset wipe
does not apply) and jumps to XNU, whose start.s draws a white 64x32 band at
the splash top-left (live observable, MMU off, plain physical stores — the
splash fb keeps refreshing from DRAM through the jump). A wedge (splash
unchanged) or any other screen change is then XNU-owned evidence. The
probes time their powerdowns to carry data: DIAG=3's PSCI return value
drives the delay (0 → ~2 s, error → ~10 s), and the DIAG=2 fb probe draws
the band itself, proving the splash-refresh assumption before trial A
relies on it.

## Design notes

- The kernel-managed RAM window comes from the ADT (`tools/mkadt_j5xnlte.py`
  sets `dram-base` `0x88000000`, 128 MiB above DRAM start) so lk2nd's load
  addresses (kernel `0x80000000`, DTB `0x81e00000`, ramdisk `0x82000000`)
  stay below it and never overlap the copied segments.
- `boot_args.Video` comes from lk2nd's `/chosen/framebuffer` (the
  continuous splash) when the cmdline carries `lk2nd.pass-simplefb`, and
  the same facts go into the in-boot mailbox at `0x92000000` for start.s'
  live screen draw. Zero if lk2nd detected no splash.
- The payload's `phys_base`/`mem_size` come from the ADT's `dram-base`/
  `dram-size` (mkpayload.py reads them), so the ADT and this shim stay
  consistent by construction.
