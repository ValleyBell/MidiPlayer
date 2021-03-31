#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
void __stdcall Sleep(unsigned long dwMilliseconds);	// from WinBase.h
#else
#include <unistd.h>
#define Sleep(x)	usleep(x * 1000)
#endif

#ifdef _MSC_VER
#define inline __inline	// used by avutil headers
#endif

#include <libavutil/version.h>
#include <libavcodec/version.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavutil/error.h>
#include <libavutil/pixfmt.h>

#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/frame.h>
#include <libavutil/buffer.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>

#include <curses.h>

#include <stdtype.h>
#include "scr-record.h"


static UINT8 IsSimilarColor(const SCRREC_PIX color, const SCRREC_PIX compare, int tolerance);
static SCRREC_RECT DetectTerminalEdge(SCRREC_IMAGE calibImgs[2], SCRREC_CAPTURE* sc);
static void WaitForSync(SCRREC_CAPTURE* sc, int x, int y, SCRREC_PIX syncColor, UINT8 match);

static void FFmpegVerCheck(void);
static UINT8 InitFFmpeg(const SCRREC_RECT* rect);
static UINT8 PrepareFFmpegCodec(void);
static void DeinitFFmpeg(void);
static UINT8 CompressFFmpeg(const SCRREC_IMAGE* xImg);
static UINT8 ProcessFFmpegFrame(void);


static const char* BLOCK_SOLID = "\xE2\x96\x88";
static const char* BLOCK_NONE = " ";

//static const char* CODEC_NAME = "libx264";
//static enum AVPixelFormat _dstPixFmt = AV_PIX_FMT_YUV444P;
static const char* CODEC_NAME = "libx264rgb";
static enum AVPixelFormat _dstPixFmt = AV_PIX_FMT_BGR0;

static SCRREC_PIX calibColor;
static SCRREC_RECT termRect;

static enum AVPixelFormat _srcPixFmt = AV_PIX_FMT_BGR0;
static int _imgWidth = 0;
static int _imgHeight = 0;
static int _frameRate = 50;
static AVFrame* _swFrame = NULL;
static struct SwsContext* _swsCtx = NULL;

static const AVCodec* _codec = NULL;
static AVCodecContext* _encCtx = NULL;
static AVPacket* _pkt = NULL;

static AVFormatContext* _outFmtCtx = NULL;
static AVStream* _outStrm;

static int _frameID = 0;
static SCRREC_CAPTURE* srCapture = NULL;

UINT8 ScrRec_InitCapture(void)
{
	UINT8 retVal;
	
	FFmpegVerCheck();
	
	retVal = ScrWin_Init(&srCapture);
	if (retVal)
		return retVal;
	return 0x00;
}

UINT8 ScrRec_DeinitCapture(void)
{
	ScrWin_Deinit(srCapture);
	return 0x00;
}

UINT8 ScrRec_InitVideo(void)
{
	return InitFFmpeg(&termRect);
}

UINT8 ScrRec_DeinitVideo(void)
{
	DeinitFFmpeg();
	
	return 0x00;
}

UINT8 ScrRec_GetWindowCoords(void)
{
	SCRREC_IMAGE calibImgs[2];
	
	initscr();
	cbreak();
	keypad(stdscr, TRUE);
	noecho();
	curs_set(0);
	
	start_color();
	init_pair(0, COLOR_WHITE, COLOR_BLACK);
	init_pair(1, COLOR_WHITE, COLOR_WHITE);
	
	// step 1 - draw a white horizontal + vertical line
	// (not using BLOCK_SOLID, because UTF-8 characters don't work well with hline/vline)
	attron(COLOR_PAIR(1));
	mvwhline(stdscr, 0, 0, ' ', COLS);
	mvwvline(stdscr, 1, 0, ' ', LINES);
	refresh();
	Sleep(200);
	calibImgs[0] = ScrWin_Image(srCapture, NULL);
	attroff(COLOR_PAIR(1));
	
	// step 2 - make the two lines black
	attron(COLOR_PAIR(0));
	mvwhline(stdscr, 0, 0, ' ', COLS);
	mvwvline(stdscr, 1, 0, ' ', LINES);
	refresh();
	Sleep(200);
	calibImgs[1] = ScrWin_Image(srCapture, NULL);
	attroff(COLOR_PAIR(0));
	
	// step 3 - detect the terminal size by doing a "diff" between the two images
	termRect = DetectTerminalEdge(calibImgs, srCapture);
	
	//calibColor[0] = ScrWin_GetImagePixel(srCapture, &calibImgs[0], termRect.x, termRect.y);
	//calibColor[1] = ScrWin_GetImagePixel(srCapture, &calibImgs[1], termRect.x, termRect.y);
	
	// keep the colour of BLOCK_SOLID (0) - we will use that later to wait for the OS graphics code to finish drawing
	attron(A_BOLD | COLOR_PAIR(0));
	mvaddstr(0, 0, BLOCK_SOLID);	refresh();
	attroff(A_BOLD | COLOR_PAIR(0));
	Sleep(200);
	calibColor = ScrWin_GetScreenPixel(srCapture, termRect.x, termRect.y);
	
	endwin();
	Sleep(50);
	
	ScrWin_FreeImage(&calibImgs[0]);
	ScrWin_FreeImage(&calibImgs[1]);
	
	return 0x00;
}

