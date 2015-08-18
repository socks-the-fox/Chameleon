#define INVALID_INDEX UINT16_MAX
#define MAX_COLOR_STATS 4096
#define MIN_CONTRAST 3.5f

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
	// Index of first BG color
	uint16_t background1;

	// Index of second BG color
	uint16_t background2;

	// Index of first FG color
	uint16_t foreground1;

	// Index of second FG color
	uint16_t foreground2;

	// Set of color data
	ColorStat *colors;
};

inline uint16_t XRGB5(uint32_t c)
{
	uint8_t b = (c >> 20) & 0x0F;
	uint8_t g = (c >> 12) & 0x0F;
	uint8_t r = (c >>  4) & 0x0F;

	return (r << 8) | (g << 4) | b;
}

void fixRGB(ColorStat *color);
void calcYUV(ColorStat *color);
float saturation(const ColorStat *color);
float distance(const ColorStat *c1, const ColorStat *c2);
float contrast(const ColorStat *c1, const ColorStat *c2);