#include <vector>
#include <memory>
#include <string>

#include <intrin.h>

#include <Windows.h>
#include <VersionHelpers.h>
#include <Shobjidl.h>

#include <shlwapi.h>
#include <commoncontrols.h>
#include <comip.h>
#include <comdef.h>

#include <RainmeterAPI.h>
#include <chameleon.h>
#include <chameleon_internal.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_MSC_SECURE_CRT
#define STBI_SSE2
#define STBI__X86_TARGET
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STBIR_SATURATE_INT
#include "stb_image_resize.h"

// An excessively large value I picked due to being a few orders of magnatude larger than the largest image I've seen (NASA Hubble image)
#define CROP_MAX_DIMENSION 16777215

enum MeasureType
{
	MEASURE_CONTAINER,
	MEASURE_BG1,
	MEASURE_BG2,
	MEASURE_FG1,
	MEASURE_FG2,
	MEASURE_AVG_LUM,
	MEASURE_AVG_COLOR,
	MEASURE_L1,
	MEASURE_L2,
	MEASURE_L3,
	MEASURE_L4,
	MEASURE_D1,
	MEASURE_D2,
	MEASURE_D3,
	MEASURE_D4
};

enum ImageType
{
	IMG_DESKTOP,
	IMG_FILE
};

struct Image
{
	void *rm;
	void *skin;
	HWND hWnd;
	std::wstring name;
	ImageType type;
	std::wstring path;
	FILETIME lastMod;
	RECT cropRect;
	RECT cachedCrop;
	LONG skinX;
	LONG skinY;
	bool draggingSkin;
	bool dirty;
	bool forceIcon;
	bool customCrop;
	bool contextAware;

	uint32_t bg1;
	uint32_t bg2;
	uint32_t fg1;
	uint32_t fg2;

	uint32_t l1;
	uint32_t l2;
	uint32_t l3;
	uint32_t l4;

	uint32_t d1;
	uint32_t d2;
	uint32_t d3;
	uint32_t d4;

	uint32_t fallback_bg1;
	uint32_t fallback_bg2;
	uint32_t fallback_fg1;
	uint32_t fallback_fg2;

	float lum;
	uint32_t avg;
};

struct Measure
{
	MeasureType type;
	std::shared_ptr<Image> parent;
	bool useHex;
	std::wstring value;
};

/*
[ChameleonDesktop]
Measure=Plugin
Plugin=Chameleon
Type=Desktop

[DesktopBG1]
Measure=Plugin
Plugin=Chameleon
Parent=ChameleonDesktop
Color=Background1

[ChameleonFile]
Measure=Plugin
Plugin=Chameleon
Type=File
Path=C:\Path\To\Image

[FileBG1]
Measure=Plugin
Plugin=Chameleon
Parent=ChameleonFile
Color=Background1

*/

std::vector< std::weak_ptr<Image> > images;
static const WCHAR whex[] = L"0123456789ABCDEF";

void SampleImage(std::shared_ptr<Image> img);
PLUGIN_EXPORT void Initialize(void* *data, void *rm);
PLUGIN_EXPORT void Reload(void *data, void *rm, double *maxVal);
PLUGIN_EXPORT double Update(void *data);
PLUGIN_EXPORT LPCWSTR GetString(void *data);
PLUGIN_EXPORT void Finalize(void *data);

_COM_SMARTPTR_TYPEDEF(IImageList, __uuidof(IImageList));

uint32_t* loadIcon(const wchar_t *path, int *w, int *h)
{
	// Outputs
	uint32_t *imgData = nullptr;
	*w = -1;
	*h = -1;

	// Internal stuff
	HICON hIcon = nullptr;
	ICONINFO icon;
	IImageListPtr spiml;
	BITMAPINFO bmi;
	BITMAP bm;
	HDC hDC = GetDC(NULL);
	SHFILEINFOW info = { 0 };
	int iconSize = SHIL_EXTRALARGE;
	if (IsWindowsVistaOrGreater())
	{
		iconSize = SHIL_JUMBO;
	}

	// These assume unconditional success.
	// I'll fix them if they ever report breakage
	// I was dreading this part until I found out it's
	// literally three functions.
	SHGetFileInfoW(path, 0, &info, sizeof(info), SHGFI_SYSICONINDEX);
	SHGetImageList(iconSize, IID_PPV_ARGS(&spiml));
	spiml->GetIcon(info.iIcon, ILD_TRANSPARENT, &hIcon);

	// Load icon image data
	GetIconInfo(hIcon, &icon);

	// I'm not even going to bother with monochrome icons
	// Though I'm sure most of the time the defaults work anyway...
	if (icon.hbmColor != 0)
	{
		// Lots of roundabout fun to get the data we want


		// First, the size
		GetObject(icon.hbmColor, sizeof(BITMAP), &bm);
		*w = bm.bmWidth;
		*h = bm.bmHeight;

		// Now the bits!
		bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
		bmi.bmiHeader.biWidth = *w;
		bmi.bmiHeader.biHeight = -(*h);
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		bmi.bmiHeader.biSizeImage = 0;
		bmi.bmiHeader.biXPelsPerMeter = 0;
		bmi.bmiHeader.biYPelsPerMeter = 0;
		bmi.bmiHeader.biClrUsed = 0;
		bmi.bmiHeader.biClrImportant = 0;

		imgData = (uint32_t*)stbi__malloc((*w) * (*h) * sizeof(uint32_t));
		if (GetDIBits(hDC, icon.hbmColor, 0, *h, imgData, &bmi, DIB_RGB_COLORS) != *h)
		{
			// Error!
			*w = -1;
			*h = -1;
			stbi_image_free(imgData);
			imgData = nullptr;

			// Fall through to normal cleanup
		}
	}

	// Cleanup!
	ReleaseDC(NULL, hDC);

	if (icon.hbmColor)
	{
		DeleteObject(icon.hbmColor);
	}

	DeleteObject(icon.hbmMask);

	DestroyIcon(info.hIcon);

	return imgData;
}

