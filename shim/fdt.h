#pragma once
#include <stdint.h>

/* Read a u32/u64 (cell-size agnostic) property of /chosen. Returns 0 if absent. */
int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out);
int fdt_chosen_fb(uintptr_t fdt, uint32_t *base, uint32_t *size,
		  uint32_t *width, uint32_t *height, uint32_t *stride,
		  uint32_t *fmt4);
