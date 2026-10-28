// SPDX-License-Identifier: GPL-2.0
/*
 * Blanks the bootloader's "not official software" warning in the boot splash
 * of the Galaxy S23 Ultra (SM-S918x) as early as possible.
 *
 * The bootloader leaves its splash (1440x3088 BGRA8888, stride 5760) at the
 * start of splash_region and the display keeps scanning it until msm_drm's
 * first commit. Rows 150..720 hold only the warning; they are filled with
 * black, but only if they still contain exactly the stock warning image.
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/printk.h>
#include <linux/vmalloc.h>

#define STRIDE		5760
#define FIRST_ROW	150
#define END_ROW		721
/* Rows 150..720 of the stock splash, one u32 per 64 bytes (uncached reads are slow). */
#define WARNING_FNV1A	0x3eec43a068563574ULL

static u64 __init fnv1a_sampled(const u8 *p, size_t n)
{
	u64 h = 0xcbf29ce484222325ULL;
	size_t i;

	for (i = 0; i < n; i += 64)
		h = (h ^ *(const u32 *)(p + i)) * 0x100000001b3ULL;
	return h;
}

static const char * __init blank_warning(void)
{
	struct device_node *node = of_find_node_by_name(NULL, "splash_region");
	size_t len = END_ROW * STRIDE, npages = PAGE_ALIGN(len) >> PAGE_SHIFT, i;
	struct resource res;
	struct page **pages;
	u32 *px;
	u8 *fb;
	u64 hash;

	if (!node || of_address_to_resource(node, 0, &res) || resource_size(&res) < len)
		return "no splash_region";
	pages = vmalloc(npages * sizeof(*pages));
	if (!pages)
		return "out of memory";
	for (i = 0; i < npages; i++)
		pages[i] = pfn_to_page(PHYS_PFN(res.start) + i);
	/* Uncached so the display engine sees the writes without cache maintenance. */
	fb = vmap(pages, npages, VM_MAP, pgprot_writecombine(PAGE_KERNEL));
	vfree(pages);
	if (!fb)
		return "vmap failed";

	hash = fnv1a_sampled(fb + FIRST_ROW * STRIDE, len - FIRST_ROW * STRIDE);
	if (hash == WARNING_FNV1A) {
		for (px = (u32 *)(fb + FIRST_ROW * STRIDE); px < (u32 *)(fb + len); px++)
			*px = 0xff000000;	/* opaque black in BGRA */
		wmb();
	}
	vunmap(fb);
	return hash == WARNING_FNV1A ? "warning blanked" : "warning rows not found, nothing written";
}

static int __init ancal_splash_init(void)
{
	pr_info("ancal_splash: %s\n", blank_warning());
	return 0;
}
early_initcall(ancal_splash_init);
