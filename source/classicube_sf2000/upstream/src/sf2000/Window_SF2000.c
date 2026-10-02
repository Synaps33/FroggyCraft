/*
Window layer for Data Frog SF2000 / GB300 running under the Multicore/FrogUI
libretro frontend. Copyright 2014-2025 ClassiCube | Licensed under BSD-3

The console's screen is a fixed 320x240 RGB565 framebuffer, so there is no
window management to speak of: Window_AllocFramebuffer just allocates a
ClassiCube Bitmap, and Window_DrawFramebuffer hands it straight to the libretro
video refresh callback provided by the frontend.

Input is polled through the libretro input state callback in Window_ProcessEvents.
*/
#include "Core.h"

/* NOTE: _WindowBase.h is not used here, it would conflict with Window_ShowDialog */
#include "Window.h"
#include "Platform.h"
#include "String_.h"
#include "Funcs.h"
#include "Bitmap.h"
#include "Options.h"
#include "Errors.h"
#include "Utils.h"
#include "Input.h"
#include "Gui.h"
#include "libretro.h"
#include "Event.h"
#include "Logger.h"
#include "Graphics.h"
#include "Game.h"

#include <stdlib.h>
#include <string.h>

/* Native panel resolution of both SF2000 and GB300 */
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

/* Default internal render mode when options.txt has nothing saved.
   0 = 320x240 (full), 1 = 160x120, 2 = 80x60. */
#ifndef SF2000_DEFAULT_RENDER_MODE
	#define SF2000_DEFAULT_RENDER_MODE 1
#endif

/* Internal render resolutions. Each is an integer fraction of the panel so the
   upscale stays a plain pixel replication. Index = value stored in options.txt
   under "sf2000-render-mode", so it survives a restart. */
static const int renderModeW[] = { SCREEN_WIDTH, 160,  80, 213 };
static const int renderModeH[] = { SCREEN_HEIGHT, 120, 60, 160 };

static int sf2000_renderMode = SF2000_DEFAULT_RENDER_MODE;

/* Backend hook: render the 3D world at w x h while the 2D UI is drawn at the
   full panel resolution. Implemented in Graphics_SoftFP.c. */
extern void Gfx_SetWorldSize(int w, int h);

int  SF2000_GetRenderMode(void) { return sf2000_renderMode; }
void SF2000_SetRenderMode(int mode);

/* NOTE: normally provided by _WindowBase.h, which isn't used here */
struct _DisplayData DisplayInfo;
struct cc_window WindowInfo;

/* Implemented in libretro/libretro_core.c */
extern void SF2000_SetVideoCallback(const void* data, unsigned width, unsigned height, size_t pitch);
extern int  SF2000_PollJoypad(unsigned port, unsigned id);
extern void SF2000_SetJoypadPort(unsigned port);
extern unsigned SF2000_GetPortMask(unsigned port);

static unsigned joypadPort;

/* printf-style logger provided by the Multicore frontend (src/debug.c).
   Use this instead of Platform_Log*() for anything with format specifiers,
   because Platform_Log*() feeds ClassiCube's own String_Format4(). */
#ifdef __mips__
extern void xlog(const char* fmt, ...);
#define SF2000_Diag(...) xlog(__VA_ARGS__)
#define SF2000_HAVE_INPUT() (SF2000_GetPortMask(joypadPort) | SF2000_GetPortMask(1))
#else
#define SF2000_Diag(...) do { } while (0)
#define SF2000_HAVE_INPUT() 0
#endif


/*########################################################################################################################*
*------------------------------------------------------General data-------------------------------------------------------*
*#########################################################################################################################*/
void Window_PreInit(void) {
	DisplayInfo.CursorVisible = false;
}