static UINT8 IsSimilarColor(const SCRREC_PIX color, const SCRREC_PIX compare, int tolerance)
{
	int compDiff[3];	// component difference
	int cIdx;
	
	compDiff[0] = compare.bgr.blue - color.bgr.blue;
	compDiff[1] = compare.bgr.green - color.bgr.green;
	compDiff[2] = compare.bgr.red - color.bgr.red;
	for (cIdx = 0; cIdx < 3; cIdx ++)
	{
		if (abs(compDiff[cIdx]) > tolerance)
			return 0;
	}
	return 1;
}

static SCRREC_RECT DetectTerminalEdge(SCRREC_IMAGE calibImgs[2], SCRREC_CAPTURE* sc)
{
	int pos, x, y, curImg;
	SCRREC_PIX baseCol[2];
	SCRREC_PIX pixCol[2];
	UINT8 found;
	SCRREC_RECT resRect;
	
	resRect.x = 0;
	resRect.y = 0;
	resRect.width = calibImgs[0].width;
	resRect.height = calibImgs[1].height;
	
	memset(baseCol, 0x00, sizeof(baseCol));
	baseCol[0].u32 = 0xFFFFFF;	// white
	baseCol[1].u32 = 0x000000;	// black
	
	found = 0;
	x = 0;
	y = 0;
	// 820: allow for 40 lines (endpos(line n) = 1+2+3+...+n)
	for (pos = 0; pos < 820; pos ++)
	{
		for (curImg = 0; curImg < 2; curImg ++)
			pixCol[curImg] = ScrWin_GetImagePixel(sc, &calibImgs[curImg], x, y);
		if (pixCol[0].u32 != pixCol[1].u32)
		{
			//printw("Different pixel at (%d, %d): 0x%X != 0x%X\n",
			//		x, y, pixCol[0].pixel, pixCol[1].pixel);
			// rather large tolerance to make up for anti-aliasing effects
			if (IsSimilarColor(pixCol[0], baseCol[0], 0x60) &&
				IsSimilarColor(pixCol[1], baseCol[1], 0x60))
			{
				found = 1;
				break;
			}
		}
		
		// move in zig-zag algorithm
		// 0 2 5
		// 1 4
		// 3
		x ++;
		y --;
		if (y < 0)
		{
			y = x;
			x = 0;
		}
	}
	if (! found)
		return resRect;
	
	resRect.x = x;
	resRect.y = y;
	for (x = resRect.x + 1; x < resRect.x + resRect.width; x ++)
	{
		pixCol[0] = ScrWin_GetImagePixel(sc, &calibImgs[0], x, resRect.y);
		pixCol[1] = ScrWin_GetImagePixel(sc, &calibImgs[1], x, resRect.y);
		if (pixCol[0].u32 == pixCol[1].u32)
		{
			resRect.width = x - resRect.x;
			break;
		}
	}
	for (y = resRect.y + 1; y < resRect.y + resRect.height; y ++)
	{
		pixCol[0] = ScrWin_GetImagePixel(sc, &calibImgs[0], resRect.x, y);
		pixCol[1] = ScrWin_GetImagePixel(sc, &calibImgs[1], resRect.x, y);
		if (pixCol[0].u32 == pixCol[1].u32)
		{
			resRect.height = y - resRect.y;
			break;
		}
	}
	printw("Actual Terminal Area: (%d, %d), size %d x %d\n",
			resRect.x, resRect.y, resRect.width, resRect.height);
	
	return resRect;
}

