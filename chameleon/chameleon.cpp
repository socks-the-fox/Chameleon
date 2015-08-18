#include <string>
#include <intrin.h>

#include "chameleon.h"
#include "chameleon_internal.h"

extern const ChameleonParams defaultImageParams[4];
extern const ChameleonParams defaultIconParams[4];

Chameleon* createChameleon()
{
	Chameleon *result = new Chameleon;
	result->background1 = result->background2 = result->foreground1 = result->foreground2 = INVALID_INDEX;

	result->colors = static_cast<ColorStat*>(_aligned_malloc(MAX_COLOR_STATS * sizeof(ColorStat), 16));
	
	memset(result->colors, 0, MAX_COLOR_STATS * sizeof(ColorStat));

	return result;
}

void destroyChameleon(Chameleon *chameleon)
{
	_aligned_free(chameleon->colors);
	delete chameleon;
}

void chameleonProcessLine(Chameleon *chameleon, const uint32_t *lineData, size_t lineWidth, bool edgeLine)
{
	ColorStat *stat = chameleon->colors;

	uint16_t cIndex = 0;

	float edge = (edgeLine ? 1.0f : 0.0f);

	for (size_t i = 0; i < lineWidth; ++i)
	{
		// Ignore pixels that are mostly transparent
		if ((lineData[i] & 0xFF000000) < 0xC0000000)
			continue;

		cIndex = XRGB5(lineData[i]);


		// TODO: SIMDize this?
		stat[cIndex].r += ((lineData[i] & 0x000000FF)      ) / 255.0f;
		stat[cIndex].g += ((lineData[i] & 0x0000FF00) >>  8) / 255.0f;
		stat[cIndex].b += ((lineData[i] & 0x00FF0000) >> 16) / 255.0f;

		stat[cIndex].count += 1.0f;

		stat[cIndex].edgeCount += edge;
	}

	if (!edgeLine)
	{
		if ((lineData[0] & 0xFF000000) >= 0xC0000000)
		{
			stat[XRGB5(lineData[0])].edgeCount++;
		}
		
		if ((lineData[lineWidth - 1] & 0xFF000000) >= 0xC0000000)
		{
			stat[XRGB5(lineData[lineWidth - 1])].edgeCount++;
		}
	}
}

void chameleonProcessImage(Chameleon *chameleon, const uint32_t *imgData, size_t imgWidth, size_t imgHeight, bool resample, bool alpha)
{
	uint32_t *resampledData = nullptr;
	// if img is > 128*128 and resample == true, resample the image.
	if ((imgWidth > 128 || imgHeight > 128) && resample)
	{
		// TODO: Convert to use floats to better space the samples
		size_t newWidth = 0, newHeight = 0;
		size_t widthStep = 1, heightStep = 1;
		if (imgWidth > 128)
		{
			newWidth = 128;
			widthStep = imgWidth / 127;
		}
		else
		{
			newWidth = imgWidth;
		}
		
		if (imgHeight > 128)
		{
			newHeight = 128;
			heightStep = imgHeight / 127;
		}
		else
		{
			newHeight = imgHeight;
		}

		resampledData = new uint32_t[newWidth * newHeight];

		// Max alpha if we're ignoring it
		uint32_t alphaVal = (alpha ? 0 : 0xFF000000);

		for (size_t y = 0; y < newHeight - 1; ++y)
		{
			for (size_t x = 0; x < newWidth - 1; ++x)
			{
				resampledData[x + (y * newWidth)] = imgData[(x * widthStep) + ((y * heightStep) * imgWidth)] | alphaVal;
			}

			resampledData[(newWidth - 1) + (y * newWidth)] = imgData[(imgWidth - 1) + ((y * heightStep) * imgWidth)] | alphaVal;
		}

		for (size_t x = 0; x < newWidth - 1; ++x)
		{
			resampledData[x + ((newHeight - 1) * newWidth)] = imgData[(x * widthStep) + ((imgHeight - 1) * imgWidth)] | alphaVal;
		}

		resampledData[(newWidth - 1) + ((newHeight - 1) * newWidth)] = imgData[(imgWidth - 1) + ((imgHeight - 1) * imgWidth)] | alphaVal;

		imgData = resampledData;
		imgWidth = newWidth;
		imgHeight = newHeight;
	}

	for (size_t i = 0; i < imgHeight; ++i)
	{
		chameleonProcessLine(chameleon, &imgData[imgWidth * i], imgWidth, (i == 0 || i == (imgHeight - 1)));
	}

	delete[] resampledData;
}

