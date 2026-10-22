# J5XNLTE port progress

This file tracks work on the Samsung Galaxy J5 2016 (SM-J510FN, `j5xnlte`,
Qualcomm MSM8916). `NOTES.md` and `A5XELTE_PROGRESS.md` are donor histories
(Redmi Note 10 Pro, Galaxy A5 2016). A local build, an observation from the
phone, and a working iOS feature are different milestones. See
`docs/j5xnlte-bringup.md` and `docs/j5xnlte-migration-map.md`.

## 2026-10-22 — port setup and research facts

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

## 2026-10-22 — first device trials: lk2nd fastboot boot chain proven

Four `fastboot boot` trials on the phone (lk2nd fastboot, entered with
Vol-Down; the phone was attached over ADB this session). All trials were
temporary boots — no BOOT-partition write, no flash write, no storage
change.

- **Trial 1 — full shim, empty-cpio ramdisk (unfixed build):** lk2nd failed
  the boot with `remote: 'dtb not found'`: header_version 0 without a DTB
  in the boot image is rejected. **Device finding: lk2nd's `fastboot boot`
  requires a DTB** (its `copy_dtb()` only parses legacy qcdt tables and the
  bootimg v2 `--dtb` field is ignored).
- **Trial 2 — bootimg v2 with `--dtb`:** still `remote: 'dtb not found'`.
  **Device finding: the v2 `--dtb` field is ignored** by lk2nd's
  `cmd_boot`; the working path is a DTB **appended to the kernel** with the
  u32 offset published at header `0x2C` (`dev_tree_appended()`, lk2nd's
  `DTB_OFFSET`). Verified against lk2nd's source (cloned shallow to a
  private /tmp path, not committed): for msm8916 lk2nd forces kernel →
  `0x80000000`, DTB → `0x81e00000`, ramdisk → `0x82000000`
  (`ABOOT_FORCE_*`, `MEMBASE` `0x8F600000`), patches `/chosen`
  (cmdline, kaslr-seed, `linux,initrd-start/end`) and enters arm64 images
  through the TZ milestone call (`scm_elexec_call`: EL1, x0 = DTB, MMU
  off). If `update_device_tree` fails, lk2nd does `ASSERT(0)` and wedges
  itself — that failure wedges permanently.
- **Trial 3 — fixed build (appended DTB, header v0), full shim:** boot
  accepted (`Booting OKAY`), then the phone wedged (no fastboot, adb or
  download mode; needed a power-cycle). Diagnosed afterwards in lk2nd's
  source + shim bisection.
- **Trial 4 — `DIAG=1` PSCI probe (marker to IMEM `0x146bf65c`, 3 s delay,
  PSCI `SYSTEM_RESET` `smc #0`→`hvc #0`):** wedged the same way. **Device
  finding: PSCI from the TZ-milestone EL1 state wedges** (the delay elapsed
  and no reset happened).
- **Trial 5 — `DIAG=1` entry probe (marker, then `PS_HOLD` `0x4ab000` = 0):**
  **the phone properly powered OFF ~1 s after the boot.** **Device finding:
  the whole chain runs** — lk2nd boot, TZ milestone entry at EL1, start.S,
  the IMEM marker write, and the PS_HOLD store all executed.
- **Trial 6 — full shim with the PS_HOLD powerdown reset, empty-cpio
  ramdisk:** **the phone powered off ~10 s after the boot — the designed
  stage-2 timing.** This proves end-to-end: the appended DTB passed lk2nd's
  `update_device_tree`, `/chosen` contained the patched
  `linux,initrd-start/end` (the initrd was found and bounds-checked), the
  payload magic check failed as expected on the cpio ramdisk (stage 2), the
  `CNTFRQ`/`CNTPCT` 10 s delay was accurate at EL1, and the PS_HOLD
  powerdown is a working, safe reset.
- **More device findings:**
  - IMEM `0x146bf800` (the original marker word) wedged the first probe —
    an unproven word, possibly secure-only. The marker now lives at
    `0x146bf65c`, lk2nd's `RESTART_REASON_ADDR`, device-proven writable at
    this context.
  - The PM8916 default PSHOLD action is powerdown: `PS_HOLD = 0` at
    `0x4ab000` needs no SPMI setup and is the proven reset. lk2nd's own
    warm reset needs SPMI (`pm8x41_reset_configure(PON_PSHOLD_WARM_RESET)`)
    — later work if a warm reset (readable marker) is wanted.
  - While fastboot is alive, lk2nd peeks/pokes memory: `fastboot oem debug
    readl 0x146bf65c`; `fastboot oem log` stages lk2nd's log.
  - The phone auto-boots to Android while charging after a powerdown
    (charging boot); power press boots it normally.
- **Fixes in the shim:** appended `dtb.dts` (8-aligned) with the offset at
  header `0x2C`, header_version 0, IMEM marker moved to `0x146bf65c`,
  `PS_HOLD` powerdown as the failure reset (PSCI kept out of the path),
  `DIAG=1` entry-probe build, and doc updates (the boot-image layout and
  device findings are in `shim/j5xnlte/README.md` and
  `docs/j5xnlte-bringup.md`).
- **No XNU claims:** these trials prove the lk2nd → shim chain and the
  failure/reset observables only. No XNU patch has been cut yet (the
  `research/bt28/xnu` tree is needed on another machine), no valid payload
  has been booted, and the screen is unproven.

## Open device-test questions

- Does lk2nd hand off a usable framebuffer (address/stride), and when?
- Virtual timer INTID (20 expected from the DT) and `CNTFRQ_EL0` (19.2 MHz
  expected) read back from EL1 — the shim's delay worked, but the exact
  values were not measured.
- Whether `PSCI SYSTEM_RESET` via `smc #0` alone (without the `hvc`
  fallback) works, or whether the HVC fallback wedges: retry with a
  dedicated probe before relying on PSCI elsewhere (e.g. `CPU_ON` for XNU's
  secondary cores).
- Ramdisk address in `/chosen` read back from EL1 (the full shim found it;
  the exact address is unmeasured — lk2nd's forced addresses put it at
  `0x82000000`).

## Next verified milestone

Cut the first XNU patches (J5XNLTE entry marker mirroring a5xelte `0082`,
board target mirroring `0085`, GICv2/timer mirroring `0086`/`0099` with
virtual-timer-intid 20 and 4-core topology), build them in CI with
`machine_config: J5XNLTE`, pack a payload with `tools/mkpayload.py` +
`tools/mkadt_j5xnlte.py`, and run the first `fastboot boot` trial of a real
payload: the shim should pass every validation (the cpio diagnostic proved
the stage-2 path) and jump to XNU's entry point.
