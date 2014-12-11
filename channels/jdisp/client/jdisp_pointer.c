/*
 * =====================================================================================
 *
 *       Filename:  jdisp_pointer.c
 *       Description:  jdisp pointer implementation
 *
 *        Version:  2.0
 *        Created:  12/11/2014
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Zhang Ximin <zhangximin@gmail.com>
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lzo/lzo1x.h>
#include <lzo/lzoconf.h>
#include <mxc_jpeg.h>

#include "jdisp_pointer.h"

#define JDISP_MOUSE_FLAG_START	0x3412
#define JDISP_MOUSE_FLAG_END		0xFF
#define JDISP_MAX_MONO_WIDTH		32
#define JDISP_MAX_MONO_HEIGHT		32
#define JDISP_MAX_COLOR_WIDTH		64
#define JDISP_MAX_COLOR_HEIGHT	64
#define JDISP_MOUSE_MONO_SIZE		(JDISP_MAX_MONO_WIDTH * JDISP_MAX_MONO_HEIGHT * 4)
#define JDISP_MOUSE_COLOR_SIZE	(JDISP_MAX_COLOR_WIDTH * JDISP_MAX_COLOR_HEIGHT * 4)
#define JDISP_MOUSE_DATA_SIZE		(JDISP_MOUSE_COLOR_SIZE + 2 + 6)

#define JDISP_MOUSE_STATUS_DISABLE		0x0
#define JDISP_MOUSE_STATUS_ENABLE			0x1
#define JDISP_MOUSE_STATUS_POSITION		0x2
#define JDISP_MOUSE_STATUS_MONO			0x3
#define JDISP_MOUSE_STATUS_ALPHA			0x6
#define JDISP_MOUSE_STATUS_COLOR			0x7

struct BIT_FIELD {
	int bit0:1;
	int bit1:1;
	int bit2:1;
	int bit3:1;
	int bit4:1;
	int bit5:1;
	int bit6:1;
	int bit7:1;
};

typedef struct _JDISP_MOUSE_HDR_ {
	short flags;
	short length;
	unsigned char type;
	unsigned char *data;
} jdisp_mouse_hdr;

static struct fb_cursor cursor;
static char *image_data = NULL;

/*
 * 	function: set_mono_cursor_pixel
 *
 * 	author: Li Yuan
 * 	email: kylel@wicresoft.com
 * 	updated date: 01/14/2014
 *
 * 	description: set pixel value of mono cursor
 *
 * 	arguments:
 * 		and:	and mask
 *		xor:	xor mask
 *		pixel:	one pixel of mono cursor
 * 	return:
 * 		VOID
 */
void set_mono_cursor_pixel(int and, int xor, int *pixel)
{
	if (xor == 0) {
		if (and == 0)
		  *pixel = 0xFF000000;
		else
		  *pixel = 0;
	} 
	else {
		if (and == 0)
		  *pixel = 0xFFFFFFFF;
		else
		  *pixel = 0xFF000000;
	} 
}

/*
 * 	function: convert_cursor_mono_to_color
 *
 * 	author: Li Yuan
 * 	email: kylel@wicresoft.com
 * 	updated date: 01/14/2014
 *
 * 	description: convert mono cursor to color cursor
 *
 * 	arguments:
 * 		dst:	color cursor data
 *		src:	mono cursor data
 *		size:	size of mono cursor data
 * 	return:
 * 		VOID
 */
int convert_cursor_mono_to_color(unsigned char* dst, unsigned char* src, int size)
{
	struct BIT_FIELD *and, *xor;
	unsigned char *p;
	int *q;
	int i;

	p = src;
	q = (int *)dst;

	for (i = 0; i < size / 2; i++) {
		and = (struct BIT_FIELD *)p++;
		xor = (struct BIT_FIELD *)p++;
		set_mono_cursor_pixel(and->bit7, xor->bit7, q++);
		set_mono_cursor_pixel(and->bit6, xor->bit6, q++);
		set_mono_cursor_pixel(and->bit5, xor->bit5, q++);
		set_mono_cursor_pixel(and->bit4, xor->bit4, q++);
		set_mono_cursor_pixel(and->bit3, xor->bit3, q++);
		set_mono_cursor_pixel(and->bit2, xor->bit2, q++);
		set_mono_cursor_pixel(and->bit1, xor->bit1, q++);
		set_mono_cursor_pixel(and->bit0, xor->bit0, q++);
	} 

	return 0;
}

/*
 * 	function: update_uncompressed_cursor_image
 *
 * 	author: Li Yuan
 * 	email: kylel@wicresoft.com
 * 	updated date: 01/14/2014
 *
 * 	description: update uncompressed cursor image
 *
 * 	arguments:
 * 		data:	cursor data
 *		size:	size of cursor data
 * 	return:
 * 		int:	0(succeed), -1(fail)
 */
