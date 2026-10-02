#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <file.h>
#include <images.h>
#include <libretro.h>
#include <logging.h>

// Takes uncompressed 24-bit bmp and turns it into raw RGB565
uint16_t* load_bmp_image(const char *file_path, unsigned *out_width, unsigned *out_height, unsigned *out_pitch) {
    if (out_width)  *out_width = 0;
    if (out_height) *out_height = 0;
    if (out_pitch)  *out_pitch = 0;

    FILE *file = fopen(file_path, "rb");
    if (!file) {
        frontend_log_cb(RETRO_LOG_INFO, "FRONTEND" ,"Failed to open BMP: %s\n", file_path);
        return NULL;
    }

    BMPFileHeader file_header;
    BMPInfoHeader info_header;

    if (fread(&file_header, sizeof(BMPFileHeader), 1, file) != 1 ||
        fread(&info_header, sizeof(BMPInfoHeader), 1, file) != 1) {
        frontend_log_cb(RETRO_LOG_ERROR, "FRONTEND" ,"Failed to read BMP headers.\n");
        fclose(file);
        return NULL;
    }

    if (file_header.type != 0x4D42 || info_header.bits_per_pixel != 24 || info_header.compression != 0) {
        frontend_log_cb(RETRO_LOG_ERROR, "FRONTEND" ,"Unsupported BMP format. Must be uncompressed 24-bit.\n");
        fclose(file);
        return NULL;
    }

    unsigned width = info_header.width;
    unsigned height = abs(info_header.height);
    bool flip_vertical = (info_header.height > 0);
    unsigned pitch = width * sizeof(uint16_t);

    uint16_t *rgb565_buffer = (uint16_t *)malloc(width * height * sizeof(uint16_t));
    if (!rgb565_buffer) {
        frontend_log_cb(RETRO_LOG_ERROR, "FRONTEND" ,"Memory allocation failed for RGB565 buffer.\n");
        fclose(file);
        return NULL;
    }

    if (fseek(file, file_header.offset, SEEK_SET) != 0) {
        frontend_log_cb(RETRO_LOG_ERROR, "FRONTEND" ,"Failed to seek to pixel data.\n");
        free(rgb565_buffer);
        fclose(file);
        return NULL;
    }

    int row_padding = (4 - (width * 3) % 4) % 4;
    uint8_t bgr_pixel[3];

    for (int y = 0; y < height; y++) {
        int target_y = flip_vertical ? (height - 1 - y) : y;
        uint16_t *row_ptr = rgb565_buffer + (target_y * width);

        for (int x = 0; x < width; x++) {
            if (fread(bgr_pixel, 3, 1, file) != 1) {
                break;
            }

            uint16_t r = (bgr_pixel[2] & 0xF8) << 8;
            uint16_t g = (bgr_pixel[1] & 0xFC) << 3;
            uint16_t b = (bgr_pixel[0] & 0xF8) >> 3;

            row_ptr[x] = r | g | b;
        }
        fseek(file, row_padding, SEEK_CUR);
    }

    fclose(file);

    if (out_width)  *out_width = width;
    if (out_height) *out_height = height;
    if (out_pitch)  *out_pitch = pitch;

    return rgb565_buffer;
}

// Takes raw RGB565 or XRGB8888 and turns it into uncompressed 24-bit bmp
void save_bmp_image(const void *data, unsigned width, unsigned height, char *filename, bool xrgb888) {
    unsigned char* framebuffer = (unsigned char*)data;
    unsigned char* img_data = (unsigned char*)malloc(width * height * 3);

    if (xrgb888) {
        // If it's XRGB8888 format (32-bit per pixel), we need to extract R, G, B components
        for (unsigned i = 0; i < width * height; i++) {
            uint32_t color = ((uint32_t*)data)[i];

            // Extract RGB components
            uint8_t r = (color >> 16) & 0xFF;
            uint8_t g = (color >> 8) & 0xFF;
            uint8_t b = color & 0xFF;

            // Store in img_data (24-bit color, RGB order)
            img_data[i * 3 + 0] = b;
            img_data[i * 3 + 1] = g;
            img_data[i * 3 + 2] = r;
        }
    } else {
        // If it's RGB565 format, convert to RGB888 (24-bit)
        for (unsigned i = 0; i < width * height; i++) {
            uint16_t color = ((uint16_t*)data)[i];

            // Extract RGB565 components
            uint8_t r = (color >> 11) & 0x1F;
            uint8_t g = (color >> 5) & 0x3F;
            uint8_t b = color & 0x1F;

            // Convert to 8-bit RGB (scale 5-bit to 8-bit)
            r = (r << 3) | (r >> 2);
            g = (g << 2) | (g >> 4);
            b = (b << 3) | (b >> 2);

            // Store in img_data (24-bit color, RGB order)
            img_data[i * 3 + 0] = b;  
            img_data[i * 3 + 1] = g;  
            img_data[i * 3 + 2] = r; 
        }
    }

    // BMP Header setup
    BMPFileHeader bmp_header = {0};
    BMPInfoHeader bmp_info_header = {0};

    bmp_header.type = 0x4D42;  // 'BM' in little-endian
    bmp_header.offset = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader);
    bmp_header.size = bmp_header.offset + width * height * 3;

    bmp_info_header.size = sizeof(BMPInfoHeader);
    bmp_info_header.width = width;
    bmp_info_header.height = -height;  // Negative height to indicate top-down BMP
    bmp_info_header.planes = 1;
    bmp_info_header.bits_per_pixel = 24;  // 24 bits per pixel (RGB)
    bmp_info_header.image_size = width * height * 3;

    FILE *bmp_file = fopen(filename, "wb");
    if (bmp_file) {
        // Write BMP header and info header
        fwrite(&bmp_header, sizeof(BMPFileHeader), 1, bmp_file);
        fwrite(&bmp_info_header, sizeof(BMPInfoHeader), 1, bmp_file);

        fwrite(img_data, 1, width * height * 3, bmp_file);

        fclose(bmp_file);
		fs_sync(filename);
        frontend_log_cb(RETRO_LOG_INFO, "FRONTEND" ,"Screenshot saved to %s\n", filename);
    } else frontend_log_cb(RETRO_LOG_ERROR, "FRONTEND" ,"Failed to save screenshot\n");

    free(img_data);
}

// Function to load a raw RGB565 image into the framebuffer
int load_rgb565_image(const char* filename, uint16_t* framebuffer, int width, int height) {
    // Open the raw image file
    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        frontend_log_cb(RETRO_LOG_ERROR, "FRONTEND" ,"Error opening file: %s\n", filename);
        return -1;
    }

    // Calculate the number of bytes to read (width * height * 2 bytes per pixel)
    size_t image_size = width * height * sizeof(uint16_t);

    // Read the raw RGB565 image data into the framebuffer
    size_t bytes_read = fread(framebuffer, 1, image_size, file);
    if (bytes_read != image_size) {
        frontend_log_cb(RETRO_LOG_ERROR, "FRONTEND" ,"Error reading the image file\n");
        fclose(file);
        return -1;
    }

    // Close the file after reading the image
    fclose(file);

    return 0;
}