#define INVALID_INDEX UINT16_MAX
#define MAX_COLOR_STATS 74
#define LAST_COLOR 0x3F
#define MIN_CONTRAST 4.0f

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
	uint8_t b = (c >> 22) & 0x03;
	uint8_t g = (c >> 14) & 0x03;
	uint8_t r = (c >>  6) & 0x03;

	return (r << 4) | (g << 2) | b;
}

void fixRGB(ColorStat *color);
void calcYUV(ColorStat *color);
float saturation(const ColorStat *color);
float distance(const ColorStat *c1, const ColorStat *c2);
float contrast(const ColorStat *c1, const ColorStat *c2);