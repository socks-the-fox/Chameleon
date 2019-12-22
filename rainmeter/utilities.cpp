#include <memory>
#include <string>

#include <intrin.h>

#include <Windows.h>

#include <RainmeterAPI.h>
#include <chameleon.h>
#include <chameleon_internal.h>

#include "utilities.h"
#include "Measure.h"

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