int update_uncompressed_cursor_image(lzo_byte *data, int size)
{
	unsigned char *p, *q, *dst = NULL;
	unsigned char t;
	int i = size;

	if (!image_data) {
		return -1;
	} 

	g2d_clear_cursor(&cursor); /* clear cursor from framebuffer */

	/* convert to big endian */
	q = p = (unsigned char *)data;
	while(i) {
		t = *p;
		*p = *(p+1);
		*(p+1) = t;
		p += 2;
		i -= 2;
	}
	p = q;
	//printf("flags = 0x%04X, length = %d, type = %d", ((short*)p)[0], ((short*)p)[1], p[4]);
	//if (p[4] > 2)
		//printf(", column = %d, row = %d", p[5], p[6]);
	//printf("\n");

	if (p[4] == 0) {
		cursor.enable = 0; /* disable cursor */
	}
	else if (p[4] == 1) {
		cursor.enable = 1; /* enable cursor */
	}
	else if (p[4] == 2) {
		return -1; /* unused type */
	}
	else if (p[4] == 3) { /* mono cursor */
		cursor.image.width = JDISP_MAX_MONO_WIDTH;
		cursor.image.height = JDISP_MAX_MONO_HEIGHT;
		cursor.image.dx = p[5];
		cursor.image.dy = p[6];
		dst = (unsigned char *)malloc(JDISP_MOUSE_MONO_SIZE);
		convert_cursor_mono_to_color(dst, p + 7, 256);
		memcpy(image_data, dst, JDISP_MOUSE_MONO_SIZE);
		free(dst);
	}
	else if (p[4] == 6) { /* color cursor */
		cursor.image.width = JDISP_MAX_COLOR_WIDTH;
		cursor.image.height = JDISP_MAX_COLOR_HEIGHT;
		cursor.image.dx = p[5];
		cursor.image.dy = p[6];
		memcpy(image_data, p + 7, JDISP_MOUSE_COLOR_SIZE);
	}
	else
	  return -1;

	g2d_cursor(&cursor); /* draw cursor to framebuffer */

	return 0;
}

/*
 * 	function: update_compressed_cursor_image
 *
 * 	author: Li Yuan
 * 	email: kylel@wicresoft.com
 * 	updated date: 01/14/2014
 *
 * 	description: update compressed cursor image
 *
 * 	arguments:
 * 		data:	compressed cursor data
 *		size:	size of compressed cursor data
 * 	return:
 * 		int:	0(succeed), -1(fail)
 */
int update_compressed_cursor_image(char *data, int size)
{
	int ret;
	lzo_uint len;
	lzo_byte *buf;

	ret = lzo_init();
	if (ret != LZO_E_OK) {
		printf("lzo_init failed, ret = %d\n", ret);
		return -1;
	}

	buf = (lzo_byte *) malloc(JDISP_MOUSE_DATA_SIZE);

	ret = lzo1x_decompress((lzo_byte *)data, (lzo_uint)size,
				(lzo_byte *)buf, &len, NULL);

	if (ret != LZO_E_OK) {
		printf("lzo1x_decompress failed, ret = %d\n", ret);
		return -1;
	}

	update_uncompressed_cursor_image(buf, len);
	free(buf);
	
	return 0;
}

/*
 * 	function: update_cursor_status
 *
 * 	author: Li Yuan
 * 	email: kylel@wicresoft.com
 * 	updated date: 01/14/2014
 *
 * 	description: update cursor status
 *
 * 	arguments:
 * 		data:	cursor data
 *		size:	size of cursor data
 *		compressed: data compressed or not
 * 	return:
 * 		int:	0(succeed), -1(fail)
 */
int update_cursor_status(char *data, int size, int compressed)
{
	if (compressed) {
		update_compressed_cursor_image(data, size);
	}
	else {
		update_uncompressed_cursor_image((lzo_byte *)data, size);
	}
	return 0;
}

/*
 * 	function: update_cursor_position
 *
 * 	author: Li Yuan
 * 	email: kylel@wicresoft.com
 * 	updated date: 01/14/2014
 *
 * 	description: update cursor position
 *
 * 	arguments:
 * 		x: x-pos value
 *		y: y-pos value
 * 	return:
 * 		int:	0(succeed), -1(fail)
 */
int update_cursor_position(int x, int y)
{
	if (!image_data) {
		return -1;
	}

	g2d_clear_cursor(&cursor);

	cursor.hot.x = x;
	cursor.hot.y = y;
	
	g2d_cursor(&cursor);

	return 0;
}

int update_cursor_init()
{
	cursor.enable = 1;
	cursor.hot.x = 0;
	cursor.hot.y = 0;
	cursor.image.dx = 0;
	cursor.image.dy = 0;
	cursor.image.width = JDISP_MAX_COLOR_WIDTH;
	cursor.image.height = JDISP_MAX_COLOR_HEIGHT;
	cursor.image.depth = 32;

	if (!image_data) {
		image_data = (char *)malloc(JDISP_MOUSE_COLOR_SIZE);
		memset(image_data, 0, JDISP_MOUSE_COLOR_SIZE);
		cursor.image.data = image_data;
	}

	return 0;
}

int update_cursor_uninit()
{
	free(image_data);
	return 0;
}
