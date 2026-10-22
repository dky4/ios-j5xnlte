# XNU build targets

The A5XELTE fork builds XNU 8796 locally with `MACHINE_CONFIG=A5XELTE` and 4 KiB pages. Its Samsung-specific changes begin at `patches/0082-a5xelte-4k-entry.patch`; the earlier SWEET patches still supply shared XNU and userland scaffolding. The physical phone has displayed dynamic C/`kprintf` output, passed CPU0 SGI1, registered pthread and HFS, selected an in-memory `md0` root and mounted its HFS+ volume. Patch `0103` passed its device test; the next stop is the expected missing `/sbin/launchd` on the empty image. iOS userland execution remains unverified. See [the A5XELTE progress log](../A5XELTE_PROGRESS.md) and [migration map](../docs/a5xelte-migration-map.md).

## J5XNLTE (Galaxy J5 2016, SM-J510FN)

The `j5xnlte` branch adds `MACHINE_CONFIG=J5XNLTE` for the Qualcomm MSM8916
(Snapdragon 410) phone, booted via lk2nd and `fastboot boot`. The series
starts at `patches/0186-j5xnlte-board-target.patch`:

- `0186` board target: `ARM64_BOARD_CONFIG_J5XNLTE` block in `board_config.h`,
  new `pexpert/pexpert/arm64/J5XNLTE.h` and `config/MASTER.arm64.j5xnlte`,
  `MakeInc.def` SUPPORTED/MACHINE_FLAGS (`-mcpu=cortex-a53 -march=armv8-a`,
  no `+crypto`: Snapdragon 410 lacks the ARMv8 crypto extensions).
- `0187` entry marker: the earliest XNU-owned marker word `0x584e5531` ("XNU1")
  stored as a **32-bit** store at Qualcomm IMEM `0x146bf65c` (Device memory,
  4-byte aligned); the five SWEET SM7150-hardware gates extended with
  `&& !defined(J5XNLTE)`; a `#elif defined(J5XNLTE)` no-op console branch in
  `sweet_early.c`.
- `0188` GICv2 platform: `pe_fiq.c` maps `/arm-io/gic` from the ADT and brings
  up the GIC-400 (GICD `0x0b000000`, GICC `0x0b002000`); `sleh.c` takes the
  virtual timer PPI 20 as a Group 1 IRQ; `KernelConfigTables.cpp` matches
  `'samsung,j5xnlte'`.
- `0189` architected timer: `gTimerBase == 0` is expected on msm8916 (CNTV,
  mirrors 0099).
- `0190` CPU identity: the SweetCPU bridge for four Cortex-A53 cores
  (MPIDR 0..3, one cluster), first trial CPU0-only.
- `0191` portable-crypto and shared-service gates: every
  `ARM64_BOARD_CONFIG_SWEET || ARM64_BOARD_CONFIG_A5XELTE` gate gains
  `|| defined(ARM64_BOARD_CONFIG_J5XNLTE)` (48 gates across 37 files).

See [the J5XNLTE progress log](../J5XNLTE_PROGRESS.md) and
[migration map](../docs/j5xnlte-migration-map.md).

## Original SWEET build notes

Built on GitHub's macOS arm64 runners by `.github/workflows/xnu.yml`, using
[blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build) (MIT) at a pinned commit.

- `MACOS_VERSION=13.5` -> xnu-8796 (same generation as iOS 16.6/16.7, the last iOS for iPhone 8).
- `patches/*.patch` are copied into the build script's patch dir and applied to the xnu tree with `git apply --unidiff-zero` (zero-context hunks need it; the workflow seds the build script's flags so patches are no longer silently skipped).
- Output: `kernel.<config>.vmapple` (later: our own SWEET machine config) as a workflow artifact.

Fetch the latest build: `gh run download -R dipa-studio/ios-sweet -n xnu-kernel -D xnu/out`
