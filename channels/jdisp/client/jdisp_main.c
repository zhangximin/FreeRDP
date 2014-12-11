/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * jdisp Virtual Channel
 *
 * Copyright 2013-2014 Zhang Ximin <zhangximin@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef WITH_DEBUG_JDISP
#include <fcntl.h>
#include <unistd.h>
#endif

#ifndef _WIN32
#include <sys/time.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <winpr/crt.h>
#include <winpr/wlog.h>
#include <winpr/synch.h>
#include <winpr/print.h>
#include <winpr/thread.h>
#include <winpr/wtypes.h>
#include <winpr/stream.h>
#include <winpr/cmdline.h>
#include <winpr/sysinfo.h>
#include <winpr/collections.h>

#include <freerdp/api.h>
#include <freerdp/svc.h>
#include <freerdp/channels/log.h>
#include <freerdp/types.h>
#include <freerdp/constants.h>
#include <freerdp/codec/jpeg.h>
#include <freerdp/client/jdisp.h>

#include <freerdp/addin.h>
#include <freerdp/constants.h>

#include <mxc_jpeg.h>
#include "jdisp_pointer.h"
#include "jdisp_main.h"

#define MAX_JPEG_FILE	(1024 * 1024 * 4)	/* 4MB */
#define MAX_PACK_FILE	(1024 * 1024 * 4)	/* 4MB */

/* jdisp messages */
#define JDISP_MSG_CONNECT			0xFFFFFF55
#define JDISP_MSG_DISCONNECT		0xFFFFFFFF

/* jdisp data types */
#define JDISP_DATA_NORMAL   		0x00000000
#define JDISP_DATA_END      		0x10000000
#define JDISP_DATA_NO_TABLE  		0x20000000
#define JDISP_DATA_SCREEN   		0x40000000
#define JDISP_DATA_MOUSE    		0x80000000
#define JDISP_DATA_ZIPPED_MOUSE 	0x01000000
#define JDISP_DATA_JPEG_TABLE   	0x02000000
#define JDISP_DATA_HELLO    		0x04000000
#define JDISP_DATA_CMD      		0x08000000

/* jdisp extension commands */
#define JDISP_CMD_HEARTBEAT  		0xFD

/* jdisp pack types */
#define JDISP_PACK_COMPLETE			0x00000000
#define JDISP_PACK_FIRST_SEGMENT		0x01000000
#define JDISP_PACK_MIDDLE_SEGMENT		0x02000000
#define JDISP_PACK_LAST_SEGMENT		0x04000000

#define TAG CHANNELS_TAG("jdisp.client")
#define JDISP_DEFAULT_JPEG_QUALITY		80

struct jdisp_plugin {
	CHANNEL_DEF channelDef;
	CHANNEL_ENTRY_POINTS_FREERDP channelEntryPoints;

	HANDLE thread;
	/* put your private data here */
	wStream* data_in;
	void* InitHandle;
	DWORD OpenHandle;
	wMessagePipe* MsgPipe;
	wStream* pack;
	UINT32 total_pack_len;
	wStream* jpeg;
	UINT32 jpeg_table_len;
	RECT jpeg_rect;
	BOOL is_segment;
	int jpeg_quality;
};

//static wQueue* g_BitmapQueue = NULL;

static wListDictionary* g_InitHandles;
static wListDictionary* g_OpenHandles;

#ifdef WITH_DEBUG_JDISP

static jTime dec_time;

double gettime(struct timeval begin, struct timeval end)
{
	int sec, usec;
	double sub;
	sec = end.tv_sec - begin.tv_sec;
	usec = end.tv_usec - begin.tv_usec;
	if (usec < 0)
	{
		sec--;
		usec = usec + 1000000;
	}

	sub = sec + usec / 1000000.;
	return sub;
}

/*
 * 	function: write_jpeg_file
 * 	description: write jpeg data to file(.jpg)
 *
 * 	arguments:
 * 		jdisp:	jdispPlugin objects
 * 	return:
 * 		VOID
 */
