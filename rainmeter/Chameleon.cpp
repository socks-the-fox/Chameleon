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

#include <png.h>

enum MeasureType
{
	MEASURE_CONTAINER,
	MEASURE_BG1,
	MEASURE_BG2,
	MEASURE_FG1,
	MEASURE_FG2
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
	bool dirty;
	bool forceIcon;

	uint32_t bg1;
	uint32_t bg2;
	uint32_t fg1;
	uint32_t fg2;

	uint32_t fallback_bg1;
	uint32_t fallback_bg2;
	uint32_t fallback_fg1;
	uint32_t fallback_fg2;
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

std::shared_ptr<Image> desktopImg = nullptr;
std::vector< std::weak_ptr<Image> > images;
static const WCHAR whex[] = L"0123456789ABCDEF";

void SampleImage(std::shared_ptr<Image> img);
PLUGIN_EXPORT void Initialize(void* *data, void *rm);
PLUGIN_EXPORT void Reload(void *data, void *rm, double *maxVal);
PLUGIN_EXPORT double Update(void *data);
PLUGIN_EXPORT LPCWSTR GetString(void *data);
PLUGIN_EXPORT void Finalize(void *data);

_COM_SMARTPTR_TYPEDEF(IImageList, __uuidof(IImageList));

// I so very hope stbi adds support for 16-bit-per-channel PNG files
// so I can dump libPNG. Not that I have anything against it, but
// it's a bit hefty to use for literally only the one specific
// subtype of PNG...
uint32_t* loadPNG16(const char *path, int *w, int *h)
{
	uint32_t *imgData = nullptr;
	FILE *fp = nullptr;
	*w = -1;
	*h = -1;

	if (fopen_s(&fp, path, "rb") != 0)
	{
		// Try again next update
		*w = 0;
		*h = 0;
		return imgData;
	}

	// Normally we would think "it has to be a PNG or we wouldn't be here"
	// But since we close the file (in the stbi library) and then re-open
	// it here, there's the potential that the OS could go in and swap the
	// image between accesses.
	png_byte sig[8];
	fread(sig, 1, 8, fp);
	if (png_sig_cmp(sig, 0, 8) != 0)
	{
		// Nope, not a PNG anymore! Let's try again next update...
		fclose(fp);
		return imgData;
	}

	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	if (!png_ptr)
	{
		// Something happened that we couldn't set up...
		// Most likely won't be able to fix it next go-around so bail.
		fclose(fp);
		return imgData;
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr)
	{
		// Again, bail.
		png_destroy_read_struct(&png_ptr, nullptr, nullptr);
		fclose(fp);
		return imgData;
	}

	png_init_io(png_ptr, fp);
	png_set_sig_bytes(png_ptr, 8);

	// Grab the info we need so we don't have to move data so much
	png_read_info(png_ptr, info_ptr);

	*h = png_get_image_height(png_ptr, info_ptr);
	*w = png_get_image_width(png_ptr, info_ptr);
	uint32_t ct = png_get_color_type(png_ptr, info_ptr);

	// Use stbi__malloc to allocate imgData to simplify code
	imgData = (uint32_t*)stbi__malloc((*w) * (*h) * sizeof(uint32_t));

	// Set up row pointers
	png_bytep *rows = new png_bytep[*h];

	for (size_t i = 0; i < *h; ++i)
	{
		png_bytep val = (png_bytep)(&imgData[(*w) * i]);
		rows[i] = val;
	}

	png_set_rows(png_ptr, info_ptr, rows);

	// Dinky convert 16-bit to 8-bit as quickly as possible. Upconvert tiny pixels. Make sure colors are what we expect.
	if (ct == PNG_COLOR_TYPE_PALETTE)
	{
		png_set_palette_to_rgb(png_ptr);
	}
	else if (ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_GRAY_ALPHA)
	{
		png_set_gray_to_rgb(png_ptr);
	}

	if (png_get_bit_depth(png_ptr, info_ptr) == 16)
	{
		png_set_strip_16(png_ptr);
	}

	png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);