void Window_Init(void) {
	/* Options are loaded before Window_Init, so the persisted mode is
	   available here. */
	sf2000_renderMode = Options_GetInt("sf2000-render-mode", 0, 3, SF2000_DEFAULT_RENDER_MODE);

	Input.Sources = INPUT_SOURCE_GAMEPAD;

	DisplayInfo.Width   = SCREEN_WIDTH;
	DisplayInfo.Height  = SCREEN_HEIGHT;
	DisplayInfo.Depth   = 16;
	DisplayInfo.ScaleX  = 0.5f;
	DisplayInfo.ScaleY  = 0.5f;
	DisplayInfo.x       = 0;
	DisplayInfo.y       = 0;
	/* Software rasterizer + libretro framebuffer: always present whole frame */
	DisplayInfo.FullRedraw = true;

	Window_Main.Width    = DisplayInfo.Width;
	Window_Main.Height   = DisplayInfo.Height;
	Window_Main.Focused  = true;
	Window_Main.Exists   = true;
	Window_Main.UIScaleX = DEFAULT_UI_SCALE_X;
	Window_Main.UIScaleY = DEFAULT_UI_SCALE_Y;
}

void Window_Free(void) { }

/*#########################################################################################################################
*-------------------------------------------------Render scale (revertible)-------------------------------------------------*
*#########################################################################################################################*/
/* The software rasterizer is fill-rate bound on this CPU, and view distance is
   not the only cost: every pixel goes through a 64-bit divide for perspective
   correct texturing. Rendering at half resolution quarters the pixel count.

   This is the ONE place to change it back. Set to 1 for full 320x240:

       #define SF2000_RENDER_SCALE 1

   The upscaled frame is still handed to the frontend at 320x240 with
   RGB565, so nothing downstream (or in the frontend) changes either way. */
#ifndef SF2000_DEFAULT_RENDER_MODE
	#define SF2000_DEFAULT_RENDER_MODE 1   /* 320x240, 160x120, 80x60 */
#endif

static void DoCreateWindow(int width, int height) {
	Window_Main.Exists   = true;
	Window_Main.Focused  = true;

	/* The UI is always laid out at panel resolution, so that buttons and text
	   stay crisp even when the 3D world renders smaller. Only the world
	   buffer size follows sf2000_renderMode. */
	if (sf2000_renderMode < 0) sf2000_renderMode = 0;
	if (sf2000_renderMode >= Array_Elems(renderModeW)) sf2000_renderMode = 0;

	(void)width; (void)height;
	Window_Main.Width    = SCREEN_WIDTH;
	Window_Main.Height   = SCREEN_HEIGHT;
	DisplayInfo.Width    = SCREEN_WIDTH;
	DisplayInfo.Height   = SCREEN_HEIGHT;
	Window_Main.UIScaleX = DEFAULT_UI_SCALE_X;
	Window_Main.UIScaleY = DEFAULT_UI_SCALE_Y;

	/* 0,0 means "same as the display buffer" i.e. full resolution world. */
	Gfx_SetWorldSize(renderModeW[sf2000_renderMode], renderModeH[sf2000_renderMode]);
}

void Window_Create2D(int width, int height) { DoCreateWindow(width, height); }
/* Applies a render mode chosen in the options menu: resizes the 3D buffers and
   forces the upscale buffer to be reallocated. */
/* Applying a resize frees the 3D framebuffer and depth buffer. Doing that from
   the options menu means doing it *while* a frame is being rendered - the menu
   is drawn into that same buffer - which left the renderer with dangling
   pointers and froze the game. So only record the request here and apply it at
   the start of the next frame, before anything is drawn. */
static int  sf2000_pendingMode = -1;

void SF2000_ApplyPendingRenderMode(void) {
	int mode;
	if (sf2000_pendingMode < 0) return;
	mode = sf2000_pendingMode;
	sf2000_pendingMode = -1;

	sf2000_renderMode = mode;
	DoCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT);   /* also calls Gfx_SetWorldSize */
	Gfx_OnWindowResize(SCREEN_WIDTH, SCREEN_HEIGHT);
	Event_RaiseVoid(&WindowEvents.Resized);
}

void SF2000_SetRenderMode(int mode) {
	if (mode < 0 || mode >= (int)Array_Elems(renderModeW)) return;
	if (mode == sf2000_renderMode && mode != sf2000_pendingMode) return;
	Options_SetInt("sf2000-render-mode", mode);
	sf2000_pendingMode = mode;
	if (!Game_Running) {
		/* no frame in flight, safe to apply immediately */
		sf2000_pendingMode = -1;
		sf2000_renderMode = mode;
	}
}

