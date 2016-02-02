#include <cstdint>
#include <intrin.h>
#include <math.h>
#include "chameleon.h"
#include "chameleon_internal.h"

void fixRGB(ColorStat *color)
{
	// The _mm_set_ps starts with the 1 (for keeping the count the same) because of the whole little-endian deal
	color->rgbc = _mm_div_ps(color->rgbc, _mm_set_ps(1, color->count, color->count, color->count));
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
	// Don't bother with sqrt-ing because we only care that A *is* further from B, but not by how much.
	__m128 xyz = _mm_sub_ps(c1->yuve, c2->yuve);
	xyz = _mm_mul_ps(xyz, _mm_set_ps(0, 1, 1, 1));
	xyz = _mm_mul_ps(xyz, xyz);

	// Horizontal add would be nice here...
	return xyz.m128_f32[0] + xyz.m128_f32[1] + xyz.m128_f32[2] + xyz.m128_f32[3];
}

float contrast(const ColorStat *c1, const ColorStat *c2)
{
	// TODO: SIMDize this?
	float r1 = (c1->r > 0.03928f) ? static_cast<float>(pow((c1->r + 0.055f) / 1.055f, 2.4f)) : c1->r / 12.92f;
	float g1 = (c1->g > 0.03928f) ? static_cast<float>(pow((c1->g + 0.055f) / 1.055f, 2.4f)) : c1->g / 12.92f;
	float b1 = (c1->b > 0.03928f) ? static_cast<float>(pow((c1->b + 0.055f) / 1.055f, 2.4f)) : c1->b / 12.92f;

	float L1 = 0.2126f * r1 + 0.7152f * g1 + 0.0722f * b1;

	float r2 = (c2->r > 0.03928f) ? static_cast<float>(pow((c2->r + 0.055f) / 1.055f, 2.4f)) : c2->r / 12.92f;
	float g2 = (c2->g > 0.03928f) ? static_cast<float>(pow((c2->g + 0.055f) / 1.055f, 2.4f)) : c2->g / 12.92f;
	float b2 = (c2->b > 0.03928f) ? static_cast<float>(pow((c2->b + 0.055f) / 1.055f, 2.4f)) : c2->b / 12.92f;

	float L2 = 0.2126f * r2 + 0.7152f * g2 + 0.0722f * b2;

	if (L1 > L2)
	{
		return (L1 + 0.05f) / (L2 + 0.05f);
	}

	return (L2 + 0.05f) / (L1 + 0.05f);
}
