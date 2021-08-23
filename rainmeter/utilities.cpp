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

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "utilities.h"
#include "Measure.h"

_COM_SMARTPTR_TYPEDEF(IImageList, __uuidof(IImageList));

void processRGB(uint32_t color, ColorStat *colorStat)
{
	__m128 rgbc;
	rgbc = _mm_set_ps(1.0f, static_cast<float>((color & 0x00FF0000) >> 16), static_cast<float>((color & 0x0000FF00) >> 8), static_cast<float>(color & 0x000000FF));
	rgbc = _mm_div_ps(rgbc, _mm_set_ps(1, 255.0f, 255.0f, 255.0f));

	colorStat->rgbc = _mm_add_ps(rgbc, colorStat->rgbc);
}

void useDefaultColors(std::shared_ptr<Image> img)
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

bool RmReadBool(void *rm, LPCWSTR option, bool defValue, BOOL replaceMeasures)
{
	std::wstring value = RmReadString(rm, option, defValue ? L"1" : L"0", replaceMeasures);
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

uint32_t RmReadColor(void *rm, LPCWSTR option, uint32_t defValue, BOOL replaceMeasures)
{
	std::wstring value = RmReadString(rm, option, L"", replaceMeasures);

	if (value.empty())
	{
		return defValue;
	}

	return (std::stoi(value, 0, 16) << 8) | 0xFF;
}

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

		imgData = createImage(*w, *h);
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

uint32_t* cropImage(uint32_t *imgData, int *oldW, int *oldH, const RECT *cropRect)
{
	if (cropRect->left == 0 && cropRect->right >= *oldW && cropRect->top == 0 && cropRect->bottom >= *oldH)
	{
		return imgData;
	}

	if (cropRect->left > *oldW || cropRect->top > *oldH)
	{
		RmLog(LOG_ERROR, L"Chameleon: Cropping out of bounds of image! Check your parameters.");
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

uint32_t * createImage(int w, int h)
{
	return (uint32_t*)STBI_MALLOC(w * h * sizeof(uint32_t));
}