void Window_Create3D(int width, int height) { DoCreateWindow(width, height); }
void Window_Destroy(void) { }

void Window_SetTitle(const cc_string* title) { }

void Clipboard_GetText(cc_string* value) { }
void Clipboard_SetText(const cc_string* value) { }

int Window_GetWindowState(void) { return WINDOW_STATE_FULLSCREEN; }
cc_result Window_EnterFullscreen(void) { return 0; }
cc_result Window_ExitFullscreen(void)  { return 0; }
int Window_IsObscured(void)            { return 0; }

void Window_Show(void) { }

void Window_SetSize(int width, int height) {
	Game_UpdateDimensions();
	Gfx_OnWindowResize(Window_Main.Width, Window_Main.Height);
	Event_RaiseVoid(&WindowEvents.Resized);
}

void Window_RequestClose(void) {
	Event_RaiseVoid(&WindowEvents.Closing);
}


/*########################################################################################################################*
*----------------------------------------------------Input processing-----------------------------------------------------*
*#########################################################################################################################*/
/* Gamepad buttons are CCPAD_* relative to GAMEPAD_BEG_BTN; the frontend maps
   RETRO_DEVICE_ID_JOYPAD_* ids, so translate them once. */
static const unsigned padMap[] = {
	/* RETRO_DEVICE_ID_JOYPAD_* order is B(0),Y(1),SELECT(2),START(3),UP(4),
	   DOWN(5),LEFT(6),RIGHT(7),A(8),X(9),L(10),R(11),L2(12),R2(13),L3(14),R3(15) */
	CCPAD_2,   /* B */
	CCPAD_4,   /* Y */
	CCPAD_SELECT,
	CCPAD_START,
	CCPAD_UP,
	CCPAD_DOWN,
	CCPAD_LEFT,
	CCPAD_RIGHT,
	CCPAD_1,   /* A */
	CCPAD_3,   /* X */
	CCPAD_L,
	CCPAD_R,
	CCPAD_ZL,
	CCPAD_ZR,
	CCPAD_LSTICK,
	CCPAD_RSTICK,
};
#define PAD_MAP_COUNT (sizeof(padMap) / sizeof(padMap[0]))

void Window_ProcessEvents(float delta) {
	int i;

	/* Safe point: no frame is being drawn yet, so the 3D buffers can be
	   resized if the options menu asked for it. */
	SF2000_ApplyPendingRenderMode();

	/* Build a bitmask of every RETRO joypad id, and log whenever it changes.
	   On hardware this is the only way to tell "controls not wired up"
	   (mask never changes) apart from "game too slow to react". */
	{
		static unsigned lastMask = 0xFFFFFFFFu;
		static int loggedPorts = 0;
		unsigned mask = 0;
		for (i = 0; i < (int)PAD_MAP_COUNT; i++) {
			if (SF2000_PollJoypad(joypadPort, i)) mask |= (1u << i);
		}
		if (!loggedPorts && SF2000_HAVE_INPUT()) {
			SF2000_Diag("input: pollPort=%u  mask(port0)=0x%04X  mask(port1)=0x%04X\n",
			            joypadPort, SF2000_GetPortMask(0), SF2000_GetPortMask(1));
			loggedPorts = 1;
		}
		if (mask != lastMask) {
			/* NOTE: must use xlog (printf style) here. Platform_Log*() goes
			   through ClassiCube's own String_Format4(), which only accepts
			   its own specifiers (%i %b %f0 %h %x ...) and calls
			   Process_Abort() on anything else. */
			SF2000_Diag("input: joypad mask=0x%04X (changed)\n", mask);
			lastMask = mask;
		}
	}

	for (i = 0; i < (int)PAD_MAP_COUNT; i++) {
		int pressed = SF2000_PollJoypad(joypadPort, i);
		/* While a menu has input grab the buttons are delivered as key codes
		   below instead. Doing both raised two input events per press, so
		   the D-pad moved the selection by two. */
		Gamepad_SetButton(0, padMap[i], pressed && !Gui.InputGrab);
	}

	/* Virtual keyboard for menus.
	   Gamepad presses arrive with a device whose enterButton/escapeButton are
	   not populated, so Menu_InputSelected() never sees them as "Enter" and
	   no menu item can be activated with the face buttons. NormDevice does
	   map CCKEY_ENTER / CCKEY_ESCAPE, so while a screen has input grab we
	   mirror the console buttons onto the equivalent key codes. */
	if (Gui.InputGrab) {
		static const struct { unsigned id; int key; } guiKeys[] = {
			{ RETRO_DEVICE_ID_JOYPAD_UP,     CCKEY_UP     },
			{ RETRO_DEVICE_ID_JOYPAD_DOWN,   CCKEY_DOWN   },
			{ RETRO_DEVICE_ID_JOYPAD_LEFT,  CCKEY_LEFT   },
			{ RETRO_DEVICE_ID_JOYPAD_RIGHT, CCKEY_RIGHT  },
			{ RETRO_DEVICE_ID_JOYPAD_A,     CCKEY_ENTER  },
			{ RETRO_DEVICE_ID_JOYPAD_B,     CCKEY_ESCAPE },
			{ RETRO_DEVICE_ID_JOYPAD_X,     CCKEY_DELETE },
			/* NOTE: START is deliberately NOT mapped to CCKEY_ESCAPE. START is
			   the pause bind, so it opens the pause menu; emitting ESCAPE in
			   the same frame closed it again and the menu appeared dead.
			   Leave a menu with the "Back to Game" button instead. */
		};
		for (i = 0; i < (int)Array_Elems(guiKeys); i++) {
			int pressed = SF2000_PollJoypad(joypadPort, guiKeys[i].id);
			Input_SetNonRepeatable(guiKeys[i].key, pressed);
		}
	}
}