#if 0
static void write_jpeg_file(jdispPlugin* jdisp)
{
	int fd;
	char path[255];
	static int i = 0;

	sprintf(path, "image_%03d.jpg", i++);

	if ((fd = open(path, O_WRONLY | O_CREAT, 0666)) == -1)
	{
		DEBUG_JDISP("create file wrong!");
	}

	write(fd, Stream_Buffer(jdisp->jpeg), Stream_Length(jdisp->jpeg));

	close(fd);
}
#endif
/*
 * 	function: write_bitmap_file
 * 	description: write bitmap raw data to file(.bmp)
 *
 * 	arguments:
 * 		data: pointer of bitmap raw data
 * 		size: data size
 * 	return:
 * 		VOID
 */
#if 0
static void write_bitmap_file(BYTE* data, int size)
{
	int fd;
	char path[255];
	static int i = 0;

	sprintf(path, "image_%03d.bmp", i++);

	if ((fd = open(path, O_WRONLY | O_CREAT, 0666)) == -1)
	{
		DEBUG_JDISP("create file wrong!");
	}

	write(fd, data, size);

	close(fd);
}
#endif
#endif

BYTE* jdisp_image_convert_24to32bpp(BYTE* srcData, BYTE* dstData, int width,
		int height) {
	int i;
	BYTE *dstp;

	if (dstData == NULL)
		dstData = (BYTE*) malloc(width * height * 4);

	dstp = dstData;
	for (i = width * height; i > 0; i--) {
		*(dstp++) = *(srcData++ + 2);
		*(dstp++) = *(srcData++);
		*(dstp++) = *(srcData++ - 2);
		*(dstp++) = 0xFF;
	}
	return dstData;
}

/*
 * 	function: parse_jpeg_data
 * 	description: parse the format of jpeg data
 *
 * 	arguments:
 * 		jdisp:	jdispPlugin objects
 * 	return:
 * 		TRUE, if jpeg data format is correct. Otherwise, FALSE.
 */
static BOOL parse_jpeg_data(jdispPlugin* jdisp) {
	BYTE* p;

	Stream_SetPosition(jdisp->pack, 6);
	Stream_GetPointer(jdisp->pack, p);

	/* search FF FE segment */
	if ((p[0] = 0xFF) && (p[1] = 0xFE)) {
		p += 8;
		jdisp->jpeg_rect.x = (p[4] << 8) + p[5];
		jdisp->jpeg_rect.y = (p[6] << 8) + p[7];

		/* search FF C0 segment */
		Stream_SetPosition(jdisp->pack, 6);
		Stream_GetPointer(jdisp->pack, p);

		while (1) {
			if ((p[0] == 0xFF) && (p[1] == 0xC0)) {
				jdisp->jpeg_rect.h = (p[5] << 8) + p[6];
				jdisp->jpeg_rect.w = (p[7] << 8) + p[8];
				break;
			} else {
				p++;
				continue;
			}
		}

		DEBUG_JDISP("image rect is {%d, %d, %d, %d}",
				jdisp->jpeg_rect.x, jdisp->jpeg_rect.y, jdisp->jpeg_rect.w, jdisp->jpeg_rect.h);

		return TRUE;
	}

	return FALSE;

}

/*
 * 	function: get_jpeg_data
 * 	description: get jpeg data from pack data
 *
 * 	arguments:
 * 		jdisp:	jdispPlugin objects.
 * 					We read jpeg data from jdisp->pack, and copy it into jdisp->jpeg.
 * 	return:
 * 		TRUE, if jdisp->jpeg gets jpeg data, otherwise FALSE.
 */
