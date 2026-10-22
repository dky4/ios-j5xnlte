# J5XNLTE migration from the SWEET and A5XELTE donors

The donor histories describe a demonstrated Redmi Note 10 Pro (`sweet`, SM7150)
iOS 16 port and the Samsung Galaxy A5 2016 (`a5xelte`, Exynos7580) port built
on top of it. Both are source and sequencing references only. This document is
the Samsung Galaxy J5 2016 SM-J510FN (`j5xnlte`, Qualcomm MSM8916) decision
record. `J5XNLTE_PROGRESS.md` records build results, device markers, screen
reports and user-visible behavior separately.

The J5 is a Qualcomm MSM8916: 4x Cortex-A53 @ 1.2 GHz, GIC-400 (GICv2), PM8916
via SPMI, 2 GiB RAM, 720x1280 AMOLED, Adreno 306, eMMC on SDHCI. Its Cortex-A53
is the same core as the A5XELTE work (no 16 KiB granule, no ARMv8.1 LSE), so
the A5XELTE compile-time findings transfer directly. Its SoC family is the same
as the SWEET donor's Qualcomm platform, but one generation earlier and a
different feature set: SWEET code that touches GICv3, UFS, DWC3, RPMh or GENI
does not apply.

| Component | Donor status | J5XNLTE decision |
| --- | --- | --- |
| Boot handoff and loader | SWEET: Xiaomi ABL `fastboot boot` with `shim/`. A5XELTE: Samsung BOOT p10 with stock DTBH and `shim/a5xelte/`. | lk2nd is installed once (`heimdall flash --BOOT lk2nd.img`) and provides Android fastboot. Every trial is `fastboot boot` of `shim/j5xnlte/` output; lk2nd passes its own DTB in x0 and the boot image's ramdisk through `/chosen linux,initrd-start/end`. Never write the BOOT partition. |
| XNU page size and board target | A5XELTE patches `0082`/`0084` (4 KiB entry) and `0085` (`MACHINE_CONFIG=A5XELTE`) are device proven to reach XNU's `start_first_cpu`. | New `MACHINE_CONFIG=J5XNLTE` patches mirroring those, plus a J5 board target. The temporary donor `SWEET` compile definition is not used on the J5. |
| Cortex-A53 codegen | A5XELTE patches `0094` (Cortex-A53 code generation), `0095` (PAN off), `0096` (commpage LL/SC trylocks) and `tools/a5xelte-build-firehose.sh` (A53 `libfirehose_kernel` rebuild) are device proven. | Reuse as-is; the J5's A53 has the same restrictions. The same LSE-atomics risk applies to any separately built archive. |
| Interrupts and timer | A5XELTE patch `0086` (GICv2 platform), `0114` (SGI sender-bit fix) and `0099` (architected timer without MMIO registers) are device proven on GICv2. | Reuse, with msm8916 addresses (GICD `0x0b000000`, GICC `0x0b002000`) and virtual-timer-intid `20` (downstream/mainline msm8916 DT: PPI 2/3/4/1; 20 is expected from the DT, not yet measured on the phone — unlike sm7150's 19). The timer counter runs at 19.2 MHz per the DT. |
| Kernel VA and zone sizes | A5XELTE patches `0093` (39-bit kernel VA) and `0097` (24 GiB zone VA claim) are device proven on the 4 KiB non-large build. | Reuse for the J5XNLTE 4 KiB build; the same compact VA layout applies. |
| Portable crypto | A5XELTE patch `0098` enables the portable SHA-1/SHA-2/AES/MD5 sources without a corecrypto kext. | Reuse. The J5 has no corecrypto kext either; `g_crypto_funcs` is NULL here too. |
| Built-in pthread provider | A5XELTE patch `0102` links the paired Apple libpthread-514.100.2 provider and its registration entry. | Reuse. The `sweet_*` filenames describe donor provenance, not a Qualcomm hardware interface. |
| HFS, RAM disk root and unsealed root | A5XELTE patches `0103` (RAM-disk root guards), `0105` (unsealed-root continuation) and `0106` (AMFI/XML-entitlements bridge) are device proven; `0026`/`0021`/`0022` are the donor HFS/ramdisk base. | Reuse. `rd=md0` from the payload's RAMDisk memory-map remains the first root. |
| 4 KiB dyld shared cache | A5XELTE patch `0107` records a 16 KiB CodeDirectory on a 4 KiB page build and relaxes shared-region enforcement. | Reuse; the iPhone 8 Plus userland staged for A5XELTE has the same 16 KiB cache pages. |
| Userland staging | `tools/a5xelte-stage-ios-root.sh` stages launchd, SpringBoard, frameworks and the dyld cache without stock apps. | Reuse. The J5's physical Home button fits the Touch-ID-class iPhone 8 Plus userland; the ADT `buttons` node advertises `home-button-type` 0. |
| Storage and root | A5XELTE: Exynos DesignWare eMMC (patches `0104`/`0108`). SWEET: Qualcomm UFS. | Neither applies. New J5 work: SDHCI (`sdhc_1`, `qcom,sdhci-msm-v4`) for the eMMC persistent root, after the RAM-disk milestone. Do not port the DesignWare FIFO access code. |
| Early display and console | A5XELTE: Exynos DECON/DSIM handoff (`shim/a5xelte/display_console.c`, patch `0089`). SWEET: Qualcomm IOMFB RAM framebuffer. | Neither applies. New J5 work: MDP5+DSI for the 720x1280 AMOLED. Whether lk2nd leaves a usable framebuffer is an open device-test question; until then the shim has no console at all and the first observable is the IMEM reboot marker and reset delay. |
| Input and keys | A5XELTE: Samsung GPA2-5 key. SWEET: GPIO keys. | New J5 work: PM8916 buttons via SPMI (Home, Power, volume up/down). Needed before iOS can be operated, not before XNU boots. |
| GPU and Metal | A5XELTE: Mali T720 via Panfrost comparison work. SWEET: Adreno 530 assumptions. | Later milestone: Adreno 306 (a2xx-class Freedreno family), separate from display bring-up. Do not import donor GPU assumptions. |
| Documentation and progress files | `NOTES.md` (SWEET) and `A5XELTE_PROGRESS.md` (A5XELTE) record donor evidence. | `J5XNLTE_PROGRESS.md` and `docs/j5xnlte-*.md` are the J5 records. Do not edit donor files to imply J5XNLTE behavior. |

Donor code that is kept (shared Apple sources, portable facilities) stays as a
build dependency; donor code that names donor hardware (UFS, DWC3, RPMh, GENI,
DECON, DesignWare eMMC, Mali, S2MPU03, HSI2C, GPA2) is not ported to the J5.
Retire a donor path only after the J5XNLTE replacement is built and reaches the
same device checkpoint; deleting donor reference code before that loses
working comparison material.
