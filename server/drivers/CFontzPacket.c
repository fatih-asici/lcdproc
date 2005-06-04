/*
 * This file driver is going to replace CFontz633.c in order to support
 * CF631, CF633 and CF635 simultaniously under a new driver name...
 * Currently, to use this version do: "cp CFontzPacket.c CFontz633.c"
 * Be carefull when using CVS, do not overwrite CFontz633.c !!!
 *
 * -- David GLAUDE
 */
/*  
 *  This is the LCDproc driver for CrystalFontz LCD using Packet protocol.
 *  It support the CrystalFontz 633 USB/Serial, the 631 USB and the 635USB
 *  (get yours from http://crystalfontz.com)
 *
 *  Applicable Data Sheets
 *  http://www.crystalfontz.com/products/631/CFA-631_v1.0.pdf
 *  http://www.crystalfontz.com/products/633/CFA_633_0_6.PDF
 *  http://www.crystalfontz.com/products/635/CFA_635_1_0.pdf
 *
 *  Copyright (C) 2002 David GLAUDE
 *  Portstions Copyright (C) 2005 Peter Marschall
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 
 */

/*
 * Driver status
 * 04/04/2002: Working driver
 * 05/06/2002: Reading of return value
 * 02/09/2002: KeyPad handling and return string
 * 03/09/2002: New icon incorporated
 * 27/01/2003: Adapted for CFontz 631
 * 16/05/2005: Adapted for CFontz 635
 *
 * THINGS NOT DONE:
 * + No checking if right hardware is connected (firmware/hardware)
 * + No BigNum (but screen is too small ???)
 * + No support for multiple instance (require private structure)
 * + No cache of custom char usage (like in MtxOrb)
 *
 * THINGS DONE:
 * + Stopping the live reporting (of temperature)
 * + Stopping the reporting of temp and fan (is it necessary after reboot)
 * + Use of library for hbar and vbar (good but library could be better)
 * + Support for keypad (Using a KeyRing)
 *
 * THINGS TO DO:
 * + Make the caching at least for heartbeat icon
 * + Create and use the library (for custom char handeling)
 *
 */

#define DEBUG

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "lcd.h"
#include "CFontz633.h"
#include "CFontz633io.h"
#include "report.h"
#include "lcd_lib.h"
#include "CFontz-charmap.h"

#define CF633_KEY_UP			1
#define CF633_KEY_DOWN			2
#define CF633_KEY_LEFT			3
#define CF633_KEY_RIGHT			4
#define CF633_KEY_ENTER			5
#define CF633_KEY_ESCAPE		6
#define CF633_KEY_UP_RELEASE		7
#define CF633_KEY_DOWN_RELEASE		8
#define CF633_KEY_LEFT_RELEASE		9
#define CF633_KEY_RIGHT_RELEASE		10
#define CF633_KEY_ENTER_RELEASE		11
#define CF633_KEY_ESCAPE_RELEASE	12
#define CF631_KEY_UL_PRESS		13
#define CF631_KEY_UR_PRESS		14
#define CF631_KEY_LL_PRESS		15
#define CF631_KEY_LR_PRESS	 	16
#define CF631_KEY_UL_RELEASE		17
#define CF631_KEY_UR_RELEASE		18
#define CF631_KEY_LL_RELEASE		19
#define CF631_KEY_LR_RELEASE		20

#define CELLWIDTH	DEFAULT_CELL_WIDTH
#define CELLHEIGHT	DEFAULT_CELL_HEIGHT


/* Constants for userdefchar_mode */
#define NUM_CCs		8 /* max. number of custom characters */

typedef enum {
	standard,	/* only char 0 is used for heartbeat */
	vbar,		/* vertical bars */
	hbar,		/* horizontal bars */
	custom,		/* custom settings */
	bignum,		/* big numbers */
	bigchar		/* big characters */
} CGmode;


typedef struct cgram_cache {
	char cache[DEFAULT_CELL_HEIGHT];
	int clean;
} CGram;

typedef struct driver_private_data {
	char device[200];

	int fd;

	int model;
	int newfirmware;

	/* dimensions */
	int width, height;
	int cellwidth, cellheight;

	/* framebuffer and buffer for old LCD contents */
	unsigned char *framebuf;
	unsigned char *backingstore;

	/* defineable characters */
	CGram cc[NUM_CCs];
	CGmode ccmode;

	int contrast;
	int brightness;
	int offbrightness;
} PrivateData;


/* Vars for the server core */
MODULE_EXPORT char *api_version = API_VERSION;
MODULE_EXPORT int stay_in_foreground = 0;
MODULE_EXPORT int supports_multiple = 0;
MODULE_EXPORT char *symbol_prefix = "CFontz633_";

