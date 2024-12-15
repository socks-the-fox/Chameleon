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

#include "stb_image.h"
#include "stb_image_resize.h"
#include "stb_image_write.h"

// Some helpful functions
#include "utilities.h"

// Definitions for the actual measure state
#include "Measure.h"

// An excessively large value I picked due to being a few orders of magnatude larger than the largest image I've seen (NASA Hubble image)
#define CROP_MAX_DIMENSION 16777215

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
static const WCHAR invalidErr[] = L"Invalid measure";

void SampleImage(std::shared_ptr<Image> img);
PLUGIN_EXPORT void Initialize(void* *data, void *rm);
PLUGIN_EXPORT void Reload(void *data, void *rm, double *maxVal);
PLUGIN_EXPORT double Update(void *data);
PLUGIN_EXPORT LPCWSTR GetString(void *data);
PLUGIN_EXPORT void Finalize(void *data);

bool IsWindows11_24H2OrGreater();

void SampleImage(std::shared_ptr<Image> img)
{
	bool isIcon = false;
	RECT monitorRect;
	RECT skinRect = { 0 };
	RECT actualCropRect = img->cropRect;
	MONITORINFO monInf;
	monInf.cbSize = sizeof(MONITORINFO);
	bool customContext = img->contextRect.left || img->contextRect.right || img->contextRect.top || img->contextRect.bottom;

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
		// Don't update by dragging if we're using a custom context area
		// or if we're just not using context-aware colors
		else if (img->contextAware && img->draggingSkin && !customContext)
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
		// Unless we're in Win11 24H2 because MS did something stupid and broke it
		if (img->type == IMG_DESKTOP && !IsWindows11_24H2OrGreater())
		{
			// Get the Real Device Context, then get a non-live copy to read from
			HWND hwDesktop = GetShellWindow();

			HDC hdcDesktop = GetDC(hwDesktop);
			HDC hdc = CreateCompatibleDC(hdcDesktop);

			// Get the upper left reference point for the virtual screen
			// which might not be (0,0) due to monitor arrangement
			int xRef = GetSystemMetrics(SM_XVIRTUALSCREEN);
			int yRef = GetSystemMetrics(SM_YVIRTUALSCREEN);

			// Set up the default monitor-based cropping
			int imgW = monitorRect.right;
			int imgH = monitorRect.bottom;

			// This is meant to shift the virtual screen coords to image space coords.
			int imgX = monInf.rcMonitor.left - xRef;
			int imgY = monInf.rcMonitor.top - yRef;

			if (img->customCrop)
			{
				// There's a custom cropping rectangle, so we can just grab the whole screen
				// (the cropping code will handle it automatically later)
				imgX = 0;
				imgY = 0;
				imgW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
				imgH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
				
				// Adjust the cropping rectangle
				actualCropRect.left -= xRef;
				actualCropRect.right -= xRef;
				actualCropRect.top -= yRef;
				actualCropRect.bottom -= yRef;
			}

			// Create the bitmap we're going to bounce the image data into, and the immediately out of
			// because it's in a device-specific format
			// (maybe 6-bit, maybe 8-bit, maybe 10-bit, rgb, bgrx, rgbx, who knows!)
			HBITMAP hBmp = CreateCompatibleBitmap(hdcDesktop, imgW, imgH);

			// do the actual copy, but save the default hbmp windows set up that we can't exactly use for this
			// so we can let windows clean that up later
			HBITMAP hOldBmp = (HBITMAP)SelectObject(hdc, hBmp);

			BitBlt(hdc, 0, 0, imgW, imgH, hdcDesktop, imgX, imgY, SRCCOPY);

			// Windows doesn't like having the handle selected when we want to read from it,
			// so now we put the old handle back 
			SelectObject(hdc, hOldBmp);

			// Now we have the data in a device specific buffer that Windows won't touch while we're using it
			// so let's convert that to a format we can actually use

			LPBITMAPINFO bmpInfo = (LPBITMAPINFO)malloc(sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD));
			ZeroMemory(&bmpInfo->bmiHeader, sizeof(BITMAPINFOHEADER));
			bmpInfo->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);

			// Get what Windows wants us to allocate, and do so
			GetDIBits(hdc, hBmp, 0, imgH, NULL, bmpInfo, DIB_RGB_COLORS);
			uint8_t *byteData = (uint8_t*)malloc(bmpInfo->bmiHeader.biSizeImage);

			// Now do the actual copy
			GetDIBits(hdc, hBmp, 0, imgH, byteData, bmpInfo, DIB_RGB_COLORS);

			w = bmpInfo->bmiHeader.biWidth;
			h = bmpInfo->bmiHeader.biHeight;

			// Windows uses a negative value to specify the bitmap is right side up
			// if it's not, we need to adjust the cropping, if any
			if (h < 0)
				h = -h;
			else if(img->customCrop)
			{
				LONG bottom = actualCropRect.bottom;
				actualCropRect.bottom = imgH - actualCropRect.top;
				actualCropRect.top = imgH - bottom;
			}

			// We're going to be doing this by bytes so it'll be easier
			imgData = (uint32_t*) createImage(w, h);
			
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


			
			if (img->contextAware)
			{
				// Now we need to create an average for the region of the skin
				int cW = w, cH = h;

				// If we have a custom context rectangle, use that
				
				if (customContext)
					skinRect = img->contextRect;

				// Image should be by the overall desktop for custom cropping
				// and by monitor for default cropping
				if (img->customCrop || customContext)
				{
					skinRect.left -= xRef;
					skinRect.right -= xRef;
					skinRect.top -= yRef;
					skinRect.bottom -= yRef;
				}
				else
				{
					skinRect.left -= monInf.rcMonitor.left;
					skinRect.right -= monInf.rcMonitor.left;
					skinRect.top -= monInf.rcMonitor.top;
					skinRect.bottom -= monInf.rcMonitor.top;
				}

				if (bmpInfo->bmiHeader.biHeight > 0)
				{
					// Oh, we're vertically flipped.
					// Gotta "flip" the rect to match (actually just shifting it)
					LONG bottom = skinRect.bottom;
					skinRect.bottom = h - skinRect.top;
					skinRect.top = h - bottom;
				}

				// Make sure I'm not doing a dumb by sampling negative values
				if (skinRect.left < 0)
					skinRect.left = 0;
				if (skinRect.top < 0)
					skinRect.top = 0;

				uint32_t *skinRegion = cropImage(imgData, &cW, &cH, &skinRect);

				//stbi_write_png("chamtest.png", cW, cH, 4, skinRegion, cW * sizeof(uint32_t));

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
			ReleaseDC(hwDesktop, hdcDesktop);
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
			RmLog(LOG_ERROR, L"Chameleon: Could not load file!");

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
			uint32_t *croppedData = cropImage(imgData, &w, &h, &actualCropRect);
			if (croppedData != imgData)
			{
				stbi_image_free(imgData);
				imgData = croppedData;
			}
		}

		// Quick Sanity Check
		if (w <= 0 || h <= 0)
		{
			// I debated having a crop size of 0 being an error, but some skins might
			// need to set it to that as a kind of "don't do anything" or maybe through a
			// procedural generation of the crop bounds so we'll just skip doing anything.
			
//			RmLog(LOG_ERROR, L"Chameleon: Width or height is less than or equal to zero!");
//			useDefaultColors(img);

			img->dirty = false;

			return;
		}

		// Resize image for Chameleon
		if (w > 256 || h > 256)
		{
			int newWidth = (w < 256 ? w : 256);
			int newHeight = (h < 256 ? h : 256);
			uint32_t *resizedData = createImage(newWidth, newHeight);

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

		if (img->type == IMG_DESKTOP && img->contextAware)
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

// Prepares the measure for Rainmeter to use
PLUGIN_EXPORT void Initialize(void* *data, void *rm)
{
	Measure* measure = new Measure;
	measure->type = MEASURE_CONTAINER;
	measure->parent = nullptr;
	*data = measure;
}

// Sets the initial state for the measure
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

		// Make sure the initial colors are the fallback
		img->bg1 = fallback_bg1;
		img->bg2 = fallback_bg2;
		img->fg1 = fallback_fg1;
		img->fg2 = fallback_fg2;

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

// Called periodically to refresh the values passed to and returned from the measure
// Dynamic variables should be re-read here.
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

		int contextX = RmReadInt(measure->parent->rm, L"ContextX", 0);
		int contextY = RmReadInt(measure->parent->rm, L"ContextY", 0);
		int contextW = RmReadInt(measure->parent->rm, L"ContextW", 0);
		int contextH = RmReadInt(measure->parent->rm, L"ContextH", 0);
		measure->parent->contextRect.left = contextX;
		measure->parent->contextRect.top = contextY;
		measure->parent->contextRect.right = contextX + contextW;
		measure->parent->contextRect.bottom = contextY + contextH;

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

// Reading the value of a measure as a c-string
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

// When we're removing a measure we don't need anymore
PLUGIN_EXPORT void Finalize(void *data)
{
	Measure *measure = static_cast<Measure*>(data);
	delete measure;
}

// Dumb function for dumb broken desktop sampling
bool isWin1124h2 = false;
bool isWin1124h2Detected = false;
#define WIN11_24H2_BUILD 26100
bool IsWindows11_24H2OrGreater() {
	// I highly doubt we'll need to detect this literally every time we call this function,
	// so we cache the value and return that
	if (isWin1124h2Detected)
		return isWin1124h2;

	// Get the version info
	NTSTATUS(WINAPI * RtlGetVersion)(LPOSVERSIONINFOEXW);
	OSVERSIONINFOEXW versionInfo;

	*(FARPROC*)&RtlGetVersion = GetProcAddress(GetModuleHandleA("ntdll"), "RtlGetVersion");

	if(RtlGetVersion != NULL)
	{
		versionInfo = { sizeof(versionInfo) };
		RtlGetVersion(&versionInfo);

		isWin1124h2 = versionInfo.dwMajorVersion == 10 && versionInfo.dwBuildNumber >= WIN11_24H2_BUILD;
	}
	else
	{
		// if we couldn't tell, just assume it is because we want to take a
		// fallback route for 24h2. Don't use this for enabling features!
		isWin1124h2 = true;
	}

	isWin1124h2Detected = true;

	return isWin1124h2;
}