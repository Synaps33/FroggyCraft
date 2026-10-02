#ifndef __FILE_H
#define __FILE_H

#define MAXPATH 	255

#define SDCARD_DIRECTORY      "/mnt/sda1"
#define SYSTEM_DIRECTORY      SDCARD_DIRECTORY "/system/bios"
#define ASSETS_DIRECTORY      SDCARD_DIRECTORY "/system/assets"
#define LOGS_DIRECTORY        SDCARD_DIRECTORY "/system/logs"
#define LOG_FILENAME          LOGS_DIRECTORY   "/Multicore.log"
#define SAVE_DIRECTORY        SDCARD_DIRECTORY "/system/saves"
#define CONFIG_DIRECTORY      SDCARD_DIRECTORY "/system/configs"
#define CORES_DIRECTORY       SDCARD_DIRECTORY "/system/Deimos/cores"
#define SAVESTATE_DIRECTORY   SDCARD_DIRECTORY "/system/Deimos/savestates"
#define BORDERS_DIRECTORY     SDCARD_DIRECTORY "/system/Deimos/borders"
#define ROMS_DIRECTORY        SDCARD_DIRECTORY "/ROMS"

extern int create_dir(const char *path);
extern void extract_extension(const char *filename, char **extension);
extern void extract_path_components(const char *filepath, char **dir, char **filename, char **extension);

extern void save_srm(int slot);
extern void load_srm(int slot);

#endif //__FILE_H
