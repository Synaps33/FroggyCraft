#include <stdio.h>
#include "Core.h"
#include "Logger.h"
#include "Window.h"
#include "Game.h"
#include "Platform.h"
#include "Options.h"
#include "String_.h"
#include "Errors.h"
#include "Funcs.h"
#include "Utils.h"
#include "Constants.h"

int main(void) {
	void (*logfn)(const char*, int) = NULL;
	setvbuf(stdout, NULL, _IONBF, 0);
	printf("A: starting %s\n", GAME_APP_NAME); fflush(stdout);

	Logger_Hook();
	printf("B: Logger_Hook ok\n"); fflush(stdout);
	Window_PreInit();
	Gamepads_PreInit();
	Platform_Init();
	Platform_SetDefaultCurrentDirectory();
	printf("C: platform init ok\n"); fflush(stdout);

	Options_Load();
	printf("D: Options_Load ok (count=%d)\n", Options.count); fflush(stdout);

	Window_Init();
	printf("E: Window_Init ok %dx%d\n", Window_Main.Width, Window_Main.Height); fflush(stdout);

	Gamepads_Init();
	printf("F: Gamepads_Init ok\n"); fflush(stdout);
	(void)logfn;
	return 0;
}
