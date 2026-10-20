/* Copyright (c) 2026 mio. GIF 首帧补充；其他图片优先使用系统服务。 */
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS
#define STBI_MAX_DIMENSIONS 1920
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
unsigned char *sb_gif(const unsigned char *data, int bytes, int *width, int *height) {
    int components = 0;
    return stbi_load_from_memory(data, bytes, width, height, &components, 4);
}