/* Internal functions */
/* static void CFontz633_linewrap (int on); */
/* static void CFontz633_autoscroll (int on);  */
static void CFontz633_hidecursor (Driver *drvthis);
static void CFontz633_reboot (Driver *drvthis);
static void CFontz633_init_vbar (Driver *drvthis);
static void CFontz633_init_hbar (Driver *drvthis);
static void CFontz633_no_live_report (Driver *drvthis);
static void CFontz633_hardware_clear (Driver *drvthis);


/*
 * Opens com port and sets baud correctly...
 */
MODULE_EXPORT int
CFontz633_init (Driver *drvthis, char *args)
{
	struct termios portset;
	int tmp, w, h;
	int reboot = 0;
	int usb = 0;
	int speed = DEFAULT_SPEED;
	char size[200] = DEFAULT_SIZE_CF633;
	char *default_size = DEFAULT_SIZE_CF633;

	PrivateData *p;

	/* Allocate and store private data */
	p = (PrivateData *) calloc(1, sizeof(PrivateData));
	if (p == NULL)
		return -1;
	if (drvthis->store_private_ptr(drvthis, p))
		return -1;

	/* Initialize the PrivateData structure */
	p->cellwidth = DEFAULT_CELL_WIDTH;
	p->cellheight = DEFAULT_CELL_HEIGHT;
	p->ccmode = standard;

	debug(RPT_INFO, "CFontz633: init(%p,%s)", drvthis, args );

	EmptyKeyRing(&keyring);
	EmptyReceiveBuffer(&receivebuffer);

	/* Read config file */
	/* Which model is it (CF633 or CF631)? */
	tmp = drvthis->config_get_int (drvthis->name, "Model", 0, DEFAULT_SPEED);
	debug (RPT_INFO,"CFontzPacket_init: Model is '%d'", tmp);
	if ((tmp != 631) && (tmp != 633) && (tmp != 635)) {
		tmp = 633;
		report (RPT_WARNING, "CFontzPacket_init: Model must be 631, 633 or 635. Using default value: %d\n", tmp);
	}
	p->model = tmp;
	
	/* Which device should be used */
	strncpy(p->device, drvthis->config_get_string (drvthis->name, "Device", 0, DEFAULT_DEVICE), sizeof(p->device));
	p->device[sizeof(p->device)-1] = '\0';
	debug (RPT_INFO,"CFontzPacket_init: Device (in config) is '%s'", p->device);

	/* Which size */
	if (p->model == 631)
		default_size = DEFAULT_SIZE_CF631;
	else if (p->model == 633)
		default_size = DEFAULT_SIZE_CF633;
	else if (p->model == 635)
		default_size = DEFAULT_SIZE_CF635;
		
	strncpy(size, drvthis->config_get_string (drvthis->name, "Size", 0, default_size), sizeof(size));
	size[sizeof(size)-1] = '\0';
	debug (RPT_INFO,"CFontzPacket_init: Size (in config) is '%s'", size);
	if ((sscanf(size, "%dx%d", &w, &h ) != 2)
	    || (w <= 0) || (w > LCD_MAX_WIDTH)
	    || (h <= 0) || (h > LCD_MAX_HEIGHT)) {
		report (RPT_WARNING, "CFontzPacket_init: Cannot read size: %s. Using default value.\n", size);
		sscanf( default_size, "%dx%d", &w, &h );
	}
	p->width = w;
	p->height = h;

	debug (RPT_INFO,"CFontzPacket_init: Real size used: %dx%d", p->width, p->height);

	/* Which contrast */
	tmp = drvthis->config_get_int (drvthis->name, "Contrast", 0, DEFAULT_CONTRAST);
	debug (RPT_INFO,"CFontzPacket_init: Contrast (in config) is '%d'", tmp);
	if ((tmp < 0) || (tmp > 1000)) {
		report (RPT_WARNING, "CFontzPacket_init: Contrast must be between 0 and 1000. Using default value.\n");
		tmp = DEFAULT_CONTRAST;
	}
	p->contrast = tmp;

	/* Which backlight brightness */
	tmp = drvthis->config_get_int (drvthis->name, "Brightness", 0, DEFAULT_BRIGHTNESS);
	debug (RPT_INFO,"CFontzPacket_init: Brightness (in config) is '%d'", tmp);
	if ((tmp < 0) || (tmp > 100)) {
		report (RPT_WARNING, "CFontzPacket_init: Brightness must be between 0 and 100. Using default value.\n");
		tmp = DEFAULT_BRIGHTNESS;
	}
	p->brightness = tmp;

	/* Which backlight-off "brightness" */
	tmp = drvthis->config_get_int (drvthis->name, "OffBrightness", 0, DEFAULT_OFFBRIGHTNESS);
	debug (RPT_INFO,"CFontzPacket_init: OffBrightness (in config) is '%d'", tmp);
	if ((tmp < 0) || (tmp > 100)) {
		report (RPT_WARNING, "CFontzPacket_init: OffBrightness must be between 0 and 100. Using default value.\n");
		tmp = DEFAULT_OFFBRIGHTNESS;
	}
	p->offbrightness = tmp;

	/* Which speed CF633 support 19200 only, CF631USB use 115200. */
	tmp = drvthis->config_get_int (drvthis->name, "Speed", 0, DEFAULT_SPEED);
	debug (RPT_INFO,"CFontzPacket_init: Speed (in config) is '%d'", tmp);
	if (tmp == 19200) speed = B19200;
	else if (tmp == 115200) speed = B115200;
	else { report (RPT_WARNING, "CFontz633_init: Speed must be 19200 or 11500. Using default value.\n", speed);
	}

	/* New firmware version?
	 * I will try to behave differently for firmware 0.6 or above.
	 * Currently this is not in use.
	 */
	p->newfirmware = drvthis->config_get_bool(drvthis->name, "NewFirmware", 0, 0);

	/* Reboot display? */
	reboot = drvthis->config_get_bool(drvthis->name, "Reboot", 0, 0);

	/* Am I USB or not? */
	usb = drvthis->config_get_bool(drvthis->name, "USB", 0, 0);
	if (usb)
		report (RPT_INFO, "CFontzPacket_init: USB is indicated (in config).\n");

	/* Set up io port correctly, and open it... */
	debug( RPT_DEBUG, "CFontzPacket_init: Opening device: %s", p->device);
	p->fd = open(p->device, (usb) ? (O_RDWR | O_NOCTTY) : (O_RDWR | O_NOCTTY | O_NDELAY));
	if (p->fd == -1) {
		report (RPT_ERR, "CFontzPacket_init: failed (%s)\n", strerror (errno));
		return -1;
	}

	tcgetattr (p->fd, &portset);

	/* We use RAW mode */
	if (usb) {
		// The USB way
		portset.c_iflag &= ~( IGNBRK | BRKINT | PARMRK | ISTRIP
					| INLCR | IGNCR | ICRNL | IXON );
		portset.c_oflag &= ~OPOST;
		portset.c_lflag &= ~( ECHO | ECHONL | ICANON | ISIG | IEXTEN );
		portset.c_cflag &= ~( CSIZE | PARENB | CRTSCTS );
		portset.c_cflag |= CS8 | CREAD | CLOCAL ;
		portset.c_cc[VMIN] = 0;
		portset.c_cc[VTIME] = 0;
	} else {
#ifdef HAVE_CFMAKERAW
		/* The easy way */
		cfmakeraw( &portset );
#else
		/* The hard way */
		portset.c_iflag &= ~( IGNBRK | BRKINT | PARMRK | ISTRIP
	        			| INLCR | IGNCR | ICRNL | IXON );
		portset.c_oflag &= ~OPOST;
		portset.c_lflag &= ~( ECHO | ECHONL | ICANON | ISIG | IEXTEN );
		portset.c_cflag &= ~( CSIZE | PARENB | CRTSCTS );
		portset.c_cflag |= CS8 | CREAD | CLOCAL ;
#endif
	}

	/* Set port speed */
	cfsetospeed (&portset, speed);
	cfsetispeed (&portset, B0);

	/* Do it... */
	tcsetattr (p->fd, TCSANOW, &portset);

	/* make sure the frame buffer is there... */
	p->framebuf = (unsigned char *) malloc(p->width * p->height);
	if (p->framebuf == NULL) {
		report(RPT_ERR, "CFontzPacket_init: unable to create framebuffer.\n");
		return -1;
	}
	memset(p->framebuf, ' ', p->width * p->height);

	/* make sure the framebuffer backing store is there... */
	p->backingstore = (unsigned char *) malloc(p->width * p->height);
	if (p->backingstore == NULL) {
		report(RPT_ERR, "CFontzPacket_init: unable to create framebuffer backing store.\n");
		return -1;
	}
	memset(p->backingstore, ' ', p->width * p->height);

	/* Set display-specific stuff.. */
	if (reboot) {
		debug(RPT_INFO, "CFontzPacket: reboot requested\n" );
		CFontz633_reboot (drvthis);
		reboot = 0;
		debug(RPT_INFO, "CFontzPacket: reboot done" );
	}
	
	CFontz633_hidecursor (drvthis);
	
	CFontz633_set_contrast (drvthis, p->contrast);
	CFontz633_no_live_report (drvthis);
  	CFontz633_hardware_clear (drvthis);

	report (RPT_DEBUG, "CFontzPacket_init: done\n");

	return 0;
}