static BOOL get_jpeg_data(jdispPlugin* jdisp) {
	UINT32 pack_len;
	UINT32 jpeg_len;
	UINT32 remain_len;

	/* parse jpeg data */
	if (!parse_jpeg_data(jdisp)) {
		DEBUG_JDISP("parse jpeg data failed");
		return FALSE;
	}

	/* copy jpeg data to jdisp->jpeg */
	Stream_SetPosition(jdisp->pack, 6);
	Stream_GetLength(jdisp->pack, pack_len);

	jpeg_len = jdisp->jpeg_table_len;
	Stream_SetPosition(jdisp->jpeg, jpeg_len);

	remain_len = Stream_Capacity(jdisp->jpeg) - jpeg_len;
	if (remain_len < pack_len - 6) {
		DEBUG_JDISP("jpeg data size is too large (need:remain(%d:%d) bytes)",
				pack_len - 6, remain_len);
		return FALSE;
	}

	Stream_Copy(jdisp->jpeg, jdisp->pack, pack_len - 6);
	Stream_SetLength(jdisp->jpeg, jpeg_len + pack_len - 6);
	DEBUG_JDISP("jpeg_table_len = %d, pack_len = %d", jpeg_len, pack_len - 6);

#ifdef WITH_DEBUG_JDISP
#if 0
	write_jpeg_file(jdisp);
#endif
#endif

	return TRUE;
}

static BOOL jpeg_decode_mxc(jdispPlugin* jdisp) {
	BYTE* input;
	int input_size, x, y, err;

	err = mxc_jpegdec_open();
	if (err)
		return FALSE;

	input = Stream_Buffer(jdisp->jpeg);
	input_size = Stream_Length(jdisp->jpeg);
	x = jdisp->jpeg_rect.x;
	y = jdisp->jpeg_rect.y;

	err = mxc_jpeg_decode((Uint32)input, input_size, x, y);
	if (err) {
		return FALSE;
	}

	err = mxc_jpegdec_close();
	if (err)
		return FALSE;

	return TRUE;
}

/*
 * 	function: jpeg_decode
 * 	description: get first item, and remove it from queue
 *
 * 	arguments:
 * 		jdisp: pointer of jdispPlugin object
 * 	return:
 * 		TRUE, if decode is done, otherwise, FALSE.
 */
static BOOL jpeg_decode(jdispPlugin* jdisp) {
	BYTE* input;
	int input_width, input_height, input_size;
	BYTE* output;
	BYTE* dst;
	int output_length;
	int bpp = 24;

#ifdef WITH_DEBUG_JDISP
	struct timeval tdec_begin, tdec_end;
	double sec_sub;

	gettimeofday(&tdec_begin, NULL);
#endif

	input = Stream_Buffer(jdisp->jpeg);
	input_width = jdisp->jpeg_rect.w;
	input_height = jdisp->jpeg_rect.h;
	input_size = Stream_Length(jdisp->jpeg);

	output_length = input_width * input_height * 3;
	output = (BYTE*) malloc(output_length);
	ZeroMemory(output, output_length);

	if (!jpeg_decompress(input, output, input_width, input_height, input_size,
			bpp))
		return FALSE;

	//jdispBitmap* bitmap = jdisp_bitmap_new(output, output_length, jdisp->jpeg_rect, bpp);
	//if (bitmap == NULL)
	//return FALSE;

	//jdisp_bitmap_queue_enqueue(bitmap);

	dst = jdisp_image_convert_24to32bpp(output, NULL, jdisp->jpeg_rect.w,
			jdisp->jpeg_rect.h);
	output_length = input_width * input_height * 4;
	g2d_rgbcopy((Uint32) dst, output_length, jdisp->jpeg_rect.w,
			jdisp->jpeg_rect.h, jdisp->jpeg_rect.x, jdisp->jpeg_rect.y);
	free(output);
	free(dst);

#ifdef WITH_DEBUG_JDISP
	gettimeofday(&tdec_end, NULL);
	dec_time.tsec_cps += gettime(tdec_begin, tdec_end);
	sec_sub = gettime(dec_time.tsec_udt, tdec_end);
	if (sec_sub >= 1)
	{
		printf("jpeg decode time: %.06f/%.06f\n", dec_time.tsec_cps, sec_sub);
		dec_time.tsec_cps = 0;
		dec_time.tsec_udt.tv_sec = tdec_end.tv_sec;
		dec_time.tsec_udt.tv_usec = tdec_end.tv_usec;
	}
#if 0
	write_bitmap_file(output, output_length);
#endif
#endif

	return TRUE;
}

