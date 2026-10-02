#ifndef _FH2_SF2000_DIRENT_H
#define _FH2_SF2000_DIRENT_H

/* Filesystem directory access for the SF2000/GB300 firmware.
 *
 * Same content as the multicore frontend's include/dirent.h, which declares the
 * firmware-backed opendir/readdir/closedir triplet implemented in
 * src/libretro_frontend/lib.c. Vendored here under a distinct filename so this
 * core builds without a multicore checkout, and so it cannot shadow the system
 * <dirent.h> on host builds (src/fheroes2/system is on the include path).
 *
 * Declarations only: the implementations live in the frontend that produces
 * the final core_87000000 binary. Linking that binary needs all three symbols. */

#ifdef __cplusplus
extern "C"
{
#endif

#define DTYPE_UNKNOWN             0
#define DTYPE_DIRECTORY           4
#define DTYPE_FILE                8
#define DTYPE_LINK                10

#define DT_UNKNOWN                DTYPE_UNKNOWN
#define DT_REG                    DTYPE_FILE
#define DT_DIR                    DTYPE_DIRECTORY
#define DT_LNK                    DTYPE_LINK

#define DIRENT_ISFILE(type)      ((type) == DTYPE_FILE)
#define DIRENT_ISDIRECTORY(type) ((type) == DTYPE_DIRECTORY)

struct dirent
{
	unsigned char  d_type;
	char           d_name[255];
};

typedef void DIR;

DIR *opendir(const char *path);
int closedir(DIR *dir);
struct dirent *readdir(DIR *dir);

#ifdef __cplusplus
}
#endif

#endif