/*
 * Clean-up
 */
MODULE_EXPORT void
CFontz633_close (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;

	if (p != NULL) {
		close(p->fd);

		if (p->framebuf)
			free(p->framebuf);
		p->framebuf = NULL;

		if (p->backingstore)
			free(p->backingstore);
		p->backingstore = NULL;

		free(p);
	}	
	drvthis->store_private_ptr(drvthis, NULL);
}


/*
 * Returns the display width in characters
 */
MODULE_EXPORT int
CFontz633_width (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;

	return p->width;
}


/*
 * Returns the display height in characters
 */
MODULE_EXPORT int
CFontz633_height (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;

	return p->height;
}


/*
 * Returns the width of a character in pixels
 */
MODULE_EXPORT int
CFontz633_cellwidth (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;

	return p->cellwidth;
}


/*
 * Returns the height of a character in pixels
 */
MODULE_EXPORT int
CFontz633_cellheight (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;

	return p->cellheight;
}


/*
 * Flushes all output to the lcd...
 */
MODULE_EXPORT void
CFontz633_flush (Driver *drvthis)
{
  PrivateData *p = drvthis->private_data;
  int i,j;

  if (p->model == 633) {
  /*
   * For CF633 we don't use delta update yet.
   * The protocol only permit update of full or partial line starting from pos 0.
   */
  unsigned char *xp = p->framebuf;
  unsigned char *xq = p->backingstore;
    
    for (i = 0; i < p->width; i++) {
      if (*xp++ != *xq++) {
	send_bytes_message(p->fd, CF633_Set_LCD_Contents_Line_One, 16, p->framebuf);
        memcpy(p->backingstore, p->framebuf, p->width);
        break;
      }
    }

    xp = p->framebuf + p->width;
    xq = p->backingstore + p->width;

    for (i = 0; i < p->width; i++) {
      if (*xp++ != *xq++) {
        send_bytes_message(p->fd, CF633_Set_LCD_Contents_Line_Two, 16, p->framebuf + p->width);
        memcpy(p->backingstore + p->width, p->framebuf + p->width, p->width);
        break;
      }
    }
  }
  else { /* (p->model != 633) */
  /*
   * CF631 / CF635 protocol is more flexible and we can do real delta update.
   */

    for (i = 0; i < p->height; i++) {
      // set frame buffer & backing store to start of the line
      unsigned char *xp = p->framebuf + (i * p->width);
      unsigned char *xq = p->backingstore + (i * p->width);

      debug (RPT_INFO,"Framebuf: '%.*s'", p->width, xp );
      debug (RPT_INFO,"     backingstore: '%.*s'", p->width, xq );

      for (j = 0; j < p->width; ) { 
	// skip over identical portions
	for ( ; *xp == *xq && j < p->width; xp++, xq++, j++ )
	  ;
		
	// deal with the differences
	if (j < p->width) {
          unsigned char out[23];
          int diff_length;
	  int first_diff = j;
	    
	  // get length of differing portions
	  for ( ; *xp != *xq && j < p->width; xp++, xq++, j++ )
	    ;

	  // send the difference to the screen
	  diff_length = j - first_diff;
	  out[0] = first_diff;	// column
	  out[1] = i;		// line
			
	  debug (RPT_INFO,"WriteDiff: l=%d c=%d count=%d string='%.*s'",
	 	 out[0], out[1], diff_length, diff_length,
		 &p->framebuf[first_diff + (i * p->width)] );

	  memcpy(&out[2], &p->framebuf[first_diff + (i * p->width)], diff_length );
	  send_bytes_message(p->fd, CF633_Send_Data_to_LCD, diff_length + 2, out);
	}  
      } // j < p->width
    }	// i < p->height
    memcpy(p->backingstore, p->framebuf, p->width * p->height);
  }
}


