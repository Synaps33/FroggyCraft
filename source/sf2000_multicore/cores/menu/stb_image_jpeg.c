// stb_image JPEG decoder implementation for FrogUI
// Configured for SF2000: JPEG only, no SIMD, no stdio

#define STBI_ONLY_JPEG        // Only compile JPEG decoder
#define STBI_NO_SIMD          // No SSE2/NEON - SF2000 is MIPS
#define STBI_NO_STDIO         // We'll use our own file I/O
#define STBI_NO_HDR           // No HDR support needed
#define STBI_NO_LINEAR        // No linear light conversion
#define STBI_NO_THREAD_LOCALS // No TLS - embedded MIPS has no TLS runtime

// Use our own assert
#define STBI_ASSERT(x)

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
