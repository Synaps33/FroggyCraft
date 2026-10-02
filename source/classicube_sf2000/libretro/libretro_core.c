/*
libretro core wrapper for ClassiCube on Data Frog SF2000 / GB300.
Copyright 2014-2025 ClassiCube | Licensed under BSD-3

This is the file the Multicore/FrogUI frontend's loader looks up. It exposes the
standard libretro API and drives ClassiCube's game loop from retro_run().

ClassiCube's own entrypoint (src/main_impl.h) is not used here, because the
frontend drives execution: retro_init does the SetupProgram() equivalent and
retro_load_game starts either the launcher or straight into singleplayer.
*/
#include <stdio.h>
#include <string.h>

#include "libretro.h"
#include "Core.h"

#include "Game.h"
#include "Entity.h"
#include "Window.h"
#include "Platform.h"
#include "Logger.h"
#include "Options.h"
#include "Server.h"
#include "String_.h"

/* Firmware helper: flushes a file to the SD card. close() alone only pushes the
   data into the firmware cache, so it has to be synced explicitly. It only
   exists on the console, hence the guard. */
#ifdef __mips__
extern int fs_sync(const char* path);
#define CC_FS_SYNC(p) fs_sync(p)
#else
#define CC_FS_SYNC(p) do { } while (0)
#endif
#include "Funcs.h"
#include "Constants.h"
#include "Utils.h"
#include "Inventory.h"
#include "Formats.h"
#include "World.h"
#include "BlockID.h"
#include "String_.h"

/* Bridges implemented by src/sf2000/Window_SF2000.c */
extern void SF2000_SetVideoCallback(const void* data, unsigned width, unsigned height, size_t pitch);
extern int  SF2000_PollJoypad(unsigned port, unsigned id);
extern void SF2000_SetJoypadPort(unsigned port);

static retro_video_refresh_t  video_cb;
static retro_audio_sample_t   audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t     input_poll_cb;
static retro_input_state_t    input_state_cb;
static retro_environment_t    environ_cb;

static cc_bool gameStarted;
static cc_bool inGame;
static cc_bool shutdownRequested;
static cc_bool pixelFormatSet;


/*########################################################################################################################*
*------------------------------------------------------Framebuffer bridge------------------------------------------------*
*#########################################################################################################################*/
/* ClassiCube's software rasterizer calls this from Window_DrawFramebuffer */
void SF2000_SetVideoCallback(const void* data, unsigned width, unsigned height, size_t pitch) {
	if (!video_cb) return;
	video_cb(data, width, height, pitch);
}

int SF2000_PollJoypad(unsigned port, unsigned id) {
	if (!input_state_cb) return 0;
	return input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, id) ? 1 : 0;
}


/*########################################################################################################################*
*-----------------------------------------------------ClassiCube startup--------------------------------------------------*
*#########################################################################################################################*/
#ifdef __mips__
extern void xlog(const char* fmt, ...);
#define CC_DIAG(fmt, ...) xlog("[CC-LIBRETRO] " fmt "\n", ##__VA_ARGS__)
#else
#define CC_DIAG(fmt, ...) do {} while(0)
#endif

/* Equivalent of SetupProgram() in src/main_impl.h */
static void SF2000_PlatformInit(void) {
	CC_DIAG("SF2000_PlatformInit: 1 Logger_Hook");
	Logger_Hook();
	CC_DIAG("SF2000_PlatformInit: 2 Window_PreInit");
	Window_PreInit();
	CC_DIAG("SF2000_PlatformInit: 3 Gamepads_PreInit");
	Gamepads_PreInit();
	CC_DIAG("SF2000_PlatformInit: 4 Platform_Init");
	Platform_Init();
	CC_DIAG("SF2000_PlatformInit: 5 Platform_SetDefaultCurrentDirectory");
	Platform_SetDefaultCurrentDirectory();
	CC_DIAG("SF2000_PlatformInit: 6 Options_Load");
	Options_Load();
	CC_DIAG("SF2000_PlatformInit: 7 Window_Init");
	Window_Init();
	CC_DIAG("SF2000_PlatformInit: 8 Gamepads_Init");
	Gamepads_Init();
	CC_DIAG("SF2000_PlatformInit: 9 Starting message");

	Platform_LogConst("Starting " GAME_APP_NAME " (SF2000) ..");
	CC_DIAG("SF2000_PlatformInit: complete");
}


