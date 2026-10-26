// stb_image for builds without RT64 (host tests). The game links RT64, whose
// texture cache already compiles this implementation from the same header.
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
