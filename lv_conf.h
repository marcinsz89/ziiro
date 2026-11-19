#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_DRAW_BUF_STRIDE_ALIGN 4
#define LV_USE_OS LV_OS_NONE
/* A downloaded 512 × 512 PNG can decode to 1 MiB of RGBA pixels.
 * Leave room for the retained image and PNG decoding buffers. */
#define LV_MEM_SIZE (8U * 1024U * 1024U)
/* Allow the artwork converter to add its temporary PNG decode pool.
 * LVGL still allocates only LV_MEM_SIZE at startup. */
#define LV_MEM_POOL_EXPAND_SIZE (88U * 1024U * 1024U)
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_USE_FS_STDIO 1
#define LV_FS_STDIO_LETTER 'A'
#define LV_USE_LODEPNG 1
#define LV_CACHE_DEF_SIZE (1024U * 1024U)
#define LV_IMAGE_HEADER_CACHE_DEF_CNT 16

#ifdef ZIIRO_DEVICE
#define LV_USE_LINUX_FBDEV 1
#endif

#endif