/*
 * Return one char from the KeyRing
 */
MODULE_EXPORT char *
CFontz633_get_key (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;
	unsigned char key;

	key = GetKeyFromKeyRing(&keyring);

	switch (key) {
		case CF633_KEY_LEFT:
			return "Left";
			break;
		case CF633_KEY_UP:
			return "Up";
			break;
		case CF633_KEY_DOWN:
			return "Down";
			break;
		case CF633_KEY_RIGHT:
			return "Right";
			break;
		case CF633_KEY_ENTER:
			return "Enter";
			break;
		case CF633_KEY_ESCAPE:
			return "Escape";
			break;
		case CF631_KEY_UL_PRESS:
			return "Up";
			break;
		case CF631_KEY_UR_PRESS:
			return "Enter";
			break;
		case CF631_KEY_LL_PRESS:
			return "Down";
			break;
		case CF631_KEY_LR_PRESS:
			return "Escape";
			break;
		case CF633_KEY_UP_RELEASE:
		case CF633_KEY_DOWN_RELEASE:
		case CF633_KEY_LEFT_RELEASE:
		case CF633_KEY_RIGHT_RELEASE:
		case CF633_KEY_ENTER_RELEASE:
		case CF633_KEY_ESCAPE_RELEASE:
		case CF631_KEY_UL_RELEASE:
		case CF631_KEY_UR_RELEASE:
		case CF631_KEY_LL_RELEASE:
		case CF631_KEY_LR_RELEASE:
			// report( RPT_INFO, "cfontz633: Returning key release 0x%2x", key);
			return NULL;
			break;
		default:
			if (key != '\0')
				report( RPT_INFO, "cfontz633: Untreated unknown key 0x%2x", key);
			return NULL;
			break;
	}
	return NULL;
}


