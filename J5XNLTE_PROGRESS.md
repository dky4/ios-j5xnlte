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

## 2026-10-22 — first XNU patches cut (0186-0191)

- **XNU source verified as xnu-8796** (`config/MasterVersion` 22.6.0 =
  macOS 13.5, matching the pinned DXB `MACOS_VERSION=13.5`), patched in
  `/tmp/opencode/xnu-src` (git workspace, private outside git): base
  `c97fa7a`, fake-xcrun staging run on Linux (libpthread-514.100.2,
  BearSSL, hfs-627.100.6), donor patches **0001-0113 applied**
  (`--unidiff-zero` from 0106 on; checkpoint `24c38ad`).
- **Donor patch repairs (committed to the repo):** 0094/0095 truncated hunks
  (headers 7,7 -> 6,6), 0112's pre-image register name
  (`S3_4_c15_c10_6` -> `ACNTVCT_EL0`, verified against upstream
  xnu-8796.141.3).
- **Apply-failure diagnosis (resolved):** the 0106+ zero-context hunks (cut
  with `diff -U0`) are rejected by plain `git apply`; they need
  `git apply --unidiff-zero`. Verified by minimal patches.
- **DXB build.sh silently skips failing patches** (`warning "Skipping patch
  ... malformed"`), so the donor's green CI runs never applied 0106+ at all.
  The workflow now seds build.sh's `git apply` flags to add `--unidiff-zero`.
- **Six new patches committed** (series starts at
  `xnu/patches/0186-j5xnlte-board-target.patch`):
  - `0186` board target: `ARM64_BOARD_CONFIG_J5XNLTE` block in
    `board_config.h` (MAX_L2_CLINE 6, MAX_CPUS 4, MAX_CPU_CLUSTERS 1,
    CORE_NCTRS 6 placeholder), new `pexpert/pexpert/arm64/J5XNLTE.h`
    (J5XNLTE 1 + SWEET 1 "reuse donor's SoC-neutral early services",
    HAS_GIC_V3 0, `__ARM_V8_CRYPTO_EXTENSIONS__ 0`, J5XNLTE_VTIMER_INTID 20,
    PLATFORM_PANIC_LOG_DISABLED), new `config/MASTER.arm64.j5xnlte`
    (mirrors the a5xelte donor configuration), `MakeInc.def` SUPPORTED +
    `MACHINE_FLAGS_ARM64_J5XNLTE = -DARM64_BOARD_CONFIG_J5XNLTE
    -mcpu=cortex-a53 -march=armv8-a` and the `ifeq filter` gate.
  - `0187` entry marker: u32 `0x584e5531` ("XNU1") via MOV64 x10/x11 and
    `str w11, [x10]` + `dsb sy` at IMEM `0x146bf65c` next to the A5XELTE
    marker block in `start.s` (an 8-byte store does not transfer: the word
    is 4-byte aligned and Device memory); the five SM7150-hardware gates
    extended with `&& !defined(J5XNLTE)` (`start.s:547`, `start.s:734`,
    `arm_vm_init.c:1828/2182/2191`); a `#elif defined(J5XNLTE)` no-op
    console branch in `sweet_early.c` before `#elif defined(SWEET)`.
  - `0188` GICv2 platform: `pe_fiq.c` `#if defined(J5XNLTE)` GIC-400 branch
    (ADT `/arm-io/gic` reg relative to `pe_arm_get_soc_base_phys()`,
    INTID enable `(1 << 20) | (1 << 1)`, ITARGETSR banked mask,
    `j5xnlte_irq_ack/eoi/send_self_ipi/send_ipi/gic_diagnostics`);
    `sleh.c` `#elif defined(J5XNLTE)` ack/eoi sub-branch with
    `J5XNLTE_VTIMER_INTID` and the GICv2 IAR mask 0x3FF;
    `KernelConfigTables.cpp` matches `'samsung,j5xnlte'` +
    `'arm,cortex-a53'` and excludes AppleCLCD/SweetBattery on the J5;
    `SweetPlatformExpert.cpp` J5XNLTE log strings.
  - `0189` architected timer: `gTimerBase == 0` returns 1 for J5XNLTE
    (CNTV has no MMIO timer base; virtual timer PPI 20 is in the GIC).
  - `0190` CPU identity: SweetCPU J5XNLTE externs/CPU_PORT_NAME, msm8916
    topology guard (MPIDR == logical, 4 cores one cluster), cpuName
    "Cortex-A53", signalCPU self-only (first trial CPU0-only; the boot CPU
    never takes `SweetCPU::startCPU` — `cpu.c:cpu_start()` handles
    `cpu == cpu_number()` via `cpu_machine_init()`).
  - `0191` portable-crypto/shared-service gates: all 48
    `ARM64_BOARD_CONFIG_SWEET || ARM64_BOARD_CONFIG_A5XELTE` flag gates
    across 37 files extended with `|| defined(ARM64_BOARD_CONFIG_J5XNLTE)`
    (on the J5 build only `ARM64_BOARD_CONFIG_J5XNLTE` is defined from
    MACHINE_FLAGS, so these are required for the portable crypto,
    pthread and userland-bridge paths); the `IOService.cpp` assert
    exclusion extended to skip it on the J5 too.
