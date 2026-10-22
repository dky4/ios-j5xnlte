/*
 * Just enough flattened-device-tree parsing to find what ABL put in
 * /chosen (initrd location). Big-endian, 4-byte aligned tokens.
 */
#include "fdt.h"

#define FDT_MAGIC	0xd00dfeed
#define FDT_BEGIN_NODE	1
#define FDT_END_NODE	2
#define FDT_PROP	3
#define FDT_NOP		4
#define FDT_END		9

static uint32_t be32(uintptr_t a)
{
	return __builtin_bswap32(*(volatile uint32_t *)a);
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static uintptr_t align4(uintptr_t a)
{
	return (a + 3) & ~3UL;
}

static uintptr_t skip_str(uintptr_t p)
{
	/* byte loads are fine on Device memory as long as they are aligned to 1 */
	while (*(volatile char *)p)
		p++;
	return p + 1;
}

int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out)
{
	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;
	uintptr_t p = fdt + be32(fdt + 8);		/* off_dt_struct */
	uintptr_t strings = fdt + be32(fdt + 12);	/* off_dt_strings */
	int depth = 0, in_chosen = 0;

	for (;;) {
		uint32_t tok = be32(p);
		p += 4;
		switch (tok) {
		case FDT_BEGIN_NODE: {
			const char *name = (const char *)p;
			depth++;
			in_chosen = depth == 2 && streq(name, "chosen");
			p = align4(skip_str(p));
			break;
		}
		case FDT_END_NODE:
			if (in_chosen)
				return 0;
			depth--;
			break;
		case FDT_PROP: {
			uint32_t len = be32(p), nameoff = be32(p + 4);
			uintptr_t val = p + 8;
			if (in_chosen && streq((const char *)(strings + nameoff), prop)) {
				if (len == 4)
					*out = be32(val);
				else if (len == 8)
					*out = (uint64_t)be32(val) << 32 | be32(val + 4);
				else
					return 0;
				return 1;
			}
			p = align4(val + len);
			break;
		}
		case FDT_NOP:
			break;
		default:	/* FDT_END or garbage */
			return 0;
		}
	}
}

/*
 * lk2nd's lk2nd.pass-simplefb publishes the framebuffer as
 * /chosen/framebuffer@<base> (compatible "simple-framebuffer") with
 * reg (2 cells), width, height, stride and format props. Return 1 and
 * fill the geometry if that child node exists.
 */
int fdt_chosen_fb(uintptr_t fdt, uint32_t *base, uint32_t *size,
		  uint32_t *width, uint32_t *height, uint32_t *stride,
		  uint32_t *fmt4)
{
	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;
	uintptr_t p = fdt + be32(fdt + 8);
	uintptr_t strings = fdt + be32(fdt + 12);
	int depth = 0, in_chosen = 0, in_fb = 0, found = 0;

	for (;;) {
		uint32_t tok = be32(p);
		p += 4;
		switch (tok) {
		case FDT_BEGIN_NODE: {
			const char *name = (const char *)p;
			depth++;
			if (depth == 2)
				in_chosen = streq(name, "chosen");
			in_fb = depth == 3 && in_chosen &&
				name[0] == 'f' && name[1] == 'r' &&
				name[2] == 'a' && name[3] == 'm' &&
				name[4] == 'e';
			p = align4(skip_str(p));
			break;
		}
		case FDT_END_NODE:
			if (in_fb)
				in_fb = 0;
			else if (in_chosen && depth == 2)
				return found;
			depth--;
			break;
		case FDT_PROP: {
			uint32_t len = be32(p), nameoff = be32(p + 4);
			uintptr_t val = p + 8;
			const char *pn = (const char *)(strings + nameoff);
			if (in_fb) {
				if (len == 8 && streq(pn, "reg")) {
					*base = be32(val);
					*size = be32(val + 4);
					found = 1;
				} else if (len == 4 && streq(pn, "width")) {
					*width = be32(val);
					found = 1;
				} else if (len == 4 && streq(pn, "height")) {
					*height = be32(val);
					found = 1;
				} else if (len == 4 && streq(pn, "stride")) {
					*stride = be32(val);
					found = 1;
				} else if (len >= 4 && streq(pn, "format")) {
					*fmt4 = (uint32_t)((volatile unsigned char *)val)[0] << 24 |
						 (uint32_t)((volatile unsigned char *)val)[1] << 16 |
						 (uint32_t)((volatile unsigned char *)val)[2] << 8 |
						 (uint32_t)((volatile unsigned char *)val)[3];
				}
			}
			p = align4(val + len);
			break;
		}
		case FDT_NOP:
			break;
		default:	/* FDT_END or garbage */
			return found;
		}
	}
}
