#include <cstdint>
#include <intrin.h>
#include "chameleon.h"
#include "chameleon_internal.h"

void fixRGB(ColorStat *color)
{
	__m128 countm;
	countm.m128_f32[0] = countm.m128_f32[1] = countm.m128_f32[2] = color->count;
	countm.m128_f32[3] = 1.0f;
	color->rgbc = _mm_div_ps(color->rgbc, countm);
}

void calcYUV(ColorStat *color)
{
	// From http://www.fourcc.org/fccyvrgb.php
	// TODO: SIMDize this?
	color->y = (0.299f * color->r) + (0.587f * color->g) + (0.114f * color->b);
	color->u = 0.492f * (color->b - color->y);
	color->v = 0.877f * (color->r - color->y);
}

float saturation(const ColorStat *color)
{
	if (color->r == 0 && color->g == 0 && color->b == 0)
	{
		return 0;
	}

	float max = color->r;
	max = (max > color->g ? max : color->g);
	max = (max > color->b ? max : color->b);


	float min = color->r;
	min = (min < color->g ? min : color->g);
	min = (min < color->b ? min : color->b);

	float result = (max - min) / (max + min);

	return result;
}

float distance(const ColorStat *c1, const ColorStat *c2)
{
	// TODO: SIMDize this
	float x = c1->y - c2->y;
	float y = c1->u - c2->u;
	float z = c1->v - c2->v;

	return x*x + y*y + z*z;
}

float contrast(const ColorStat *c1, const ColorStat *c2)
{
	// TODO: actual sRGB conversion stuff
	// TODO: SIMDize this?
	float L1 = 0.2126f * c1->r + 0.7152f * c1->g + 0.0722f * c1->b;
	float L2 = 0.2126f * c2->r + 0.7152f * c2->g + 0.0722f * c2->b;

	if (L1 > L2)
	{
		return (L1 + 0.05f) / (L2 + 0.05f);
	}

	return (L2 + 0.05f) / (L1 + 0.05f);
}