	png_color_16 bg;
	bg.red = 0;
	bg.green = 0;
	bg.blue = 0;
	png_set_background(png_ptr, &bg, PNG_BACKGROUND_GAMMA_SCREEN, 0, 1);

	png_set_interlace_handling(png_ptr);

	png_read_update_info(png_ptr, info_ptr);

	png_read_image(png_ptr, rows);

	// Cleanup!
	png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
	delete[] rows;
	fclose(fp);

	return imgData;
}

uint32_t* loadIcon(const char *path, int *w, int *h)
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
	SHFILEINFO info = { 0 };
	int iconSize = SHIL_EXTRALARGE;
	if (IsWindowsVistaOrGreater())
	{
		iconSize = SHIL_JUMBO;
	}

	// These assume unconditional success.
	// I'll fix them if they ever report breakage
	// I was dreading this part until I found out it's
	// literally three functions.
	SHGetFileInfo(path, 0, &info, sizeof(info), SHGFI_SYSICONINDEX);
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

void SampleImage(std::shared_ptr<Image> img)
{
	HRESULT r = CoInitializeEx(NULL, COINIT_SPEED_OVER_MEMORY);

	bool isIcon = false;

	// If we're sampling the desktop, grab that
	if (img->type == IMG_DESKTOP)
	{
		std::wstring path = L"";
		if (IsWindows8OrGreater())
		{
			// Use the multi-desktop fun version!

			// First, get the info we need from the old API

			HMONITOR mon = MonitorFromWindow(img->hWnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFO monInf;
			monInf.cbSize = sizeof(MONITORINFO);
			GetMonitorInfo(mon, &monInf);

			// Now set up the new API

			LPWSTR monPath;
			LPWSTR wallPath;

			// Because making these just a few loose functions would have been too easy
			IDesktopWallpaper *wp = nullptr;

			CoCreateInstance(__uuidof(DesktopWallpaper), NULL, CLSCTX_ALL, IID_PPV_ARGS(&wp));

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
						break;
					}
				}
			}

			// Clean up
			wp->Release();
		}
		else
		{
			// Use the "boring" WinXP - Win7 version
			WCHAR wallPath[MAX_PATH];
			ZeroMemory((void*)wallPath, sizeof(WCHAR) * MAX_PATH);
			SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, (void*)wallPath, 0);
			path = wallPath;
		}

		// First, is the current path the same as the new path
		if (img->path.compare(path) != 0)
		{
			// It's different, mark it dirty!
			img->path = path;
			img->dirty = true;
		}
		else
		{
			// Check the file modification time
			HANDLE file = CreateFileW(img->path.c_str(), GENERIC_READ, FILE_SHARE_DELETE | FILE_SHARE_WRITE | FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
			FILETIME ft;
			GetFileTime(file, NULL, NULL, &ft);
			CloseHandle(file);

			if (ft.dwHighDateTime != img->lastMod.dwHighDateTime || ft.dwLowDateTime != img->lastMod.dwLowDateTime)
			{
				// Same path, but different modification times...
				img->lastMod.dwHighDateTime = ft.dwHighDateTime;
				img->lastMod.dwLowDateTime = ft.dwLowDateTime;
				img->dirty = true;
			}
		}

		// Special case! User doesn't have a background image:
		if (img->path.length() == 0)
		{
			img->bg1 = _byteswap_ulong(GetSysColor(COLOR_ACTIVECAPTION)) | 0xFF;
			img->bg2 = _byteswap_ulong(GetSysColor(COLOR_INACTIVECAPTION)) | 0xFF;
			img->fg1 = _byteswap_ulong(GetSysColor(COLOR_CAPTIONTEXT)) | 0xFF;
			img->fg2 = _byteswap_ulong(GetSysColor(COLOR_INACTIVECAPTIONTEXT)) | 0xFF;

			img->dirty = false;
		}
	}
	else
	{
		// it's a file, let's check the path
		if (img->path.empty())
		{
			// Empty path! Let's dump some default values and be done with it...
			img->bg1 = img->fallback_bg1;
			img->bg2 = img->fallback_bg2;
			img->fg1 = img->fallback_fg1;
			img->fg2 = img->fallback_fg2;
			img->dirty = false;

			if (r == S_OK || r == S_FALSE)
			{
				CoUninitialize();
			}
			return;
		}

		// Now the "file modified" time
		HANDLE file = CreateFileW(img->path.c_str(), GENERIC_READ, FILE_SHARE_DELETE | FILE_SHARE_WRITE | FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
		FILETIME ft;
		GetFileTime(file, NULL, NULL, &ft);
		CloseHandle(file);

		if (ft.dwHighDateTime != img->lastMod.dwHighDateTime || ft.dwLowDateTime != img->lastMod.dwLowDateTime)
		{
			// Same path, but different modification times...
			img->lastMod.dwHighDateTime = ft.dwHighDateTime;
			img->lastMod.dwLowDateTime = ft.dwLowDateTime;
			img->dirty = true;
		}
	}

	if (img->dirty)
	{
		std::wstring debug = L"Chameleon: Updating colors based on ";
		debug += img->path;
		RmLog(LOG_DEBUG, debug.c_str());

		// Convert the path
		size_t toss;
		char *path = new char[MAX_PATH];
		
		wcstombs_s(&toss, path, MAX_PATH, img->path.c_str(), MAX_PATH);

		// Load image data
		int w, h, n;
		uint32_t *imgData = (uint32_t*) stbi_load(path, &w, &h, &n, 4);

		if (imgData == nullptr)
		{
			RmLog(LOG_ERROR, L"Could not load desktop!");
			const char *debug = stbi_failure_reason();
			if (strcmp(debug, "can't fopen") == 0)
			{
				// Try again next update
				if (r == S_OK || r == S_FALSE)
				{
					CoUninitialize();
				}
				return;
			}
			else if (strcmp(debug, "1/2/4/8-bit only") == 0)
			{
				// It's a 16-bit PNG, time to do this manually
				imgData = loadPNG16(path, &w, &h);

				if (imgData == nullptr && w == -1)
				{
					// Something goofed, don't bother continuing
					img->bg1 = img->fallback_bg1;
					img->bg2 = img->fallback_bg2;
					img->fg1 = img->fallback_fg1;
					img->fg2 = img->fallback_fg2;
					img->dirty = false;
					if (r == S_OK || r == S_FALSE)
					{
						CoUninitialize();
					}
					return;
				}
				else if (imgData == nullptr && w == 0)
				{
					// Something goofed, but we can try again
					if (r == S_OK || r == S_FALSE)
					{
						CoUninitialize();
					}
					return;
				}
			}
			else
			{
				imgData = loadIcon(path, &w, &h);

				if (imgData == nullptr)
				{
					// It's something we don't actually know how to handle, so let's not.
					img->bg1 = img->fallback_bg1;
					img->bg2 = img->fallback_bg2;
					img->fg1 = img->fallback_fg1;
					img->fg2 = img->fallback_fg2;
					img->dirty = false;
					if (r == S_OK || r == S_FALSE)
					{
						CoUninitialize();
					}
					return;
				}

				// RGB swap image data?
				uint32_t temp = 0;
				for (size_t i = 0; i < w * h; ++i)
				{
					temp = imgData[i];
					imgData[i] = (temp & 0xFF000000) | ((temp & 0x00FF0000) >> 16) | (temp & 0x0000FF00) | ((temp & 0x000000FF) << 16);
				}

				isIcon = true;
			}
		}

		isIcon |= img->forceIcon;

		// Run through Chameleon
		Chameleon *chameleon = createChameleon();

		chameleonProcessImage(chameleon, imgData, w, h, true, isIcon);

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

		destroyChameleon(chameleon);
		chameleon = nullptr;

		stbi_image_free(imgData);

		img->dirty = false;
	}

	if (r == S_OK || r == S_FALSE)
	{
		CoUninitialize();
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