inline void useDefaultColors(std::shared_ptr<Image> img)
{
   img->bg1 = img->fallback_bg1;
   img->bg2 = img->fallback_bg2;
   img->fg1 = img->fallback_fg1;
   img->fg2 = img->fallback_fg2;

   // TODO: Actually find the brightness of these to sort it properly:
   img->l1 = img->d4 = img->bg1;
   img->l2 = img->d3 = img->bg2;
   img->l3 = img->d2 = img->fg1;
   img->l4 = img->d1 = img->fg2;

   img->lum = 1.0f;
   img->avg = 0xFFFFFFFF;
}

void processRGB(uint32_t color, ColorStat *colorStat)
{
	__m128 rgbc;
	rgbc = _mm_set_ps(1.0f, static_cast<float>((color & 0x00FF0000) >> 16), static_cast<float>((color & 0x0000FF00) >> 8), static_cast<float>(color & 0x000000FF));
	rgbc = _mm_div_ps(rgbc, _mm_set_ps(1, 255.0f, 255.0f, 255.0f));

	colorStat->rgbc = _mm_add_ps(rgbc, colorStat->rgbc);
}

uint32_t* cropImage(uint32_t *imgData, int *oldW, int *oldH, const RECT *cropRect)
{
	if (cropRect->left == 0 && cropRect->right >= *oldW && cropRect->top == 0 && cropRect->bottom >= *oldH)
	{
		return imgData;
	}

	if (cropRect->left > *oldW || cropRect->top > *oldH)
	{
		RmLog(LOG_ERROR, L"Cropping out of bounds of image! Check your parameters.");
		return imgData;
	}

	int newW, newH;
	newW = (cropRect->right <= *oldW ? cropRect->right : *oldW) - cropRect->left;
	newH = (cropRect->bottom <= *oldH ? cropRect->bottom : *oldH) - cropRect->top;

	uint32_t *result = (uint32_t*)stbi__malloc(newW * newH * sizeof(uint32_t));

	for (int i = 0; i < newH; ++i)
	{
		memcpy(&result[i * newW], &imgData[cropRect->left + ((i + cropRect->top) * (*oldW))], newW * sizeof(uint32_t));
	}

	*oldW = newW;
	*oldH = newH;

	return result;
}

