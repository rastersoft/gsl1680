gsl1680
=======

Version 2

An user-space driver for Silead's GSL1680 capacitive touch screen driver chip.

This driver also uses the multi-touch capabilities of the chip to emulate horizontal and vertical scrolling (by doing it with two fingers), zoom in/zoom out (pinching with two fingers), and right-click (touch with finger 1; without releasing finger 1, tap with finger 2; now each new tap with finger 2 will be a right click in the coordinates in finger 1). Finally, when touching with three fingers will emulate Ctrl+COMPOSE (also known as MENU), which allows to show the on-screen keyboard in TabletWM.

## How to use the driver ##

This is a little program that runs in user space, but makes use of the UFILE driver to link itself to the INPUT subsystem, allowing it to work like any other input driver. So it is mandatory to have a kernel with UFILE support (Device Drivers -> Input devices support -> Miscellaneous devices -> User level driver support).

Also, since the GSL1680 needs a firmware code to be uploaded before being able to detect touchs, the driver needs it in a file.

The format of this firmware must be several code blocks in the form:

    {0xf0,0x3},
    {0x00,0xa5a5ffc0},
    {0x04,0x00000000},
    {0x08,0xe810c4e1},
    {0x0c,0xd3dd7f4d},
    {0x10,0xd7c56634},
    ...

The first value in each line is the register number, and the second value is the data itself. Each code block starts with a 0xf0 register value (the PAGE register), and a data value with the page number where this piece of code has to be copied. After it, comes up to 128 bytes, grouped in 4-byte values.

To launch the driver, just use:

	./driver DEVICE FIRMWARE_FILE
	
DEVICE is the I2C bus where the driver chip is installed (in the case of the Scenio 1207 tablet, it is /dev/i2c-1).

FIRMWARE_FILE is the file with the firmware, in the format explained before. This firmware is ussually specific for each tablet. In the case of the Scenio 1207 tablet, it is located in the folder */system/etc*.

The driver presumes that */sys/devices/virtual/misc/sun4i-gpio/pin/pb3* enables or disables the chip.

## More info about this chip ##

There's a page with technical info about this chip at http://linux-sunxi.org/GSL1680

## Contacting the author ##

Sergio Costas
Raster Software Vigo
http://www.rastersoft.com
raster@rastersoft.com
