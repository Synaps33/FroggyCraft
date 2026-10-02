/*
Platform layer for Data Frog SF2000 / GB300 running under the Multicore/FrogUI
libretro frontend. Copyright 2014-2025 ClassiCube | Licensed under BSD-3

The console runs a newlib based firmware, and the Multicore loader provides the
usual C library entry points (open/read/write/lseek/close/stat/mkdir/dirent,
malloc via sbrk, gettimeofday). So this backend mostly only has to wire up
ClassiCube's platform abstraction on top of them.
*/
#define CC_NO_UPDATER
#define CC_NO_DYNLIB
#define CC_NO_SOCKETS
#define CC_NO_THREADING
#define CC_NO_OPEN
#define CC_NO_ENCRYPTION
#define CC_NO_CRASHHANDLER
/* NOTE: Platform_GetCommandLineArgs is implemented below using GetGameArgs() */

#include "../Stream.h"
#include "../ExtMath.h"
#include "../Funcs.h"
#include "../Window.h"
#include "../Utils.h"
#include "../Errors.h"
#include "../Options.h"
#include "../PackedCol.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>

const char* Platform_AppNameSuffix  = " SF2000";
cc_bool Platform_ReadonlyFilesystem = false;
cc_uint8 Platform_Flags = PLAT_FLAG_SINGLE_PROCESS | PLAT_FLAG_APP_EXIT;
#include "../_PlatformBase.h"


/*########################################################################################################################*
*-----------------------------------------------------Directory/File------------------------------------------------------*
*#########################################################################################################################*/
/* NOTE: errno values are returned as error codes, same as the POSIX backend */
const cc_result ReturnCode_FileShareViolation = 1000000000; /* unused */
const cc_result ReturnCode_FileNotFound     = ENOENT;
const cc_result ReturnCode_PathNotFound     = ENOENT;
const cc_result ReturnCode_DirectoryExists  = EEXIST;

void Platform_EncodePath(cc_filepath* dst, const cc_string* path) {
	char* str = dst->buffer;
#ifdef __mips__
	static const char prefix[] = "/mnt/sda1/ROMS/classicube/";
	int prelen = sizeof(prefix) - 1;
	/* If path is relative (does not start with '/'), prepend the ROMS/classicube directory */
	if (path->length > 0 && path->buffer[0] != '/') {
		memcpy(str, prefix, prelen);
		String_EncodeUtf8(str + prelen, path);
		str[NATIVE_STR_LEN - 1] = '\0';
		return;
	}
#endif
	String_EncodeUtf8(str, path);
	str[NATIVE_STR_LEN - 1] = '\0';
}

void Platform_DecodePath(cc_string* dst, const cc_filepath* path) {
	const char* str = path->buffer;
	String_AppendUtf8(dst, str, String_Length(str));
}

void Directory_GetCachePath(cc_string* path) { }

