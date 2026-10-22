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

There is no console on the J5, so failures are observable as a powerdown
with a stage-dependent delay and an IMEM marker at `0x0860065c`
(`0x4a355800 | stage`; lk2nd's `RESTART_REASON_ADDR`, read on warm boots
and never written by lk2nd):

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
Success jumps to XNU; a wedge or later XNU-owned output is then XNU
evidence. While fastboot is alive, lk2nd's `oem debug readl 0x0860065c`
peek/poke can read the marker of a warm reset once the PMIC warm-reset path
(SPMI) is implemented.

## Design notes

- The kernel-managed RAM window comes from the ADT (`tools/mkadt_j5xnlte.py`
  sets `dram-base` `0x88000000`, 128 MiB above DRAM start) so lk2nd's load
  addresses (kernel `0x80000000`, DTB `0x81e00000`, ramdisk `0x82000000`)
  stay below it and never overlap the copied segments.
- `boot_args.Video` stays zero: the J5's MDP5+DSI display is a later
  milestone, and no lk2nd framebuffer handoff is assumed yet.
- The payload's `phys_base`/`mem_size` come from the ADT's `dram-base`/
  `dram-size` (mkpayload.py reads them), so the ADT and this shim stay
  consistent by construction.