void Cursor_SetPosition(int x, int y) { }
void Window_EnableRawMouse(void)  { }
void Window_DisableRawMouse(void) { }
void Window_UpdateRawMouse(void)  { }


/*########################################################################################################################*
*-------------------------------------------------------Gamepads----------------------------------------------------------*
*#########################################################################################################################*/
void Gamepads_PreInit(void) { }

/* Console specific button layout.
   The console has no mouse and no keyboard, so looking around is done with the
   face buttons, hotbar switching with the D-pad sideways, and option values
   are stepped with LEFT/RIGHT on the menus (see MenuOptionsScreen_InputDown).
   Order must match enum InputBind_ in Input.h.

     D-pad up/down ... walk forward / back
     D-pad left/right  previous / next block in the hotbar
     X / B ......... look up / down
     Y / A ......... look left / right
     SELECT ........ jump, and fly up when flying
     MENU .......... start (set spawn), and fly down when flying
     L / R ......... break / place block
     L + R ......... toggle fly mode
     ZL / ZR ....... strafe left / right (if the pad has triggers)
*/
static const BindMapping SF2000_BindDefaults[BIND_COUNT] = {
	{ CCPAD_UP,   0 }, { CCPAD_DOWN,  0 }, /* BIND_FORWARD, BIND_BACK */
	{ CCPAD_ZL, 0 },   { CCPAD_ZR, 0 },   /* BIND_LEFT, BIND_RIGHT (strafe) */
	{ CCPAD_SELECT, 0 }, { 0, 0 },         /* BIND_JUMP, BIND_RESPAWN */
	{ CCPAD_START, 0 }, { 0, 0 },          /* BIND_SET_SPAWN, BIND_CHAT */
	{ 0, 0 },           { 0, 0 },          /* BIND_INVENTORY, BIND_FOG */
	{ 0, 0 },           { 0, 0 },          /* BIND_SEND_CHAT, BIND_TABLIST */
	{ 0, 0 },           { 0, 0 },          /* BIND_SPEED, BIND_NOCLIP */
	{ CCPAD_L, CCPAD_R },                   /* BIND_FLY (L + R together) */
	{ CCPAD_SELECT, 0 }, { CCPAD_START, 0 },/* BIND_FLY_UP, BIND_FLY_DOWN */
	{ 0, 0 },           { 0, 0 },          /* BIND_EXT_INPUT, BIND_HIDE_FPS */
	{ 0, 0 },           { 0, 0 },          /* BIND_SCREENSHOT, BIND_FULLSCREEN */
	{ 0, 0 },           { 0, 0 },          /* BIND_THIRD_PERSON, BIND_HIDE_GUI */
	{ 0, 0 },           { 0, 0 },          /* BIND_AXIS_LINES, BIND_ZOOM_SCROLL */
	{ 0, 0 },                               /* BIND_HALF_SPEED */
	{ CCPAD_L, 0 },     { 0, 0 }, { CCPAD_R, 0 }, /* DELETE, PICK, PLACE block */
	{ 0, 0 },           { 0, 0 },          /* BIND_AUTOROTATE, BIND_HOTBAR_SWITCH */
	{ 0, 0 },           { 0, 0 },          /* BIND_SMOOTH_CAMERA, BIND_DROP_BLOCK */
	{ 0, 0 },           { 0, 0 },          /* BIND_IDOVERLAY, BIND_BREAK_LIQUIDS */
	{ CCPAD_3, 0 },     { CCPAD_2, 0 },    /* LOOK_UP (X),  LOOK_DOWN (B) */
	{ CCPAD_1, 0 },     { CCPAD_4, 0 },    /* LOOK_RIGHT (A), LOOK_LEFT (Y) */
	{ 0, 0 }, { 0, 0 }, { 0, 0 },          /* BIND_HOTBAR_1..3 */
	{ 0, 0 }, { 0, 0 }, { 0, 0 },          /* BIND_HOTBAR_4..6 */
	{ 0, 0 }, { 0, 0 }, { 0, 0 },          /* BIND_HOTBAR_7..9 */
	{ CCPAD_LEFT, 0 },  { CCPAD_RIGHT, 0 }  /* BIND_HOTBAR_LEFT, BIND_HOTBAR_RIGHT */
};