cc_result Directory_Create2(const cc_filepath* path) {
	/* read/write/search for owner and group, read/search for others */
	return mkdir(path->buffer, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1 ? errno : 0;
}

int File_Exists(const cc_filepath* path) {
	struct stat sb;
	return stat(path->buffer, &sb) == 0 && S_ISREG(sb.st_mode);
}

cc_result Directory_Enum(const cc_string* dirPath, void* obj, Directory_EnumCallback callback) {
	cc_string path; char pathBuffer[FILENAME_SIZE];
	cc_filepath raw;
	DIR* dirPtr;
	struct dirent* entry;
	char* src;
	int len, res, is_dir;

	Platform_EncodePath(&raw, dirPath);
	dirPtr = opendir(raw.buffer);
	if (!dirPtr) return errno;

	/* POSIX: readdir only sets errno when the end is NOT reached */
	errno = 0;
	String_InitArray(path, pathBuffer);

	while ((entry = readdir(dirPtr))) {
		path.length = 0;
		String_Format1(&path, "%s/", dirPath);

		/* ignore '.' and '..' entries */
		src = entry->d_name;
		if (src[0] == '.' && (src[1] == '\0' || (src[1] == '.' && src[2] == '\0'))) {
			errno = 0;
			continue;
		}

		len = String_Length(src);
		String_AppendUtf8(&path, src, len);

		#ifdef DTYPE_DIRECTORY
		/* Firmware dirent.h (SF2000) */
		is_dir = (entry->d_type == DTYPE_DIRECTORY);
#elif defined DT_DIR
		/* Native/newlib dirent.h */
		is_dir = (entry->d_type == DT_DIR);
#else
		is_dir = 0;
#endif
		callback(&path, obj, is_dir);
		errno = 0;
	}

	res = errno;
	closedir(dirPtr);
	return res;
}

static cc_result File_Do(cc_file* file, const char* path, int mode) {
	int fd = open(path, mode, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (fd < 0) {
		*file = 0;
		return errno ? errno : ENOENT;
	}
	*file = fd;
	return 0;
}

cc_result File_Open(cc_file* file, const cc_filepath* path) {
	return File_Do(file, path->buffer, O_RDONLY);
}
cc_result File_Create(cc_file* file, const cc_filepath* path) {
	return File_Do(file, path->buffer, O_RDWR | O_CREAT | O_TRUNC);
}
cc_result File_OpenOrCreate(cc_file* file, const cc_filepath* path) {
	return File_Do(file, path->buffer, O_RDWR | O_CREAT);
}

cc_result File_Read(cc_file file, void* data, cc_uint32 count, cc_uint32* bytesRead) {
	if (file <= 0) { *bytesRead = 0; return EBADF; }
	ssize_t ret = read(file, data, count);
	if (ret < 0) { *bytesRead = 0; return errno ? errno : EIO; }
	*bytesRead = (cc_uint32)ret;
	return 0;
}

cc_result File_Write(cc_file file, const void* data, cc_uint32 count, cc_uint32* bytesWrote) {
	if (file <= 0) { *bytesWrote = 0; return EBADF; }
	ssize_t ret = write(file, data, count);
	if (ret < 0) { *bytesWrote = 0; return errno ? errno : EIO; }
	*bytesWrote = (cc_uint32)ret;
	return 0;
}

cc_result File_Close(cc_file file) {
	if (file <= 0) return 0;
	return close(file) == -1 ? errno : 0;
}

cc_result File_Seek(cc_file file, int offset, int seekType) {
	if (file <= 0) return EBADF;
	static cc_uint8 modes[3] = { SEEK_SET, SEEK_CUR, SEEK_END };
	return lseek(file, offset, modes[seekType]) == -1 ? errno : 0;
}

cc_result File_Position(cc_file file, cc_uint32* pos) {
	if (file <= 0) { *pos = 0; return EBADF; }
	off_t ret = lseek(file, 0, SEEK_CUR);
	if (ret == -1) { *pos = 0; return errno; }
	*pos = (cc_uint32)ret;
	return 0;
}

cc_result File_Length(cc_file file, cc_uint32* len) {
	if (file <= 0) { *len = 0; return EBADF; }
	struct stat st;
	if (fstat(file, &st) == 0 && st.st_size > 0) {
		*len = (cc_uint32)st.st_size;
		return 0;
	}
	/* Fallback: determine length via lseek */
	off_t cur = lseek(file, 0, SEEK_CUR);
	off_t end = lseek(file, 0, SEEK_END);
	lseek(file, cur >= 0 ? cur : 0, SEEK_SET);
	if (end >= 0) {
		*len = (cc_uint32)end;
		return 0;
	}
	*len = 0;
	return errno;
}


/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
/* The frontend routes fd 1/2 writes to xlog(), but stdio's own buffering can
   drop them on a hard crash. Write straight to the file descriptor instead. */
#ifdef __mips__
extern void xlog(const char* fmt, ...);
#endif

void Platform_Log(const char* msg, int len) {
#ifdef __mips__
	xlog("[CC] %.*s\n", len, msg);
#else
	ssize_t ignored;
	int start = 0, i;
	char line[512];

	/* Make sure each entry is a single line */
	for (i = 0; i < len && i < (int)sizeof(line) - 2; i++) {
		line[i] = (msg[i] == '\n' || msg[i] == '\r') ? ' ' : msg[i];
		start = i;
	}
	if (len == 0) return;
	line[start + 1] = '\n';

	ignored = write(1, line, start + 2);
	(void)ignored;
#endif
}

TimeMS DateTime_CurrentUTC(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (TimeMS)tv.tv_sec;
}

void DateTime_CurrentLocal(struct cc_datetime* t) {
	time_t now = time(NULL);
	struct tm local;
	localtime_r(&now, &local);

	t->second = local.tm_sec;
	t->minute = local.tm_min;
	t->hour   = local.tm_hour;
	t->day    = local.tm_mday;
	t->month  = local.tm_mon + 1;
	t->year   = local.tm_year + 1900;
	t->__milli = 0;
}


/*########################################################################################################################*
*--------------------------------------------------------Stopwatch--------------------------------------------------------*
*#########################################################################################################################*/
/* Returns elapsed microseconds since an arbitrary epoch */
cc_uint64 Stopwatch_Measure(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (cc_uint64)tv.tv_sec * 1000000ULL + (cc_uint64)tv.tv_usec;
}

cc_uint64 Stopwatch_ElapsedMicroseconds(cc_uint64 beg, cc_uint64 end) {
	if (end < beg) return 0;
	return end - beg;
}


/*########################################################################################################################*
*--------------------------------------------------------Threading--------------------------------------------------------*
*#########################################################################################################################*/
void Thread_Sleep(cc_uint32 milliseconds) {
#ifdef __mips__
	/* NOTE: The console's libc has neither usleep() nor nanosleep(), but the
	   Multicore loader exports the firmware's millisecond tick counter. */
	extern unsigned os_get_tick_count(void);
	{
		unsigned start = os_get_tick_count();
		while (os_get_tick_count() - start < milliseconds) { }
	}
#else
	usleep((useconds_t)milliseconds * 1000);
#endif
}


/*########################################################################################################################*
*--------------------------------------------------------Platform---------------------------------------------------------*
*#########################################################################################################################*/
cc_result Process_StartGame2(const cc_string* args, int numArgs) {
	return SetGameArgs(args, numArgs);
}

int Platform_GetCommandLineArgs(int argc, STRING_REF char** argv, cc_string* args) {
	return GetGameArgs(args);
}

cc_result Platform_SetDefaultCurrentDirectory(void) { return 0; }

void Platform_Init(void) {
	/* The loader already set up the C heap (sbrk) using the stock 64MB scratch
	   buffer, so there is no memory pool to create here. */
}

void Platform_Free(void) { }

cc_bool Platform_DescribeError(cc_result res, cc_string* dst) {
	String_Format1(dst, "errno %i", &res);
	return true;
}

void Process_Exit(cc_result code) { exit((int)code); }

void Platform_LoadSysFonts(void) { }