void chameleonFindKeyColors(Chameleon *chameleon, const ChameleonParams *params, bool forceContrast)
{
	ColorStat *stat = chameleon->colors;

	// Convert colors to YUV for processing
	for (uint16_t i = 0; i < MAX_COLOR_STATS; ++i)
	{
		fixRGB(&stat[i]);
		calcYUV(&stat[i]);
	}

	// First, find the first background color.

	uint16_t bg1 = INVALID_INDEX;
	uint16_t bg2 = INVALID_INDEX;
	uint16_t fg1 = INVALID_INDEX;
	uint16_t fg2 = INVALID_INDEX;

	const ChameleonParams *bg1Param = &params[0];
	const ChameleonParams *fg1Param = &params[1];
	const ChameleonParams *bg2Param = &params[2];
	const ChameleonParams *fg2Param = &params[3];

	float result = 1, temp = 0;

	for (uint16_t i = 0; i < MAX_COLOR_STATS; ++i)
	{
		if (stat[i].count > 0)
		{
			temp = (stat[i].count * bg1Param->countWeight) + 1;
			temp *= (stat[i].edgeCount * bg1Param->edgeWeight) + 1;

			if (temp > result)
			{
				bg1 = i;
				result = temp;
			}
		}
	}

	// Now the foreground color...
	result = 1;
	for (uint16_t i = 0; i < MAX_COLOR_STATS; ++i)
	{
		if (i != bg1 && stat[i].count > 0)
		{
			temp = (stat[i].count * fg1Param->countWeight) + 1;
			temp *= (stat[i].edgeCount * fg1Param->edgeWeight) + 1;
			temp *= (distance(&stat[i], &stat[bg1]) * fg1Param->bg1distanceWeight) + 1;
			temp *= (saturation(&stat[i]) * fg1Param->saturationWeight) + 1;
			temp *= (contrast(&stat[i], &stat[bg1]) * fg1Param->contrastWeight) + 1;

			if (temp >= result)
			{
				fg1 = i;
				result = temp;
			}
		}
	}

	// Second background...
	result = 1;
	for (uint16_t i = 0; i < MAX_COLOR_STATS; ++i)
	{
		if (i != bg1 && i != fg1 && stat[i].edgeCount > 0)
		{
			temp = (stat[i].count * bg2Param->countWeight) + 1;
			temp *= (stat[i].edgeCount * bg2Param->edgeWeight) + 1;
			temp *= (distance(&stat[i], &stat[bg1]) * bg2Param->bg1distanceWeight) + 1;
			temp *= (distance(&stat[i], &stat[fg1]) * bg2Param->fg1distanceWeight) + 1;
			temp *= (saturation(&stat[i]) * bg2Param->saturationWeight) + 1;
			temp *= (contrast(&stat[i], &stat[fg1]) * bg2Param->contrastWeight) + 1;

			if (temp >= result)
			{
				bg2 = i;
				result = temp;
			}
		}
	}

	// Second foreground...
	result = 1;
	for (uint16_t i = 0; i < MAX_COLOR_STATS; ++i)
	{
		if (i != bg1 && i != fg1 && i != bg2 && stat[i].count > 0)
		{
			temp = (stat[i].count * fg2Param->countWeight) + 1;
			temp *= (stat[i].edgeCount * fg2Param->edgeWeight) + 1;
			temp *= (distance(&stat[i], &stat[bg1]) * fg2Param->bg1distanceWeight) + 1;
			temp *= (distance(&stat[i], &stat[fg1]) * fg2Param->fg1distanceWeight) + 1;
			temp *= (saturation(&stat[i]) * fg2Param->saturationWeight) + 1;
			temp *= (contrast(&stat[i], &stat[bg1]) * fg2Param->contrastWeight) + 1;

			if (temp >= result)
			{
				fg2 = i;
				result = temp;
			}
		}
	}

	if (bg2 == INVALID_INDEX)
	{
		bg2 = bg1;
	}

	if (fg1 == INVALID_INDEX)
	{
		fg1 = bg2;
		bg2 = bg1;
	}

	if (fg2 == INVALID_INDEX)
	{
		fg2 = fg1;
	}

	if (forceContrast)
	{
		// Set up a couple of key colors for the next step
		if (stat[0x0000].count == 0)
		{
			stat[0x0000].r = stat[0x0000].g = stat[0x0000].b = 0.0f;
		}

		if (stat[0x0FFF].count == 0)
		{
			stat[0x0FFF].r = stat[0x0FFF].g = stat[0x0FFF].b = 1.0f;
		}

		// Find the contrast between the background and the foreground
		if (contrast(&stat[fg1], &stat[bg1]) < MIN_CONTRAST)
		{
			if (stat[bg1].y >= 0.5f)
			{
				fg1 = 0x0000;
			}
			else
			{
				fg1 = 0x0FFF;
			}
		}

		if (contrast(&stat[fg2], &stat[bg1]) < MIN_CONTRAST)
		{
			if (stat[bg1].y >= 0.5f)
			{
				fg2 = 0x0000;
			}
			else
			{
				fg2 = 0x0FFF;
			}
		}
	}

	chameleon->background1 = bg1;
	chameleon->background2 = bg2;
	chameleon->foreground1 = fg1;
	chameleon->foreground2 = fg2;
}

