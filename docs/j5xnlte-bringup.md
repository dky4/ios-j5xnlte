# J5XNLTE bring-up

Samsung Galaxy J5 (2016), SM-J510FN, codename `j5xnlte`. Qualcomm MSM8916
(Snapdragon 410). This is the operating guide for bringing XNU up on the
device; every trial boots with lk2nd's Android fastboot.

## Hardware facts (from downstream/mainline device trees, not yet measured on this phone)

| Item | Value |
| --- | --- |
| SoC | Qualcomm MSM8916 (Snapdragon 410), 4x Cortex-A53 @ 1.2 GHz |
| CPU feature registers | Same A53 as the A5XELTE work: `ID_AA64MMFR0_EL1=0x1122` expected, no 16 KiB granule, no ARMv8.1 LSE |
| MPIDR | Aff0 0..3, single cluster (downstream DT: cpu@0..cpu@3) |
| GIC-400 (GICv2) | GICD `0x0b000000`, GICC `0x0b002000` (`qcom,msm-qgic2` in the DT) |
| Architected timer | PPI 2/3/4/1 (secure/non-secure/virtual/hyp) → virtual timer INTID **20**, non-secure physical 19; counter 19.2 MHz (`clock-frequency` in the DT) |
| PMIC | PM8916 via SPMI bus `@0x0200f000` (buttons, keys, VADC) |
| RAM | 2 GiB at `0x80000000` |
| Display | 720x1280 AMOLED, MDP5 + DSI (new work) |
| GPU | Adreno 306 (a2xx class, later milestone) |
| Storage | eMMC on SDHCI `sdhc_1` `@0x07824900` (core) / `@0x07824000` (host), bus-width 8, `qcom,sdhci-msm-v4` |
| USB | `@0x078d9000`, `qcom,ci-hdrc` peripheral + `qcom,ehci-host` |
| IMEM | lk2nd maps only `@0x08600000` (1 MB; `RESTART_REASON_ADDR` `0x0860065c`, read on warm boots, never written — reboot modes go to the PMIC spare). The downstream dts `qcom,msm-imem@0x146bf000` (`0x146bf65c`) is real but **not mapped by lk2nd**: lk2nd reads of it data-abort. **Post-reset readback is dead**: IMEM words read `0` after any warm cycle (writel/readl work in-session only — the boot ROM/SBL re-initializes IMEM on every boot). No marker home persists across resets; handoff is in-boot only |
| PSHOLD | `@0x004ab000` |
| Scratch DRAM | lk2nd maps `SCRATCH_ADDR` `0x90000000` for the full 256 MB and uses it as the fastboot download buffer; its ramoops (scratch + 256M − 512K) starts at `0x9FF80000`. `0x92000000` (32 MB in; past our 16 MB download image) is the **in-boot mailbox**: the loader stashes the splash-fb facts there in the SAME boot for start.s' live screen draw. **It does not survive resets** (device-proven 2026-10-23: reads `0xffffffff` after lk2nd's own `fastboot reboot`) — only same-boot reads are valid |

Values marked "not yet measured" must be confirmed from EL1 on the phone before
they are treated as device facts; record those confirmations in
`J5XNLTE_PROGRESS.md`.

## Boot flow

1. lk2nd is flashed once into the BOOT partition with
   `heimdall flash --BOOT lk2nd.img`. It does not replace the stock Samsung
   bootloader; it is itself booted by it, and it keeps the real boot image at
   a 512 KiB offset inside the boot partition.
2. Hold **Volume Down** while booting (after the Samsung logo/vibration, so
   the stock bootloader ignores the key) to enter lk2nd's Android fastboot.
   This is the only recovery/log path: there is no TWRP on this phone.
3. Trials use `fastboot boot out/j5xnlte-boot.img` from `shim/j5xnlte/`.
   Never `fastboot flash boot`: that writes over the 512 KiB-offset boot slot.
   Never use the Samsung bootchain, TWRP or `shim/a5xelte/build-xnu-boot.sh`.