/*#########################################################################################################################
*-------------------------------------------------------Save / load (SRAM)-----------------------------------------------------*
*#########################################################################################################################*/
/* The Multicore frontend saves whatever retro_get_memory_data(RETRO_MEMORY_SAVE_RAM)
   points at into system/saves/<random world name>.srm and restores it into the
   same buffer on the next launch. Previously this core exposed size 0, so
   save_srm()/load_srm() bailed out immediately and nothing was ever written.

   Upstream ClassiCube has no world persistence of its own, so this stores the
   state that can actually be restored: the player's inventory/hotbar, the
   held block, the username and the player position in the generated world. */
#define CC_SAVE_MAGIC   0x43435553u /* "CCUS" */
#define CC_SAVE_VERSION 2u

struct CCSaveData {
	cc_uint32 magic;
	cc_uint32 version;
	char      worldName[24];   /* must fit the generated name without truncating */
	char      username[16];
	cc_uint32 gameTime;
	float     posX, posY, posZ;
	int       selectedIndex;
	int       hotbarIndex;
	cc_uint32 hasPlayer;
	BlockID   table[INVENTORY_HOTBARS * INVENTORY_BLOCKS_PER_HOTBAR];
};

static struct CCSaveData ccSaveData;

/* Randomly generated name for the current world. */
static cc_string ccWorldName;
static char ccWorldNameBuf[STRING_SIZE];
static cc_bool ccWorldNameReady;


/*########################################################################################################################*
*---------------------------------------------------------World map save/load------------------------------------------------------*
*#########################################################################################################################*/
/* ClassiCube can already write and read .cw maps (SaveLevelScreen / LoadLevelScreen
   in Menus.c), but both are only reachable from menus that need a keyboard, and
   this port skips the menus entirely. So the world is saved automatically to
   maps/<random world name>.cw, and restored from it on the next launch. */
static void Map_LoadAuto(void) {
	cc_string path;
	char pathBuffer[FILENAME_SIZE];
	cc_result res;

	if (!ccWorldName.length) return;
	String_InitArray(path, pathBuffer);
	String_Format2(&path, "maps/%s.cw", &ccWorldName, NULL);

	/* No save file is the normal case on a fresh world, and Map_LoadFrom
	   reports it through the logger, which pops an error dialog. Check first. */
	{
		cc_filepath raw;
		Platform_EncodePath(&raw, &path);
		if (!File_Exists(&raw)) {
			CC_DIAG("map: no saved world at %s, generating fresh\n", path.buffer);
			return;
		}
	}

	res = Map_LoadFrom(&path);
	CC_DIAG("map: load '%s' res=%d\n", path.buffer, res);
}

static void CCSave_EnsureWorldName(void) {
	cc_uint32 seed;
	int i;
	if (ccWorldNameReady) return;
	ccWorldNameReady = true;

	String_InitArray(ccWorldName, ccWorldNameBuf);

	/* The firmware's gettimeofday() has real sub-second resolution here. */
	seed = (cc_uint32)Stopwatch_Measure();
	if (!seed) seed = 0x1234567u;

	for (i = 0; i < 8; i++) {
		char pair[3];
		seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
		pair[0] = (char)('a' + (seed % 26));
		pair[1] = (char)('0' + ((seed >> 8) % 10));
		pair[2] = 0;
		String_AppendConst(&ccWorldName, pair);
	}
	CC_DIAG("save: new random world name '%s'\n", ccWorldName.buffer);
}

static void CCSave_Capture(void) {
	struct Entity* e;
	int i;
	if (!inGame) return;

	memset(&ccSaveData, 0, sizeof(ccSaveData));
	ccSaveData.magic   = CC_SAVE_MAGIC;
	ccSaveData.version = CC_SAVE_VERSION;

	String_CopyToRawArray(ccSaveData.username, &Game_Username);
	ccSaveData.gameTime = (cc_uint32)Game.Time;
	ccSaveData.selectedIndex = Inventory.SelectedIndex;
	ccSaveData.hotbarIndex   = Inventory.Offset;

	e = &Entities.CurPlayer->Base;
	ccSaveData.hasPlayer = 1;
	ccSaveData.posX = e->Position.x;
	ccSaveData.posY = e->Position.y;
	ccSaveData.posZ = e->Position.z;

	for (i = 0; i < INVENTORY_HOTBARS * INVENTORY_BLOCKS_PER_HOTBAR; i++) {
		ccSaveData.table[i] = Inventory.Table[i];
	}
}