void SampleImage(std::shared_ptr<Image> img)
{
	bool isIcon = false;
	RECT monitorRect;
	RECT skinRect = { 0 };
	MONITORINFO monInf;
	monInf.cbSize = sizeof(MONITORINFO);

	// If we're sampling the desktop, grab that
	if (img->type == IMG_DESKTOP)
	{
		std::wstring path = L"";

		// First, get the info we need from the old API

		HMONITOR mon = MonitorFromWindow(img->hWnd, MONITOR_DEFAULTTONEAREST);
		GetMonitorInfo(mon, &monInf);

		if (IsWindows8OrGreater())
		{
			// Use the multi-desktop fun version!

			// Set up the new API

			LPWSTR monPath;
			LPWSTR wallPath;

			// Because making these just a few loose functions would have been too easy
			IDesktopWallpaper *wp = nullptr;

			CoCreateInstance(__uuidof(DesktopWallpaper), NULL, CLSCTX_ALL, IID_PPV_ARGS(&wp));
			if (wp == nullptr)
			{
				// We couldn't get the wallpaper info. Just use the fallback colors
				useDefaultColors(img);

				img->dirty = false;

				return;
			}

			// Count the monitors
			UINT monCount = 0;
			wp->GetMonitorDevicePathCount(&monCount);

			// Compare the rectangles

			for (UINT i = 0; i < monCount; ++i)
			{
				if (wp->GetMonitorDevicePathAt(i, &monPath) == S_OK)
				{
					RECT testRect;
					wp->GetMonitorRECT(monPath, &testRect);

					if (testRect.left == monInf.rcMonitor.left && testRect.top == monInf.rcMonitor.top)
					{
						// It's the monitor we're looking for!
						wp->GetWallpaper(monPath, &wallPath);
						path = wallPath;
						CoTaskMemFree(wallPath);
						CoTaskMemFree(monPath);
						break;
					}

					CoTaskMemFree(monPath);
				}
			}

			// Clean up
			wp->Release();
		}
		else
		{
			// Use the "boring" WinXP - Win7 version
			WCHAR wallPath[MAX_PATH + 1];
			ZeroMemory((void*)wallPath, sizeof(WCHAR) * (MAX_PATH + 1));

			if (SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, (void*)wallPath, 0))
			{
				path = wallPath;
			}
			else
			{
				path = L"";
			}
		}

		// First, is the current path the same as the new path
		if (img->path.compare(path) != 0)
		{
			// It's different, mark it dirty!
			img->path = path;
			img->dirty = true;
		}

		// Prepare cropping info
		monitorRect.left = 0;
		monitorRect.top = 0;
		monitorRect.right = monInf.rcMonitor.right - monInf.rcMonitor.left;
		monitorRect.bottom = monInf.rcMonitor.bottom - monInf.rcMonitor.top;

		// Get the actual area the skin is in, relative to the monitor
		GetWindowRect(img->hWnd, &skinRect);

		// Mark the skin as dragging until there is no movement
		if (img->skinX != skinRect.left || img->skinY != skinRect.top)
		{
			img->skinX = skinRect.left;
			img->skinY = skinRect.top;
			img->draggingSkin = true;
		}
		else if (img->draggingSkin)
		{
			img->draggingSkin = false;
			img->dirty = true;
		}
	}

	// Determine if the skin's cropping area has changed
	if (img->customCrop && !EqualRect(&img->cropRect, &img->cachedCrop))
	{
		img->cachedCrop = img->cropRect;
		img->dirty = true;
	}

	// Let's check the path
	if (img->path.empty())
	{
		// Empty path! Let's dump some default values and be done with it...
		useDefaultColors(img);

		img->dirty = false;

		return;
	}

	// Now the "file modified" time
	HANDLE file = CreateFileW(img->path.c_str(), 0, FILE_SHARE_DELETE | FILE_SHARE_WRITE | FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		// Couldn't get the last accessed time for the file.
		std::wstring debug = L"Chameleon: Could not get handle on ";
		debug += img->path;
		RmLog(LOG_ERROR, debug.c_str());

		useDefaultColors(img);

		img->dirty = false;

		return;
	}

	FILETIME ft;
	GetFileTime(file, NULL, NULL, &ft);
	CloseHandle(file);

	if (ft.dwHighDateTime != img->lastMod.dwHighDateTime || ft.dwLowDateTime != img->lastMod.dwLowDateTime)
	{
		// Different modification times...
		img->lastMod.dwHighDateTime = ft.dwHighDateTime;
		img->lastMod.dwLowDateTime = ft.dwLowDateTime;
		img->dirty = true;
	}

	if (img->dirty)
	{
		std::wstring debug = L"Chameleon: Updating colors based on ";
		debug += img->path;
		RmLog(LOG_DEBUG, debug.c_str());

		int w, h, n;
		uint32_t *imgData = nullptr;
		ColorStat spotAverage = { 0 };

		// If we're reading from the desktop, read from Windows, not the file
		if (img->type == IMG_DESKTOP)
		{
			// Get the Real Device Context, then get a non-live copy to read from
			HDC hdcDesktop = GetDC(GetShellWindow());
			HDC hdc = CreateCompatibleDC(hdcDesktop);

			// Create the bitmap we're going to bounce the image data into, and the immediately out of
			// because it's in a device-specific format
			// (maybe 6-bit, maybe 8-bit, maybe 10-bit, rgb, bgrx, rgbx, who knows!)
			HBITMAP hBmp = CreateCompatibleBitmap(hdcDesktop, monitorRect.right, monitorRect.bottom);

			// do the actual copy, but save the default hbmp windows set up that we can't exactly use for this
			// so we can let windows clean that up later
			HBITMAP hOldBmp = (HBITMAP)SelectObject(hdc, hBmp);

			// TODO: Negative coordinates? (i.e. due to odd monitor arrangements)
			int xRef = GetSystemMetrics(SM_XVIRTUALSCREEN);
			int yRef = GetSystemMetrics(SM_YVIRTUALSCREEN);

			BitBlt(hdc, 0, 0, monitorRect.right, monitorRect.bottom, hdcDesktop, monInf.rcMonitor.left - xRef, monInf.rcMonitor.top - yRef, SRCCOPY);

			// Windows doesn't like having the handle selected when we want to read from it,
			// so now we put the old handle back 
			SelectObject(hdc, hOldBmp);

			// Now we have the data in a device specific buffer that Windows won't touch while we're using it
			// so let's convert that to a format we can actually use

			LPBITMAPINFO bmpInfo = (LPBITMAPINFO)malloc(sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD));
			ZeroMemory(&bmpInfo->bmiHeader, sizeof(BITMAPINFOHEADER));
			bmpInfo->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);

			// Get what Windows wants us to allocate, and do so
			GetDIBits(hdc, hBmp, 0, monitorRect.bottom, NULL, bmpInfo, DIB_RGB_COLORS);
			uint8_t *byteData = (uint8_t*)malloc(bmpInfo->bmiHeader.biSizeImage);

			// Now do the actual copy
			GetDIBits(hdc, hBmp, 0, monitorRect.bottom, byteData, bmpInfo, DIB_RGB_COLORS);

			w = bmpInfo->bmiHeader.biWidth;
			h = bmpInfo->bmiHeader.biHeight;

			// Windows uses a negative value to specify the bitmap is flipped
			// but we don't care about the orientation because it doesn't matter
			if (h < 0)
				h = -h;

			// We're going to be doing this by bytes so it'll be easier
			imgData = (uint32_t*)stbi__malloc(4 * w * h);
			
			// At this point, we have the color data but probably not in the layout we are expecting...
			// (i.e. BGRX instead of RGBX)
			// We need to figure out what it *is* so we can convert it to what we *want*
			// It should be one of 3 possibilities: 16, 24, or 32-bit
			// Of those, 16 and 32-bit can be either BI_RGB or BI_BITFIELDS
			// If they are BI_BITFIELDS we need to use the info in bmpInfo->bmiColors[0,1,2]
			// to decode where the R, G. and B bits actually are.
			
			// For 32-bit this is easy and we can actually basically use the same code path for BI_RGB and BI_BITFIELDS.

			// These are AFTER shifting the data down to LSB
			uint32_t rMask = 0x000000FF;
			uint32_t gMask = 0x000000FF;
			uint32_t bMask = 0x000000FF;

			// These are how much to shift to get to LSB
			unsigned long rShift = 16;
			unsigned long gShift =  8;
			unsigned long bShift =  0;
			size_t pixelBytes = bmpInfo->bmiHeader.biBitCount / 8;

			// Anything that is BI_RGB follows a standard format, with only 16-bit (2-byte)
			// images requiring special treatment. HOWEVER anything BI_BITFIELDS has to be
			// masked and shifted according to what the OS says. It can all be treated the same
			// by the following code due to how it ends up not caring about details.

			if (bmpInfo->bmiHeader.biCompression == BI_BITFIELDS)
			{
				// Could be R5G5B5, X8R8G8B8, R5G6B5, or anything really.
				// Let's figure it out

				// Get the actual masks. These are shifted, so we'll need to unshift them
				// (and count by how much for later)
				rMask = ((uint32_t*)bmpInfo->bmiColors)[0];
				gMask = ((uint32_t*)bmpInfo->bmiColors)[1];
				bMask = ((uint32_t*)bmpInfo->bmiColors)[2];

				// Count the least significant zeros (how much to shift the masks)
				_BitScanForward(&rShift, rMask);
				_BitScanForward(&gShift, gMask);
				_BitScanForward(&bShift, bMask);

				rMask >>= rShift;
				gMask >>= gShift;
				bMask >>= bShift;
			}
			else if (pixelBytes == 2)
			{
				// Standard R5G5B5

				rMask = 0x0000001F;
				gMask = 0x0000001F;
				bMask = 0x0000001F;

				rShift = 10;
				gShift =  5;
				bShift =  0;
			}
			
			uint8_t r, g, b;
			uint32_t pixel;
			for (size_t i = 0; i < w * h; ++i)
			{
				// Augh...
				// We want the data of the whole pixel we're at, as a uint32_t
				// If the data is smaller than 32-bit the extra will automatically
				// be ignored by the shift and masking we're about to do
				pixel = *((uint32_t*)&byteData[i * pixelBytes]);

				r = (pixel >> rShift) & rMask;
				g = (pixel >> gShift) & gMask;
				b = (pixel >> bShift) & bMask;

				imgData[i] = 0xFF000000 | (b << 16) | (g << 8) | r;
			}
			
			n = 4;

			//stbi_write_png("chamdebug.png", w, h, n, imgData, w*n);

			// Now we need to create an average for the region of the skin
			int cW = w, cH = h;
			

			skinRect.left -= monInf.rcMonitor.left;
			skinRect.right -= monInf.rcMonitor.left;
			skinRect.top -= monInf.rcMonitor.top;
			skinRect.bottom -= monInf.rcMonitor.top;

			if (bmpInfo->bmiHeader.biHeight > 0)
			{
				// Oh, we're vertically flipped.
				// Gotta "flip" the rect to match (actually just shifting it)
				LONG bottom = skinRect.bottom;
				skinRect.bottom = h - skinRect.top;
				skinRect.top = h - bottom;
			}
			
			if (img->contextAware)
			{
				uint32_t *skinRegion = cropImage(imgData, &cW, &cH, &skinRect);

				for (size_t i = 0; i < cW * cH; ++i)
				{
					processRGB(skinRegion[i], &spotAverage);
				}

				fixRGB(&spotAverage, (float)cW * cH);
				calcYUV(&spotAverage, 1);

				if (skinRegion != imgData)
					stbi_image_free(skinRegion);
			}

			DeleteObject(hBmp);
			DeleteDC(hdc);
			ReleaseDC(GetDesktopWindow(), hdcDesktop);
			free(bmpInfo);
			free(byteData);
		}
		else
		{
			// Convert the path

			FILE *fp;

			if (_wfopen_s(&fp, img->path.c_str(), L"rb") != 0)
			{
				// Something goofed, but we can try again
				return;
			}

			// Load image data
			imgData = (uint32_t*)stbi_load_from_file(fp, &w, &h, &n, 4);

			fclose(fp);
		}

		if (imgData == nullptr)
		{
			RmLog(LOG_ERROR, L"Could not load file!");

			imgData = loadIcon(img->path.c_str(), &w, &h);

			if (imgData == nullptr)
			{
				// It's something we don't actually know how to handle, so let's not.
				useDefaultColors(img);

				img->dirty = false;

				return;
			}

			// RGB swap image data?
			uint32_t temp = 0;
			size_t area = w * h;
			for (size_t i = 0; i < area; ++i)
			{
				temp = imgData[i];
				imgData[i] = (temp & 0xFF000000) | ((temp & 0x00FF0000) >> 16) | (temp & 0x0000FF00) | ((temp & 0x000000FF) << 16);
			}

			isIcon = true;
		}

		isIcon |= img->forceIcon;

		//  Crop image as requested
		if (img->customCrop)
		{
			uint32_t *croppedData = cropImage(imgData, &w, &h, &img->cropRect);
			if (croppedData != imgData)
			{
				stbi_image_free(imgData);
				imgData = croppedData;
			}
		}

		// Resize image for Chameleon
		if (w > 256 || h > 256)
		{
			int newWidth = (w < 256 ? w : 256);
			int newHeight = (h < 256 ? h : 256);
			uint32_t *resizedData = static_cast<uint32_t*>(stbi__malloc(newWidth * newHeight * sizeof(uint32_t)));

			stbir_resize_uint8_generic(reinterpret_cast<unsigned char*>(imgData), w, h, 0, reinterpret_cast<unsigned char*>(resizedData), newWidth, newHeight, 0, 4, -1, 0, STBIR_EDGE_CLAMP, STBIR_FILTER_BOX, STBIR_COLORSPACE_LINEAR, NULL);

			stbi_image_free(imgData);

			imgData = resizedData;
			w = newWidth;
			h = newHeight;
		}

		// Run through Chameleon
		Chameleon *chameleon = createChameleon();

		chameleonProcessImage(chameleon, imgData, w, h, isIcon);

		if (isIcon)
		{
			chameleonFindKeyColors(chameleon, chameleonDefaultIconParams(), false);
		}
		else
		{
			chameleonFindKeyColors(chameleon, chameleonDefaultImageParams(), true);
		}

		// Swap the colors because the byte ordering is opposite that of what we want to give Rainmeter...
		img->bg1 = _byteswap_ulong(chameleonGetColor(chameleon, CHAMELEON_BACKGROUND1)) | 0xFF;
		img->bg2 = _byteswap_ulong(chameleonGetColor(chameleon, CHAMELEON_BACKGROUND2)) | 0xFF;
		img->fg1 = _byteswap_ulong(chameleonGetColor(chameleon, CHAMELEON_FOREGROUND1)) | 0xFF;
		img->fg2 = _byteswap_ulong(chameleonGetColor(chameleon, CHAMELEON_FOREGROUND2)) | 0xFF;

		img->l1 = _byteswap_ulong(chameleonGetColor(chameleon, CHAMELEON_LIGHT1)) | 0xFF;
		img->l2 = _byteswap_ulong(chameleonGetColor(chameleon, CHAMELEON_LIGHT2)) | 0xFF;
		img->l3 = _byteswap_ulong(chameleonGetColor(chameleon, CHAMELEON_LIGHT3)) | 0xFF;
		img->l4 = _byteswap_ulong(chameleonGetColor(chameleon, CHAMELEON_LIGHT4)) | 0xFF;

		img->d1 = _byteswap_ulong(chameleonGetColor(chameleon, CHAMELEON_DARK1)) | 0xFF;
		img->d2 = _byteswap_ulong(chameleonGetColor(chameleon, CHAMELEON_DARK2)) | 0xFF;
		img->d3 = _byteswap_ulong(chameleonGetColor(chameleon, CHAMELEON_DARK3)) | 0xFF;
		img->d4 = _byteswap_ulong(chameleonGetColor(chameleon, CHAMELEON_DARK4)) | 0xFF;

		img->avg = _byteswap_ulong(chameleonGetColor(chameleon, CHAMELEON_AVERAGE)) | 0xFF;

		img->lum = chameleonGetLuminance(chameleon, CHAMELEON_AVERAGE);

		if (img->contextAware && img->type == IMG_DESKTOP)
		{
			// Find which value is closest to the average value for the skin
			ColorStat bg1 = { 0 };
			ColorStat bg2 = { 0 };
			ColorStat fg1 = { 0 };
			ColorStat fg2 = { 0 };

			processRGB(chameleonGetColor(chameleon, CHAMELEON_BACKGROUND1), &bg1);
			calcYUV(&bg1, 1);
			processRGB(chameleonGetColor(chameleon, CHAMELEON_BACKGROUND2), &bg2);
			calcYUV(&bg2, 1);
			processRGB(chameleonGetColor(chameleon, CHAMELEON_FOREGROUND1), &fg1);
			calcYUV(&fg1, 1);
			processRGB(chameleonGetColor(chameleon, CHAMELEON_FOREGROUND2), &fg2);
			calcYUV(&fg2, 1);

			float bg1Dist = distance(&spotAverage, &bg1);
			float bg2Dist = distance(&spotAverage, &bg2);
			float fg1Dist = distance(&spotAverage, &fg1);
			float fg2Dist = distance(&spotAverage, &fg2);

			uint32_t sbg1 = img->bg1;
			uint32_t sbg2 = img->bg2;
			uint32_t sfg1 = img->fg1;
			uint32_t sfg2 = img->fg2;
			// Set the background/foreground based on that
			if ((bg2Dist < bg1Dist) && (bg2Dist < fg1Dist) && (bg2Dist < fg2Dist))
			{
				// BG2 is the closest match for the background under the skin, use that instead
				
				img->bg1 = sbg2;
				img->bg2 = sbg1;
			}
			else if ((fg1Dist < bg1Dist) && (fg1Dist < fg2Dist))
			{
				// FG1 is the closest match, swap the FG and BG
				img->bg1 = sfg1;
				img->bg2 = sfg2;
				img->fg1 = sbg1;
				img->fg2 = sbg2;
			}
			else if (fg2Dist < bg1Dist)
			{
				// FG2 is the closest match
				img->bg1 = sfg2;
				img->bg2 = sfg1;
				img->fg1 = sbg1;
				img->fg2 = sbg2;
			}
		}

		destroyChameleon(chameleon);
		chameleon = nullptr;

		stbi_image_free(imgData);

		img->dirty = false;
	}
}

