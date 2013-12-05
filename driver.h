#ifndef _GSLX680_H_
#define _GSLX680_H_

#include <unistd.h>

#define SCREEN_MAX_X 		800
#define SCREEN_MAX_Y 		480

#define GSLX680_I2C_ADDR 	0x40

#define GSL_DATA_REG		0x80
#define GSL_STATUS_REG		0xe0
#define GSL_PAGE_REG		0xf0

#define X_THRESHOLD 25
#define Y_THRESHOLD 25
// 150^2=150*150=22500
#define Z_THRESHOLD 22500

// When entering the Drag and Drop mode, displace the cursor SINGLE_CLICK_OFFSET pixels to ensure that the user sees it
#define SINGLE_CLICK_OFFSET 10

typedef unsigned int u32;
typedef unsigned short int u16;
typedef unsigned char u8;
typedef unsigned int  bool;

#define true 1
#define false 0

enum read_status {RS_idle, RS_one_A, RS_one_B, RS_one_C, RS_two_A, RS_two_B, RS_right_A, RS_right_B, RS_three_A, RS_three_B};

//#define USE_FB

#endif
