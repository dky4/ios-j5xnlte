/*
 * Milestone 1: act as iBoot for xnu under lk2nd.
 *
 * lk2nd enters at EL1, MMU and D-cache off, x0 = its own DTB (arm64 Linux
 * boot protocol). The SWEETXNU payload arrives as the boot image's ramdisk
 * (build.sh) and lk2nd publishes it through /chosen linux,initrd-start/end.
 * mkpayload.py already resolved the kernel's chained fixups and built the
 * ADT, so this only has to: find the payload, copy the segments to
 * phys_base + (vmaddr - virt_base), place the ADT and boot_args on 4 KiB
 * pages behind the kernel, and jump to the entry point with the MMU and
 * caches off, x0 = boot_args (physical).
 *
 * There is no console, and post-reset readback is dead: IMEM 0x0860065c
 * reads 0 after any warm cycle and scratch DRAM 0x92000000 reads 0xffffffff
 * after lk2nd's own reboot — the boot ROM/SBL wipes both on every reset
 * (device-proven 2026-10-23). 0x92000000 (32 MB into lk2nd's 256 MB scratch
 * region, SCRATCH_ADDR 0x90000000, target/msm8916/rules.mk) is still the
 * in-boot mailbox: the full loader stashes the splash-fb facts there in the
 * SAME boot for start.s' live screen draw, and the probes keep their
 * in-session words for lk2nd's readl while fastboot is up. Observables
 * that need no readback: the powerdown delay (stage * 5 s, probe-dependent)
 * and what the AMOLED shows. See docs/j5xnlte-bringup.md.
 */
#include <stdint.h>
#include "../fdt.h"

#define DRAM_MARK		0x92000000UL	/* in-boot mailbox: lk2nd-mapped, wiped by resets */
#define RAM_BASE		0x80000000UL	/* DRAM start */
#define RAM_END			0x100000000UL	/* 2 GiB */
#define PAGE_SIZE		4096UL
#define BOOT_LINE_LENGTH	608
#define PSCI_SYSTEM_RESET	0x84000009UL

struct payload_seg {
	uint64_t vmaddr, vmsize, file_off, file_size;
};

struct payload {
	char magic[8];
	uint32_t version, nsegs;
	uint64_t virt_base, entry_va, adt_off, adt_size, kernel_end;
	uint64_t phys_base, mem_size;
	char cmdline[BOOT_LINE_LENGTH];
	struct payload_seg segs[];
};

/* pexpert/pexpert/arm64/boot.h, Revision/Version 2 */
struct boot_video {
	uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};

struct boot_args {
	uint16_t Revision, Version;
	uint64_t virtBase, physBase, memSize, topOfKernelData;
	struct boot_video Video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char CommandLine[BOOT_LINE_LENGTH];
	uint64_t bootFlags, memSizeActual;
};

_Static_assert(__builtin_offsetof(struct payload, segs) == 0x2a8, "payload header layout");
_Static_assert(__builtin_offsetof(struct boot_args, Video) == 40, "boot_args layout");
_Static_assert(__builtin_offsetof(struct boot_args, CommandLine) == 108, "boot_args layout");
_Static_assert(sizeof(struct boot_args) == 736, "boot_args layout");

#if !defined(J5XNLTE_DIAG) && !defined(J5XNLTE_DIAG_FB) && !defined(J5XNLTE_DIAG_SMC)
#define J5XNLTE_FULL_LOADER 1
#else
#undef J5XNLTE_FULL_LOADER
#endif

#ifdef J5XNLTE_FULL_LOADER
/* MMU off: everything is Device memory, so copy in aligned 8-byte words. */
static void copy64(uintptr_t dst, uintptr_t src, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	volatile const uint64_t *s = (volatile const uint64_t *)src;
	for (uint64_t i = 0; i < (n + 7) / 8; i++)
		d[i] = s[i];
}

static void zero64(uintptr_t dst, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	for (uint64_t i = 0; i < n / 8; i++)
		d[i] = 0;
}

static int magic_ok(const char *m)
{
	static const char want[] = "SWEETXNU";
	for (int i = 0; i < 8; i++)
		if (m[i] != want[i])
			return 0;
	return 1;
}
#endif /* J5XNLTE_FULL_LOADER */

#if !defined(J5XNLTE_DIAG)
/* CNTFRQ may be 0 on this firmware (the a5xelte probe measured 0), so the
 * fallback is a crude DRAM-fetch loop; the delay is approximate by design. */
