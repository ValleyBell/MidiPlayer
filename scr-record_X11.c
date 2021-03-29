#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

#include <sys/types.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
//#include <X11/cursorfont.h>
#include <X11/Xutil.h>

#include <stdtype.h>
#include "scr-record.h"

struct _scr_rec_capture
{
	Display* dsp;
	Colormap colmap;
	Window activeWin;
	XImage* pixTestImg;
};


static Window GetProp_Window(Display* disp, Window win, const char* propName);
//static void* get_property(Display* disp, Window win, Atom propType, const char* propName, unsigned long* size);
static XImage* GetWindowImage(Display* dsp, Window win, const SCRREC_RECT* rect);


UINT8 ScrWin_Init(SCRREC_CAPTURE** retSC)
{
	SCRREC_CAPTURE sc;
	
	sc.pixTestImg = NULL;
	sc.dsp = XOpenDisplay(NULL);
	if (sc.dsp == NULL)
	{
		printf("Error opening display!\n");
		return 1;
	}
	sc.colmap = DefaultColormap(sc.dsp, DefaultScreen(sc.dsp));
	
	sc.activeWin = GetProp_Window(sc.dsp, DefaultRootWindow(sc.dsp), "_NET_ACTIVE_WINDOW");
	if (sc.activeWin == 0)
	{
		XCloseDisplay(sc.dsp);
		return 2;
	}
	
	*retSC = (SCRREC_CAPTURE*)calloc(1, sizeof(SCRREC_CAPTURE));
	**retSC = sc;
	return 0;
}

void ScrWin_Deinit(SCRREC_CAPTURE* sc)
{
	if (sc->pixTestImg != NULL)
		XFree(sc->pixTestImg);
	
	XCloseDisplay(sc->dsp);
	free(sc);
	
	return;
}

static Window GetProp_Window(Display* disp, Window win, const char* propName)
{
	Atom propNameA;
	Atom retPropType;
	int propBits;
	unsigned long nItems;
	unsigned long bytesLeft;
	Window* retWindow;
	Window winID;
	int retVal;
	
	propNameA = XInternAtom(disp, propName, False);
	retVal = XGetWindowProperty(disp, win, propNameA, 0x00, sizeof(Window) / 4, False,
			XA_WINDOW, &retPropType, &propBits, &nItems, &bytesLeft, (unsigned char**)&retWindow);
	if (retVal != Success)
		return 0;
	winID = (nItems >= 1) ? retWindow[0] : 0;
	free(retWindow);
	return winID;
}

#if 0
#define MAX_PROPERTY_VALUE_LEN 4096
static void* get_property(Display* disp, Window win, Atom propType, const char* propName, unsigned long* size)
{
	Atom propNameA;
	Atom retPropType;
	int propBits;
	unsigned long nItems;
	unsigned long bytesLeft;
	unsigned long propSize;
	unsigned char* propData;
	char* ret_data;
	int ret_val;
	
	propNameA = XInternAtom(disp, propName, False);
	
	// (MAX_PROPERTY_VALUE_LEN / 4) is correct due to a weird API design decision
	ret_val = XGetWindowProperty(disp, win, propNameA, 0x00, MAX_PROPERTY_VALUE_LEN / 4, False,
			propType, &retPropType, &propBits, &nItems, &bytesLeft, &propData);
	if (ret_val != Success)
	{
		printf("Cannot get %s property.\n", propName);
		return NULL;
	}
	
	if (retPropType != propType)
	{
		printf("Invalid type of %s property.\n", propName);
		XFree(propData);
		return NULL;
	}
	
	propSize = (propBits / 8) * nItems;
	if (propBits == 32)
		propSize *= sizeof(long) / 4;	// fix size of "32-bit" values on 64-bit systems
	
	ret_data = (char*)malloc(propSize + 1);
	memcpy(ret_data, propData, propSize);
	ret_data[propSize] = '\0';	// for easier string handling
	if (size != NULL)
		*size = propSize;
	
	XFree(propData);
	return ret_data;
}
#endif

static XImage* GetWindowImage(Display* dsp, Window win, const SCRREC_RECT* rect)
{
	if (rect != NULL)
	{
		return XGetImage(dsp, win, rect->x, rect->y, rect->width, rect->height, AllPlanes, ZPixmap);
	}
	else
	{
		XWindowAttributes winAttr;
		
		XGetWindowAttributes(dsp, win, &winAttr);
		return XGetImage(dsp, win, 0, 0, winAttr.width, winAttr.height, AllPlanes, ZPixmap);
	}
}

SCRREC_IMAGE ScrWin_Image(SCRREC_CAPTURE* sc, const SCRREC_RECT* rect)
{
	SCRREC_IMAGE result;
	XImage* xImg;
	
	XSync(sc->dsp, False);
	xImg = GetWindowImage(sc->dsp, sc->activeWin, rect);
	
	result.width = xImg->width;
	result.height = xImg->height;
	result.align = xImg->bitmap_unit / 8;
	result.data = (SCRREC_PIX*)xImg->data;
	result.internal = xImg;
	return result;
}

void ScrWin_FreeImage(SCRREC_IMAGE* si)
{
	XImage* xImg = (XImage*)si->internal;
	XDestroyImage(xImg);
	
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
	XColor pixColor;
	
	if (sc->pixTestImg == NULL)
		sc->pixTestImg = XGetImage(sc->dsp, sc->activeWin, x, y, 1, 1, AllPlanes, ZPixmap);
	else
		XGetSubImage(sc->dsp, sc->activeWin, x, y, 1, 1, AllPlanes, ZPixmap, sc->pixTestImg, 0, 0);
	pixColor.pixel = XGetPixel(sc->pixTestImg, 0, 0);
	pixColor.pixel &= 0x00FFFFFF;
	XQueryColor(sc->dsp, sc->colmap, &pixColor);
	
	result.bgr.reserved = 0x00;
	result.bgr.blue = pixColor.blue >> 8;
	result.bgr.green = pixColor.green >> 8;
	result.bgr.red = pixColor.red >> 8;
	return result;
}