/*
 * 	function: merge_segments
 * 	description: merge segments into one pack.
 *
 * 	arguments:
 * 		jdisp:	jdispPlugin objects
 * 		data_in:	data received from server
 * 		type:
 *				macro definitions below:
 *
 *				JDISP_PACK_COMPLETE: the pack is normal type, contains completed data.
 *				JDISP_PACK_FIRST_SEGMENT: the pack is first segment of data.
 *				JDISP_PACK_MIDDLE_SEGMENT: the pack is middle segment of data.
 *				JDISP_PACK_LAST_SEGMENT: the pack is last segment of data.
 * 	return:
 * 		VOID
 */
static void merge_segments(jdispPlugin* jdisp, wStream* data_in, int type) {
	UINT32 bytes;
	UINT32 pack_len;

	if (type == JDISP_PACK_COMPLETE) {
		/* add normal data to pack */
		Stream_SetPosition(data_in, 0);
		Stream_GetCapacity(data_in, bytes);

		Stream_SetPosition(jdisp->pack, 0);

		Stream_Copy(jdisp->pack, data_in, bytes);
		Stream_SetLength(jdisp->pack, bytes);
	} else if (type == JDISP_PACK_FIRST_SEGMENT) {
		/* add first segment to pack */
		Stream_SetPosition(data_in, 0);
		Stream_GetCapacity(data_in, bytes);

		Stream_SetPosition(jdisp->pack, 0);

		Stream_Copy(jdisp->pack, data_in, bytes);
		Stream_SetLength(jdisp->pack, bytes);

		jdisp->is_segment = TRUE;
	} else if (type == JDISP_PACK_MIDDLE_SEGMENT) {
		/* add middle segment to pack */
		Stream_SetPosition(data_in, 4);
		Stream_GetCapacity(data_in, bytes);

		Stream_GetLength(jdisp->pack, pack_len);
		Stream_SetPosition(jdisp->pack, pack_len);

		Stream_Copy(jdisp->pack, data_in, bytes - 4);
		Stream_SetLength(jdisp->pack, pack_len + bytes - 4);
	} else if (type == JDISP_PACK_LAST_SEGMENT) {
		/* add last segment to pack */
		Stream_SetPosition(data_in, 4);
		Stream_GetCapacity(data_in, bytes);

		Stream_GetLength(jdisp->pack, pack_len);
		Stream_SetPosition(jdisp->pack, pack_len);

		Stream_Copy(jdisp->pack, data_in, bytes - 4);
		Stream_SetLength(jdisp->pack, pack_len + bytes - 4);

		jdisp->is_segment = FALSE;
	}
}

/*
 * 	function: handle_jdisp_pack
 * 	description: handle jdisp packs which are received from server
 *
 * 	arguments:
 * 		jdisp:	jdispPlugin objects
 * 		data_in:	data received from server
 * 	return:
 * 		-1, error occurs
 * 		0, to handle jdisp pack is done
 * 		1, need decode jpeg data
 */