void Gamepads_Init(void) {
	/* Register the single controller ClassiCube uses, with console bindings */
	Gamepad_Connect(1, SF2000_BindDefaults);
}

void Gamepads_Process(float delta) { }


/*########################################################################################################################*
*------------------------------------------------------Framebuffer--------------------------------------------------------*
*#########################################################################################################################*/
void Window_AllocFramebuffer(struct Bitmap* bmp, int width, int height) {
	bmp->scan0  = (BitmapCol*)Mem_Alloc(width * height, BITMAPCOLOR_SIZE, "window pixels");
	bmp->width  = width;
	bmp->height = height;
}

/* ClassiCube's internal 16bpp bitmaps are 5-5-5 plus an alpha bit, whereas the
   libretro frontend expects RGB565. Convert a frame in place on submission. */
static void ConvertToRGB565_Rect(BitmapCol* pixels, int w, int h) {
	int i, size = w * h;

	/* Same maths as a plain loop, but unrolled 4x and without the named
	   temporaries. This touches every pixel of every frame, so on a soft-float
	   CPU the shift/or sequence is worth minimising. Output is bit-identical. */
	for (i = 0; i + 4 <= size; i += 4) {
		cc_uint32 a, g5;

		a = pixels[i + 0]; g5 = (a >> 5) & 0x1F;
		pixels[i + 0] = (BitmapCol)(((a & 0x1F) << 11) | (((g5 << 1) | (g5 >> 4)) << 5) | ((a >> 10) & 0x1F));
		a = pixels[i + 1]; g5 = (a >> 5) & 0x1F;
		pixels[i + 1] = (BitmapCol)(((a & 0x1F) << 11) | (((g5 << 1) | (g5 >> 4)) << 5) | ((a >> 10) & 0x1F));
		a = pixels[i + 2]; g5 = (a >> 5) & 0x1F;
		pixels[i + 2] = (BitmapCol)(((a & 0x1F) << 11) | (((g5 << 1) | (g5 >> 4)) << 5) | ((a >> 10) & 0x1F));
		a = pixels[i + 3]; g5 = (a >> 5) & 0x1F;
		pixels[i + 3] = (BitmapCol)(((a & 0x1F) << 11) | (((g5 << 1) | (g5 >> 4)) << 5) | ((a >> 10) & 0x1F));
	}
	for (; i < size; i++) {
		cc_uint32 a  = pixels[i];
		cc_uint32 g5 = (a >> 5) & 0x1F;
		pixels[i] = (BitmapCol)(((a & 0x1F) << 11) | (((g5 << 1) | (g5 >> 4)) << 5) | ((a >> 10) & 0x1F));
	}
}

