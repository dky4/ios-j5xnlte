# j5xnlte boot shim

Minimal lk2nd loader for `MACHINE_CONFIG=J5XNLTE`. It plays iBoot for xnu:
lk2nd enters it at EL1 (MMU and D-cache off, x0 = lk2nd's own DTB, arm64 Linux
boot protocol), and it boots the SWEETXNU payload that `tools/mkpayload.py`
packed into the boot image's ramdisk. The flow follows `shim/a5xelte/xnu_boot.c`
(4 KiB-aligned ADT and boot_args pages) and the donor `shim/loader.c` (payload
format, `/chosen` initrd), minus everything the J5 does not have: no console,
no framebuffer, no UFS probe.

## Files

- `start.S` — arm64 Linux Image header (flags `0xa`: LE, 4 KiB pages, place
  anywhere) and entry. No vectors: an unexpected exception wedges.
- `loader.c` — payload loader: validates the payload, copies segments to
  `phys_base + (vmaddr - virt_base)`, places the ADT and `boot_args` behind
  the kernel, and jumps to the entry point with x0 = boot_args (physical).
- `j5xnlte.ld` — link script; the image is linked at 0 and runs wherever
  lk2nd puts it.
- `fdt.c`/`fdt.h` are shared with the donor shim (`../fdt.c`).

## Build and test

```sh
shim/j5xnlte/build.sh payload.bin   # payload from tools/mkpayload.py
fastboot boot shim/j5xnlte/out/j5xnlte-boot.img
```

The boot image is header_version 0 (kernel + ramdisk): the installed mkbootimg
requires a non-empty `--dtb` for v2, and lk2nd passes its own DTB in x0
regardless. Without a payload argument the ramdisk is an empty cpio, so a
boot reaches the shim's failure path and tests the markers. Trials are always
`fastboot boot`; never `fastboot flash boot` (that would overwrite lk2nd's
512 KiB-offset boot slot). lk2nd is installed once with
`heimdall flash --BOOT lk2nd.img`.

## Failure observables

There is no console on the J5 (whether lk2nd leaves a usable framebuffer is
an open device-test question), so failures are observable as a reboot with a
stage-dependent delay and an IMEM marker at `0x146bf800`
(`0x4a355800 | stage`; IMEM keeps it across the warm reset):

| Marker | Meaning | Reboot delay |
| --- | --- | --- |
| `0x4a355800` | shim entry reached (stage 0; also present if XNU wedges before its own marker) | no reset from the shim |
| `0x4a355801` | no initrd in `/chosen`, or initrd outside DRAM | ~5 s |
| `0x4a355802` | bad payload magic/version/header | ~10 s |
| `0x4a355803` | payload placement or segment out of bounds | ~15 s |
| `0x4a355804` | ramdisk overlaps the kernel window | ~20 s |

After the delay the shim issues PSCI `SYSTEM_RESET` via `smc #0`, falling back
to `hvc #0` (which conduit lk2nd provides is an open device-test question), so
a failed trial returns to lk2nd/Android by itself. Success jumps to XNU; a
wedge or later XNU-owned output is then XNU evidence.

## Design notes

- The kernel-managed RAM window comes from the ADT (`tools/mkadt_j5xnlte.py`
  sets `dram-base` `0x88000000`, 128 MiB above DRAM start) so lk2nd's own load
  addresses (kernel near `0x80080000`, ramdisk near `0x82000000`) stay below
  it and never overlap the copied segments.
- `boot_args.Video` stays zero: the J5's MDP5+DSI display is a later
  milestone, and no lk2nd framebuffer handoff is assumed yet.
- The payload's `phys_base`/`mem_size` come from the ADT's `dram-base`/
  `dram-size` (mkpayload.py reads them), so the ADT and this shim stay
  consistent by construction.