PLUGIN_EXPORT void Initialize(void* *data, void *rm)
{
	Measure* measure = new Measure;
	measure->type = MEASURE_CONTAINER;
	measure->parent = nullptr;
	*data = measure;
}

bool RmReadBool(void *rm, LPCWSTR option, bool defValue, BOOL replaceMeasures = 1)
{
	std::wstring value = RmReadString(rm, option, defValue? L"1" : L"0", replaceMeasures);
	// Anything that isn't an explicit "false" value should be considered true
	bool result = true;
	if (value.empty() || value.compare(L"0") == 0 ||
		value.compare(L"False") == 0 || value.compare(L"false") == 0 ||
		value.compare(L"No") == 0 || value.compare(L"no") == 0 ||
		value.compare(L"F") == 0 || value.compare(L"f") == 0 ||
		value.compare(L"N") == 0 || value.compare(L"n") == 0)
		result = false;

	return result;
}

uint32_t RmReadColor(void *rm, LPCWSTR option, uint32_t defValue, BOOL replaceMeasures = 1)
{
	std::wstring value = RmReadString(rm, option, L"", replaceMeasures);

	if (value.empty())
	{
		return defValue;
	}
	
	return (std::stoi(value, 0, 16) << 8) | 0xFF;
}

PLUGIN_EXPORT void Reload(void *data, void *rm, double *maxVal)
{
	Measure *measure = static_cast<Measure*>(data);

	void *skin = RmGetSkin(rm);

	std::wstring parent = RmReadString(rm, L"Parent", L"");

	if (parent.empty())
	{
		std::wstring type = RmReadString(rm, L"Type", L"Desktop");
		std::wstring path = RmReadPath(rm, L"Path", L"");
		bool desktopCrop = RmReadBool(rm, L"CropDesktop", true);
		bool forceIcon = RmReadBool(rm, L"ForceIcon", false);
		bool contextAware = RmReadBool(rm, L"ContextAwareColors", true);

		uint32_t fallback_bg1 = RmReadColor(rm, L"FallbackBG1", 0xFFFFFFFF);
		uint32_t fallback_bg2 = RmReadColor(rm, L"FallbackBG2", fallback_bg1);
		uint32_t fallback_fg1 = RmReadColor(rm, L"FallbackFG1", 0x000000FF);
		uint32_t fallback_fg2 = RmReadColor(rm, L"FallbackFG2", fallback_fg1);

		int cropX = RmReadInt(rm, L"CropX", 0);
		int cropY = RmReadInt(rm, L"CropY", 0);
		int cropW = RmReadInt(rm, L"CropW", CROP_MAX_DIMENSION);
		int cropH = RmReadInt(rm, L"CropH", CROP_MAX_DIMENSION);

		bool customCrop = !(cropX == 0 && cropY == 0 && cropW == CROP_MAX_DIMENSION && cropH == CROP_MAX_DIMENSION);

		// Does the measure already have a parent?
		std::shared_ptr<Image> img;

		if (measure->parent != nullptr)
		{
			// Refresh it!
			img = measure->parent;
		}
		else
		{
			img = std::make_shared<Image>();
			img->rm = rm;
			img->name = RmGetMeasureName(rm);
			img->skin = skin;
			img->hWnd = RmGetSkinWindow(rm);

			img->lastMod.dwHighDateTime = img->lastMod.dwLowDateTime = 0;

			measure->type = MEASURE_CONTAINER;
			measure->parent = img;

			images.push_back(img);

			img->bg1 = img->bg2 = img->fg1 = img->fg2 = 0x000000FF;

			img->customCrop = false;

			img->skinX = 0;
			img->skinY = 0;
			img->draggingSkin = false;

			// We'll need to reload the color data...
			img->dirty = true;

			std::wstring debug = L"Chameleon: Created container ";
			debug += RmGetMeasureName(rm);
			RmLog(LOG_DEBUG, debug.c_str());
		}

		if (type.compare(L"Desktop") == 0)
		{
			img->type = IMG_DESKTOP;
			img->forceIcon = false;

			// Define a "custom" crop of max-size to prevent the default
			// "crop to monitor size" behavior but only use it if
			// CropDesktop is anything other than 0
			img->cropRect.left = img->cropRect.top = 0;
			img->cropRect.right = img->cropRect.bottom = LONG_MAX;
			img->customCrop = !desktopCrop;
		}
		else if (type.compare(L"File") == 0)
		{
			img->type = IMG_FILE;
			
			if (path.compare(img->path) != 0)
			{
				img->path = path;
				img->dirty = true;
			}

			img->forceIcon = forceIcon;
		}
		else
		{
			RmLog(LOG_ERROR, L"Chameleon: Invalid Type=");
			return;
		}

		// Shuffle the colors based on the average color
		// of the area under the skin
		img->contextAware = contextAware;

		img->fallback_bg1 = fallback_bg1;
		img->fallback_bg2 = fallback_bg2;
		img->fallback_fg1 = fallback_fg1;
		img->fallback_fg2 = fallback_fg2;

		// Grab cropping info
		img->cropRect.left = cropX;
		img->cropRect.top = cropY;
		img->cropRect.right = cropW + cropX;
		img->cropRect.bottom = cropH + cropY;

		// Use the cropping info if it's not the defaults
		img->customCrop = customCrop;

		Update(data);
	}
	else
	{
		for (std::weak_ptr<Image> imgPtr : images)
		{
			if (!imgPtr.expired())
			{
				std::shared_ptr<Image> img = imgPtr.lock();
				if (img->skin == skin && img->name.compare(parent) == 0)
				{
					measure->parent = img;

					std::wstring debug = L"Measure ";
					debug += RmGetMeasureName(rm);
					debug += L" has a parent of ";
					debug += measure->parent->name;
					debug += L" and is exposing the color ";

					std::wstring color = RmReadString(rm, L"Color", L"");
					debug += color;
					RmLog(LOG_DEBUG, debug.c_str());

					if (color.compare(L"Background1") == 0)
					{
						measure->type = MEASURE_BG1;
					}
					else if (color.compare(L"Background2") == 0)
					{
						measure->type = MEASURE_BG2;
					}
					else if (color.compare(L"Foreground1") == 0)
					{
						measure->type = MEASURE_FG1;
					}
					else if (color.compare(L"Foreground2") == 0)
					{
						measure->type = MEASURE_FG2;
					}
					else if (color.compare(L"Luminance") == 0)
					{
						measure->type = MEASURE_AVG_LUM;
					}
					else if (color.compare(L"Average") == 0)
					{
						measure->type = MEASURE_AVG_COLOR;
					}
					else if (color.compare(L"Light1") == 0)
					{
						measure->type = MEASURE_L1;
					}
					else if (color.compare(L"Light2") == 0)
					{
						measure->type = MEASURE_L2;
					}
					else if (color.compare(L"Light3") == 0)
					{
						measure->type = MEASURE_L3;
					}
					else if (color.compare(L"Light4") == 0)
					{
						measure->type = MEASURE_L4;
					}
					else if (color.compare(L"Dark1") == 0)
					{
						measure->type = MEASURE_D1;
					}
					else if (color.compare(L"Dark2") == 0)
					{
						measure->type = MEASURE_D2;
					}
					else if (color.compare(L"Dark3") == 0)
					{
						measure->type = MEASURE_D3;
					}
					else if (color.compare(L"Dark4") == 0)
					{
						measure->type = MEASURE_D4;
					}
					else
					{
						RmLog(LOG_ERROR, L"Chameleon: Invalid Color=");
						return;
					}

					std::wstring useHex = RmReadString(rm, L"Format", L"Hex");

					if (useHex.compare(L"Hex") == 0)
					{
						measure->useHex = true;
					}
					else if (useHex.compare(L"Dec") == 0)
					{
						measure->useHex = false;
					}
					else
					{
						measure->useHex = true;
						RmLog(LOG_ERROR, L"Chameleon: Invalid Format=");
						return;
					}

					return;
				}
			}
		}

		// If we got here, we didn't find the parent
		std::wstring err = L"Chameleon: Invalid Parent=";
		err += parent;
		err += L" in ";
		err += RmGetMeasureName(rm);
		RmLog(LOG_ERROR, err.c_str());
	}
}

