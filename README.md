gsl1680
=======

Version 9

An user-space driver for Silead's GSL1680 capacitive touch screen driver chip.

This driver also uses the multi-touch capabilities of the chip to emulate horizontal and vertical scrolling (by doing it with two fingers by default, or with a single finger with **-new_scroll** enabled), zoom in/zoom out (pinching with two fingers), drag and drop (just touching and moving in default mode, or keeping the touch during one second to start DnD mode with **-new_scroll** enabled) and right-click (touch with finger 1; without releasing finger 1, tap with finger 2; now each new tap with finger 2 will be a right click in the coordinates in finger 1). Finally, when touching with three fingers will emulate Ctrl+COMPOSE (also known as MENU), which allows to show the on-screen keyboard in TabletWM.

Version 7 has been changed to be launched from systemd. After running "make" and "sudo make install", run "sudo systemctl start gslx680.service" to make systemd launch it at startup.

## How to use the driver ##

This is a little program that runs in user space, but makes use of the UFILE driver to link itself to the INPUT subsystem, allowing it to work like any other input driver. So it is mandatory to have a kernel with UFILE support (Device Drivers -> Input devices support -> Miscellaneous devices -> User level driver support).

Also, since the GSL1680 needs a firmware code to be uploaded before being able to detect touchs, the driver needs it in a file.

The firmware can be available in two formats: plain-text and binary.

The plain-text format has the form:

    {0xf0,0x3},
    {0x00,0xa5a5ffc0},
    {0x04,0x00000000},
    {0x08,0xe810c4e1},
    {0x0c,0xd3dd7f4d},
    {0x10,0xd7c56634},
    ...

The first value in each line is the register number, and the second value is the data itself. Each code block starts with a 0xf0 register value (the PAGE register), and a data value with the page number where this piece of code has to be copied. After it, comes up to 128 bytes, grouped in 4-byte values.

The binary format is the same, but the values are directly in binary, as 4-byte integers, in little-endian format, and without any ASCII markers; just the raw values. So the previous firmware would be represented with a raw sequence of bytes with these values:

    F0 00 00 00 03 00 00 00 00 00 00 00 C0 FF A5 A5 04 00 00 00 00 00 00 00 08 00 00 00 E1 C4 10 E8...

To launch the driver, just use:

	./driver [-res XxY] [-gpio PATH] [-invert_x] [-invert_y] [-new_scroll] DEVICE FIRMWARE_FILE

DEVICE is the I2C bus where the driver chip is installed (in the case of the Scenio 1207 tablet, it is /dev/i2c-1).

FIRMWARE_FILE is the file with the firmware, in the format explained before. This firmware is ussually specific for each tablet. In the case of the Scenio 1207 tablet, it is located in the folder */system/etc*.

**-res** allows to specify the screen resolution. If not set, the driver will use 800x600 pixels.

**-gpio** allows to specify the path to the GPIO device that enables or disables the chip. By default, it presumes it is */sys/devices/virtual/misc/sun4i-gpio/pin/pb3*. In order to make this work, it a must to have the GPIO support in the kernel and to enable that pin as an **OUTPUT** gpio.

**-invert_x** and **-invert_y** allows to invert the horizontal or vertical coordinates, in case that, when you touch the left part of the screen, the cursor moves to the right, and so on.

**-swap_axis** swaps the X and Y axis, allowing to use the driver with rotated screens.

**-new_scroll** allows to use a single finger to do scrolling.

In the case of the sun4i SoCs, for example, to know which pin correspond to the enable/disable option of the chip, you need to check the .FEX configuration file and find the *ctp_wakeup* pin in the *ctp* option part (where the touch screen is defined) and create a GPIO entry at the end of the file with:

    [gpio_para]
    gpio_used = 1
    gpio_num = 1
    gpio_pin_1 = port:PB03<1><default><default><1>

Also you must load the CONFIG_SUN4I_GPIO_UGLY module.

For other SoCs you have to discovery that for yourself (sorry).

## More info about this chip ##

There's a page with technical info about this chip at http://linux-sunxi.org/GSL1680

## Contacting the author ##

Sergio Costas Rodriguez  
rastersoft@gmail.com  
http://www.rastersoft.com  
https://gitlab.com/rastersoft/gsl1680