/*
 * Prints a character on the lcd display, at position (x,y).
 * The upper-left is (1,1), and the lower right should be (16,2).
 */
MODULE_EXPORT void
CFontz633_chr (Driver *drvthis, int x, int y, char c)
{
	PrivateData *p = drvthis->private_data;

	y--;
	x--;

	p->framebuf[(y * p->width) + x] = (p->model == 633)
		                          ? c
					  : CFontz_charmap[(unsigned) c];
}


/*
 * Prints a character on the lcd display, at position (x,y).
 * The upper-left is (1,1), and the lower right should be (16,2).
 */
static void
CFontz633_raw_chr (Driver *drvthis, int x, int y, unsigned char c)
{
	PrivateData *p = drvthis->private_data;

	y--;
	x--;

	p->framebuf[(y * p->width) + x] = c;
}


/*
 * Returns current contrast
 * This is only the locally stored contrast, the contrast value
 * cannot be retrieved from the LCD.
 * Value 0 to 1000.
 */
MODULE_EXPORT int
CFontz633_get_contrast (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;

	return p->contrast;
}


/*
 *  Changes screen contrast (valid hardware value: 0-50)
 *  Value 0 to 1000.
 */
MODULE_EXPORT void
CFontz633_set_contrast (Driver *drvthis, int promille)
{
	PrivateData *p = drvthis->private_data;
	int hardware_contrast;

	/* Check it */
	if (promille < 0 || promille > 1000)
		return;

	/* Store the software value since there is not get. */
	p->contrast = promille;

	// on CF633: 0 - 50, on CF631, CF635: 0 - 255
	hardware_contrast = (p->model == 633)
			    ? (p->contrast / 20)
			    : ((p->contrast * 255) / 1000);
	/* Next line is to be checked $$$ */
	send_onebyte_message(p->fd, CF633_Set_LCD_Contrast, hardware_contrast);
}


/*
 * Sets the backlight on or off.
 * The hardware support any value between 0 and 100.
 * Need to find out if we have support for intermediate value.
 */
MODULE_EXPORT void
CFontz633_backlight (Driver *drvthis, int on)
{
	PrivateData *p = drvthis->private_data;

	/* Next line is to be checked $$$ */
	send_onebyte_message(p->fd, CF633_Set_LCD_And_Keypad_Backlight,
			     (on) ? p->brightness : p->offbrightness);
}


/* OK631
 * Get rid of the blinking curson
 */
static void
CFontz633_hidecursor (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;

	send_onebyte_message(p->fd, CF633_Set_LCD_Cursor_Style, 0);
}


/*
 * Stop live reporting of temperature.
 */
static void
CFontz633_no_live_report (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;
	unsigned char out[2] = { 0, 0 };

	if (p->model == 633) {
		for (out[0] = 0; out[0] < 8; out[0]++)
			send_bytes_message(p->fd, CF633_Set_Up_Live_Fan_or_Temperature_Display, 2, out);
	}	
}


/*
 * Stop the reporting of any fan.
 */
static void
CFontz633_no_fan_report (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;

	if (p->model == 633)
		send_onebyte_message(p->fd, CF633_Set_Up_Fan_Reporting, 0);
}


/* KO631
 * Stop the reporting of any temperature.
 */
static void
CFontz633_no_temp_report (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;
	unsigned char out[4] = { 0, 0, 0, 0 };

	if (p->model == 633)
		send_bytes_message(p->fd, CF633_Set_Up_Temperature_Reporting, 4, out);
}


/* OK631
 * Reset the display bios
 */
static void
CFontz633_reboot (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;
	unsigned char out[3] = { 8, 18, 99 };

	send_bytes_message(p->fd, CF633_Reboot, 3, out);
	sleep(2);	
}


/*
 * Sets up for vertical bars.
 */
