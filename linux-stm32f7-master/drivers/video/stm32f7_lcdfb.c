#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <asm/setup.h>
#include <asm/system.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <asm/pgtable.h>
#include <asm/cacheflush.h>
#include <linux/clk.h>

#define DRIVER_NAME "stm32f7-ltdc"

#define LCD_WIDTH	480
#define LCD_HEIGHT	272
#define LCD_BPP		16
#define LCD_LINE_LEN	(LCD_WIDTH * 2)			/* 960 bytes */
#define LCD_FRAME_SIZE	(LCD_LINE_LEN * LCD_HEIGHT)	/* 261,120 bytes */

#define CONSOLE_MARGIN_X	8	/* 8-pixel margin on left and right (1 col @ 8px) */
#define CONSOLE_MARGIN_Y	8	/* 8-pixel margin on top and bottom (half row @ 16px) */
#define CONSOLE_WIDTH		(LCD_WIDTH - 2 * CONSOLE_MARGIN_X)	/* 464 pixels (58 cols) */
#define CONSOLE_HEIGHT		(LCD_HEIGHT - 2 * CONSOLE_MARGIN_Y)	/* 256 pixels (16 rows) */

#define LTDC_SSCR	0x08
#define LTDC_BPCR	0x0c
#define LTDC_AWCR	0x10
#define LTDC_TWCR	0x14
#define LTDC_GCR	0x18
#define LTDC_SRCR	0x24 
#define LTDC_BCCR	0x2c 
#define LTDC_IER	0x34

#define LTDC_L1CR	0x84
#define LTDC_L1WHPCR	0x88
#define LTDC_L1WVPCR	0x8c
#define LTDC_L1CKCR	0x90
#define LTDC_L1PFCR	0x94
#define LTDC_L1CACR	0x98
#define LTDC_L1DCCR	0x9c
#define LTDC_L1BFCR	0xa0
#define LTDC_L1CFBAR	0xac
#define LTDC_L1CFBLR	0xb0
#define LTDC_L1CFBLNR	0xb4

static struct fb_fix_screeninfo fb_fix __initdata = {
	.id		= DRIVER_NAME,
	.smem_len	= LCD_FRAME_SIZE,
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_TRUECOLOR,
	.line_length	= LCD_LINE_LEN,
	.accel		= FB_ACCEL_NONE,
};

static struct fb_var_screeninfo fb_var __initdata = {
	.xres		= CONSOLE_WIDTH,
	.yres		= CONSOLE_HEIGHT,
	.xres_virtual	= CONSOLE_WIDTH,
	.yres_virtual	= CONSOLE_HEIGHT,
	.bits_per_pixel	= LCD_BPP,
	.red		= {11, 5, 0},
	.green		= {5, 6, 0},
	.blue		= {0, 5, 0},
	.activate	= FB_ACTIVATE_NOW,
	.height		= CONSOLE_HEIGHT,
	.width		= CONSOLE_WIDTH,
	.vmode		= FB_VMODE_NONINTERLACED,
};

static u32 *fb_base;

static int fb_setcolreg(u32 regno, u32 red, u32 green,
			u32 blue, u32 transp, struct fb_info *info)
{
	if (regno >= 16)
		return -EINVAL;

	/* 16 bpp RGB565 truecolor pseudo-palette */
	((u32 *)info->pseudo_palette)[regno] =
		((red >> 11) << 11) |
		((green >> 10) << 5) |
		(blue >> 11);
	return 0;
}

static int fb_sync(struct fb_info *info)
{
	/* Clean CPU L1 D-cache lines to SDRAM for LTDC DMA covering full physical frame */
	dmac_clean_range((const void *)fb_fix.smem_start,
			 (const void *)(fb_fix.smem_start + LCD_FRAME_SIZE));
	return 0;
}

static void stm32f7_fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	cfb_fillrect(info, rect);
	if (info->screen_base && rect->height) {
		const void *start = info->screen_base + rect->dy * info->fix.line_length;
		const void *end = start + rect->height * info->fix.line_length;
		dmac_clean_range(start, end);
	}
}

static void stm32f7_fb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	cfb_copyarea(info, area);
	if (info->screen_base && area->height) {
		const void *start = info->screen_base + area->dy * info->fix.line_length;
		const void *end = start + area->height * info->fix.line_length;
		dmac_clean_range(start, end);
	}
}

static void stm32f7_fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	cfb_imageblit(info, image);
	if (info->screen_base && image->height) {
		const void *start = info->screen_base + image->dy * info->fix.line_length;
		const void *end = start + image->height * info->fix.line_length;
		dmac_clean_range(start, end);
	}
}

static struct fb_ops fb_ops = {
	.owner		= THIS_MODULE,
	.fb_setcolreg	= fb_setcolreg,
	.fb_fillrect	= stm32f7_fb_fillrect,
	.fb_copyarea	= stm32f7_fb_copyarea,
	.fb_imageblit	= stm32f7_fb_imageblit,
	.fb_sync	= fb_sync,
};

static void stm_clock_init(void)
{
	clk_enable(clk_get_sys(0, "sai_r_clk"));
	clk_enable(clk_get_sys("stm32f4-ltdc.0", 0));
	clk_enable(clk_get_sys(0, "gpioe"));
	clk_enable(clk_get_sys(0, "gpiog"));
	clk_enable(clk_get_sys(0, "gpioi"));
	clk_enable(clk_get_sys(0, "gpioj"));
	clk_enable(clk_get_sys(0, "gpiok"));
}