static void WaitForSync(SCRREC_CAPTURE* sc, int x, int y, SCRREC_PIX syncColor, UINT8 match)
{
	SCRREC_PIX pixColor;
	unsigned int syncTime;
	
	for (syncTime = 0; syncTime < 1000; syncTime ++)
	{
		pixColor = ScrWin_GetScreenPixel(sc, x, y);
		if (match && pixColor.u32 == syncColor.u32)
			break;	// leave loop when expected color appeared
		else if (! match && pixColor.u32 != syncColor.u32)
			break;	// leave loop when expected color disappeared
		Sleep(1);
	}
	
	return;
}

UINT8 ScrRec_TakeAndSave(void)
{
	const SCRREC_RECT* rect = &termRect;
	const SCRREC_PIX syncColor = calibColor;
	SCRREC_IMAGE image;
	
	chtype oldChar;
	attr_t oldAttr;
	short oldPair;
	
	if (srCapture == NULL)
		return 0xB0;
	if (_encCtx == NULL)
		return 0xB1;
	
	attr_get(&oldAttr, &oldPair, NULL);
	oldChar = mvinch(0, 0);
	
	// draw a solid block and wait for it to show up on the screen
	attron(A_BOLD | COLOR_PAIR(0));
	mvaddstr(0, 0, BLOCK_SOLID);	refresh();
	attroff(A_BOLD | COLOR_PAIR(0));
	WaitForSync(srCapture, rect->x, rect->y, syncColor, 1);
	
	// restore the old character
	attrset(0);
	move(0, 0);	echochar(oldChar);
	WaitForSync(srCapture, rect->x, rect->y, syncColor, 0);
	attr_set(oldAttr, oldPair, NULL);
	
	// now take the screen shot
	image = ScrWin_Image(srCapture, rect);
	CompressFFmpeg(&image);
	ScrWin_FreeImage(&image);
	ProcessFFmpegFrame();
	
	return 0;
}


static void FFmpegVerCheck(void)
{
	unsigned avLibVer;
	int logLevel;
	
	avLibVer = avcodec_version();
	if (AV_VERSION_MAJOR(avLibVer) != AV_VERSION_MAJOR(LIBAVCODEC_VERSION_INT))
	{
		printf("Warning: Compiled against %s but running with %d.%d.%d\n",
			LIBAVCODEC_IDENT, AV_VERSION_MAJOR(avLibVer), AV_VERSION_MINOR(avLibVer), AV_VERSION_MICRO(avLibVer));
		printf("This may cause bugs when using the libav API!\n");
		fflush(stdout);
	}
	
	// set log level below "warning" in order to suppress the following message
	//  [mjpeg_qsv @ 00000237c8023800] Unknown FrameType, set pict_type to AV_PICTURE_TYPE_NONE.
	logLevel = av_log_get_level();
	if (logLevel >= AV_LOG_WARNING)
		av_log_set_level(AV_LOG_WARNING - 1);
	
	return;
}

static UINT8 InitFFmpeg(const SCRREC_RECT* rect)
{
	int retVal;
	
	av_log_set_level(AV_LOG_TRACE);
	_imgWidth = rect->width;
	_imgHeight = rect->height;
	
#if LIBAVCODEC_VERSION_MAJOR < 58
	avcodec_register_all();	// required for FFmpeg <=v57, deprecated in v58
	av_register_all();
	printf("Codecs registered.\n");
#endif
	
	_codec = avcodec_find_encoder_by_name(CODEC_NAME);
	if (_codec == NULL)
		return 0x83;
	
	_swFrame = av_frame_alloc();
	_swFrame->width = _imgWidth;
	_swFrame->height = _imgHeight;
	_swFrame->format = _dstPixFmt;
	retVal = av_frame_get_buffer(_swFrame, 0);
	if (retVal < 0)
		return 0x80;
	
	// setup Software Scaler for colourspace conversion
	_swsCtx = sws_getContext(_imgWidth, _imgHeight, _srcPixFmt,
			_swFrame->width, _swFrame->height, _dstPixFmt,
			SWS_FAST_BILINEAR, NULL, NULL, NULL);
	if (_swsCtx == NULL)
		return 0x81;
	
	_pkt = av_packet_alloc();
	_outFmtCtx = NULL;
	
	av_log_set_level(AV_LOG_ERROR);
	return 0x00;
}