static void delay(unsigned s)
{
	uint64_t f;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
	if (f) {
		uint64_t t, end;
		__asm__ volatile("mrs %0, cntpct_el0" : "=r"(t));
		end = t + f * s;
		do {
			__asm__ volatile("mrs %0, cntpct_el0" : "=r"(t));
		} while (t < end);
		return;
	}
	volatile unsigned n = s * 400u * 1000u * 1000u;
	while (n--)
		__asm__ volatile("nop");
}
#endif /* !J5XNLTE_DIAG */

/* Reset: drop PS_HOLD (0x4ab000). The PM8916 default PSHOLD action powers
 * the phone off; this is device-proven (DIAG=1 entry probe) and needs no
 * timers or PSCI. PSCI SYSTEM_RESET from the TZ-milestone EL1 state wedged
 * a dedicated probe (stage markers + delay elapsed, then no powerdown), so
 * it stays out of the reset path. lk2nd's own `fastboot reboot` also goes
 * through a hard reset that wipes scratch DRAM (device-proven 2026-10-23),
 * so nothing survives into the next boot. */
static void __attribute__((noreturn)) fail_reset(void)
{
	*(volatile uint32_t *)0x004ab000UL = 0;
	__asm__ volatile("dsb sy" ::: "memory");
	for (;;)
		__asm__ volatile("wfe");
}

#ifdef J5XNLTE_FULL_LOADER
static void __attribute__((noreturn)) fail(int stage)
{
	/* The stage is observable as the powerdown delay (stage * 5 s);
	 * the word below is in-session only (wiped by the reset). */
	*(volatile uint32_t *)DRAM_MARK = 0x4a355800u | (uint32_t)stage;
	__asm__ volatile("dsb sy" ::: "memory");
	delay(stage * 5);
	fail_reset();
}
#endif /* J5XNLTE_FULL_LOADER */

#ifdef J5XNLTE_DIAG
/*
 * Diagnostic build (DIAG=1): entry probe only. Writes the stage-1 marker
 * to the scratch-DRAM marker word (0x92000000), then drops PS_HOLD
 * (0x4ab000): the PM8916
 * default PSHOLD action powers the phone off. Observable: the phone turns
 * OFF ~1 s after `fastboot boot` (the shim runs; PSCI, timers and DRAM
 * are all fine) or wedges (the shim never started: entry or the TZ
 * milestone call failed). No timers, no PSCI, no SPMI needed.
 */
void __attribute__((noreturn)) shim_main(uintptr_t dtb)
{
	(void)dtb;
	*(volatile uint32_t *)DRAM_MARK = 0x4a355800u | 1u;
	__asm__ volatile("dsb sy" ::: "memory");
	/* MPM2_MPM_PS_HOLD (target/msm8916 iomap): low = PMIC powerdown. */
	fail_reset();
}
#elif defined(J5XNLTE_DIAG_FB)
/*
 * Display probe (DIAG=2): lk2nd.pass-simplefb publishes the framebuffer as
 * /chosen/framebuffer@<base> (compatible "simple-framebuffer", boot_type
 * ARM64 only — BOOT_DOWNSTREAM/BOOT_LK2ND skip). Post-reset readback is
 * dead (scratch DRAM is wiped by the boot ROM/SBL on every reset), so the
 * observable is live: when the node exists in the 32bpp x8r8g8b8 format,
 * draw a white 64x32 band at the fb top-left — a band on the AMOLED says
 * the node parsed and the MDP keeps refreshing from DRAM (the assumption
 * trial A relies on). The probe holds it ~10 s, then drops PS_HOLD.
 * 0x92000000 keeps the in-session facts (00 base, +4 size, +8 width,
 * +12 height, +16 stride, +20 format) for lk2nd's readl while fastboot
 * is up; 0x4a355801 there = the node is missing.
 */
