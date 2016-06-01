#include <vector>
#include <memory>
#include <string>

#include <Windows.h>
#include <VersionHelpers.h>
#include <Shobjidl.h>

#include <shlwapi.h>
#include <commoncontrols.h>
#include <comip.h>
#include <comdef.h>

#include <RainmeterAPI.h>
#include <chameleon.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_SSE2
#define STBI__X86_TARGET
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STBIR_SATURATE_INT
#include "stb_image_resize.h"

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
	bool dirty;
	bool forceIcon;
	bool customCrop;

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

uint32_t* cropImage(uint32_t *imgData, int oldW, int oldH, const RECT *cropRect)
{
	if (cropRect->left == 0 && cropRect->right >= oldW && cropRect->top == 0 && cropRect->bottom >= oldH)
	{
		return imgData;
	}

	if (cropRect->left > oldW || cropRect->top > oldH)
	{
		RmLog(LOG_ERROR, L"Cropping out of bounds of image! Check your parameters.");
		return imgData;
	}

	int newW, newH;
	newW = (cropRect->right <= oldW ? cropRect->right : oldW) - cropRect->left;
	newH = (cropRect->bottom <= oldH ? cropRect->bottom : oldH) - cropRect->top;

	uint32_t *result = (uint32_t*)stbi__malloc(newW * newH * sizeof(uint32_t));

	for (int i = 0; i < newH; ++i)
	{
		memcpy(&result[i * newW], &imgData[cropRect->left + ((i + cropRect->top) * oldW)], newW * sizeof(uint32_t));
	}

	stbi_image_free(imgData);
	return result;
}

void SampleImage(std::shared_ptr<Image> img)
{
	bool isIcon = false;
	RECT monitorRect;

	// If we're sampling the desktop, grab that
	if (img->type == IMG_DESKTOP)
	{
		std::wstring path = L"";

		// First, get the info we need from the old API

		HMONITOR mon = MonitorFromWindow(img->hWnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO monInf;
		monInf.cbSize = sizeof(MONITORINFO);
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

		// Convert the path

		FILE *fp;
		
		if (_wfopen_s(&fp, img->path.c_str(), L"rb") != 0)
		{
			// Something goofed, but we can try again
			return;
		}

		// Load image data
		int w, h, n;
		uint32_t *imgData = (uint32_t*) stbi_load_from_file(fp, &w, &h, &n, 4);

		fclose(fp);

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
			imgData = cropImage(imgData, w, h, &img->cropRect);
		}
		else if (img->type == IMG_DESKTOP)
		{
			RECT cropRect;
			cropRect.left = (w - monitorRect.right) / 2;
			if(cropRect.left < 0)
			{
				cropRect.left = 0;
			}

			cropRect.top = (h - monitorRect.bottom) / 2;
			if (cropRect.top < 0)
			{
				cropRect.top = 0;
			}

			cropRect.right = monitorRect.right + cropRect.left;
			cropRect.bottom = monitorRect.bottom + cropRect.top;

			imgData = cropImage(imgData, w, h, &cropRect);
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

PLUGIN_EXPORT void Reload(void *data, void *rm, double *maxVal)
{
	Measure *measure = static_cast<Measure*>(data);

	void *skin = RmGetSkin(rm);

	std::wstring parent = RmReadString(rm, L"Parent", L"");

	std::wstring type = RmReadString(rm, L"Type", L"Desktop");

	if (parent.empty())
	{
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

			std::wstring desktopCrop = RmReadString(rm, L"CropDesktop", L"1");

			// Define a "custom" crop of max-size to prevent the default
			// "crop to monitor size" behavior but only use it if
			// CropDesktop is anything other than 0
			img->cropRect.left = img->cropRect.top = 0;
			img->cropRect.right = img->cropRect.bottom = LONG_MAX;
			img->customCrop = desktopCrop.compare(L"0") == 0;
		}
		else if (type.compare(L"File") == 0)
		{
			img->type = IMG_FILE;
			std::wstring path = RmReadPath(rm, L"Path", L"");
			if (path.compare(img->path) != 0)
			{
				img->path = path;
				img->dirty = true;
			}

			std::wstring forceIcon = RmReadString(rm, L"ForceIcon", L"0");
			img->forceIcon = forceIcon.compare(L"0") != 0;
		}
		else
		{
			RmLog(LOG_ERROR, L"Chameleon: Invalid Type=");
			return;
		}

		std::wstring fallbackColorStr = RmReadString(rm, L"FallbackBG1", L"FFFFFF");

		// If statements are due to the fact that FallbackXXX might be specified but not initialized yet.
		if (fallbackColorStr.empty())
		{
			img->fallback_bg1 = 0xFFFFFFFF;
		}
		else
		{
			img->fallback_bg1 = (std::stoi(fallbackColorStr, 0, 16) << 8) | 0xFF;
		}

		fallbackColorStr = RmReadString(rm, L"FallbackBG2", fallbackColorStr.c_str());
		if (fallbackColorStr.empty())
		{
			img->fallback_bg2 = img->fallback_bg1;
		}
		else
		{
			img->fallback_bg2 = (std::stoi(fallbackColorStr, 0, 16) << 8) | 0xFF;
		}

		fallbackColorStr = RmReadString(rm, L"FallbackFG1", L"000000");
		if (fallbackColorStr.empty())
		{
			img->fallback_fg1 = 0x000000FF;
		}
		else
		{
			img->fallback_fg1 = (std::stoi(fallbackColorStr, 0, 16) << 8) | 0xFF;
		}

		fallbackColorStr = RmReadString(rm, L"FallbackFG2", fallbackColorStr.c_str());
		if (fallbackColorStr.empty())
		{
			img->fallback_fg2 = img->fallback_fg1;
		}
		else
		{
			img->fallback_fg2 = (std::stoi(fallbackColorStr, 0, 16) << 8) | 0xFF;
		}

		// Grab cropping info
		img->cropRect.left = RmReadInt(rm, L"CropX", 0);
		img->cropRect.top = RmReadInt(rm, L"CropY", 0);
		img->cropRect.right = RmReadInt(rm, L"CropW", 16777215) + img->cropRect.left;
		img->cropRect.right = RmReadInt(rm, L"CropH", 16777215) + img->cropRect.top;

		// Use the cropping info if it's not the defaults
		img->customCrop = !(img->cropRect.left == 0 && img->cropRect.top == 0 && img->cropRect.right == 16777215 && img->cropRect.bottom == 16777215);

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