static void ltdc_init(u32 base)
{
	stm_clock_init();

	/* Assert display enable LCD_DISP pin (PI12) via GPIOI_BSRR */
	writel(BIT(12), 0x40022018);
	/* Assert backlight enable LCD_BL_CTRL pin (PK3) via GPIOK_BSRR */
	writel(BIT(3), 0x40022818);

	/* Timing registers for Rocktech RK043FN48H (480x272) */
	writel(0x280009, base + LTDC_SSCR);	/* HSW=41, VSH=10 */
	writel(0x35000b, base + LTDC_BPCR);	/* AHBP=54, AVBP=12 */
	writel(0x215011b, base + LTDC_AWCR);	/* AAW=534, AAH=284 */
	writel(0x235011d, base + LTDC_TWCR);	/* TOTALW=566, TOTALH=286 */
	writel(0, base + LTDC_BCCR);		/* Background color: black */
	writel(0x6, base + LTDC_IER);		/* Enable RRIE and TERRIE interrupts */
	writel(0x2221, base + LTDC_GCR);	/* LTDCEN, PCPOL, HSPOL, VSPOL */
}

static void ltdc_layer_init(u32 base, u32 addr)
{
	/* Layer 1 Window horizontal: Start=54 (0x36), Stop=533 (0x215) -> 480 wide */
	writel(0x2150036, base + LTDC_L1WHPCR);
	/* Layer 1 Window vertical: Start=12 (0x0C), Stop=283 (0x11B) -> 272 high */
	writel(0x11b000c, base + LTDC_L1WVPCR);

	/* Pixel format: 2 = RGB565 (16 bpp) */
	writel(2, base + LTDC_L1PFCR);

	/* Constant alpha: 0xFF (fully opaque) */
	writel(0xff, base + LTDC_L1CACR);

	/* Default color: black */
	writel(0, base + LTDC_L1DCCR);

	/* Blending factors: BF1 = Const Alpha (6), BF2 = 1 - Const Alpha (7) */
	writel(0x607, base + LTDC_L1BFCR);

	/* Framebuffer physical address */
	writel(addr, base + LTDC_L1CFBAR);

	/*
	 * Line length and pitch:
	 * Pitch = 960 bytes (0x3C0), Line length = 960 + 3 = 963 bytes (0x3C3)
	 */
	writel(0x03C003C3, base + LTDC_L1CFBLR);

	/* Total lines = 272 (0x110) */
	writel(LCD_HEIGHT, base + LTDC_L1CFBLNR);

	/* Enable Layer 1 */
	writel(1, base + LTDC_L1CR);

	/* Disable Layer 2 (offset + 0x80) */
	writel(0, base + LTDC_L1CR + 0x80);
}

static int __init fb_probe(struct platform_device *dev)
{
	struct fb_info *info;
	int ret = -ENOMEM;
	u32 base = 0x40016800;

	/* Allocate contiguous memory for 480x272x2 = 261,120 bytes + 256 for alignment */
	if (!(fb_base = kmalloc(LCD_FRAME_SIZE + 256, GFP_KERNEL)))
		goto err_exit;

	fb_fix.smem_start = (u32) fb_base + 256 - ((u32)fb_base % 256);

	/* Initialize to black */
	memset((void *)fb_fix.smem_start, 0, LCD_FRAME_SIZE);

	/* Flush initial buffer to SDRAM */
	dmac_clean_range((const void *)fb_fix.smem_start,
			 (const void *)(fb_fix.smem_start + LCD_FRAME_SIZE));

	ltdc_init(base);
	ltdc_layer_init(base, fb_fix.smem_start);
	writel(1, base + LTDC_SRCR); /* force shadow register reload */

	if (!(info = framebuffer_alloc(sizeof(u32) * 16, &dev->dev)))
		goto err_free_mem;

	info->var = fb_var;
	info->fix = fb_fix;
	info->fbops = &fb_ops;
	info->flags = FBINFO_DEFAULT;
	info->pseudo_palette = info->par;
	info->screen_base = (char *) (fb_fix.smem_start +
				      (CONSOLE_MARGIN_Y * LCD_LINE_LEN) +
				      (CONSOLE_MARGIN_X * 2));

	if (fb_alloc_cmap(&info->cmap, 256, 0) < 0)
		goto err_free_fb;

	if (register_framebuffer(info) < 0) {
		ret = -EINVAL;
		goto err_free_cmap;
	}

	pr_info("%s: fb%d registered @ 0x%p (viewport %ux%u, physical %ux%u, 16 bpp)\n",
		DRIVER_NAME, info->node, info->screen_base,
		CONSOLE_WIDTH, CONSOLE_HEIGHT, LCD_WIDTH, LCD_HEIGHT);

	return 0;

err_free_cmap:
	fb_dealloc_cmap(&info->cmap);
err_free_fb:
	framebuffer_release(info);
err_free_mem:
	kfree(fb_base);
err_exit:
	return ret;
}

static struct platform_driver fb_pdrv = {
	.probe	= fb_probe,
	.driver	= {
		.name	= DRIVER_NAME,
	},
};

int __init init_module0(void)
{
	if (platform_driver_register(&fb_pdrv)) {
		pr_info("failed to register %s driver\n", DRIVER_NAME);
		return -ENODEV;
	}
	
	return 0;
}

void __exit cleanup_module0(void)
{
	platform_driver_unregister(&fb_pdrv);
	kfree(fb_base);
	pr_info("clean");
}

module_init(init_module0);
module_exit(cleanup_module0);

MODULE_LICENSE("GPL v2");