static int handle_jdisp_pack(jdispPlugin* jdisp, wStream* data_in) {
	UINT32 pack_header, bytes, pack_len;
	wStream* data_out;
	BYTE* buf;

	Stream_SetPosition(data_in, 0);
	Stream_GetLength(data_in, bytes);
	Stream_Read_UINT32(data_in, pack_header);
	DEBUG_JDISP("pack_header is 0x%08X", pack_header);

	if (pack_header & JDISP_DATA_HELLO) {
		DEBUG_JDISP("receive hello pack from server");
		/* send connect message(0xFFFFFF55) to server */
		data_out = Stream_New(NULL, 4);
		Stream_Write_UINT32(data_out, JDISP_MSG_CONNECT);

		Stream_GetBuffer(data_out, buf);
		buf[0] = jdisp->jpeg_quality;
		DEBUG_JDISP("send data: %02X %02X %02X %02X",
				buf[0], buf[1], buf[2], buf[3]);

		/* svc_plugin_send takes ownership of data_out, that is why
		 we do not free it */
	} else if (pack_header & JDISP_DATA_JPEG_TABLE) {
		DEBUG_JDISP("receive jpeg table pack from server");

		pack_len = bytes - 4 - 2;
		Stream_SetPosition(jdisp->jpeg, 0);

		Stream_Copy(jdisp->jpeg, data_in, pack_len);

		jdisp->jpeg_table_len = pack_len;
		Stream_SetLength(jdisp->jpeg, pack_len);
		DEBUG_JDISP("received jpeg table: %d bytes", pack_len);
	} else if (pack_header & JDISP_DATA_MOUSE) {
		DEBUG_JDISP("receive mouse pack from server");
		/* TODO: use local mouse icon */
		char *buf;
		pack_len = pack_header & 0x00FFFFFF;
		buf = (char *) malloc(pack_len);
		memcpy(buf, Stream_Pointer(data_in), pack_len);

		update_cursor_status(buf, pack_len, JDISP_DATA_UNCOMPRESSED);

		free(buf);
	} else if (pack_header & JDISP_DATA_ZIPPED_MOUSE) {
		DEBUG_JDISP("receive compressed mouse pack from server");
		/* TODO: use compressed mouse icon */
		char *buf;
		pack_len = pack_header & 0x00FFFFFF;
		buf = (char *) malloc(pack_len);
		memcpy(buf, Stream_Pointer(data_in), pack_len);

		update_cursor_status(buf, pack_len, JDISP_DATA_COMPRESSED);

		free(buf);
	} else if (pack_header & JDISP_DATA_SCREEN) {
		DEBUG_JDISP("receive screen pack from server");
		/* TODO: handle desktop image */
	} else if (pack_header & JDISP_DATA_CMD) {
		BYTE cmd;
		Stream_Read_UINT8(data_in, cmd);
		switch (cmd) {
		case JDISP_CMD_HEARTBEAT:
			DEBUG_JDISP("receive heartbeat command from server");
			break;
		default:
			DEBUG_JDISP("receive unknown command: 0x%02x", cmd);
			break;
		}
	} else if (pack_header & JDISP_DATA_NO_TABLE) {
		/* TODO: handle no table jpeg pack*/
		pack_len = pack_header & 0x00FFFFFF;
		if ((bytes - 4) != pack_len) {
			jdisp->is_segment = FALSE;
			DEBUG_JDISP("bad pack length (%d:%d)", bytes - 4, pack_len);
			return -1;
		}

		if (pack_header & JDISP_DATA_END) {
			if (jdisp->is_segment) {
				merge_segments(jdisp, data_in, JDISP_PACK_LAST_SEGMENT);
				DEBUG_JDISP("this is a last segment, length (%d:%d)",
						bytes - 4, pack_len);
			} else {
				merge_segments(jdisp, data_in, JDISP_PACK_COMPLETE);
				DEBUG_JDISP("this is a complete jpeg pack, length (%d:%d)",
						bytes - 4, pack_len);
			}

			return 1;
		} else {
			if (jdisp->is_segment) {
				merge_segments(jdisp, data_in, JDISP_PACK_MIDDLE_SEGMENT);
				DEBUG_JDISP("this is a middle segment (%d:%d)",
						bytes - 4, pack_len);
			} else {
				merge_segments(jdisp, data_in, JDISP_PACK_FIRST_SEGMENT);
				DEBUG_JDISP("this is a first segment (%d:%d)",
						bytes - 4, pack_len);
			}
		}
	}

	return 0;
}

