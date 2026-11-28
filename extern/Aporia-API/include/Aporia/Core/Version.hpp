#pragma once

// Version of the public API contract, not of any particular engine branch.
// Bump it whenever a header under include/Aporia changes in a way that engine
// hosts or other backends must know about.

#define APORIA_API_VERSION_MAJOR 1
#define APORIA_API_VERSION_MINOR 0
#define APORIA_API_VERSION_PATCH 0

#define APORIA_API_VERSION_STRING "1.0.0"

// 0xMMmmpp: major << 16 | minor << 8 | patch
#define APORIA_API_VERSION \
    ((APORIA_API_VERSION_MAJOR << 16) | (APORIA_API_VERSION_MINOR << 8) | APORIA_API_VERSION_PATCH)