uint32_t chameleonGetColor(Chameleon *chameleon, ChameleonColor color)
{
	uint32_t result;

	float r, g, b;

	switch (color)
	{
	case CHAMELEON_BACKGROUND1:
		r = chameleon->colors[chameleon->background1].r;
		g = chameleon->colors[chameleon->background1].g;
		b = chameleon->colors[chameleon->background1].b;
		break;
	case CHAMELEON_FOREGROUND1:
		r = chameleon->colors[chameleon->foreground1].r;
		g = chameleon->colors[chameleon->foreground1].g;
		b = chameleon->colors[chameleon->foreground1].b;
		break;
	case CHAMELEON_BACKGROUND2:
		r = chameleon->colors[chameleon->background2].r;
		g = chameleon->colors[chameleon->background2].g;
		b = chameleon->colors[chameleon->background2].b;
		break;
	case CHAMELEON_FOREGROUND2:
		r = chameleon->colors[chameleon->foreground2].r;
		g = chameleon->colors[chameleon->foreground2].g;
		b = chameleon->colors[chameleon->foreground2].b;
		break;
	}

	result = int(r * 255) | (int(g * 255) << 8) | (int(b * 255) << 16) | 0xFF000000;

	return result;
}

const ChameleonParams* chameleonDefaultImageParams()
{
	return defaultImageParams;
}

const ChameleonParams* chameleonDefaultIconParams()
{
	return defaultIconParams;
}

const ChameleonParams defaultImageParams[4] =
{
	// BG1
	{
		1.0f,
		1.0f,
		0.0f,
		0.0f,
		0.0f,
		0.0f
	},

	// FG1
	{
		0.04f,
		0.0f,
		60.0f,
		0.0f,
		10.0f,
		250.0f
	},

	// BG2
	{
		1.0f,
		1.0f,
		0.0f,
		0.0f,
		0.0f,
		10.0f
	},

	// FG2
	{
		0.015f,
		0.0f,
		60.0f,
		5.0f,
		200.0f,
		1.0f
	}
};

const ChameleonParams defaultIconParams[4] =
{
	// BG1
	{
		1.0f,
		0.0f,
		0.0f,
		0.0f,
		0.0f,
		0.0f
	},

	// FG1
	{
		2.0f,
		0.0f,
		5.0f,
		0.0f,
		10.0f,
		1.0f
	},

	// BG2
	{
		2.0f,
		0.0f,
		100.0f,
		10.0f,
		5.0f,
		1.0f
	},

	// FG2
	{
		2.0f,
		0.0f,
		50.0f,
		200.0f,
		10.0f,
		0.5f
	}
};