static void jdisp_process_receive(jdispPlugin* plugin, wStream* data_in) {
	UINT32 bytes;

	jdispPlugin* jdisp = plugin;

	if (!jdisp) {
		DEBUG_JDISP("jdisp_process_receive: jdisp is nil");
		return;
	}

	/* process data in (from server) here */

	bytes = Stream_Capacity(data_in);
	if (bytes > 0) {
		if (handle_jdisp_pack(jdisp, data_in) == 1) {
			/* TODO: get jpeg data */
			if (get_jpeg_data(jdisp)) {
				/* TODO: decode jpeg data */
				if ((jdisp->jpeg_rect.w % 16) == 0
						&& (jdisp->jpeg_rect.h % 16) == 0
						&& jdisp->jpeg_rect.w > 32 && jdisp->jpeg_rect.h > 32) {
					jpeg_decode_mxc(jdisp);
				} else {
			 		jpeg_decode(jdisp);
				}
			} else {
				/* TODO: error occurs, exit(0) */
			}
		}
	}
	Stream_Free(data_in, TRUE);
}

void jdisp_add_init_handle_data(void* pInitHandle, void* pUserData) {
	if (!g_InitHandles)
		g_InitHandles = ListDictionary_New(TRUE);

	ListDictionary_Add(g_InitHandles, pInitHandle, pUserData);
}
void* jdisp_get_init_handle_data(void* pInitHandle) {
	void* pUserData = NULL;
	pUserData = ListDictionary_GetItemValue(g_InitHandles, pInitHandle);

	return pUserData;
}

void jdisp_remove_init_handle_data(void* pInitHandle) {
	ListDictionary_Remove(g_InitHandles, pInitHandle);
}

void jdisp_add_open_handle_data(DWORD openHandle, void* pUserData) {
	void* pOpenHandle = (void*) (size_t) openHandle;

	if (!g_OpenHandles)
		g_OpenHandles = ListDictionary_New(TRUE);

	ListDictionary_Add(g_OpenHandles, pOpenHandle, pUserData);
}

void* jdisp_get_open_handle_data(DWORD openHandle) {
	void* pUserData = NULL;
	void* pOpenHandle = (void*) (size_t) openHandle;

	pUserData = ListDictionary_GetItemValue(g_OpenHandles, pOpenHandle);
	return pUserData;
}

void jdisp_remove_open_handle_data(DWORD openHandle) {
	void* pOpenHandle = (void*) (size_t) openHandle;
	ListDictionary_Remove(g_OpenHandles, pOpenHandle);
}

static void jdisp_virtual_channel_event_data_received(jdispPlugin *plugin,
		void* pData, UINT32 dataLength, UINT32 totalLength, UINT32 dataFlags) {
	wStream* data_in;
	if ((dataFlags & CHANNEL_FLAG_SUSPEND)
			|| (dataFlags & CHANNEL_FLAG_RESUME)) {
		return;
	}

	if (dataFlags & CHANNEL_FLAG_FIRST) {
		if (plugin->data_in != NULL)
			Stream_Free(plugin->data_in, TRUE);

		plugin->data_in = Stream_New(NULL, totalLength);
	}

	data_in = plugin->data_in;
	Stream_EnsureRemainingCapacity(data_in, (int) dataLength);
	Stream_Write(data_in, pData, dataLength);

	if (dataFlags & CHANNEL_FLAG_LAST) {
		if (Stream_Capacity(data_in) != Stream_GetPosition(data_in)) {
			WLog_ERR(TAG,
					"jdisp_virtual_channel_event_data_received: read error\n");
		}
		plugin->data_in = NULL;
		Stream_SealLength(data_in);
		Stream_SetPosition(data_in, 0);

		MessageQueue_Post(plugin->MsgPipe->In, NULL, 0, (void*) data_in, NULL);
	}
}

