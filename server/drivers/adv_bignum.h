/* 	This is the bignumber-library used by the serialVFD driver.

	Copyright (C) 2006 Stefan Herdler

	Based on drivers.c and lcd-lib.c.
	It may contain parts of other drivers of this package too.

	2006-01-26 Version 0.1: everything should work (not all hardware tested!)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 */

#ifndef ADV_BIGNUM_H
#define ADV_BIGNUM_H

#include "lcd.h"

#define  b_______	0x00
#define  b______X	0x01
#define  b_____X_	0x03
#define  b_____XX	0x03
#define  b____X__	0x04
#define  b____XX_	0x06
#define  b____XXX	0x07
#define  b___X___	0x08
#define  b___XX__	0x09
#define  b___X_X_	0x0A
#define  b___XXX_	0x0E
#define  b___XXXX	0x0F
#define  b__X____	0x10
#define  b__X___X	0x11
#define  b__X_X_X	0x15
#define  b__X_XX_	0x16
#define  b__XX___	0x18
#define  b__XX__X	0x19
#define  b__XX_XX	0x1B
#define  b__XXX__	0x1C
#define  b__XXX_X	0x1D
#define  b__XXXX_	0x1E
#define  b__XXXXX	0x1F
#define  b_XXX___	0x38
#define  b_XXXXXX	0x3F


void lib_adv_bignum (Driver *drvthis, int x, int num, int do_init, int customchars);



#endif