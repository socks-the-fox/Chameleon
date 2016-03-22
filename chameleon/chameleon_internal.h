#define INVALID_INDEX UINT16_MAX
#define MAX_COLOR_STATS 67
#define LAST_COLOR 0x3F
#define MIN_CONTRAST (3.5f / 21.0f)

#define FG1_BACKUP_INDEX 64
#define FG2_BACKUP_INDEX 65
#define AVG_INDEX 66

struct ColorStat
{
	union
	{
		__m128 rgbc;
		struct
		{
			float r;
			float g;
			float b;
			float count;
		};
	};

	union
	{
		__m128 yuve;
		struct
		{
			float y;
			float u;
			float v;
			float edgeCount;
		};
	};
};

struct Chameleon
{
	// List of colors Chameleon has picked for the image
	uint16_t colorIndex[CHAMELEON_COLORS];

	// Set of color data
	ColorStat *colors;

	// Whether or not we've already fixed the RGB values so we don't screw them up
	float pixelcount;
	float edgecount;
	bool rgbFixed;
};

inline uint16_t XRGB5(uint32_t c)
{
	uint8_t b = (c >> 22) & 0x03;
	uint8_t g = (c >> 14) & 0x03;
	uint8_t r = (c >>  6) & 0x03;

	return (r << 4) | (g << 2) | b;
}

void fixRGB(ColorStat *color, float pixelcount);
void calcYUV(ColorStat *color, float edgecount);
float saturation(const ColorStat *color);
float distance(const ColorStat *c1, const ColorStat *c2);
float contrast(const ColorStat *c1, const ColorStat *c2);