static VOID VCAPITYPE jdisp_virtual_channel_open_event(DWORD openHandle,
		UINT event, LPVOID pData, UINT32 dataLength, UINT32 totalLength,
		UINT32 dataFlags) {
	jdispPlugin* plugin;
	plugin = (jdispPlugin *) jdisp_get_open_handle_data(openHandle);

	if (!plugin) {
		WLog_ERR(TAG, "jdisp_virtual_channel_open_event: error no match\n");
		return;
	}

	switch (event) {
	case CHANNEL_EVENT_DATA_RECEIVED:
		jdisp_virtual_channel_event_data_received(plugin, pData, dataLength,
				totalLength, dataFlags);

		break;
	case CHANNEL_EVENT_WRITE_COMPLETE:
		Stream_Free((wStream*)pData, TRUE);
		break;
	case CHANNEL_EVENT_USER:
		break;
	}
}

COMMAND_LINE_ARGUMENT_A jdisp_args[] = { { "jpeg-quality",
		COMMAND_LINE_VALUE_REQUIRED, "<percentage>", NULL, NULL, -1, NULL,
		"JPEG quality" }, { NULL, 0, NULL, NULL, NULL, -1, NULL, NULL } };

static void jdisp_process_addin_args(jdispPlugin* jdisp, ADDIN_ARGV* args) {
	int status;
	DWORD flags;

	COMMAND_LINE_ARGUMENT_A* arg;

	jdisp->jpeg_quality = JDISP_DEFAULT_JPEG_QUALITY;

	flags = COMMAND_LINE_SIGIL_NONE | COMMAND_LINE_SEPARATOR_COLON;

	status = CommandLineParseArgumentsA(args->argc, (const char**) args->argv,
			jdisp_args, flags, jdisp, NULL, NULL);

	if (status < 0)
		return;

	arg = jdisp_args;

	do {
		if (!(arg->Flags & COMMAND_LINE_VALUE_PRESENT))
			continue;

		CommandLineSwitchStart(arg)
		CommandLineSwitchCase(arg, "jpeg-quality") {
			int v = atoi(arg->Value);
			if (v > 0 && v < 101)
				jdisp->jpeg_quality = v;
		}

		CommandLineSwitchDefault(arg) {

		}
CommandLineSwitchEnd	(arg)
}

while((arg = CommandLineFindNextArgumentA(arg)) != NULL);

}

static void jdisp_process_connect(jdispPlugin* jdisp) {
	ADDIN_ARGV* args;

	jdisp->jpeg_quality = JDISP_DEFAULT_JPEG_QUALITY;

	args = (ADDIN_ARGV*) jdisp->channelEntryPoints.pExtendedData;
	if (args)
		jdisp_process_addin_args(jdisp, args);

}

static void* jdisp_virtual_channel_client_thread(void* arg) {
	wStream* data;
	wMessage message;
	jdispPlugin* plugin = (jdispPlugin *) arg;

	jdisp_process_connect(plugin);

	while (1) {
		if (!MessageQueue_Wait(plugin->MsgPipe->In))
			break;
		if (MessageQueue_Peek(plugin->MsgPipe->In, &message, TRUE)) {
			if (message.id == WMQ_QUIT)
				break;
			if (message.id == 0) {
				data = (wStream *) message.wParam;
				jdisp_process_receive(plugin, data);
			}
		}
	}
	ExitThread(0);
	return NULL;
}

