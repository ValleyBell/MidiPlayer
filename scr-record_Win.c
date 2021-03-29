#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include <stdtype.h>
#include "scr-record.h"

struct _scr_rec_capture
{
	HDC hdcDisp;
	HDC hdcImg;
	HWND activeWin;
};


static SCRREC_IMAGE GetWindowImage(HDC dispDC, HDC imgDC, const SCRREC_RECT* rect);


UINT8 ScrWin_Init(SCRREC_CAPTURE** retSC)
{
	SCRREC_CAPTURE sc;
	
	sc.activeWin = GetConsoleWindow();
	if (sc.activeWin == NULL)
		return 2;
	sc.hdcDisp = GetDC(sc.activeWin);
	sc.hdcImg = CreateCompatibleDC(sc.hdcDisp);
	if (sc.hdcImg == NULL)
	{
		ReleaseDC(sc.activeWin, sc.hdcDisp);
		return 1;
	}
	
	*retSC = (SCRREC_CAPTURE*)calloc(1, sizeof(SCRREC_CAPTURE));
	**retSC = sc;
	return 0;
}

void ScrWin_Deinit(SCRREC_CAPTURE* sc)
{
	DeleteDC(sc->hdcImg);
	ReleaseDC(sc->activeWin, sc->hdcDisp);
	free(sc);
	
	return;
}

static SCRREC_IMAGE GetScreenImage(HDC dispDC, HDC imgDC, int x, int y, int width, int height)
{
	BITMAPINFO bmpInfo;
	BITMAPINFOHEADER* biH;
	HBITMAP iBitmap;
	UINT32 retVal;
	DWORD retErr;
	SCRREC_IMAGE result;
	
	memset(&result, 0x00, sizeof(SCRREC_IMAGE));
	memset(&bmpInfo, 0x00, sizeof(BITMAPINFO));
	biH = &bmpInfo.bmiHeader;
	biH->biBitCount = 32;
	biH->biCompression = BI_RGB;
	biH->biPlanes = 1;
	biH->biSize = sizeof(BITMAPINFOHEADER);
	biH->biWidth = width;
	biH->biHeight = -height;
	
	result.width = width;
	result.height = height;
	result.align = 4;
	
	iBitmap = CreateDIBSection(imgDC, &bmpInfo, DIB_RGB_COLORS, (void**)&result.data, NULL, 0x00);
	if (iBitmap == NULL)
	{
		retErr = GetLastError();
		printf("Get Picture: CreateDIBSection failed!\tError %u\n", retErr);
		return result;
	}
	result.internal = iBitmap;
	
	SelectObject(imgDC, iBitmap);
	retVal = BitBlt(imgDC, 0, 0, width, height, dispDC, x, y, SRCCOPY);
	if (! retVal)
	{
		retErr = GetLastError();
		printf("Get Picture: BitBlt failed!\tError %u\n", retErr);
	}
	
	return result;
}

SCRREC_IMAGE ScrWin_Image(SCRREC_CAPTURE* sc, const SCRREC_RECT* rect)
{
	if (rect != NULL)
	{
		return GetScreenImage(sc->hdcDisp, sc->hdcImg, rect->x, rect->y, rect->width, rect->height);
	}
	else
	{
		RECT cRect;
		int width, height;
		GetClientRect(sc->activeWin, &cRect);	// get window size
		width = cRect.right - cRect.left;
		height = cRect.bottom - cRect.top;
		return GetScreenImage(sc->hdcDisp, sc->hdcImg, 0, 0, width, height);
	}
}

void ScrWin_FreeImage(SCRREC_IMAGE* si)
{
	HBITMAP iBitmap = (HBITMAP)si->internal;
	DeleteObject(iBitmap);
	
	si->data = NULL;
	si->internal = NULL;
	return;
}

SCRREC_PIX ScrWin_GetImagePixel(SCRREC_CAPTURE* sc, const SCRREC_IMAGE* img, int x, int y)
{
	int coord = y * img->width + x;
	return img->data[coord];
}

SCRREC_PIX ScrWin_GetScreenPixel(SCRREC_CAPTURE* sc, int x, int y)
{
	SCRREC_PIX result;
	COLORREF pixColor = GetPixel(sc->hdcDisp, x, y);
	result.bgr.reserved = (pixColor >> 24) & 0xFF;
	result.bgr.blue = (pixColor >> 16) & 0xFF;
	result.bgr.green = (pixColor >> 8) & 0xFF;
	result.bgr.red = (pixColor >> 0) & 0xFF;
	return result;
}