4. lk2nd enters the booted image at EL1, MMU and D-cache off, x0 = the boot
   image's appended DTB copy at `0x81e00000` (lk2nd's arm64 entry goes
   through the TZ milestone call `scm_elexec_call`: `el1_x0` = DTB,
   `el1_elr` = entry, under the stock Samsung TrustZone firmware). The boot
   image must contain a DTB (see below), and its ramdisk (the SWEETXNU
   payload, `tools/mkpayload.py`) is published through `/chosen
   linux,initrd-start/end`. The payload's `phys_base`/`mem_size` come from
   the ADT's `/chosen dram-base/dram-size` (`tools/mkadt_j5xnlte.py`).

### lk2nd DTB requirement (device finding, 2026-10-22)

lk2nd's `fastboot boot` fails with `remote: 'dtb not found'` unless it
finds a DTB in the received image: its `copy_dtb()` only parses legacy
qcdt dt.img tables and the bootimg v2 `--dtb` field is ignored. The
working path is `dev_tree_appended()` — it reads a u32 DTB offset at
kernel+`0x2C` (reserved `res3` in the arm64 header), accepts a single
non-skales DTB at exactly that offset, copies it to `0x81e00000`, patches
`/chosen` (cmdline, kaslr-seed, `linux,initrd-start/end`) and passes it in
x0. `build.sh` appends `dtb.dts` (8-aligned) to the Image and publishes
the offset at `0x2C`; header version 0 (`dt_size` = 0) skips lk2nd's bogus
qcdt-table parse cleanly. lk2nd's `update_device_tree` requires `/memory`
and `/chosen` nodes in the passed DTB and pads the DTB in place with
`fdt_open_into` (`DTB_PAD_SIZE` 1024 + cmdline_len). The DTB also must not
overlap lk2nd's own memory: lk2nd runs near the top of RAM (`MEMBASE`
`0x8F600000`), above the payload's kernel window.

### Diagnostics without a console

The J5 shim has no console. **Post-reset readback is dead** (device-proven
2026-10-23: IMEM and scratch DRAM are both wiped by the boot ROM/SBL on
every reset — do not retry reads after a reset). The observables are live:

- **Loader failure** (before the jump): the shim writes the stage number to
  scratch DRAM at `0x92000000` (`0x4a355800 | stage`, "J5XN" + stage;
  in-session only), waits `stage * 5` seconds and drops `PS_HOLD`
  (`0x4ab000`) — the PM8916 default PSHOLD action powers the phone off. The
  observable is the powerdown delay: ~5 s per failure stage (see
  `shim/j5xnlte/README.md` for the stage table); the phone boots back on a
  power press (it auto-boots while charging).
- **Loader success**: the shim stashes the splash-fb mailbox at
  `0x92000000` in the SAME boot (magic `0x584e5531`, fb base/stride/depth)
  and jumps to XNU, whose start.s draws a white 64x32 band at the splash
  top-left — the band on the AMOLED says XNU entered. A wedge (splash
  unchanged) or any other screen change is then XNU-owned evidence.
- **In-session reads** (only while fastboot is up, never after a reset):
  `fastboot oem debug readl 0x92000000` reads the probe's in-session words.
- **lk2nd's own log** (not XNU's): `fastboot oem log && fastboot get_staged
  OUT`.
- Later options once XNU runs: a screen console from the MDP5 bring-up, then
  a persistent-log partition or USB.

### Device findings (2026-10-22, first trials)

- lk2nd `fastboot boot` accepted the appended-DTB image; four boots total:
  two identical wedges (unproven IMEM word + PSCI reset), the PSCI probe,
  the entry/PS_HOLD probe (phone properly powered off), and the full shim
  powering off at ~10 s — the stage-2 timing as designed.