- **SWEET-gate audit (46 bare `defined(SWEET)` gates):** all SoC-neutral
  services (kmem RNG, entropy ccdigest, prng_random, panic copy, SMP
  opt-in clamp, cpu.c VA cleans, IOService includes, trustcache no-AMFI,
  SweetCPU IOCPU bridge). `pe_fiq.c`'s SWEET GICv3 section is inside
  `#if HAS_GIC_V3` (excluded with 0). The only SM7150-hardware exclusion
  points are the sweet_early.c chain and the five 0082-pattern gates
  extended in 0187.
- **Series validation:** the workspace applied 0186-0191 from the cut
  patches in sequence and the resulting tree is **byte-identical** to the
  committed chain (round-trip exact, plain `git apply`).
- **CI wiring (repo changes, uncommitted):** `0019-stage-libpthread.sh`
  config case extended with J5XNLTE; `.github/workflows/xnu.yml` seds
  build.sh's `git apply` to `--unidiff-zero` and defaults
  `MACHINE_CONFIG` to `J5XNLTE` for `j5xnlte`-branch pushes.
- **No device claims:** these are build-tree changes validated by patch
  round-trip only. No CI kernel exists yet, no payload has been booted,
  and no XNU marker or screen report has been observed on the phone.

## 2026-10-22 — CI run 39826891848 diagnosis (machine_config input, not patches)

- **First CI run failed in the Build step with exit code 2** (run
  39826891848, manual `workflow_dispatch`, head `70edf39`). Diagnosis from
  the full job log (fetched with the local git credential-manager token;
  `gh` is not installed and the unauthenticated logs endpoint returns 403):
  - All six J5 patches **0186-0191 applied cleanly** in CI
    (`Applying patch:` lines; 142 patches applied, donor campaign-2 patches
    0114-0185 skip with the expected warnings).
  - build_setup, exporthdrs and installhdrs for
    `DEVELOPMENT_ARM64_J5XNLTE` ran with **zero compile errors** before the
    abort.
  - **Root cause:** the dispatch passed `machine_config = "J5XNLTE SWEET"`.
    build.sh expands `TARGET_CONFIGS="$KERNEL_CONFIG $ARCH_CONFIG
    $MACHINE_CONFIG"` and xnu parses that list in triples (KERNEL ARCH
    MACHINE), so the 4-word list left a dangling `SWEET` which make
    rendered as a bogus second build config `SWEET^^` (empty arch + machine)
    and rejected at `MakeInc.kernel:14: *** Unsupported
    CURRENT_ARCH_CONFIG .  Stop.` (`make[1]: ***
    [build_all_bootstrap_SWEET^^] Error 2`), failing `make install` → step
    exit 2. The genuine J5 kernel was never compiled — the step aborted
    during installhdrs.
- **Fixes (committed):**
  - `03ddc67`: the empty `machine_config` dispatch input auto-selects
    `J5XNLTE` on `j5xnlte` (the old default `SWEET` built the donor config),
    and the Build step keeps only the first word of `MACHINE_CONFIG` so a
    multi-word value can never break `TARGET_CONFIGS` again.
  - `405a13a`: quotes the input description (the unquoted `auto:` colon
    broke workflow parsing — push run 39829414041 failed with zero jobs).
- **Push-triggered CI run 4 (head `405a13a`) queued with
  `MACHINE_CONFIG=J5XNLTE`.**

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

1. Check the push-triggered CI run (head `405a13a`, `MACHINE_CONFIG`
   auto-selected to J5XNLTE on the branch; all patches apply with
   `--unidiff-zero` after the build-script sed). Check the run and the
   `xnu-kernel` artifact via GitHub web (`gh` is not installed).
2. Once a kernel artifact exists: pack a real payload with
   `tools/mkpayload.py` + `tools/mkadt_j5xnlte.py` and run the first
   `fastboot boot` trial of a real kernel payload. The IMEM word at
   `0x146bf65c` should read `0x584e5531` ("XNU1") via
   `fastboot oem debug readl 0x146bf65c` — distinct from the shim's
   "J5X" stage markers.
3. Then: MDP5+DSI console bring-up (the J5 branch in `sweet_early.c` is a
   no-op until the framebuffer exists), eMMC/SDHCI for the rootfs, and the
   smc-only PSCI probe before relying on PSCI for XNU's secondary cores.
