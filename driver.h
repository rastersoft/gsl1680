#ifndef _GSLX680_H_
#define _GSLX680_H_

#include <unistd.h>

#define SCREEN_MAX_X 		800
#define SCREEN_MAX_Y 		480

#define GSLX680_I2C_ADDR 	0x40

#define GSL_DATA_REG		0x80
#define GSL_STATUS_REG		0xe0
#define GSL_PAGE_REG		0xf0

typedef unsigned int u32;
typedef unsigned short int u16;
typedef unsigned char u8;
typedef unsigned int  bool;

#define true 1
#define false 0

#endif
