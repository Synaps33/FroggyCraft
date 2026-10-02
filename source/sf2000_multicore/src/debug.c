/* Copyright (C) 2023 Nikita Burnashev

Permission to use, copy, modify, and/or distribute this software
for any purpose with or without fee is hereby granted.

THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND! */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <file.h>
#include <stockfw.h>
#include <lcd_font.h>

extern unsigned long PINMUXL;
extern unsigned long PINMUXT;
extern unsigned long GPIOLCTRL;
extern unsigned long GPIOTCTRL;

#define COLS 	(320/FONT_WIDTH_STRIDE)
#define ROWS	(240/FONT_HEIGHT)

static char *lcd_buf = NULL;
static unsigned lcd_y, lcd_x;

uint8_t madctl = 0;
void get_madctl() {
	FILE *fp = fopen("/mnt/sda1/bios/madctl.bin", "rb");
	if (fp) {
		fw_fread(&madctl, 1, 1, fp);
		fclose(fp);
	} else madctl = FROGGY_MADCTL;
}

void lcd_init(void)
{
	if (!lcd_buf)
		lcd_buf = (char*)malloc(COLS * ROWS * sizeof(char));

	if (lcd_buf)
		memset(lcd_buf, ' ', COLS * ROWS * sizeof(char));

	lcd_y = lcd_x = 0;
}

static int lcd_vprintf(const char* fmt, va_list ap)
{
	int ret;
	char buf[999];

	ret = vsnprintf(buf, sizeof buf, fmt, ap);

	for (char *pc = buf; *pc; pc++) {
		if (lcd_x >= COLS) {
			lcd_x = 0;
			lcd_y++;
		}
		if (lcd_y >= ROWS)
			continue;
		if (*pc == '\n') {
			lcd_x = 0;
			lcd_y++;
		}
		else if (*pc >= ' ') {
			lcd_buf[lcd_y*COLS + lcd_x] = *pc;
			lcd_x++;
		}
	}
	return ret;
}

static void lcd_printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	lcd_vprintf(fmt, ap);
	va_end(ap);
}

void lcd_pinmux_gpio(void)
{
	*((volatile unsigned *)&PINMUXL + 0) &= 0xffff; // L02-03 (D0-1)
	*((volatile unsigned *)&PINMUXL + 1) = 0; // L04-07 (D2-4, WR)
	*((volatile unsigned *)&PINMUXL + 2) &= 0xff00ffff; // L10 (CS)

	*((volatile unsigned *)&PINMUXT + 0) &= 0xff; // T01-03 (RS, D11-12)
	*((volatile unsigned *)&PINMUXT + 1) &= 0xff000000; // T04-06 (D13-15)
	*((volatile unsigned *)&PINMUXT + 2) &= 0xff; // T09-11 (D5-D7)
	*((volatile unsigned *)&PINMUXT + 3) &= 0xff000000; // T12-14 (D8-10)
}

static void lcd_send(unsigned short data)
{
	*((volatile unsigned *)&GPIOLCTRL + 4) = // clear L10 (CS), L07 (WR); L02-06 <- D0-4
		*((volatile unsigned *)&GPIOLCTRL + 4) & 0xfffffb03 | data << 2 & 0x7c;
	*((volatile unsigned *)&GPIOTCTRL + 4) = // T09-14 <- D5-10, D11-15 -> T02-06, tDST
		*((volatile unsigned *)&GPIOTCTRL + 4) & 0xffff8183 | data << 4 & 0x7e00 | data >> 9 & 0x7c;
	*((volatile unsigned *)&GPIOLCTRL + 4) |= 1 << 7; // set L07 (WR), tCSH
	*((volatile unsigned *)&GPIOLCTRL + 4) |= 1 << 10; // set L10 (CS)
}

void lcd_send_cmd(unsigned char cmd)
{
	*((volatile unsigned *)&GPIOTCTRL + 4) &= ~(1 << 1); // clear T01 (RS)
	lcd_send(cmd);
}

void lcd_send_data(unsigned short data)
{
	*((volatile unsigned *)&GPIOTCTRL + 4) |= 1 << 1; // set T01 (RS)
	lcd_send(data);
}

