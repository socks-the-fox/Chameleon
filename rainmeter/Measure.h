#pragma once

enum MeasureType
{
	MEASURE_CONTAINER,
	MEASURE_BG1,
	MEASURE_BG2,
	MEASURE_FG1,
	MEASURE_FG2,
	MEASURE_AVG_LUM,
	MEASURE_AVG_COLOR,
	MEASURE_L1,
	MEASURE_L2,
	MEASURE_L3,
	MEASURE_L4,
	MEASURE_D1,
	MEASURE_D2,
	MEASURE_D3,
	MEASURE_D4
};

enum ImageType
{
	IMG_DESKTOP,
	IMG_FILE
};

struct Image
{
	void *rm;
	void *skin;
	HWND hWnd;
	std::wstring name;
	ImageType type;
	std::wstring path;
	FILETIME lastMod;
	RECT cropRect;
	RECT cachedCrop;
	LONG skinX;
	LONG skinY;
	bool draggingSkin;
	bool dirty;
	bool forceIcon;
	bool customCrop;
	bool contextAware;
	RECT contextRect;

	uint32_t bg1;
	uint32_t bg2;
	uint32_t fg1;
	uint32_t fg2;

	uint32_t l1;
	uint32_t l2;
	uint32_t l3;
	uint32_t l4;

	uint32_t d1;
	uint32_t d2;
	uint32_t d3;
	uint32_t d4;

	uint32_t fallback_bg1;
	uint32_t fallback_bg2;
	uint32_t fallback_fg1;
	uint32_t fallback_fg2;

	float lum;
	uint32_t avg;
};

struct Measure
{
	MeasureType type;
	std::shared_ptr<Image> parent;
	bool useHex;
	std::wstring value;
};
