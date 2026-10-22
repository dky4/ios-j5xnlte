# J5XNLTE port progress

This file tracks work on the Samsung Galaxy J5 2016 (SM-J510FN, `j5xnlte`,
Qualcomm MSM8916). `NOTES.md` and `A5XELTE_PROGRESS.md` are donor histories
(Redmi Note 10 Pro, Galaxy A5 2016). A local build, an observation from the
phone, and a working iOS feature are different milestones. See
`docs/j5xnlte-bringup.md` and `docs/j5xnlte-migration-map.md`.

## 2026-10-22 — port setup and research facts (no device work yet)

- **Device research (web sources, not device evidence):** the SM-J510FN is a
  Qualcomm MSM8916 (Snapdragon 410) device: 4x Cortex-A53 @ 1.2 GHz, GIC-400
  (GICv2, GICD `0x0b000000` / GICC `0x0b002000`), PM8916 via SPMI, 2 GiB RAM,
  720x1280 AMOLED (MDP5+DSI), Adreno 306, eMMC on SDHCI (`sdhc_1`
  `@0x07824900`, `qcom,sdhci-msm-v4`, bus-width 8). Sources: downstream
  `msm8916.dtsi` (Android GoogleSource), mainline arm64 qcom device trees,
  upstream lk2nd device list. The A53 is the same core as the A5XELTE work
  (`ID_AA64MMFR0_EL1=0x1122` expected; no 16 KiB granule, no ARMv8.1 LSE).
- **Correction vs the first research pass:** the msm8916 architected-timer DT
  lists PPI 2/3/4/1 (secure/non-secure/virtual/hyp), so the virtual timer
  INTID is **20**, not the sm7150/SWEET value 19. Not yet measured on the
  phone; recorded in the ADT and docs as expected-from-DT only.
- **lk2nd facts (upstream README):** install once with `heimdall flash --BOOT
  lk2nd.img`; it is booted by the stock Samsung bootloader and keeps the real
  boot image at a 512 KiB offset in the boot partition. Trials use
  `fastboot boot`; `fastboot oem log && fastboot get_staged OUT` dumps
  lk2nd's own log; lk2nd provides PSCI where the firmware lacks it (conduit
  smc-vs-hvc is an open device-test question).
- **New files (local build checks only, nothing flashed or booted):**
  - `shim/j5xnlte/` — lk2nd loader: arm64 Image header (flags `0xa`), payload
    loader with 4 KiB-aligned ADT/boot_args pages (mirroring
    `shim/a5xelte/xnu_boot.c`), IMEM stage markers at `0x146bf800`
    (`0x4a355800 | stage`) and a PSCI `smc`→`hvc` reset on failure. Cross-
    compiles with `aarch64-linux-gnu-gcc`: 34,496-byte Image, 45,056-byte
    boot image (header_version 0 — the installed mkbootimg requires a
    non-empty `--dtb` for v2, and lk2nd passes its own DTB in x0 anyway).
  - `tools/mkadt_j5xnlte.py` — entry ADT (5,984 bytes): `samsung,j5xnlte` +
    `qcom,msm8916` + `AppleARM` identity, 4x Cortex-A53 with MPIDR 0..3 in
    one cluster, GICv2 node with `virtual-timer-intid` 20 and GICD/GICC at
    `0x0b000000`/`0x0b002000` (relative to MMIO base `0x01000000`), eMMC
    window `0x07824900`, and kernel-managed RAM `dram-base 0x88000000` /
    `dram-size 0x78000000` so lk2nd's own load addresses (kernel near
    `0x80080000`, ramdisk near `0x82000000`) stay below the kernel window.
    All values verified by `tools/adt.py dump`.
  - `docs/j5xnlte-bringup.md`, `docs/j5xnlte-migration-map.md`, rewritten
    `AGENTS.md` (J5XNLTE top section; SWEET/A5XELTE demoted to donor
    reference), CI workflow accepts the `j5xnlte` branch.
- **Payload round-trip check (host only):** a synthetic arm64 Mach-O built
  through `tools/mkpayload.py` with the j5xnlte ADT produces a payload whose
  layout (`0x2a8` segment table, `phys_base`/`mem_size` from the ADT) and
  placement pass every validation in `shim/j5xnlte/loader.c`, with segments
  at `0x89004000`, ADT at `0x8900c000`, boot_args 4 KiB-aligned at
  `0x8900e000` and the simulated lk2nd initrd at `0x82000000` outside the
  kernel window.
- **No device claims:** no XNU patch has been cut yet (the `research/bt28/xnu`
  tree is needed on another machine), nothing has been booted, and no phone
  was attached this session.

## Open device-test questions

- Does lk2nd hand off a usable framebuffer (address/stride), and when?
- PSCI conduit under this lk2nd: `smc` (Samsung TZ) or `hvc` (lk2nd)?
- Virtual timer INTID (20 expected from the DT) and `CNTFRQ_EL0` (19.2 MHz
  expected) measured from EL1.
- Ramdisk presence and address in `/chosen` after `fastboot boot`.
- Whether the stock Samsung bootloader reacts badly to the unknown IMEM
  marker at `0x146bf800` (expected: ignored → normal boot).

## Next verified milestone

Cut the first XNU patches (J5XNLTE entry marker mirroring a5xelte `0082`,
board target mirroring `0085`, GICv2/timer mirroring `0086`/`0099` with
virtual-timer-intid 20 and 4-core topology), build them in CI with
`machine_config: J5XNLTE`, pack a payload with `tools/mkpayload.py` +
`tools/mkadt_j5xnlte.py`, and run the first `fastboot boot` trial of
`shim/j5xnlte/out/j5xnlte-boot.img`.
