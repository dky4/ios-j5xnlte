# Working on the J5XNLTE fork

- For Samsung SM-J510FN (j5xnlte) work, read `J5XNLTE_PROGRESS.md` and
  `docs/j5xnlte-migration-map.md` first. Record new device evidence in the
  former and commit completed changes. Keep private VM addresses, passwords,
  device dumps and downloaded Apple binaries out of git.
- Device: Qualcomm MSM8916 (Snapdragon 410), 4x Cortex-A53 @ 1.2 GHz, GIC-400
  (GICv2: GICD `0x0b000000`, GICC `0x0b002000`), PM8916 via SPMI, 2 GiB RAM,
  720x1280 AMOLED (MDP5+DSI), Adreno 306, eMMC on SDHCI (`sdhc_1`
  `@0x07824900`). The A53 is the same core as the A5XELTE work: 4 KiB pages
  and Cortex-A53 codegen findings carry over; there is no 16 KiB granule.
- Boot method is lk2nd, installed once with `heimdall flash --BOOT lk2nd.img`,
  which provides Android fastboot. Every XNU trial uses `fastboot boot`.
  Never write the BOOT partition and never use the Samsung bootchain, TWRP or
  `shim/a5xelte/build-xnu-boot.sh`: this phone has no TWRP, and lk2nd's
  fastboot is the only device access. Keep lk2nd itself intact: `fastboot
  flash boot boot.img` would write over the 512 KiB-offset boot slot.
- XNU uses `MACHINE_CONFIG=J5XNLTE`. Reuse the proven A5XELTE compile-time
  facilities (4 KiB entry, Cortex-A53 codegen, GICv2 platform) instead of
  donor hardware code: SWEET is GICv3/UFS (SM7150) and a5xelte is Exynos
  DECON/DesignWare eMMC (Exynos7580) — neither matches this phone. See
  `docs/j5xnlte-migration-map.md` for what reuses and what is new work.
- Keep build success, device markers, physical screen reports and iOS runtime
  behavior as separate claims. `NOTES.md` and `A5XELTE_PROGRESS.md` are donor
  histories; do not edit them to imply J5XNLTE behavior.

## Original SWEET contributor instructions (donor reference)

- Read `NOTES.md` before starting; it contains the project goal, hardware facts,
  current blocker, local artifact locations and the next concrete action.
- The user switches agents frequently. Update the active handoff in `NOTES.md`
  after each milestone and before stopping. Record commits, CI run IDs, exact
  build/boot results and unfinished work. Distinguish observations from guesses.
- Commit completed changes. Use `gh` for GitHub Actions kernel builds and logs.
  Local single-file cross-compiles are useful before spending a CI run.
- Device bring-up uses temporary `fastboot boot` images. Preserve the Android
  installation and partition backups. The user approved (2026-10-15) using
  internal UFS for iOS and said Android may be deleted: the iOS root goes
  into the existing `userdata` partition (no GPT changes). Keep bootloaders,
  recovery, modem, persist and the backups intact, and confirm each
  destructive step with the user before running it.
- Keep device dumps, serial numbers and downloaded Apple artifacts out of git.
