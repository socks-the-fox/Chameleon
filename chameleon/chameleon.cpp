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

	result->colorIndex[CHAMELEON_AVERAGE] = AVG_INDEX;

	result->colors = static_cast<ColorStat*>(_aligned_malloc(MAX_COLOR_STATS * sizeof(ColorStat), 16));
	
	memset(result->colors, 0, MAX_COLOR_STATS * sizeof(ColorStat));

	result->pixelcount = 1;
	result->edgecount = 1;
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

	__m128 rgbc;

	for (size_t i = 0; i < lineWidth; ++i)
	{
		// Ignore pixels that are mostly transparent
		if ((lineData[i] & 0xFF000000) < 0xC0000000)
			continue;

		cIndex = XRGB5(lineData[i]);

		rgbc = _mm_set_ps(1.0f, ((lineData[i] & 0x00FF0000) >> 16) / 255.0f, ((lineData[i] & 0x0000FF00) >> 8) / 255.0f, ((lineData[i] & 0x000000FF)) / 255.0f);

		stat[cIndex].rgbc = _mm_add_ps(rgbc, stat[cIndex].rgbc);
		stat[AVG_INDEX].rgbc = _mm_add_ps(rgbc, stat[AVG_INDEX].rgbc);

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

		chameleon->edgecount += 2;
	}
	else
	{
		chameleon->edgecount += lineWidth;
	}

	chameleon->pixelcount += lineWidth;
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
		if (imgWidth > 256)
		{
			newWidth = 256;
			widthStep = imgWidth / 255;
		}
		else
		{
			newWidth = imgWidth;
		}
		
		if (imgHeight > 256)
		{
			newHeight = 256;
			heightStep = imgHeight / 255;
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
		for (uint16_t i = 0; i < LAST_COLOR + 1; ++i)
		{
			if (stat[i].count)
			{
				fixRGB(&stat[i], chameleon->pixelcount);
				calcYUV(&stat[i], chameleon->edgecount);
			}
		}

		fixRGB(&stat[AVG_INDEX], chameleon->pixelcount);
		calcYUV(&stat[AVG_INDEX], chameleon->edgecount);

		chameleon->rgbFixed = true;
	}

	// First, find the first background color.

	uint16_t bg1 = AVG_INDEX;
	uint16_t bg2 = INVALID_INDEX;
	uint16_t fg1 = AVG_INDEX;
	uint16_t fg2 = INVALID_INDEX;

	const ChameleonParams *bg1Param = &params[0];
	const ChameleonParams *fg1Param = &params[1];
	const ChameleonParams *bg2Param = &params[2];
	const ChameleonParams *fg2Param = &params[3];

	float result = 0, temp = 0;

	for (uint16_t i = 0; i < LAST_COLOR + 1; ++i)
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
	result = 0;
	for (uint16_t i = 0; i < LAST_COLOR + 1; ++i)
	{
		if (i != bg1 && stat[i].count > 0)
		{
			temp = stat[i].count * fg1Param->countWeight;
			temp += stat[i].edgeCount * fg1Param->edgeWeight;
			temp += distance(&stat[i], &stat[bg1]) * fg1Param->bg1distanceWeight;
			temp += saturation(&stat[i]) * fg1Param->saturationWeight;
			temp += contrast(&stat[i], &stat[bg1]) * fg1Param->contrastWeight;

			if (temp > result)
			{
				fg1 = i;
				result = temp;
			}
		}
	}

	// Second background...
	result = 0;
	for (uint16_t i = 0; i < LAST_COLOR + 1; ++i)
	{
		if (i != bg1 && i != fg1 && stat[i].edgeCount > 0)
		{
			temp = stat[i].count * bg2Param->countWeight;
			temp += stat[i].edgeCount * bg2Param->edgeWeight;
			temp += distance(&stat[i], &stat[bg1]) * bg2Param->bg1distanceWeight;
			temp += distance(&stat[i], &stat[fg1]) * bg2Param->fg1distanceWeight;
			temp += saturation(&stat[i]) * bg2Param->saturationWeight;
			temp += contrast(&stat[i], &stat[fg1]) * bg2Param->contrastWeight;

			if (temp > result)
			{
				bg2 = i;
				result = temp;
			}
		}
	}

	// Second foreground...
	result = 0;
	for (uint16_t i = 0; i < LAST_COLOR + 1; ++i)
	{
		if (i != bg1 && i != fg1 && i != bg2 && stat[i].count > 0)
		{
			temp = stat[i].count * fg2Param->countWeight;
			temp += stat[i].edgeCount * fg2Param->edgeWeight;
			temp += distance(&stat[i], &stat[bg1]) * fg2Param->bg1distanceWeight;
			temp += distance(&stat[i], &stat[fg1]) * fg2Param->fg1distanceWeight;
			temp += saturation(&stat[i]) * fg2Param->saturationWeight;
			temp += contrast(&stat[i], &stat[bg1]) * fg2Param->contrastWeight;

			if (temp > result)
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

	if (fg2 == INVALID_INDEX)
	{
		fg2 = fg1;
	}

	if (forceContrast)
	{
		// Find the contrast between the background and the foreground
		float cont = contrast(&stat[fg1], &stat[bg1]);
		float factor = (cont / MIN_CONTRAST);

		if (stat[0].count == 0)
		{
			stat[0].rgbc = _mm_set_ps(0, 0, 0, 0);
		}

		if (stat[LAST_COLOR].count == 0)
		{
			stat[LAST_COLOR].rgbc = _mm_set_ps(0, 1, 1, 1);
		}

		if (cont < MIN_CONTRAST)
		{
			stat[FG1_BACKUP_INDEX] = stat[fg1];
			fg1 = FG1_BACKUP_INDEX;

			if (stat[bg1].y > 0.5f)
			{
				stat[fg1].rgbc = _mm_mul_ps(stat[fg1].rgbc, _mm_set_ps(1, factor, factor, factor));
			}
			else
			{
				stat[fg1].rgbc = _mm_div_ps(stat[fg1].rgbc, _mm_set_ps(1, factor, factor, factor));
				stat[fg1].rgbc = _mm_min_ps(stat[fg1].rgbc, _mm_set_ps(1, 1, 1, 1));
			}

			cont = contrast(&stat[fg1], &stat[bg1]);
			if (cont < MIN_CONTRAST)
			{
				if (stat[bg1].y > 0.5f)
				{
					fg1 = 0;
				}
				else
				{
					fg1 = LAST_COLOR;
				}
			}

			calcYUV(&stat[FG1_BACKUP_INDEX], 1);
		}

		cont = contrast(&stat[fg2], &stat[bg1]);
		factor = (cont / MIN_CONTRAST);

		if (cont < MIN_CONTRAST)
		{
			stat[FG2_BACKUP_INDEX] = stat[fg2];
			fg2 = FG2_BACKUP_INDEX;

			if (stat[bg1].y > 0.5f)
			{
				stat[fg2].rgbc = _mm_mul_ps(stat[fg2].rgbc, _mm_set_ps(1, factor, factor, factor));
			}
			else
			{
				stat[fg2].rgbc = _mm_div_ps(stat[fg2].rgbc, _mm_set_ps(1, factor, factor, factor));
				stat[fg2].rgbc = _mm_min_ps(stat[fg2].rgbc, _mm_set_ps(1, 1, 1, 1));
			}

			cont = contrast(&stat[fg2], &stat[bg1]);
			if (cont < MIN_CONTRAST)
			{
				if (stat[bg1].y > 0.5f)
				{
					fg2 = 0;
				}
				else
				{
					fg2 = LAST_COLOR;
				}
			}

			calcYUV(&stat[FG2_BACKUP_INDEX], 1);
		}

		if (contrast(&stat[fg1], &stat[bg2]) < MIN_CONTRAST || contrast(&stat[fg2], &stat[bg2]) < MIN_CONTRAST)
		{
			bg2 = bg1;
		}
	}

	// Sort picked colors by brightness to fill LIGHTx and DARKx

	chameleon->colorIndex[CHAMELEON_LIGHT1] = bg1;
	chameleon->colorIndex[CHAMELEON_LIGHT2] = bg2;
	chameleon->colorIndex[CHAMELEON_LIGHT3] = fg1;
	chameleon->colorIndex[CHAMELEON_LIGHT4] = fg2;

	for (size_t j = 0; j < 4; ++j)
	{
		for (size_t i = 0; i < (3 - j); ++i)
		{
			if (stat[chameleon->colorIndex[CHAMELEON_LIGHT1 + i]].y < stat[chameleon->colorIndex[CHAMELEON_LIGHT1 + i + 1]].y)
			{
				uint16_t color = chameleon->colorIndex[CHAMELEON_LIGHT1 + i];
				chameleon->colorIndex[CHAMELEON_LIGHT1 + i] = chameleon->colorIndex[CHAMELEON_LIGHT1 + i + 1];
				chameleon->colorIndex[CHAMELEON_LIGHT1 + i + 1] = color;
			}
		}
	}

	chameleon->colorIndex[CHAMELEON_DARK1] = chameleon->colorIndex[CHAMELEON_LIGHT4];
	chameleon->colorIndex[CHAMELEON_DARK2] = chameleon->colorIndex[CHAMELEON_LIGHT3];
	chameleon->colorIndex[CHAMELEON_DARK3] = chameleon->colorIndex[CHAMELEON_LIGHT2];
	chameleon->colorIndex[CHAMELEON_DARK4] = chameleon->colorIndex[CHAMELEON_LIGHT1];

	chameleon->colorIndex[CHAMELEON_BACKGROUND1] = bg1;
	chameleon->colorIndex[CHAMELEON_FOREGROUND1] = fg1;
	chameleon->colorIndex[CHAMELEON_BACKGROUND2] = bg2;
	chameleon->colorIndex[CHAMELEON_FOREGROUND2] = fg2;
}

uint32_t chameleonGetColor(Chameleon *chameleon, ChameleonColor color)
{
	uint32_t result;

	uint16_t i = chameleon->colorIndex[color];

	if (i == INVALID_INDEX)
	{
		i = AVG_INDEX;
	}

	result = int(chameleon->colors[i].r * 255) | (int(chameleon->colors[i].g * 255) << 8) | (int(chameleon->colors[i].b * 255) << 16) | 0xFF000000;

	return result;
}

float chameleonGetLuminance(Chameleon *chameleon, ChameleonColor color)
{
	uint16_t i = chameleon->colorIndex[color];

	if (i == INVALID_INDEX)
	{
		i = AVG_INDEX;
	}

	return chameleon->colors[i].y;
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
		0.300f, // countWeight
		1.000f, // edgeWeight
		0.000f, // bg1distanceWeight
		0.000f, // fg1distanceWeight
		0.000f, // saturationWeight
		0.000f  // contrastWeight
	},

	// FG1
	{
		 0.234f,
		-0.400f,
		 0.568f,
		 0.000f,
		 0.327f,
		 0.303f
	},

	// BG2
	{
		 1.000f,
		 0.619f,
		-0.830f,
		 0.500f,
		 0.000f,
		 0.000f
	},

	// FG2
	{
		 0.700f,
		-0.100f,
		 0.410f,
		 0.396f,
		 0.134f,
		 0.112f
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