static AVRational make_avrational(int num, int den)
{
	AVRational result = {num, den};
	return result;
}

static UINT8 PrepareFFmpegCodec(void)
{
	_encCtx = avcodec_alloc_context3(_codec);
	if (_encCtx == NULL)
		return 0x84;
	
	// setup codec parameters
	_encCtx->width = _swFrame->width;
	_encCtx->height = _swFrame->height;
	_encCtx->pix_fmt = (enum AVPixelFormat)_swFrame->format;
	//_encCtx->color_range = _swFrame->color_range;
	_encCtx->sample_aspect_ratio = make_avrational(1, 1);
	_encCtx->time_base = make_avrational(1, _frameRate);
	_encCtx->framerate = make_avrational(_frameRate, 1);
	//_encCtx->bit_rate = 0;	// use "default"
	//_encCtx->gop_size = _frameRate * 2;	// a full frame every 2 seconds is sufficient
	
	if (_codec->id == AV_CODEC_ID_H264)
	{
		av_opt_set(_encCtx->priv_data, "preset", "veryslow", 0);
		av_opt_set_int(_encCtx->priv_data, "qp", 0, 0);	// lossless encoding
	}
	
	return 0x00;
}

UINT8 ScrRec_TestVideoRec(void)
{
	UINT8 retValU8;
	int retVal;
	char avebuf[AV_ERROR_MAX_STRING_SIZE];
	
	av_log_set_level(AV_LOG_TRACE);
	
	retValU8 = PrepareFFmpegCodec();
	if (retValU8)
		return retValU8;
	
	retVal = avcodec_open2(_encCtx, _codec, NULL);
	if (retVal < 0)
	{
		av_make_error_string(avebuf, AV_ERROR_MAX_STRING_SIZE, retVal);
		printf("Could not open codec: %s\n", avebuf);
		if (retVal == AVERROR(EINVAL))
		{
			const enum AVPixelFormat* pf;
			printf("Supported pixel formats:\n");
			for (pf = _codec->pix_fmts; *pf != AV_PIX_FMT_NONE; pf ++)
				printf("%d, ", *pf);
			printf("\b\b  \n");
		}
		return 0x88;
	}
	
	retVal = avformat_alloc_output_context2(&_outFmtCtx, NULL, "m4v", NULL);
	if (retVal < 0)
	{
		printf("Could not create output context!\n");
		return 0x89;
	}
	
	_outStrm = avformat_new_stream(_outFmtCtx, NULL);
	if (_outStrm == NULL)
	{
		printf("Failed allocating output stream\n");
		return 0x8A;
	}
	
	if (_outFmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
		_encCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	retVal = avcodec_parameters_from_context(_outStrm->codecpar, _encCtx);
	if (retVal < 0)
	{
		av_make_error_string(avebuf, AV_ERROR_MAX_STRING_SIZE, retVal);
		printf("Failed to copy encoder parameters to output stream: %s\n", avebuf);
		return 0x8B;
	}
	_outStrm->time_base = _encCtx->time_base;
	_outStrm->avg_frame_rate = _encCtx->framerate;
	
	av_dump_format(_outFmtCtx, 0, "outputTest.m4v", 1);	// show format info
	
	avcodec_parameters_free(&_outStrm->codecpar);
	avformat_free_context(_outFmtCtx);	_outFmtCtx = NULL;
	avcodec_free_context(&_encCtx);
	
	av_log_set_level(AV_LOG_ERROR);
	return 0;
}

UINT8 ScrRec_StartVideoRec(const char* fileName, int frameRate)
{
	UINT8 retValU8;
	int retVal;
	char avebuf[AV_ERROR_MAX_STRING_SIZE];
	
	_frameRate = frameRate;
	
	retValU8 = PrepareFFmpegCodec();
	if (retValU8)
		return retValU8;
	
	retVal = avcodec_open2(_encCtx, _codec, NULL);
	if (retVal < 0)
		return 0x88;
	
	// Note: file name is used for determining the output format here
	retVal = avformat_alloc_output_context2(&_outFmtCtx, NULL, NULL, fileName);
	if (retVal < 0)
		return 0x89;
	
	_outStrm = avformat_new_stream(_outFmtCtx, NULL);
	if (_outStrm == NULL)
		return 0x8A;
	
	if (_outFmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
		_encCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	retVal = avcodec_parameters_from_context(_outStrm->codecpar, _encCtx);
	if (retVal < 0)
		return 0x8B;
	_outStrm->time_base = _encCtx->time_base;
	_outStrm->avg_frame_rate = _encCtx->framerate;
	
	if (! (_outFmtCtx->oformat->flags & AVFMT_NOFILE))
	{
		retVal = avio_open(&_outFmtCtx->pb, fileName, AVIO_FLAG_WRITE);
		if (retVal < 0)
		{
			av_make_error_string(avebuf, AV_ERROR_MAX_STRING_SIZE, retVal);
			printf("Could not open output file %s: %s\n", fileName, avebuf);
			return 0x90;
		}
	}
	
	retVal = avformat_write_header(_outFmtCtx, NULL);
	if (retVal < 0)
	{
		av_make_error_string(avebuf, AV_ERROR_MAX_STRING_SIZE, retVal);
		printf("Error occurred when writing file header: %s\n", avebuf);
		return 0x91;
	}
	
	_frameID = 0;
	return 0x00;
}

UINT8 ScrRec_StopVideoRec(void)
{
	if (_outFmtCtx != NULL)
	{
		avcodec_send_frame(_encCtx, NULL);
		ProcessFFmpegFrame();
		av_write_trailer(_outFmtCtx);
		
		avcodec_parameters_free(&_outStrm->codecpar);
		if (! (_outFmtCtx->oformat->flags & AVFMT_NOFILE))
			avio_closep(&_outFmtCtx->pb);
		avformat_free_context(_outFmtCtx);	_outFmtCtx = NULL;
	}
	
	avcodec_free_context(&_encCtx);
	
	return 0;
}

static void DeinitFFmpeg(void)
{
	av_frame_free(&_swFrame);	_swFrame = NULL;
	av_packet_free(&_pkt);		_pkt = NULL;
	sws_freeContext(_swsCtx);	_swsCtx = NULL;
	
	return;
}

static UINT8 CompressFFmpeg(const SCRREC_IMAGE* xImg)
{
	int retVal;
	uint8_t* srcImgPtrs[4];
	int srcImgStrides[4];
	
	if (xImg->width != _imgWidth)
	{
		printw("Image width mismatch!\n");
		return 0xA0;
	}
	if (xImg->height != _imgHeight)
	{
		printw("Image height mismatch!\n");
		return 0xA1;
	}
	retVal = av_image_fill_arrays(srcImgPtrs, srcImgStrides, (const uint8_t*)xImg->data,
			_srcPixFmt, _imgWidth, _imgHeight, xImg->align);
	if (retVal < 0)
	{
		printw("av_image_fill_arrays error\n");
		return 0xA2;
	}
	// convert image to correct colour format
	retVal = sws_scale(_swsCtx, (const uint8_t* const*)srcImgPtrs, srcImgStrides, 0, _imgHeight, _swFrame->data, _swFrame->linesize);
	if (retVal < 0)
	{
		printw("sws_scale error!\n");
		return 0xA3;
	}
	
	_swFrame->pts = _frameID;
	_frameID ++;
	retVal = avcodec_send_frame(_encCtx, _swFrame);
	if (retVal < 0)
	{
		printw("Error sending a frame for encoding\n");
		return 0xA4;
	}
	
	return 0x00;
}

static UINT8 ProcessFFmpegFrame(void)
{
	int retVal;
	
	while(1)
	{
		// get converted data from the encoder
		retVal = avcodec_receive_packet(_encCtx, _pkt);
		if (retVal == AVERROR(EAGAIN) || retVal == AVERROR_EOF)
			break;
		if (retVal < 0)
		{
			printw("avcodec_receive_packet error\n");
			return 0xA7;
		}
		
		// add a chunk of encoded data to the file writer
		//printw("Pkt PTS = %d, DTS = %d\n", _pkt->pts, _pkt->dts);
		_pkt->duration = 1;	// important! else the video duration will be off
		av_packet_rescale_ts(_pkt, _encCtx->time_base, _outStrm->time_base);
		_pkt->stream_index = 0;	// we only have 1 stream here
		retVal = av_interleaved_write_frame(_outFmtCtx, _pkt);
		av_packet_unref(_pkt);
	}
	
	return 0x00;
}