static void ConvertToRGB565(struct Bitmap* bmp) {
	ConvertToRGB565_Rect(bmp->scan0, bmp->width, bmp->height);
}

void Window_DrawFramebuffer(Rect2D r, struct Bitmap* bmp) {
	if (!bmp->scan0) return;

	/* The frontend only accepts a full frame, and the software rasterizer
	   already renders into this buffer, so just submit all of it. */

	/* The backend already upscales the world into a full size buffer before
	   the UI is drawn, so normally bmp is already SCREEN_WIDTH x SCREEN_HEIGHT.
	   The fallback below only triggers if the split is disabled. */
	if (bmp->width != SCREEN_WIDTH || bmp->height != SCREEN_HEIGHT) {
		static BitmapCol* upscaled;
		static int upW, upH;
		int sx, sy;
		int srcW = bmp->width, srcH = bmp->height;

		if (upW != SCREEN_WIDTH || upH != SCREEN_HEIGHT) {
			upW = SCREEN_WIDTH; upH = SCREEN_HEIGHT;   /* forces realloc below */
			Mem_Free(upscaled);
			upscaled = (BitmapCol*)Mem_Alloc(upW * upH, BITMAPCOLOR_SIZE, "upscaled frame");
			if (!upscaled) { upW = upH = 0; return; }
		}
		if (!upscaled) return;

		for (sy = 0; sy < upH; sy++) {
			BitmapCol* dstRow = upscaled + (size_t)sy * upW;
			const BitmapCol* srcRow = (const BitmapCol*)bmp->scan0
				+ (size_t)((sy * srcH) / upH) * srcW;
			/* Index by ratio, not by an integer scale: 213x160 is 2/3 of the
			   panel and is not an integer divisor. upW/upH are the fixed
			   panel size, so the divisors are never zero. */
			for (sx = 0; sx < upW; sx++)
				dstRow[sx] = srcRow[(sx * srcW) / upW];
		}

#ifndef SF2000_NO_RGB565_CONVERT
		ConvertToRGB565_Rect(upscaled, upW, upH);
#endif
		SF2000_SetVideoCallback(upscaled, (unsigned)upW, (unsigned)upH,
			(size_t)upW * BITMAPCOLOR_SIZE);
		return;
	}

#ifndef SF2000_NO_RGB565_CONVERT
	ConvertToRGB565(bmp);
#endif
	SF2000_SetVideoCallback(bmp->scan0, (unsigned)bmp->width, (unsigned)bmp->height,
		(size_t)bmp->width * BITMAPCOLOR_SIZE);
}

void Window_FreeFramebuffer(struct Bitmap* bmp) {
	Mem_Free(bmp->scan0);
	bmp->scan0 = NULL;
}


/*########################################################################################################################*
*------------------------------------------------------Soft keyboard------------------------------------------------------*
*#########################################################################################################################*/
void OnscreenKeyboard_Open(struct OpenKeyboardArgs* args) { }
void OnscreenKeyboard_SetText(const cc_string* text) { }
void OnscreenKeyboard_Close(void) { }


/*########################################################################################################################*
*-------------------------------------------------------Misc/Other--------------------------------------------------------*
*#########################################################################################################################*/
void Window_ShowDialog(const char* title, const char* msg) {
	Platform_LogConst(title);
	Platform_LogConst(msg);
}

cc_result Window_OpenFileDialog(const struct OpenFileDialogArgs* args) {
	return ERR_NOT_SUPPORTED;
}

cc_result Window_SaveFileDialog(const struct SaveFileDialogArgs* args) {
	return ERR_NOT_SUPPORTED;
}

void SF2000_SetJoypadPort(unsigned port) {
	joypadPort = port;
}