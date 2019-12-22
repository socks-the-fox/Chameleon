#pragma once

struct Image;
struct ColorStat;

// Formats a uint32_t RGBA color for the Chameleon
// internal functions to use without fussing with all
// of the various bits and bobs of fully using Chameleon
void processRGB(uint32_t color, ColorStat *colorStat);

// switch to the fallback colors defined by the user
void useDefaultColors(std::shared_ptr<Image> img);

// Simple helper to read a measure input as a bool
bool RmReadBool(void *rm, LPCWSTR option, bool defValue, BOOL replaceMeasures = 1);

// Simple helper to read a hex r,g,b value to a uint32_t RGBA color
uint32_t RmReadColor(void *rm, LPCWSTR option, uint32_t defValue, BOOL replaceMeasures = 1);
