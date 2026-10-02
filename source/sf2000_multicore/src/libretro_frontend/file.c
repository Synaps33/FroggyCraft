#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

#include <deimos.h>
#include <file.h>
#include <libretro.h>
#include <logging.h>
#include <options.h>

// Make a directory if it doesn't exist
int create_dir(const char *path) {
	if (fs_access(path, 0) != 0) {
        frontend_log_cb(RETRO_LOG_INFO, "FRONTEND" ,"filepath: creating %s\n", path);
        fs_mkdir(path, 0755);
		fs_sync(path);
    }
}

void extract_extension(const char *filename, char **extension) {
    char *dot = strrchr(filename, '.');
	if (dot == NULL) {
        *extension = NULL;
    } else {
        *extension = strdup(dot + 1);
    }
}

void extract_path_components(const char *filepath, char **dir, char **filename, char **extension) {
    // Copy filepath to avoid modifying the original string
    char *path_copy = strdup(filepath);

    // Extract directory part by finding the last slash
    char *last_slash = strrchr(path_copy, '/');
    *last_slash = '\0';
    *dir = strdup(path_copy);
    *filename = strdup(last_slash + 1);
    extract_extension(*filename, extension);

	if (*extension != NULL) {
        char *dot = strrchr(*filename, '.');
        if (dot != NULL) {
            *dot = '\0';
        }
    }

    free(path_copy);
}

/*		SRM Files		*/
/* Cores that generate a fresh world on every launch (ClassiCube on SF2000
   does) would otherwise derive the save file name from the mangled ROM path
   the firmware hands us - e.g. "mc.srm" - and overwrite the previous world's
   save. Give each session its own random world name instead, derived from the
   core name so the files stay identifiable, and generated once per session so
   that saving and loading stay consistent within a run. */
static char srm_basename[MAXPATH];

static void init_srm_basename(void) {
	struct timeval tv;
	uint32_t seed;
	char suffix[17];
	int i;

	if (srm_basename[0]) return;

	if (gettimeofday(&tv, NULL) != 0) { tv.tv_sec = 0; tv.tv_usec = 0; }
	/* gettimeofday() gives real sub-second resolution here (Stopwatch_Measure
	   relies on it), so the boot time already varies per session. */
	seed = (uint32_t)tv.tv_sec * 2654435761u ^ ((uint32_t)tv.tv_usec << 8);

	/* xorshift, so the name is not a trivial function of the timestamp */
	for (i = 0; i < 8; i++) {
		seed ^= seed << 13;
		seed ^= seed >> 17;
		seed ^= seed << 5;
		suffix[i * 2]     = 'a' + (char)(seed % 26);
		suffix[i * 2 + 1] = '0' + (char)((seed >> 8) % 10);
	}
	suffix[16] = '\0';

	snprintf(srm_basename, MAXPATH, "%s-%s", sysinfo.library_name, suffix);
	frontend_log_cb(RETRO_LOG_INFO, "FRONTEND", "random world name: %s\n", srm_basename);
}

static void build_srm_filepath(char *filepath, size_t size, const char *basename, const char *extension, int slot, const char * type) {
    char ext[5];
    if (slot) snprintf(ext, 5, "%s%d", extension, slot);
    else snprintf(ext, 5, "%s", extension);
    
	char directory[MAXPATH] = SAVE_DIRECTORY;
	create_dir(directory); // Make sure SAVE_DIRECTORY exists 

	if(g_per_core_srm){
		snprintf(directory, size, "%s/%s", SAVE_DIRECTORY, sysinfo.library_name);
		create_dir(directory);	// Make sure SAVE_DIRECTORY/sysinfo.library_name exists 
	}

	snprintf(filepath, size, "%s/%s.%s", directory, basename, extension);
    frontend_log_cb(RETRO_LOG_INFO, "FRONTEND" ,"%s_%s file: %s\n", type, extension, filepath);
}

void save_srm(int slot) {
    char ram_filepath[MAXPATH];

    char basename[MAXPATH];
	init_srm_basename();
	snprintf(basename, sizeof(basename), "%s", srm_basename);

    // Save SRM
    build_srm_filepath(ram_filepath, sizeof(ram_filepath), basename, "srm", slot, "Save");
    size_t save_size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);	
	if(save_size == 0) return;
	FILE *ram_file = fopen(ram_filepath, "wb");
	if (!ram_file) return;
	fwrite(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), save_size, 1, ram_file);
    fclose(ram_file);
    fs_sync(ram_filepath);

	// Save RTC
    build_srm_filepath(ram_filepath, sizeof(ram_filepath), basename, "rtc", slot, "Save");
    size_t rtc_size = retro_get_memory_size(RETRO_MEMORY_RTC);
    if (rtc_size == 0) return;
    FILE *rtc_file = fopen(ram_filepath, "wb");
    if (!rtc_file) return;
    fwrite(retro_get_memory_data(RETRO_MEMORY_RTC), rtc_size, 1, rtc_file);
    fclose(rtc_file);
    fs_sync(ram_filepath);
}

void load_srm(int slot) {
    char ram_filepath[MAXPATH];

    char basename[MAXPATH];
	init_srm_basename();
	snprintf(basename, sizeof(basename), "%s", srm_basename);

    // Load SRM
    build_srm_filepath(ram_filepath, sizeof(ram_filepath), basename, "srm", slot, "Load");
	size_t save_size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    FILE *ram_file = fopen(ram_filepath, "rb");
	if (!ram_file) return;
	fseeko(ram_file, 0, SEEK_END);
	size_t ram_file_size = ftell(ram_file);
	fseeko(ram_file, 0, SEEK_SET);

	if(ram_file_size < save_size){
		save_size = ram_file_size;
	}

	if(save_size == 0){
		fclose(ram_file);
		return;
	}

	fread(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), 1, save_size, ram_file);
	fclose(ram_file);

	// Load RTC
    build_srm_filepath(ram_filepath, sizeof(ram_filepath), basename, "rtc", slot, "Load");
    size_t rtc_size = retro_get_memory_size(RETRO_MEMORY_RTC);
    FILE *rtc_file = fopen(ram_filepath, "rb");
    if (!rtc_file) return;
    fseeko(rtc_file, 0, SEEK_END);
    size_t rtc_file_size = ftell(rtc_file);
    fseeko(rtc_file, 0, SEEK_SET);

    if (rtc_file_size < rtc_size) {
        rtc_size = rtc_file_size;
    }

    if (rtc_size == 0) {
        fclose(rtc_file);
        return;
    }

    fread(retro_get_memory_data(RETRO_MEMORY_RTC), 1, rtc_size, rtc_file);
    fclose(rtc_file);
}