/* Self-managed save file.
   The frontend only writes its .srm on a graceful unload, which a console
   never performs, so the core keeps its own file next to the game data. The
   file name is fixed (so the previous world is always found) while the *world
   name* inside it is generated randomly, giving every fresh world its own
   identity without losing the ability to load it again. */
static void CCSave_FileRead(void) {
	cc_filepath path;
	cc_string name;
	char nameBuf[STRING_SIZE];
	cc_file f;
	cc_uint32 read = 0;
	cc_result res;

	(void)nameBuf;
	name = (cc_string)String_FromConst("world.srm");
	Platform_EncodePath(&path, &name);

	res = File_Open(&f, &path);
	if (res) return;
	res = File_Read(f, &ccSaveData, sizeof(ccSaveData), &read);
	File_Close(f);

	if (res) return;
	if (read != sizeof(ccSaveData)) return;
	if (ccSaveData.magic != CC_SAVE_MAGIC) return;
	if (ccSaveData.version != CC_SAVE_VERSION) return;

	{
		int n = 0;
		while (n < (int)sizeof(ccSaveData.worldName) - 1 && ccSaveData.worldName[n]) n++;
		ccWorldName.length = 0;
		String_AppendAll(&ccWorldName, ccSaveData.worldName, n);
	}
	CC_DIAG("save: loaded world '%s'\n", ccWorldName.buffer);
}

static void CCSave_FileWrite(void) {
	cc_filepath path;
	cc_string name;
	char nameBuf[STRING_SIZE];
	cc_file f;
	cc_uint32 wrote = 0;

	if (ccSaveData.magic != CC_SAVE_MAGIC) return;

	(void)nameBuf;
	name = (cc_string)String_FromConst("world.srm");
	Platform_EncodePath(&path, &name);

	/* keep the name inside the file in sync with what we show */
	String_CopyToRawArray(ccSaveData.worldName, &ccWorldName);

	if (File_Create(&f, &path)) return;
	File_Write(f, &ccSaveData, sizeof(ccSaveData), &wrote);
	File_Close(f);
	CC_FS_SYNC(path.buffer);
	CC_DIAG("save: wrote %u bytes, world '%s'\n", wrote, ccSaveData.worldName);
}

static void CCSave_Restore(void) {
	int i;
	if (ccSaveData.magic != CC_SAVE_MAGIC) return;
	if (ccSaveData.version != CC_SAVE_VERSION) return;

	{
		/* copy back the stored name into the username string */
		int n = 0;
		while (n < 15 && ccSaveData.username[n]) n++;
		/* String_AppendAll appends at str->length, so the length has to be
		   reset too - clearing buffer[0] alone would leave the old length. */
		Game_Username.length = 0;
		String_AppendAll(&Game_Username, ccSaveData.username, n);
	}
	Inventory.SelectedIndex = ccSaveData.selectedIndex;
	Inventory.Offset       = ccSaveData.hotbarIndex;
	for (i = 0; i < INVENTORY_HOTBARS * INVENTORY_BLOCKS_PER_HOTBAR; i++) {
		Inventory.Table[i] = ccSaveData.table[i];
	}
	if (ccSaveData.hasPlayer) {
		struct Entity* e = &Entities.CurPlayer->Base;
		e->Position.x = ccSaveData.posX;
		e->Position.y = ccSaveData.posY;
		e->Position.z = ccSaveData.posZ;
	}
	CC_DIAG("save: restored hotbar (sel=%d bar=%d) pos=(%.0f,%.0f,%.0f)",
	        ccSaveData.selectedIndex, ccSaveData.hotbarIndex,
	        ccSaveData.posX, ccSaveData.posY, ccSaveData.posZ);
}

static void SF2000_ShutdownGame(void) {
	if (!inGame) return;
	CCSave_Capture();
	inGame = false;
	Game_Free();
	Window_Destroy();
}

cc_bool Game_ShouldClose(void) {
	if (!Game_Running) return true;
	return shutdownRequested;
}