- `PSCI SYSTEM_RESET` (`smc #0` `0x84000009`, then `hvc #0`) from the
  TZ-milestone EL1 state wedges the phone (delay elapsed, then no
  powerdown); it stays out of the reset path. PS_HOLD powerdown is the
  proven reset.
- IMEM `0x146bf800` (the original marker word) wedged the first probe —
  an unproven word, possibly secure-only. `0x146bf65c` (the downstream dts
  restart-reason word) is loader-writable (MMU off), but is **not mapped by
  lk2nd**: three `fastboot oem debug readl` attempts ended with the phone
  booting Android, printing no value — lk2nd's `readl` takes a data abort
  on the unmapped VA, panics and warm-reboots, and lk2nd re-enters with no
  key held. `oem debug` itself is safe on mapped addresses.
- Marker moved to `(MSM_SHARED_IMEM_BASE 0x08600000) + 0x65c =
  0x0860065c` (lk2nd's `RESTART_REASON_ADDR`): lk2nd maps this 1 MB base,
  reads the word on warm boots (`check_alarm_boot`) and never writes it
  (`USE_PON_REBOOT_REG=1` sends reboot modes to the PMIC spare; the dload
  cookie goes through SCM; BS_INFO is unused on msm8916) — device-proven
  mapped, writable at plain EL1 SRAM semantics, and readable through
  `fastboot oem debug readl` with the unmodified installed lk2nd.
- Timer reads work at EL1: the full shim's 10 s `CNTFRQ`/`CNTPCT` delay
  was accurate (no fallback needed).

### Device findings (2026-10-23, readback is dead — new debug approach)

- `fastboot oem debug readl 0x0860065c` reads OKAY with the unmodified
  installed lk2nd (the readl-reboot bug is fixed: device-verified, no more
  data-abort reboots into Android).
- **IMEM words do NOT persist across resets**: lk2nd's
  `oem debug writel 0x0860065c` writes and readl reads `0x4a355800`
  in-session, but the word is `0x00000000` after any warm cycle — including
  a clean single cycle (boot payload → force-reboot → read, no Android in
  between) and lk2nd's own `fastboot reboot`. The boot ROM/SBL likely
  re-initializes IMEM on every boot; restart-reason persistence relies on
  the PMIC spare (`USE_PON_REBOOT_REG=1`), not IMEM.
- **Scratch DRAM `0x92000000` does NOT persist either**: a `writel`
  round-trips in-session (`0x4a355800`), but after lk2nd's own
  `fastboot reboot` the word read `0xffffffff` (the phone booted straight
  into fastboot, no Android). User direction: the boot ROM wipes it — do
  not try post-reset readback; it won't work.
- **New debug approach — live observables only** (committed 2026-10-23):
  1. **In-boot mailbox** at fixed scratch `0x92000000`: the full loader
     stashes it in the SAME boot (same-boot DRAM is fine; only the reset
     wipes it): +0 magic `0x584e5531` ("XNU1"), +4 fb physical base,
     +8 rowBytes/stride, +12 depth. Written inside the existing
     `fdt_chosen_fb` Video fill.
  2. **Live screen draw in start.s** (patch `0187`): with the MMU still
     off, XNU reads the mailbox (32-bit `ldr w…` — the loader stores u32s)
     and draws a white 64x32 band at the splash top-left (only when the
     mailbox is valid and depth == 32). The splash framebuffer keeps
     refreshing from DRAM through the jump (MDP set up by the stock
     bootchain, nothing reconfigures it) — the band on the AMOLED is the
     observable that XNU entered. No MDP5 registers, no console, no
     readback.
  3. **Timed ladders** for the probes (DIAG=3): the PSCI return value
     drives the powerdown delay (0 → ~2 s, error → ~10 s). The DIAG=2 fb
     probe draws the band itself, proving the splash-refresh assumption
     before trial A relies on it.
- `readl` still works **in-session** (while fastboot is up) — the probes
  keep their in-session facts at `0x92000000`; nothing is read after a
  reset.

## Open device-test questions

- Does lk2nd hand off a usable framebuffer, and at which address/stride?
  The loader publishes it in the in-boot mailbox at `0x92000000`; trial A
  (white band in start.s) and trial B (DIAG=2 band) answer it live.
- Virtual timer INTID: 20 expected from the DT (sm7150 uses 19); measure from
  EL1 before relying on it.
- `CNTFRQ_EL0` value (19.2 MHz expected from the DT; the shim's delay worked
  at EL1, but the exact frequency was not read back).
- Is the ramdisk present in `/chosen` on `fastboot boot`, and at which
  address? lk2nd's classic LK load addresses put it near `0x82000000`; the
  ADT's `dram-base` must stay above it. (Resolved for the shim: the full
  shim found the initrd via `/chosen` and failed the magic check at stage
  2.)