static void
CFontz633_init_vbar (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;
	char a[] = {
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 1, 1, 1, 1, 1,
	};
	char b[] = {
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 1, 1, 1, 1, 1,
		0, 1, 1, 1, 1, 1,
	};
	char c[] = {
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 1, 1, 1, 1, 1,
		0, 1, 1, 1, 1, 1,
		0, 1, 1, 1, 1, 1,
	};
	char d[] = {
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 1, 1, 1, 1, 1,
		0, 1, 1, 1, 1, 1,
		0, 1, 1, 1, 1, 1,
		0, 1, 1, 1, 1, 1,
	};
	char e[] = {
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 1, 1, 1, 1, 1,
		0, 1, 1, 1, 1, 1,
		0, 1, 1, 1, 1, 1,
		0, 1, 1, 1, 1, 1,
		0, 1, 1, 1, 1, 1,
	};
	char f[] = {
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 1, 1, 1, 1, 1,
		0, 1, 1, 1, 1, 1,
		0, 1, 1, 1, 1, 1,
		0, 1, 1, 1, 1, 1,
		0, 1, 1, 1, 1, 1,
		0, 1, 1, 1, 1, 1,
	};
	char g[] = {
		0, 0, 0, 0, 0, 0,
		0, 1, 1, 1, 1, 1,
		0, 1, 1, 1, 1, 1,
		0, 1, 1, 1, 1, 1,
		0, 1, 1, 1, 1, 1,
		0, 1, 1, 1, 1, 1,
		0, 1, 1, 1, 1, 1,
		0, 1, 1, 1, 1, 1,
	};

	if (p->ccmode != vbar) {
		if (p->ccmode != standard) {
			/* Not supported(yet) */
			report(RPT_WARNING, "CFontz633_init_vbar: Cannot combine two modes using user defined characters");
			return;
		}
	        p->ccmode = vbar;

		CFontz633_set_char (drvthis, 1, a);
		CFontz633_set_char (drvthis, 2, b);
		CFontz633_set_char (drvthis, 3, c);
		CFontz633_set_char (drvthis, 4, d);
		CFontz633_set_char (drvthis, 5, e);
		CFontz633_set_char (drvthis, 6, f);
		CFontz633_set_char (drvthis, 7, g);
	}
}


/*
 * Inits horizontal bars...
 */
static void
CFontz633_init_hbar (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;
	char a[] = {
		1, 0, 0, 0, 0, 0,
		1, 0, 0, 0, 0, 0,
		1, 0, 0, 0, 0, 0,
		1, 0, 0, 0, 0, 0,
		1, 0, 0, 0, 0, 0,
		1, 0, 0, 0, 0, 0,
		1, 0, 0, 0, 0, 0,
		1, 0, 0, 0, 0, 0,
	};
	char b[] = {
		1, 1, 0, 0, 0, 0,
		1, 1, 0, 0, 0, 0,
		1, 1, 0, 0, 0, 0,
		1, 1, 0, 0, 0, 0,
		1, 1, 0, 0, 0, 0,
		1, 1, 0, 0, 0, 0,
		1, 1, 0, 0, 0, 0,
		1, 1, 0, 0, 0, 0,
	};
	char c[] = {
		1, 1, 1, 0, 0, 0,
		1, 1, 1, 0, 0, 0,
		1, 1, 1, 0, 0, 0,
		1, 1, 1, 0, 0, 0,
		1, 1, 1, 0, 0, 0,
		1, 1, 1, 0, 0, 0,
		1, 1, 1, 0, 0, 0,
		1, 1, 1, 0, 0, 0,
	};
	char d[] = {
		1, 1, 1, 1, 0, 0,
		1, 1, 1, 1, 0, 0,
		1, 1, 1, 1, 0, 0,
		1, 1, 1, 1, 0, 0,
		1, 1, 1, 1, 0, 0,
		1, 1, 1, 1, 0, 0,
		1, 1, 1, 1, 0, 0,
		1, 1, 1, 1, 0, 0,
	};
	char e[] = {
		1, 1, 1, 1, 1, 0,
		1, 1, 1, 1, 1, 0,
		1, 1, 1, 1, 1, 0,
		1, 1, 1, 1, 1, 0,
		1, 1, 1, 1, 1, 0,
		1, 1, 1, 1, 1, 0,
		1, 1, 1, 1, 1, 0,
		1, 1, 1, 1, 1, 0,
	};
	char f[] = {
		1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1,
	};

	if (p->ccmode != hbar) {
		if (p->ccmode != standard) {
			/* Not supported(yet) */
			report(RPT_WARNING, "CFontz633_init_hbar: Cannot combine two modes using user defined characters");
			return;
		}
	        p->ccmode = hbar;

		CFontz633_set_char (drvthis, 1, a);
		CFontz633_set_char (drvthis, 2, b);
		CFontz633_set_char (drvthis, 3, c);
		CFontz633_set_char (drvthis, 4, d);
		CFontz633_set_char (drvthis, 5, e);
		CFontz633_set_char (drvthis, 6, f);
	}
}


/*
 * Draws a vertical bar...
 */
MODULE_EXPORT void
CFontz633_vbar (Driver *drvthis, int x, int y, int len, int promille, int options)
{
/* x and y are the start position of the bar.
 * The bar by default grows in the 'up' direction
 * (other direction not yet implemented).
 * len is the number of characters that the bar is long at 100%
 * promille is the number of promilles (0..1000) that the bar should be filled.
 */
	PrivateData *p = drvthis->private_data;

	CFontz633_init_vbar(drvthis);
	lib_vbar_static(drvthis, x, y, len, promille, options, p->cellheight, 0);
}