/*########################################################################################################################*
*----------------------------------------------------------libretro API-----------------------------------------------------*
*#########################################################################################################################*/
RETRO_API void retro_set_environment(retro_environment_t cb) {
	environ_cb = cb;
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { audio_cb = cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }
/* Returns a bitmask of currently pressed joypad ids on the given libretro
   port (bit n == RETRO_DEVICE_ID_JOYPAD_n). Used by Window_ProcessEvents for
   diagnostics, so it also works on a port the core itself does not poll. */
unsigned SF2000_GetPortMask(unsigned port) {
	unsigned mask = 0;
	int k;
	if (!input_state_cb) return 0;
	for (k = 0; k < 16; k++) {
		if (input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, k)) mask |= (1u << k);
	}
	return mask;
}
RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device) {
	/* NOTE: deliberately ignored. The console has a single controller on
	   libretro port 0 (FrogUI's own menu polls "input_state_cb(0, JOYPAD,
	   0, id)"). The frontend calls this for port 0 *and then* port 1 after
	   retro_load_game, so honouring it would leave us polling port 1, which
	   the firmware never populates - every button would read as released. */
	(void)port; (void)device;
}

RETRO_API unsigned retro_api_version(void) {
	return RETRO_API_VERSION;
}

RETRO_API void retro_get_system_info(struct retro_system_info* info) {
	memset(info, 0, sizeof(*info));
	info->library_name     = "ClassiCube";
	info->library_version  = GAME_APP_VER;
	info->valid_extensions = "";
	info->need_fullpath    = false;
	info->block_extract    = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info* info) {
	memset(info, 0, sizeof(*info));
	info->timing.fps            = 60.0;
	info->timing.sample_rate    = 44100.0;
	info->geometry.base_width   = 320;
	info->geometry.base_height  = 240;
	info->geometry.max_width    = 320;
	info->geometry.max_height   = 240;
	info->geometry.aspect_ratio = 4.0f / 3.0f;
}

RETRO_API void retro_init(void) {
	CC_DIAG("retro_init enter");
	gameStarted = false;
	inGame = false;
	shutdownRequested = false;

	/* Request RGB565 from the frontend. This is only advisory here - the
	   software rasterizer always produces 16bpp and Window_DrawFramebuffer
	   converts to RGB565 on submit - but some frontends need the call. */
	if (environ_cb) {
		enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
		pixelFormatSet = environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt) ? true : false;
	}

	SF2000_PlatformInit();
	gameStarted = true;
	CC_DIAG("retro_init done");
}

RETRO_API void retro_deinit(void) {
	SF2000_ShutdownGame();
	Window_Free();
	gameStarted = false;
}

RETRO_API void retro_unload_game(void) {
	SF2000_ShutdownGame();
}

RETRO_API bool retro_load_game(const struct retro_game_info* game) {
	CC_DIAG("retro_load_game enter (game=%p, path=%s)", game, game ? game->path : "null");
	if (!gameStarted) return false;

	/* Console has no networking, so go straight into singleplayer world */
	Options_Get(LOPT_USERNAME, &Game_Username, DEFAULT_USERNAME);

	CC_DIAG("retro_load_game calling Game_Setup");
	Game_Setup();
	CCSave_EnsureWorldName();
	CCSave_FileRead();
	/* Loading is a single read at startup - cheap, and it is what makes a
	   menu-saved world actually come back. Saving stays menu driven.
	   This must come BEFORE World.Name is set below, because loading a map
	   resets the world and clears the name again. */
	/* NOTE: the world name is assigned on the first rendered frame, not here:
	   the world itself is only created once the game loop starts, and
	   World_SetNewMap() clears World.Name. */
	inGame = true;

	Platform_LogConst("ClassiCube SF2000: entering game");
	CC_DIAG("retro_load_game done, inGame=1");
	return true;
}

RETRO_API unsigned retro_get_region(void) {
	return RETRO_REGION_NTSC;
}

RETRO_API void* retro_get_memory_data(unsigned id) {
	if (id == RETRO_MEMORY_SAVE_RAM) return &ccSaveData;
	return NULL;
}

RETRO_API size_t retro_get_memory_size(unsigned id) {
	if (id == RETRO_MEMORY_SAVE_RAM) return sizeof(ccSaveData);
	return 0;
}

RETRO_API bool retro_load_game_special(unsigned type, const struct retro_game_info* info, size_t count) {
	(void)type; (void)info; (void)count;
	return false;
}

