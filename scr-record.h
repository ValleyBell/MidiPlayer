#ifndef __SCR_RECORD_H__
#define __SCR_RECORD_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdtype.h>

typedef struct _scr_rec_rectangle
{
	int x, y;
	int width, height;
} SCRREC_RECT;
typedef struct _pixel_bgr0
{
	UINT8 blue;
	UINT8 green;
	UINT8 red;
	UINT8 reserved;
} PIX_BGR0;
typedef struct _scr_rec_pixel
{
	union
	{
		PIX_BGR0 bgr;
		UINT32 u32;
	};
} SCRREC_PIX;
typedef struct _scr_rec_image
{
	int width, height, align;
	SCRREC_PIX* data;
	void* internal;	// for internal use
} SCRREC_IMAGE;


UINT8 ScrRec_InitCapture(void);
UINT8 ScrRec_GetWindowCoords(void);
UINT8 ScrRec_DeinitCapture(void);
UINT8 ScrRec_TakeAndSave(void);

UINT8 ScrRec_InitVideo(void);
UINT8 ScrRec_DeinitVideo(void);
UINT8 ScrRec_TestVideoRec(void);
UINT8 ScrRec_StartVideoRec(const char* fileName, int frameRate);
UINT8 ScrRec_StopVideoRec(void);


// internal functions
typedef struct _scr_rec_capture SCRREC_CAPTURE;

UINT8 ScrWin_Init(SCRREC_CAPTURE** retSC);
void ScrWin_Deinit(SCRREC_CAPTURE* sc);
SCRREC_IMAGE ScrWin_Image(SCRREC_CAPTURE* sc, const SCRREC_RECT* rect);
void ScrWin_FreeImage(SCRREC_IMAGE* si);
SCRREC_PIX ScrWin_GetImagePixel(SCRREC_CAPTURE* sc, const SCRREC_IMAGE* img, int x, int y);
SCRREC_PIX ScrWin_GetScreenPixel(SCRREC_CAPTURE* sc, int x, int y);

#ifdef __cplusplus
}
#endif

#endif	// __SCR_RECORD_H__