static void lcd_flush(unsigned short text_color, unsigned short background_color) {
	if (madctl == 0) get_madctl();

	lcd_pinmux_gpio();

	// applicable ONLY to SF2000's screen FIXME
	lcd_send_cmd(0x36); // MADCTL
	lcd_send_data(madctl); // MX (X mirror) MV (rotation)

	lcd_send_cmd(0x2a); // CASET
	lcd_send_data(0);
	lcd_send_data(0);
	lcd_send_data((320 - 1) >> 8);
	lcd_send_data((320 - 1) & 255);

	lcd_send_cmd(0x2b); // RASET
	lcd_send_data(0);
	lcd_send_data(0);
	lcd_send_data((240 - 1) >> 8);
	lcd_send_data((240 - 1) & 255);

	lcd_send_cmd(0x2c); // RAMWR
	for (unsigned y = 0; y < 240; y++) {
		for (unsigned x = 0; x < 320; x++) {
			unsigned row = y / FONT_HEIGHT;
			unsigned col = x / FONT_WIDTH_STRIDE;

			unsigned char symbol_index = ' '; // default to blank

			if (row < ROWS && col < COLS)
				symbol_index = lcd_buf[row * COLS + col];

			//unsigned symbol_index = lcd_buf[(y / FONT_HEIGHT) * COLS + (x / FONT_WIDTH_STRIDE)];
			unsigned i = (x % FONT_WIDTH_STRIDE), j = y % FONT_HEIGHT;
			unsigned char rem = 1 << ((i + j * FONT_WIDTH) & 7);
			unsigned offset = (i + j * FONT_WIDTH) >> 3;

			if (i < FONT_WIDTH && (lcd_font[FONT_OFFSET(symbol_index) + offset] & rem) > 0)
				lcd_send_data(text_color);
			else
				lcd_send_data(background_color);
		}
	}
}

void dbg_show_noblock(unsigned short text_color, unsigned short background_color, const char *fmt, ...)
{
	os_disable_interrupt();
	lcd_init();
	va_list ap;
	va_start(ap, fmt);
	lcd_vprintf(fmt, ap);
	va_end(ap);

	for (int i=0; i<15; i++)
	{
		lcd_printf(".");
		lcd_flush(text_color, background_color);
	}

	os_enable_interrupt();
}

void lcd_bsod(const char *fmt, ...)
{
	os_disable_interrupt();
	lcd_init();
	lcd_printf("\n "); // guard against some screen misalignment

	va_list ap;
	va_start(ap, fmt);
	lcd_vprintf(fmt, ap);
	va_end(ap);

	lcd_flush(0xffff, 0x1f); // White text, Blue background 
	do {
	} while (1);
}

static bool is_file_exists(const char *path)
{
	return fs_access(path, 0) == 0;
}

static bool is_xlog_enabled()
{
	static int enabled = -1;
	if (enabled < 0) {
		/* Only enable logging if explicitly requested via flag file */
		enabled = (fs_access("/mnt/sda1/bios/enable_xlog.txt", 0) == 0) ? 1 : 0;
	}
	return enabled == 1;
}

static int g_xlog_count = 0;

void xlog_clear()
{
	if (!is_xlog_enabled())
		return;

	// we don't have a fs delete function, so instead just truncate the log file
	g_xlog_count = 0;

	FILE *hlog = fopen(LOG_FILENAME, "w");
	if (!hlog)
		return;

	fclose(hlog);
	fs_sync(LOG_FILENAME);
}

/* Log throttling.
 *
 * xlog() used to do fopen()+fwrite()+fclose()+fs_sync() for every line. On
 * GB300 that is slow enough to look like a hang, and it produced hundreds of
 * kB per session. The handle is still opened per call (each core needs its own
 * copy), but fs_sync runs only every 64th line, which keeps the log safe to
 * read after a crash while cutting the flash writes by 64x.
 *
 * Only one static int is used: the multicore loader has a hard .bss budget
 * (linker_scripts/bisrv_GB300_V2.ld) and a file handle does not fit. */
void xlog(const char *fmt, ...)
{
	char buffer[999];
	va_list args;
	FILE *hlog;

	if (!is_xlog_enabled())
		return;

	hlog = fopen(LOG_FILENAME, "a");
	if (!hlog)
		return;

	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	fwrite(buffer, strlen(buffer), 1, hlog);
	fclose(hlog);

	g_xlog_count++;
	if ((g_xlog_count & 63) == 0)
		fs_sync(LOG_FILENAME);
}