RETRO_API void retro_run(void) {
	static int run_count = 0;
	if (run_count < 5) {
		CC_DIAG("retro_run frame %d (inGame=%d, running=%d)", run_count++, inGame, Game_Running);
	}

#ifdef __mips__
	/* On-device diagnostics: the software rasterizer is far slower on the
	   console's clock-less MIPS than on the build host, so measure the real
	   frame time here and report it. Also used to tell "frozen" apart from
	   "running but slow" when testing on hardware. */
	{
		static cc_uint64 t0;
		static int nFrames;
		cc_uint64 now = Stopwatch_Measure();
		if (!t0) t0 = now;
		nFrames++;
		if (nFrames >= 60) {
			cc_uint64 dt = now - t0;
			struct Entity* e = &Entities.CurPlayer->Base;
			/* NOTE: integer only - soft-float newlib's vsnprintf may not
			   support %f, and ClassiCube's own formatter is not printf. */
			CC_DIAG("perf: frames=%d avg_us=%d pos=(%d,%d,%d)",
			        nFrames, (int)(dt / nFrames),
			        (int)e->Position.x, (int)e->Position.y, (int)e->Position.z);
			t0 = now; nFrames = 0;
		}
	}
#endif

	if (!gameStarted) return;

	/* ClassiCube only flushes options.txt from Game_Free(), which on a console
	   never runs - the process is simply killed when you leave the game. So
	   options appeared to "not save". Options_SaveIfChanged() returns
	   immediately unless something actually changed, so calling it per frame
	   costs one branch and writes the file at most once per edit. */
	if (Options_HasUnsavedChanges()) {
		Options_SaveIfChanged();
		{
			cc_string on; char onBuf[STRING_SIZE];
			cc_filepath op;
			(void)onBuf;
			on = (cc_string)String_FromConst("options.txt");
			Platform_EncodePath(&op, &on);
			CC_FS_SYNC(op.buffer);
		}
	}

	{
		/* The frontend restores the SRAM buffer after retro_load_game
		   returns, and the world is not created until the game loop runs,
		   so both the save restore and the world name must wait for the
		   first frame. */
		static int restored;
		if (!restored && inGame) {
			/* Restoring a previously saved world is a single read, done once
			   at startup. Saving stays menu driven. */
			Map_LoadAuto();

			/* Only consume the buffer once it actually holds a valid save:
			   if there is no save file the frontend leaves it zeroed, and
			   the per-frame CCSave_Capture() keeps it current. */
			if (ccSaveData.magic == CC_SAVE_MAGIC &&
			    ccSaveData.version == CC_SAVE_VERSION) {
				restored = 1;
				CCSave_Restore();
			}
		}
	}

	if (input_poll_cb) input_poll_cb();

	if (inGame && Game_Running) {
		Game_RenderFrame();
		/* The frontend serialises retro_get_memory_data(RETRO_MEMORY_SAVE_RAM)
		   at arbitrary points (opening the pause menu, unloading, ...), so the
		   buffer has to be current all the time - not only on shutdown.
		   It is only 136 bytes, so refreshing it per frame is free. */
		CCSave_Capture();

		/* The level is generated asynchronously, and World_SetNewMap() clears
		   World.Name whenever it runs. The pause menu's "Save level..." uses
		   that name as the file name, so keep restoring it while it is empty.
		   This is one integer compare per frame. */
		if (ccWorldName.length && !World.Name.length) {
			String_Copy(&World.Name, &ccWorldName);
		}

		/* Saving is menu driven (the pause menu's "Save level..." button).
		   Map_SaveTo() stamps World.LastSave, so that is the cue to persist
		   the small state file too - one write per manual save, no polling
		   and no periodic SD traffic. */
		{
			static int lastSaveStamp = -1;
			if (World.LastSave != lastSaveStamp) {
				lastSaveStamp = World.LastSave;
				CCSave_FileWrite();
			}
		}
		if (Game_ShouldClose()) SF2000_ShutdownGame();
		return;
	}

	/* Idle: present a dark grey frame rather than passing NULL, which the
	   frontend renders as pure black and makes a "hung" core
	   indistinguishable from a running one. */
	if (video_cb) {
		static uint16_t idleFrame[320 * 240];
		static int idleInit;
		if (!idleInit) {
			int i;
			/* RGB565 dark grey, so a stalled core is visually distinct */
			for (i = 0; i < 320 * 240; i++) idleFrame[i] = 0x0842;
			idleInit = 1;
		}
		video_cb(idleFrame, 320, 240, 320 * 2);
	}
}

RETRO_API void retro_reset(void) { }

RETRO_API size_t retro_serialize_size(void) { return 0; }
RETRO_API bool retro_serialize(void* data, size_t size) { (void)data; (void)size; return false; }
RETRO_API bool retro_unserialize(const void* data, size_t size) { (void)data; (void)size; return false; }
RETRO_API void retro_cheat_reset(void) { }
RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char* code) {
	(void)index; (void)enabled; (void)code;
}