/*
 * Draws a horizontal bar to the right.
 */
MODULE_EXPORT void
CFontz633_hbar (Driver *drvthis, int x, int y, int len, int promille, int options)
{
/* x and y are the start position of the bar.
 * The bar by default grows in the 'right' direction
 * (other direction not yet implemented).
 * len is the number of characters that the bar is long at 100%
 * promille is the number of promilles (0..1000) that the bar should be filled.
 */
	PrivateData *p = drvthis->private_data;

	CFontz633_init_hbar(drvthis);
	lib_hbar_static(drvthis, x, y, len, promille, options, p->cellwidth, 0);
}


/*
 * Writes a big number.
 * This is not supported on 633 because we only have 2 lines...
 */
MODULE_EXPORT void
CFontz633_num (Driver *drvthis, int x, int num)
{
/*
	PrivateData *p = drvthis->private_data;
	unsigned char out[5];

	snprintf (out, sizeof(out), "%c%c%c", 28, x, num);
	write (p->fd, out, 3);
*/
}


/*
 * Sets a custom character from 0 - (NUM_CCs-1)
 *
 * For input, values > 0 mean "on" and values <= 0 are "off".
 *
 * The input is just an array of characters...
 */
MODULE_EXPORT void
CFontz633_set_char (Driver *drvthis, int n, char *dat)
{
	PrivateData *p = drvthis->private_data;
 	unsigned char out[9];
	int row, col;

	if ((n < 0) || (n >= NUM_CCs))
		return;
	if (!dat)
		return;

	out[0] = n;	/* Custom char to define. xxx */

	for (row = 0; row < p->cellheight; row++) {
		int letter = 0;

		for (col = 0; col < p->cellwidth; col++) {
			letter <<= 1;
			letter |= (dat[(row * p->cellwidth) + col] > 0);
		}
		out[row+1] = letter;
	}
	send_bytes_message(p->fd, CF633_Set_LCD_Special_Character_Data, 9, out);
}


/*
 * Places an icon on screen
 */