void __attribute__((noreturn)) shim_main(uintptr_t dtb)
{
	uint32_t base = 0, size = 0, width = 0, height = 0, stride = 0, fmt4 = 0;

	/* Stage 0: the probe itself is running. */
	*(volatile uint32_t *)DRAM_MARK = 0x4a355800u;
	__asm__ volatile("dsb sy" ::: "memory");

	if (fdt_chosen_fb(dtb, &base, &size, &width, &height, &stride, &fmt4)) {
		*(volatile uint32_t *)DRAM_MARK = base;
		*(volatile uint32_t *)(DRAM_MARK + 4) = size;
		*(volatile uint32_t *)(DRAM_MARK + 8) = width;
		*(volatile uint32_t *)(DRAM_MARK + 12) = height;
		*(volatile uint32_t *)(DRAM_MARK + 16) = stride;
		*(volatile uint32_t *)(DRAM_MARK + 20) = fmt4;
		if (fmt4 == 0x78387238u /* "x8r8" */) {
			volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)base;
			for (uint32_t row = 0; row < 32; ++row) {
				volatile uint32_t *px =
				    fb + (uint64_t)row * (stride / 4);
				for (uint32_t col = 0; col < 64; ++col)
					px[col] = 0xffffffffu;
			}
			__asm__ volatile("dsb sy" ::: "memory");
		}
	} else {
		*(volatile uint32_t *)DRAM_MARK = 0x4a355801u;
	}
	__asm__ volatile("dsb sy" ::: "memory");

	/* Hold the band on screen, then power down. */
	delay(10);
	/* MPM2_MPM_PS_HOLD (target/msm8916 iomap): low = PMIC powerdown. */
	fail_reset();
}
#elif defined(J5XNLTE_DIAG_SMC)
/*
 * PSCI probe (DIAG=3): CPU_ON via smc #0 alone, without the hvc fallback
 * that wedged the earlier SYSTEM_RESET pair in the TZ-milestone EL1 state.
 * Target core 1, entry = a wfe loop, context 0. Post-reset readback is
 * dead (scratch DRAM is wiped by the boot ROM/SBL on every reset), so the
 * return value is observable as the powerdown delay: 0 (PSCI success)
 * powers off ~2 s after the boot, any PSCI error code ~10 s. 0x92000000
 * keeps the in-session words (0x4a355800 = the probe reached, written
 * before the smc; then the return value) for lk2nd's readl while
 * fastboot is up.
 */
static void psci_loop(void)
{
	for (;;)
		__asm__ volatile("wfe");
}

