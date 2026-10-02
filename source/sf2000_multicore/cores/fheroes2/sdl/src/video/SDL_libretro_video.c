/*
  SDL 1.2 libretro video driver
*/

#include "SDL_config.h"

#if SDL_VIDEO_DRIVER_LIBRETRO

#include "SDL_video.h"
#include "SDL_mouse.h"
#include "SDL_sysvideo.h"
#include "SDL_pixels_c.h"
#include "SDL_events_c.h"

#include <stdint.h>

#define _THIS SDL_VideoDevice *this

#define LIBRETRO_DRIVER_NAME "libretro"

/* 320x240 RGB565 screen buffer */
uint16_t s_libretro_framebuffer[320 * 240];

extern void libretro_video_flip(const void *pixels, int width, int height, int pitch);
extern void libretro_poll_events(void);

static int LIBRETRO_Available(void)
{
    return 1;
}

static int LIBRETRO_VideoInit(_THIS, SDL_PixelFormat *vformat)
{
    vformat->BitsPerPixel = 16;
    vformat->BytesPerPixel = 2;
    vformat->Rloss = 3;
    vformat->Gloss = 2;
    vformat->Bloss = 3;
    vformat->Aloss = 8;
    vformat->Rshift = 11;
    vformat->Gshift = 5;
    vformat->Bshift = 0;
    vformat->Ashift = 0;
    vformat->Rmask = 0xF800;
    vformat->Gmask = 0x07E0;
    vformat->Bmask = 0x001F;
    vformat->Amask = 0;

    return 0;
}

static SDL_Rect **LIBRETRO_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags)
{
    return (SDL_Rect **) -1;
}

static SDL_Surface *LIBRETRO_SetVideoMode(_THIS, SDL_Surface *current,
                                         int width, int height, int bpp, Uint32 flags)
{
    current->flags = flags | SDL_PREALLOC;
    current->w = 320;
    current->h = 240;
    current->pitch = 320 * sizeof(uint16_t);
    current->pixels = s_libretro_framebuffer;

    if (!SDL_ReallocFormat(current, 16, 0xF800, 0x07E0, 0x001F, 0)) {
        return NULL;
    }

    return current;
}

static void LIBRETRO_UpdateRects(_THIS, int numrects, SDL_Rect *rects)
{
    (void)numrects;
    (void)rects;
    libretro_video_flip(s_libretro_framebuffer, 320, 240, 320 * sizeof(uint16_t));
}

static int LIBRETRO_AllocHWSurface(_THIS, SDL_Surface *surface) { return -1; }
static void LIBRETRO_FreeHWSurface(_THIS, SDL_Surface *surface) {}
static int LIBRETRO_LockHWSurface(_THIS, SDL_Surface *surface) { return 0; }
static void LIBRETRO_UnlockHWSurface(_THIS, SDL_Surface *surface) {}
static void LIBRETRO_VideoQuit(_THIS) {}

static void LIBRETRO_InitOSKeymap(_THIS)
{
}

static void LIBRETRO_PumpEvents(_THIS)
{
    libretro_poll_events();
}

static void LIBRETRO_DeleteDevice(SDL_VideoDevice *device)
{
    if (device) {
        SDL_free(device);
    }
}

static SDL_VideoDevice *LIBRETRO_CreateDevice(int devindex)
{
    SDL_VideoDevice *device = (SDL_VideoDevice *)SDL_malloc(sizeof(SDL_VideoDevice));
    if (!device) return NULL;
    SDL_memset(device, 0, sizeof(*device));

    device->VideoInit = LIBRETRO_VideoInit;
    device->ListModes = LIBRETRO_ListModes;
    device->SetVideoMode = LIBRETRO_SetVideoMode;
    device->UpdateRects = LIBRETRO_UpdateRects;
    device->VideoQuit = LIBRETRO_VideoQuit;
    device->AllocHWSurface = LIBRETRO_AllocHWSurface;
    device->LockHWSurface = LIBRETRO_LockHWSurface;
    device->UnlockHWSurface = LIBRETRO_UnlockHWSurface;
    device->FreeHWSurface = LIBRETRO_FreeHWSurface;
    device->InitOSKeymap = LIBRETRO_InitOSKeymap;
    device->PumpEvents = LIBRETRO_PumpEvents;
    device->free = LIBRETRO_DeleteDevice;

    return device;
}

VideoBootStrap LIBRETRO_bootstrap = {
    LIBRETRO_DRIVER_NAME, "SDL libretro video driver",
    LIBRETRO_Available, LIBRETRO_CreateDevice
};

#endif /* SDL_VIDEO_DRIVER_LIBRETRO */