static void jdisp_virtual_channel_event_connected(jdispPlugin* plugin,
		LPVOID pData, UINT32 dataLength) {
	UINT32 status;
	int err;
	status = plugin->channelEntryPoints.pVirtualChannelOpen(plugin->InitHandle,
			&plugin->OpenHandle, plugin->channelDef.name,
			jdisp_virtual_channel_open_event);
	jdisp_add_open_handle_data(plugin->OpenHandle, plugin);

	if (status != CHANNEL_RC_OK) {
		WLog_ERR(TAG,
				"jdisp_virtual_channel_event_connected: open failed status: %d\n",
				status);
		return;
	}

	plugin->pack = Stream_New(NULL, MAX_PACK_FILE);
	Stream_Zero(plugin->pack, MAX_PACK_FILE);

	plugin->jpeg = Stream_New(NULL, MAX_JPEG_FILE);
	Stream_Zero(plugin->jpeg, MAX_JPEG_FILE);

	err = mxc_jpegdec_init();
	if (err) {
		DEBUG_JDISP("mxc_jpegdec_init failed\n");
		exit(0);
	}

	err = g2d_init();
	if (err) {
		DEBUG_JDISP("g2d_init failed\n");
		exit(0);
	}

	update_cursor_init();

	plugin->MsgPipe = MessagePipe_New();
	plugin->thread = CreateThread(NULL, 0,
			(LPTHREAD_START_ROUTINE) jdisp_virtual_channel_client_thread,
			(void*) plugin, 0, NULL);
}

static void jdisp_virtual_channel_event_terminated(jdispPlugin* jdisp) {
	MessagePipe_PostQuit(jdisp->MsgPipe, 0);
	WaitForSingleObject(jdisp->thread, INFINITE);
	CloseHandle(jdisp->thread);

	Stream_Free(jdisp->pack, TRUE);
	Stream_Free(jdisp->jpeg, TRUE);

	update_cursor_uninit();
	g2d_uninit();
	mxc_jpegdec_uninit();

	jdisp->channelEntryPoints.pVirtualChannelClose(jdisp->OpenHandle);

	if (jdisp->data_in) {
		Stream_Free(jdisp->data_in, TRUE);
		jdisp->data_in = NULL;
	}

	MessagePipe_Free(jdisp->MsgPipe);
	jdisp_remove_open_handle_data(jdisp->OpenHandle);
	jdisp_remove_init_handle_data(jdisp->InitHandle);

	free(jdisp);
}

static VOID VCAPITYPE jdisp_virtual_channel_init_event(LPVOID pInitHandle,
		UINT event, LPVOID pData, UINT dataLength) {
	jdispPlugin* plugin;

	plugin = (jdispPlugin *) jdisp_get_init_handle_data(pInitHandle);

	if (!plugin) {
		WLog_ERR(TAG, "jdisp_virtual_channel_init_event: error no match\n");
		return;
	}

	switch (event) {
	case CHANNEL_EVENT_CONNECTED:
		jdisp_virtual_channel_event_connected(plugin, pData, dataLength);
		break;
	case CHANNEL_EVENT_DISCONNECTED:
		break;
	case CHANNEL_EVENT_TERMINATED:
		jdisp_virtual_channel_event_terminated(plugin);
		break;
	}

}

#define VirtualChannelEntry	jdisp_VirtualChannelEntry

BOOL VCAPITYPE VirtualChannelEntry(PCHANNEL_ENTRY_POINTS pEntryPoints) {
	jdispPlugin* jdisp;

	jdisp = (jdispPlugin*) calloc(1, sizeof(jdispPlugin));

	if (jdisp) {
		jdisp->channelDef.options = CHANNEL_OPTION_INITIALIZED
				| CHANNEL_REMOTE_CONTROL_PERSISTENT | CHANNEL_OPTION_PRI_HIGH;

		strcpy(jdisp->channelDef.name, JDISP_SVC_CHANNEL_NAME);

		CopyMemory(&(jdisp->channelEntryPoints), pEntryPoints,
				sizeof(CHANNEL_ENTRY_POINTS_FREERDP));
		jdisp->channelEntryPoints.pVirtualChannelInit(&jdisp->InitHandle,
				&jdisp->channelDef, 1, VIRTUAL_CHANNEL_VERSION_WIN2000,
				jdisp_virtual_channel_init_event);
		jdisp_add_init_handle_data(jdisp->InitHandle, (void*) jdisp);
	}

	return 1;
}