void __attribute__((noreturn)) shim_main(uintptr_t dtb)
{
	(void)dtb;
	volatile uint32_t *mark = (volatile uint32_t *)DRAM_MARK;
	*mark = 0x4a355800u;
	__asm__ volatile("dsb sy" ::: "memory");

	register long x0 __asm__("x0") = 0x84000003UL;	/* PSCI CPU_ON */
	register long x1 __asm__("x1") = 1UL;		/* target core 1 */
	register long x2 __asm__("x2") = (long)psci_loop;
	register long x3 __asm__("x3") = 0UL;		/* context */
	__asm__ volatile("smc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3) : "memory", "cc");
	*mark = (volatile uint32_t)(uint32_t)x0;
	__asm__ volatile("dsb sy" ::: "memory");

	/* Timed ladder: the delay IS the return value (readback is dead). */
	delay(x0 ? 10 : 2);
	/* MPM2_MPM_PS_HOLD (target/msm8916 iomap): low = PMIC powerdown. */
	fail_reset();
}
#else
typedef void (*xnu_entry_t)(uint64_t boot_args_pa);

/* Boot the SWEETXNU payload found in the lk2nd ramdisk; never returns.
 * Failure stages (IMEM 0x4a355800 | stage, reset after stage * 5 s):
 *   1: no initrd in /chosen, or initrd outside DRAM
 *   2: bad payload magic/version/header
 *   3: payload placement or segment out of bounds
 *   4: ramdisk overlaps the kernel window */
void __attribute__((noreturn)) shim_main(uintptr_t dtb)
{
	uint64_t rd_start, rd_end;

	/* Stage 0: the shim itself is running (an early wedge still has it). */
	*(volatile uint32_t *)DRAM_MARK = 0x4a355800u;
	__asm__ volatile("dsb sy" ::: "memory");

	if (!fdt_chosen_u64(dtb, "linux,initrd-start", &rd_start) ||
	    !fdt_chosen_u64(dtb, "linux,initrd-end", &rd_end))
		fail(1);
	if (rd_start < RAM_BASE || rd_end > RAM_END || rd_end <= rd_start)
		fail(1);

	const struct payload *p = (const struct payload *)(uintptr_t)rd_start;
	uint64_t length = rd_end - rd_start;
	if (length < sizeof(*p) || !magic_ok(p->magic) || p->version != 1 ||
	    !p->nsegs || p->nsegs > 32 ||
	    __builtin_offsetof(struct payload, segs) + p->nsegs * sizeof(struct payload_seg) > length ||
	    p->adt_off > length || p->adt_size > length - p->adt_off)
		fail(2);
	if (p->phys_base < RAM_BASE || p->phys_base >= RAM_END ||
	    !p->mem_size || p->mem_size > RAM_END - p->phys_base ||
	    p->entry_va < p->virt_base || p->kernel_end < p->virt_base)
		fail(3);

	uintptr_t memory_end = p->phys_base + p->mem_size;
	uintptr_t kernel_end = p->phys_base + (p->kernel_end - p->virt_base);
	if (kernel_end >= memory_end)
		fail(3);
	/* lk2nd keeps its own load addresses (kernel, ramdisk) below the
	 * kernel window; if the ramdisk ever overlaps it, refuse rather than
	 * corrupt the copy. */
	if (rd_end > p->phys_base && rd_start < memory_end)
		fail(4);

	for (uint32_t i = 0; i < p->nsegs; ++i) {
		const struct payload_seg *s = &p->segs[i];
		if (s->vmaddr < p->virt_base || s->file_size > s->vmsize ||
		    s->file_off > length || s->file_size > length - s->file_off ||
		    s->vmsize > p->mem_size || s->vmaddr - p->virt_base > p->mem_size - s->vmsize)
			fail(3);
		uintptr_t dst = p->phys_base + (s->vmaddr - p->virt_base);
		copy64(dst, rd_start + s->file_off, s->file_size);
		if (s->vmsize > s->file_size)
			zero64(dst + ((s->file_size + 7) & ~7UL), s->vmsize - ((s->file_size + 7) & ~7UL));
	}

	/* ADT, then boot_args, behind the kernel; 4 KiB pages like a5xelte. */
	uintptr_t adt_pa = kernel_end;
	uintptr_t ba_pa = (adt_pa + p->adt_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	if (ba_pa + PAGE_SIZE > memory_end || ba_pa < adt_pa)
		fail(3);
	copy64(adt_pa, rd_start + p->adt_off, p->adt_size);
	zero64(ba_pa, PAGE_SIZE);
	volatile struct boot_args *ba = (volatile struct boot_args *)ba_pa;
	ba->Revision = 2;
	ba->Version = 2;
	ba->virtBase = p->virt_base;
	ba->physBase = p->phys_base;
	ba->memSize = p->mem_size;
	ba->topOfKernelData = ba_pa + PAGE_SIZE;
	/* lk2nd.pass-simplefb publishes the continuous splash as
	 * /chosen/framebuffer; hand it to xnu (v_baseAddr stays physical —
	 * the splash framebuffer lives in DRAM below the kernel window and
	 * keeps displaying while the MDP keeps refreshing through the
	 * jump). Zero if lk2nd detected no splash. */
	{
		uint32_t fb_base, fb_size, fb_w, fb_h, fb_stride, fmt4 = 0;
		if (fdt_chosen_fb(dtb, &fb_base, &fb_size, &fb_w, &fb_h, &fb_stride, &fmt4)) {
			ba->Video.v_baseAddr = fb_base;
			ba->Video.v_display = 0;
			ba->Video.v_rowBytes = fb_stride;
			ba->Video.v_width = fb_w;
			ba->Video.v_height = fb_h;
			ba->Video.v_depth = fmt4 == 0x78387238u /* "x8r8" */ ? 32 :
					     fmt4 == 0x72356736u /* "r5g6" */ ? 16 : 24;
			/* In-boot mailbox for start.s' live screen draw (MMU off,
			 * plain physical stores): the splash fb keeps refreshing
			 * from DRAM through the jump, so XNU draws on the AMOLED
			 * instead of reading back a marker after a reset (IMEM and
			 * scratch DRAM are both wiped by the boot ROM/SBL on every
			 * reset - device-proven 2026-10-23; only the readback is
			 * dead: this mailbox is written and consumed in ONE boot). */
			*(volatile uint32_t *)DRAM_MARK = 0x584e5531u;
			*(volatile uint32_t *)(DRAM_MARK + 4) = fb_base;
			*(volatile uint32_t *)(DRAM_MARK + 8) = fb_stride;
			*(volatile uint32_t *)(DRAM_MARK + 12) = ba->Video.v_depth;
		}
	}
	ba->deviceTreeP = p->virt_base + (adt_pa - p->phys_base);	/* VA: xnu reads it with the MMU on */
	ba->deviceTreeLength = (uint32_t)p->adt_size;
	for (unsigned i = 0; i < BOOT_LINE_LENGTH; ++i)
		ba->CommandLine[i] = p->cmdline[i];
	ba->memSizeActual = p->mem_size;

	uintptr_t entry_pa = p->phys_base + (p->entry_va - p->virt_base);
	if (entry_pa >= kernel_end)
		fail(3);

	/* Everything was written with the MMU and caches off; drop any stale I-cache lines too. */
	__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
	((xnu_entry_t)entry_pa)(ba_pa);
	for (;;)
		__asm__ volatile("wfe");
}
#endif