MODULE_EXPORT int
CFontz633_icon (Driver *drvthis, int x, int y, int icon)
{
	PrivateData *p = drvthis->private_data;
	char icons[8][6 * 8] = {
	/* Empty Heart */
		{
		 1, 1, 1, 1, 1, 1,
		 1, 1, 0, 1, 0, 1,
		 1, 0, 0, 0, 0, 0,
		 1, 0, 0, 0, 0, 0,
		 1, 0, 0, 0, 0, 0,
		 1, 1, 0, 0, 0, 1,
		 1, 1, 1, 0, 1, 1,
		 1, 1, 1, 1, 1, 1,
		 },
	/* Filled Heart */
		{
		 1, 1, 1, 1, 1, 1,		  
		 1, 1, 0, 1, 0, 1,
		 1, 0, 1, 0, 1, 0,
		 1, 0, 1, 1, 1, 0,
		 1, 0, 1, 1, 1, 0,
		 1, 1, 0, 1, 0, 1,
		 1, 1, 1, 0, 1, 1,
		 1, 1, 1, 1, 1, 1,
		 },
	/* arrow_up */
		{
		 0, 0, 0, 1, 0, 0,
		 0, 0, 1, 1, 1, 0,
		 0, 1, 0, 1, 0, 1,
		 0, 0, 0, 1, 0, 0,
		 0, 0, 0, 1, 0, 0,
		 0, 0, 0, 1, 0, 0,
		 0, 0, 0, 1, 0, 0,
		 0, 0, 0, 0, 0, 0,
		 },
	/* arrow_down */
		{
		 0, 0, 0, 1, 0, 0,
		 0, 0, 0, 1, 0, 0,
		 0, 0, 0, 1, 0, 0,
		 0, 0, 0, 1, 0, 0,
		 0, 1, 0, 1, 0, 1,
		 0, 0, 1, 1, 1, 0,
		 0, 0, 0, 1, 0, 0,
		 0, 0, 0, 0, 0, 0,
		 },
	/* checkbox_off */
		{
		 0, 0, 0, 0, 0, 0,
		 0, 0, 0, 0, 0, 0,
		 0, 1, 1, 1, 1, 1,
		 0, 1, 0, 0, 0, 1,
		 0, 1, 0, 0, 0, 1,
		 0, 1, 0, 0, 0, 1,
		 0, 1, 1, 1, 1, 1,
		 0, 0, 0, 0, 0, 0,
		 },
	/* checkbox_on */
		{
		 0, 0, 0, 1, 0, 0,
		 0, 0, 0, 1, 0, 0,
		 0, 1, 1, 1, 0, 1,
		 0, 1, 0, 1, 1, 0,
		 0, 1, 0, 1, 0, 1,
		 0, 1, 0, 0, 0, 1,
		 0, 1, 1, 1, 1, 1,
		 0, 0, 0, 0, 0, 0,
		 },
	/* checkbox_gray */
		{
		 0, 0, 0, 0, 0, 0,
		 0, 0, 0, 0, 0, 0,
		 0, 1, 1, 1, 1, 1,
		 0, 1, 0, 1, 0, 1,
		 0, 1, 1, 0, 1, 1,
		 0, 1, 0, 1, 0, 1,
		 0, 1, 1, 1, 1, 1,
		 0, 0, 0, 0, 0, 0,
		 },
	 /* Ellipsis */
		{
		 0, 0, 0, 0, 0, 0,		 
		 0, 0, 0, 0, 0, 0,
		 0, 0, 0, 0, 0, 0,
		 0, 0, 0, 0, 0, 0,
		 0, 0, 0, 0, 0, 0,
		 0, 0, 0, 0, 0, 0,
		 0, 0, 0, 0, 0, 0,
		 1, 0, 1, 0, 1, 0,
		 },
	};

	/* Yes we know, this is a VERY BAD implementation :-) */
	switch (icon) {
		case ICON_BLOCK_FILLED:
			if (p->model == 633)
				CFontz633_chr(drvthis, x, y, 255);
			else
				CFontz633_raw_chr(drvthis, x, y, 31);
			break;
		case ICON_HEART_FILLED:
			CFontz633_set_char(drvthis, 0, icons[1]);
			CFontz633_chr(drvthis, x, y, 0);
			break;
		case ICON_HEART_OPEN:
			CFontz633_set_char(drvthis, 0, icons[0]);
			CFontz633_chr(drvthis, x, y, 0);
			break;
		case ICON_ARROW_UP:
			if (p->model == 633) {
				CFontz633_set_char(drvthis, 1, icons[2]);
				CFontz633_chr(drvthis, x, y, 1);
			}
			else
				CFontz633_raw_chr(drvthis, x, y, 0xDE);
			break;
		case ICON_ARROW_DOWN:
			if (p->model == 633) {
				CFontz633_set_char(drvthis, 2, icons[3]);
				CFontz633_chr(drvthis, x, y, 2);
			}
			else
				CFontz633_raw_chr(drvthis, x, y, 0xE0);
			break;
		case ICON_ARROW_LEFT:
			if (p->model == 633)
				CFontz633_raw_chr(drvthis, x, y, 0x7F);
			else
				CFontz633_raw_chr(drvthis, x, y, 0xE1);
			break;
		case ICON_ARROW_RIGHT:
			if (p->model == 633)
				CFontz633_raw_chr(drvthis, x, y, 0x7E);
			else
				CFontz633_raw_chr(drvthis, x, y, 0xDF);
			break;
		case ICON_CHECKBOX_OFF:
			CFontz633_set_char(drvthis, 3, icons[4]);
			CFontz633_chr(drvthis, x, y, 3);
			break;
		case ICON_CHECKBOX_ON:
			CFontz633_set_char(drvthis, 4, icons[5]);
			CFontz633_chr(drvthis, x, y, 4);
			break;
		case ICON_CHECKBOX_GRAY:
			CFontz633_set_char(drvthis, 5, icons[6]);
			CFontz633_chr(drvthis, x, y, 5);
			break;
		case ICON_SELECTOR_AT_LEFT:
			if (p->model == 633)
				return -1;
			CFontz633_raw_chr(drvthis, x, y, 0x10);
			break;
		case ICON_SELECTOR_AT_RIGHT:
			if (p->model == 633)
				return -1;
			CFontz633_raw_chr(drvthis, x, y, 0x11);
			break;
		default:
			return -1; /* Let the core do other icons */
	}
	return 0;
}


/* OK631
 * Clears the LCD screen
 */
MODULE_EXPORT void
CFontz633_clear (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;

	memset(p->framebuf, ' ', p->width * p->height);
	p->ccmode = standard;
}


/* OK631
 * Hardware clears the LCD screen
 */
static void
CFontz633_hardware_clear (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;

	send_zerobyte_message(p->fd, CF633_Clear_LCD_Screen);
}


/* OK631
 * Prints a string on the lcd display, at position (x,y).  The
 * upper-left is (1,1), and the lower right should be (p->width, p->height).
 */
MODULE_EXPORT void
CFontz633_string (Driver *drvthis, int x, int y, char string[])
{
	PrivateData *p = drvthis->private_data;
	int i;

	/* Convert 1-based coords to 0-based... */
	x--;
	y--;

	for (i = 0; string[i] != '\0'; i++) {
		/* Check for buffer overflows... */
		if ((y * p->width) + x + i > (p->width * p->height))
			break;
		p->framebuf[(y * p->width) + x + i] = (p->model == 633)
						      ? c
						      : CFontz_charmap[(unsigned) string[i]];
	}
}