PLUGIN_EXPORT double Update(void *data)
{
	Measure *measure = static_cast<Measure*>(data);

	// Containers are the "parent" type, so they get to be the ones that actually update
	if (measure->type == MEASURE_CONTAINER)
	{
		// Update the path
		if (measure->parent->type == IMG_FILE)
		{
			std::wstring newPath = RmReadPath(measure->parent->rm, L"Path", L"");
			if (measure->parent->path.compare(newPath) != 0)
			{
				// Different path, need to update image
				measure->parent->path = newPath;
				measure->parent->dirty = true;
			}
		}

		int cropX = RmReadInt(measure->parent->rm, L"CropX", 0);
		int cropY = RmReadInt(measure->parent->rm, L"CropY", 0);
		int cropW = RmReadInt(measure->parent->rm, L"CropW", CROP_MAX_DIMENSION);
		int cropH = RmReadInt(measure->parent->rm, L"CropH", CROP_MAX_DIMENSION);

		bool customCrop = !(cropX == 0 && cropY == 0 && cropW == CROP_MAX_DIMENSION && cropH == CROP_MAX_DIMENSION);

		// Grab cropping info
		measure->parent->cropRect.left = cropX;
		measure->parent->cropRect.top = cropY;
		measure->parent->cropRect.right = cropW + cropX;
		measure->parent->cropRect.bottom = cropH + cropY;

		// Use the cropping info if it's not the defaults
		measure->parent->customCrop = customCrop;

		SampleImage(measure->parent);
	}
	else
	{
		// We're updating a child measure, it just needs to format the value from the parent
		uint32_t value = 0;

		if (measure->type == MEASURE_AVG_LUM)
		{
			return measure->parent->lum;
		}

		// Choose the right value
		switch (measure->type)
		{
		case MEASURE_BG1:
			value = measure->parent->bg1;
			break;
		case MEASURE_BG2:
			value = measure->parent->bg2;
			break;
		case MEASURE_FG1:
			value = measure->parent->fg1;
			break;
		case MEASURE_FG2:
			value = measure->parent->fg2;
			break;

		case MEASURE_AVG_COLOR:
			value = measure->parent->avg;
			break;

		case MEASURE_L1:
			value = measure->parent->l1;
			break;
		case MEASURE_L2:
			value = measure->parent->l2;
			break;
		case MEASURE_L3:
			value = measure->parent->l3;
			break;
		case MEASURE_L4:
			value = measure->parent->l4;
			break;

		case MEASURE_D1:
			value = measure->parent->d1;
			break;
		case MEASURE_D2:
			value = measure->parent->d2;
			break;
		case MEASURE_D3:
			value = measure->parent->d3;
			break;
		case MEASURE_D4:
			value = measure->parent->d4;
			break;
		}

		// Lop off the alpha
		value >>= 8;

		measure->value.clear();

		// Which format?
		if (measure->useHex)
		{
			for (size_t i = 0; i < 6; ++i) { measure->value += whex[(value >> (20 - 4 * i)) & 0x0F]; }
		}
		else
		{
			measure->value = std::to_wstring((value >> 16) & 0xFF) + L"," + std::to_wstring((value >> 8) & 0xFF) + L"," + std::to_wstring(value & 0xFF);
		}
	}

	// This plugin only returns strings so...
	return 0;
}

static const WCHAR invalidErr[] = L"Invalid measure";

PLUGIN_EXPORT LPCWSTR GetString(void *data)
{
	Measure *measure = static_cast<Measure*>(data);
	std::shared_ptr<Image> img = measure->parent;

	if (img == nullptr)
	{
		return invalidErr;
	}

	if (measure->type == MEASURE_CONTAINER)
	{
		return img->path.c_str();
	}
	else if (measure->type == MEASURE_AVG_LUM)
	{
		return NULL;
	}
	else
	{
		return measure->value.c_str();
	}
}

PLUGIN_EXPORT void Finalize(void *data)
{
	Measure *measure = static_cast<Measure*>(data);
	delete measure;
}