- Whether `PSCI SYSTEM_RESET` via `smc #0` alone (no `hvc` fallback) works,
  or whether the HVC fallback wedges: retry with a dedicated probe before
  relying on PSCI anywhere else (e.g. `CPU_ON` for XNU's secondary cores).

## Milestone ladder

1. **Shim loader boots a payload** (`shim/j5xnlte/`): lk2nd `fastboot boot`
   of the shim with a valid payload reaches the XNU entry point. Observables:
   the powerdown delay on failure, and on success the loader's in-boot fb
   mailbox plus whatever XNU draws on the splash.
2. **First XNU entry** (`MACHINE_CONFIG=J5XNLTE`, patch `0187`): XNU reads
   the loader's in-boot mailbox at `0x92000000` and draws a white 64x32
   band at the splash top-left — the band on the AMOLED is the observable
   (no post-reset readback; see the 2026-10-23 findings).
3. **4 KiB MMU + board target** (mirroring `0084`/`0085`): dedicated
   `J5XNLTE` board header, ADT identity, 39-bit kernel VA.
4. **Interrupts and timer** (mirroring `0086`/`0099` with virtual-timer-intid
   20, GICD/GICC msm8916 addresses, 4 cores MPIDR 0..3 single cluster).
5. **Cortex-A53 codegen and crypto** (reusing `0094`–`0098` findings).
6. **HFS RAM-disk root and userland** (reusing `0021`–`0026`, `0102`–`0107`).
7. **SDHCI eMMC persistent root** (new, `qcom,sdhci-msm-v4`).
8. **MDP5+DSI display, PM8916 keys** (new).
9. **Adreno 306 GPU** (new, later).

## First XNU patch plan

- **J5XNLTE entry observable** (patch `0187`, mirroring a5xelte `0082`):
  with the MMU still off, XNU reads the loader's in-boot mailbox at scratch
  DRAM `0x92000000` (magic `0x584e5531`, fb base/stride/depth — written in
  the SAME boot, so the reset wipe does not apply) and draws a white 64x32
  band at the splash top-left, rather than a console (there is no early
  console) or a marker readback (device-proven dead 2026-10-23). Build
  success, the mailbox and any screen output remain separate claims.
- **Board target** (mirroring `0085`): `MACHINE_CONFIG=J5XNLTE` board header,
  `samsung,j5xnlte` + `qcom,msm8916` + `AppleARM` ADT identity.
- **GICv2 platform and timer** (mirroring `0086`/`0099`): GICD `0x0b000000` /
  GICC `0x0b002000`, `virtual-timer-intid` 20, architected-timer-only path.
- **Topology** (mirroring `0101`): 4 Cortex-A53 cores, MPIDR 0..3, single
  cluster, `cpus=4` boot argument; the `cpus=N` prefix convention still
  allows `cpus=1` for the first trial.

Patches are cut against xnu-8796 (DXB SHA `663906528d1cd5ea8fde7de6f16de73db9511549`)
after all prior patches, on the machine that has the `research/bt28/xnu`
tree (`diff -u preNNNN btNNNN`); CI builds them via `.github/workflows/xnu.yml`
with `machine_config: J5XNLTE`.
