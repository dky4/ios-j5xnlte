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
| IMEM | `@0x146bf000` (`qcom,msm-imem`); downstream restart-reason word at `0x146bf65c` |
| PSHOLD | `@0x004ab000` |

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

The J5 shim has no console: whether lk2nd leaves a usable framebuffer is an
open device-test question. Until a display path exists:

- **Loader failure** (before the jump): the shim writes the stage number to
  IMEM at `0x146bf65c` (`0x4a355800 | stage`, "J5XN" + stage; lk2nd's
  `RESTART_REASON_ADDR`, device-proven NS-writable at this context), waits
  `stage * 5` seconds and drops `PS_HOLD` (`0x4ab000`) — the PM8916 default
  PSHOLD action powers the phone off. The observable is the powerdown
  delay: ~5 s per failure stage (see `shim/j5xnlte/README.md` for the stage
  table); the phone boots back on a power press (it auto-boots while
  charging).
- **Loader success**: the shim jumps to XNU. A wedge, a powerdown without a
  delay, or anything else on screen is then XNU-owned evidence.
- **lk2nd's own log** (not XNU's): `fastboot oem log && fastboot get_staged
  OUT`.
- **lk2nd peek/poke** (while fastboot is alive): `fastboot oem debug readl
  0x146bf65c` reads the restart-reason word; once the PMIC warm-reset path
  (SPMI, `pm8x41_reset_configure(PON_PSHOLD_WARM_RESET)`) is implemented, a
  warm reset keeps the marker readable.
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
  an unproven word, possibly secure-only; `0x146bf65c` (lk2nd's own
  restart-reason word) is proven writable.
- Timer reads work at EL1: the full shim's 10 s `CNTFRQ`/`CNTPCT` delay
  was accurate (no fallback needed).

## Open device-test questions

- Does lk2nd hand off a usable framebuffer, and at which address/stride?
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
   of the shim with a valid payload reaches the XNU entry point. First
   observable: IMEM stage markers / reset delay on failure, a wedge or later
   XNU-owned output on success.
2. **First XNU entry** (`MACHINE_CONFIG=J5XNLTE` marker patch mirroring
   a5xelte `0082`): XNU writes a J5XNLTE marker at its own entry.
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

- **J5XNLTE entry marker** (mirroring a5xelte `0082`): writes a J5XNLTE-owned
  marker at `start_first_cpu` entry — to IMEM `0x146bf65c` (the downstream
  restart-reason word, readable after reboot) rather than a console, since
  there is no early console. Build success, the marker and any screen output
  remain separate claims.
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
