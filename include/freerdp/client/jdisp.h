/*
 * 	jdisp.h
 *
 *  	Created on: 2014-12-11
 *     Author: Zhang Ximin <zhangximin@gmail.com>
 */

#ifndef FREERDP_CHANNEL_CLIENT_JDISP_H
#define FREERDP_CHANNEL_CLIENT_JDISP_H

#include <freerdp/graphics.h>
#include <freerdp/api.h>
#include <freerdp/types.h>
#include <sys/time.h>

typedef struct j_time
{
	struct timeval tsec_udt;
	double tsec_cps;
} jTime;

/*
 * Client Interface
 */
#define JDISP_SVC_CHANNEL_NAME "disp"

typedef struct rect
{
	UINT32 x;
	UINT32 y;
	UINT32 w;
	UINT32 h;
} RECT;

#define Rect_Copy(_dst, _src) do { \
	_dst->x = _src->x; \
	_dst->y = _src->y; \
	_dst->w = _src->w; \
	_dst->h = _src->h; \
	} while (0)

//typedef struct _jdisp_bitmap
//{
//	wStream* data;
//	RECT* rect;
//	int bpp;
//} LxtdispBitmap;
//
//LxtdispBitmap* jdisp_bitmap_new(BYTE* data, int size, RECT* rect, int bpp);
//void jdisp_bitmap_free(void* obj);
//void jdisp_bitmap_queue_enqueue(LxtdispBitmap* bitmap);
//LxtdispBitmap* jdisp_bitmap_queue_dequeue(void);

int update_cursor_status(char *data, int size, int compressed);
int update_cursor_position(int x, int y);
int update_cursor_init();
int update_cursor_uninit();

#endif /* FREERDP_CHANNEL_CLIENT_JDISP_H */
