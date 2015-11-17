#include <string>
#include <intrin.h>

#include "chameleon.h"
#include "chameleon_internal.h"

extern const ChameleonParams defaultImageParams[4];
extern const ChameleonParams defaultIconParams[4];

Chameleon* createChameleon()
{
	Chameleon *result = new Chameleon;
	for (size_t i = 0; i < CHAMELEON_COLORS; ++i)
	{
		result->colorIndex[i] = INVALID_INDEX;
	}

	result->colors = static_cast<ColorStat*>(_aligned_malloc(MAX_COLOR_STATS * sizeof(ColorStat), 16));
	
	memset(result->colors, 0, MAX_COLOR_STATS * sizeof(ColorStat));

	result->rgbFixed = false;

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

	chameleon->rgbFixed = false;

	delete[] resampledData;
}

void chameleonFindKeyColors(Chameleon *chameleon, const ChameleonParams *params, bool forceContrast)
{
	ColorStat *stat = chameleon->colors;

	// Convert colors to YUV for processing
	if (!chameleon->rgbFixed)
	{
		for (uint16_t i = 0; i < LAST_COLOR; ++i)
		{
			if (stat[i].count)
			{
				fixRGB(&stat[i]);
				calcYUV(&stat[i]);
			}
		}

		chameleon->rgbFixed = true;
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

	for (uint16_t i = 0; i < LAST_COLOR; ++i)
	{
		if (stat[i].count > 0)
		{
			temp = stat[i].count * bg1Param->countWeight;
			temp += stat[i].edgeCount * bg1Param->edgeWeight;
			temp += saturation(&stat[i]) * bg1Param->saturationWeight;

			if (temp > result)
			{
				bg1 = i;
				result = temp;
			}
		}
	}

	// Now the foreground color...
	result = 1;
	for (uint16_t i = 0; i < LAST_COLOR; ++i)
	{
		if (i != bg1 && stat[i].count > 0)
		{
			temp = stat[i].count * fg1Param->countWeight;
			temp += stat[i].edgeCount * fg1Param->edgeWeight;
			temp += distance(&stat[i], &stat[bg1]) * fg1Param->bg1distanceWeight;
			temp += saturation(&stat[i]) * fg1Param->saturationWeight;
			temp += contrast(&stat[i], &stat[bg1]) * fg1Param->contrastWeight;

			if (temp >= result)
			{
				fg1 = i;
				result = temp;
			}
		}
	}

	// Second background...
	result = 1;
	for (uint16_t i = 0; i < LAST_COLOR; ++i)
	{
		if (i != bg1 && i != fg1 && stat[i].edgeCount > 0)
		{
			temp = stat[i].count * bg2Param->countWeight;
			temp += stat[i].edgeCount * bg2Param->edgeWeight;
			temp += distance(&stat[i], &stat[bg1]) * bg2Param->bg1distanceWeight;
			temp += distance(&stat[i], &stat[fg1]) * bg2Param->fg1distanceWeight;
			temp += saturation(&stat[i]) * bg2Param->saturationWeight;
			temp += contrast(&stat[i], &stat[fg1]) * bg2Param->contrastWeight;

			if (temp >= result)
			{
				bg2 = i;
				result = temp;
			}
		}
	}

	// Second foreground...
	result = 1;
	for (uint16_t i = 0; i < LAST_COLOR; ++i)
	{
		if (i != bg1 && i != fg1 && i != bg2 && stat[i].count > 0)
		{
			temp = stat[i].count * fg2Param->countWeight;
			temp += stat[i].edgeCount * fg2Param->edgeWeight;
			temp += distance(&stat[i], &stat[bg1]) * fg2Param->bg1distanceWeight;
			temp += distance(&stat[i], &stat[fg1]) * fg2Param->fg1distanceWeight;
			temp += saturation(&stat[i]) * fg2Param->saturationWeight;
			temp += contrast(&stat[i], &stat[bg1]) * fg2Param->contrastWeight;

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
		// Find the contrast between the background and the foreground
		float cont = contrast(&stat[fg1], &stat[bg1]);
		// prevent infinite loops;
		size_t loops;
		if (cont < MIN_CONTRAST)
		{
			stat[FG1_BACKUP_INDEX] = stat[fg1];
			fg1 = FG1_BACKUP_INDEX;
			loops = 0;
			while (cont < MIN_CONTRAST && loops < 8)
			{
				if (stat[bg1].y > 0.5f)
				{
					stat[fg1].rgbc = _mm_div_ps(stat[fg1].rgbc, _mm_set_ps(1, 2, 2, 2));
				}
				else
				{
					stat[fg1].rgbc = _mm_div_ps(_mm_add_ps(stat[fg1].rgbc, _mm_set_ps(0, 1, 1, 1)), _mm_set_ps(1, 2, 2, 2));
				}

				cont = contrast(&stat[fg1], &stat[bg1]);
				loops++;
			}
		}

		cont = contrast(&stat[fg2], &stat[bg1]);
		if (cont < MIN_CONTRAST)
		{
			stat[FG2_BACKUP_INDEX] = stat[fg2];
			fg2 = FG2_BACKUP_INDEX;
			loops = 0;
			while (cont < MIN_CONTRAST && loops < 8)
			{
				if (stat[bg1].y > 0.5f)
				{
					stat[fg2].rgbc = _mm_div_ps(stat[fg2].rgbc, _mm_set_ps(1, 2, 2, 2));
				}
				else
				{
					stat[fg2].rgbc = _mm_div_ps(_mm_add_ps(stat[fg2].rgbc, _mm_set_ps(0, 1, 1, 1)), _mm_set_ps(1, 2, 2, 2));
				}

				cont = contrast(&stat[fg2], &stat[bg1]);
				loops++;
			}
		}
	}

	// TODO: Sort picked colors by brightness to fill LIGHTx and DARKx

	chameleon->colorIndex[CHAMELEON_BACKGROUND1] = bg1;
	chameleon->colorIndex[CHAMELEON_BACKGROUND2] = bg2;
	chameleon->colorIndex[CHAMELEON_FOREGROUND1] = fg1;
	chameleon->colorIndex[CHAMELEON_FOREGROUND2] = fg2;
}

uint32_t chameleonGetColor(Chameleon *chameleon, ChameleonColor color)
{
	uint32_t result;

	uint16_t i = chameleon->colorIndex[color];

	float r = chameleon->colors[i].r;
	float g = chameleon->colors[i].g;
	float b = chameleon->colors[i].b;

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
		0.5f, // countWeight
		100.0f, // edgeWeight
		0.0f, // bg1distanceWeight
		0.0f, // fg1distanceWeight
		0.0f, // saturationWeight
		0.0f  // contrastWeight
	},

	// FG1
	{
		0.188f,
		-10.0f,
		78.0f,
		0.0f,
		190.0f,
		10.0f
	},

	// BG2
	{
		0.75f,
		10.0f,
		-1000.0f,
		1000.0f,
		0.0f,
		160.0f
	},

	// FG2
	{
		0.042f,
		-1.0f,
		24.0f,
		100.0f,
		146.0f,
		8.0f
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