#include "driver.h"

#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <linux/i2c-dev.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <math.h>

#ifdef USE_FB
#include "linux/fb.h"
#endif

struct i2c_client {
	int adapter;
	int ufile;
	int mfile;
	char *gpio;
	char invert_x;
	char invert_y;
	int resx;
	int resy;
	char new_scroll;
};

void send_value(int value, struct i2c_client *cliente) {

	int fd;
	char buffer[20];

	sprintf(buffer,"%d",value);
	fd=open(cliente->gpio,O_WRONLY);
	if (fd<=0) {
		printf("Can't open device %s\n",cliente->gpio);
		return;
	}
	write(fd,buffer,strlen(buffer));
	close(fd);
}

static int gslX680_shutdown_low(struct i2c_client *cliente) {
	send_value(0,cliente);
	return 0;
}

static int gslX680_shutdown_high(struct i2c_client *cliente) {
	send_value(1,cliente);
	return 0;
}

static __inline__ void fw2buf(u8 *buf, const u32 *fw) {
	u32 *u32_buf = (u32 *)buf;
	*u32_buf = *fw;
}

static int gsl_ts_write(struct i2c_client *client, u8 addr, u8 *pdata, int datalen) {

	int ret = 0;
	u8 tmp_buf[128];
	unsigned int bytelen;

	if (client==NULL) {
		return datalen+1;
	}

	if (datalen > 125) {
		printf("%s too big datalen = %d!\n", __func__, datalen);
		return -1;
	}
	
	tmp_buf[0] = addr;
	bytelen=1;
	
	if (datalen != 0 && pdata != NULL) {
		memcpy(tmp_buf+1, pdata, datalen);
		bytelen += datalen;
	}
	
	ret = write(client->adapter, tmp_buf, bytelen);

	return ret;
}

static int gsl_ts_read(struct i2c_client *client, u8 addr, u8 *pdata, unsigned int datalen) {
	int ret = 0;

	if (datalen > 126) {
		printf("%s too big datalen = %d!\n", __func__, datalen);
		return -1;
	}

	ret = gsl_ts_write(client, addr, NULL, 0);
	if (ret < 0) {
		printf("%s set data address fail!\n", __func__);
		return ret;
	}
	
	return read(client->adapter, pdata, datalen);
}

static void reset_chip(struct i2c_client *client) {

	u8 buf[1];
	
	buf[0]=0x88;
	gsl_ts_write(client, GSL_STATUS_REG, buf, 1);
	usleep(10000);

	buf[0]=0x04;
	gsl_ts_write(client, 0xe4, buf, 1);
	usleep(10000);

	buf[0]=0x00;
	gsl_ts_write(client, 0xbc, buf, 1);
	usleep(10000);
	buf[0]=0x00;
	gsl_ts_write(client, 0xbd, buf, 1);
	usleep(10000);
	buf[0]=0x00;
	gsl_ts_write(client, 0xbe, buf, 1);
	usleep(10000);
	buf[0]=0x00;
	gsl_ts_write(client, 0xbf, buf, 1);
	usleep(10000);

}

static void gsl_load_fw(struct i2c_client *client,char *fw_file) {

	u8 buf[9] = {0};
	u32 source_line = 0;
	int retval;

	printf("=============gsl_load_fw start==============\n");

	FILE *fichero;
	
	fichero=fopen(fw_file,"r");
	
	if (fichero==NULL) {
		printf("Can't open firmware file %s\n",fw_file);
		return;
	}

	u32 offset;
	u32 val;
	char is_binary;
	unsigned char buffer[8];

	// check if it is a plain-text or binary file
	retval=fscanf(fichero,"{%x,%x}, ",&offset,&val);
	if (retval==2) {
		is_binary=0;
	} else {
		is_binary=1;
	}
	rewind(fichero);

	printf("Sending code\n");
	for (source_line = 0; !feof(fichero); source_line++) {
		if (is_binary==1) {
			fread(buffer,4,2,fichero);
			offset=((u32)buffer[0])+0x0100*((u32)buffer[1])+0x010000*((u32)buffer[2])+0x01000000*((u32)buffer[3]);
			val   =((u32)buffer[4])+0x0100*((u32)buffer[5])+0x010000*((u32)buffer[6])+0x01000000*((u32)buffer[7]);
		} else {
			fscanf(fichero,"{%x,%x}, ",&offset,&val);
		}
		/* init page trans, set the page val */
		if (GSL_PAGE_REG == offset) {
			fw2buf(buf, &val);
			gsl_ts_write(client, GSL_PAGE_REG, buf, 4);
		} else {
			buf[0] = (u8)offset;
			fw2buf(buf+1, &val);
			retval=gsl_ts_write(client, buf[0], buf+1, 4);
			if(retval!=5) {
				errno=retval;
				perror("Error while sending firmware data\n");
			}
		}
	}

	printf("=============gsl_load_fw end==============\n");

}

static void startup_chip(struct i2c_client *client) {
	u8 tmp = 0x00;
	gsl_ts_write(client, GSL_STATUS_REG, &tmp, 1);
	usleep(10000);	
}

static void init_chip(struct i2c_client *client,char *fw_file) {

	reset_chip(client);
	gsl_load_fw(client,fw_file);
	startup_chip(client);
	reset_chip(client);
	gslX680_shutdown_low(client);
	usleep(50000);
	gslX680_shutdown_high(client);
	usleep(30000);
	gslX680_shutdown_low(client);
	usleep(5000);
	gslX680_shutdown_high(client);
	usleep(20000);
	reset_chip(client);
	startup_chip(client);	
}

void do_sync(struct i2c_client *cliente,int file) {

	struct input_event ev;
	memset(&ev, 0, sizeof(struct input_event));

	ev.type = EV_SYN;
	ev.code = 0;
	ev.value = 0;
	write(file, &ev, sizeof(struct input_event));

}

void move_to(struct i2c_client *cliente,int x, int y) {

	struct input_event ev;

	memset(&ev, 0, sizeof(struct input_event));
	ev.type = EV_ABS;
	ev.code = ABS_X;
	ev.value = x;
	write(cliente->ufile, &ev, sizeof(struct input_event));
		
	memset(&ev, 0, sizeof(struct input_event));
	ev.type = EV_ABS;
	ev.code = ABS_Y;
	ev.value = y;
	write(cliente->ufile, &ev, sizeof(struct input_event));

	do_sync(cliente,cliente->ufile);
}

void click(struct i2c_client *cliente,int press) {

	struct input_event ev;
	memset(&ev, 0, sizeof(struct input_event));

	ev.type = EV_KEY;
	ev.code = BTN_TOUCH;
	ev.value = press;
	write(cliente->ufile, &ev, sizeof(struct input_event));

	do_sync(cliente,cliente->ufile);
}

void click_r(struct i2c_client *cliente,int press) {

	struct input_event ev;
	memset(&ev, 0, sizeof(struct input_event));

	ev.type = EV_KEY;
	ev.code = BTN_RIGHT;
	ev.value = press;
	write(cliente->mfile, &ev, sizeof(struct input_event));

	do_sync(cliente,cliente->mfile);
}

void scroll(struct i2c_client *cliente,int value) {

	struct input_event ev;
	memset(&ev, 0, sizeof(struct input_event));

	ev.type = EV_REL;
	ev.code = REL_WHEEL;
	ev.value = value;
	write(cliente->mfile, &ev, sizeof(struct input_event));

	do_sync(cliente,cliente->mfile);
}

void scrollh(struct i2c_client *cliente,int value) {

	struct input_event ev;
	memset(&ev, 0, sizeof(struct input_event));

	ev.type = EV_REL;
	ev.code = REL_HWHEEL;
	ev.value = value;
	write(cliente->mfile, &ev, sizeof(struct input_event));

	do_sync(cliente,cliente->mfile);
}

void zoom(struct i2c_client *cliente,int value) {

	struct input_event ev;

	memset(&ev, 0, sizeof(struct input_event));
	ev.type = EV_KEY;
	ev.code = KEY_LEFTCTRL;
	ev.value = 1;
	write(cliente->mfile, &ev, sizeof(struct input_event));
	do_sync(cliente,cliente->mfile);

	memset(&ev, 0, sizeof(struct input_event));
	ev.type = EV_REL;
	ev.code = REL_WHEEL;
	ev.value = value;
	write(cliente->mfile, &ev, sizeof(struct input_event));
	do_sync(cliente,cliente->mfile);

	memset(&ev, 0, sizeof(struct input_event));
	ev.type = EV_KEY;
	ev.code = KEY_LEFTCTRL;
	ev.value = 0;
	write(cliente->mfile, &ev, sizeof(struct input_event));
	do_sync(cliente,cliente->mfile);
}

void menu_ctrl(struct i2c_client *cliente) {

	struct input_event ev;

	memset(&ev, 0, sizeof(struct input_event));
	ev.type = EV_KEY;
	ev.code = KEY_LEFTCTRL;
	ev.value = 1;
	write(cliente->mfile, &ev, sizeof(struct input_event));
	do_sync(cliente,cliente->mfile);

	memset(&ev, 0, sizeof(struct input_event));
	ev.type = EV_KEY;
	ev.code = KEY_COMPOSE;
	ev.value = 1;
	write(cliente->mfile, &ev, sizeof(struct input_event));
	do_sync(cliente,cliente->mfile);

	memset(&ev, 0, sizeof(struct input_event));
	ev.type = EV_KEY;
	ev.code = KEY_COMPOSE;
	ev.value = 0;
	write(cliente->mfile, &ev, sizeof(struct input_event));
	do_sync(cliente,cliente->mfile);

	memset(&ev, 0, sizeof(struct input_event));
	ev.type = EV_KEY;
	ev.code = KEY_LEFTCTRL;
	ev.value = 0;
	write(cliente->mfile, &ev, sizeof(struct input_event));
	do_sync(cliente,cliente->mfile);

}

void read_coords(struct i2c_client *cliente) {

	u8 buffer[10];
	int retval;
	static enum read_status cstatus=RS_idle;
	static int old_x=0;
	static int old_y=0;
	static int old_dist=0;
	static int old_startx=0;
	static int old_starty=0;
	static char old_touches=0;
	static time_t old_time=0;
	static suseconds_t old_ms=0;
	struct timeval now;

	int x1=0;
	int y1=0;
	int x2=0;
	int y2=0;
	int xm=0;
	int ym=0;
	int dist=0;
	int time_passed=0;

	retval=gsl_ts_read(cliente, GSL_DATA_REG, buffer, 1);
	if (retval<=0) {
		printf("error reading number of touches: %d\n",retval);
		return;
	}
	gettimeofday(&now,NULL);
	u8 touches=buffer[0]<=3 ? buffer[0] : 3;

	if (touches==0) {
		old_time = now.tv_sec;
		old_ms = now.tv_usec/1000;
		old_touches=0;
	} else {
		time_passed=(int)(((1000*now.tv_sec)+(now.tv_usec/1000))-((old_time*1000)+old_ms));
		retval=gsl_ts_read(cliente,0x84,buffer,4);
		x1=(((unsigned int)buffer[0])+256*((unsigned int)buffer[1]))&0x0FFF;
		y1=(((unsigned int)buffer[2])+256*((unsigned int)buffer[3]))&0x0FFF;
		if (old_touches==0) {
			old_startx=x1;
			old_starty=y1;
			old_x=xm;
			old_y=ym;
			old_touches=touches;
		}
		if (cliente->invert_x) {
			x1=cliente->resx-x1-1;
		}
		if (cliente->invert_y) {
			y1=cliente->resy-y1-1;
		}
		if (touches>1) {
			retval=gsl_ts_read(cliente,0x88,buffer,4);
			x2=(((unsigned int)buffer[0])+256*((unsigned int)buffer[1]))&0x0FFF;
			y2=(((unsigned int)buffer[2])+256*((unsigned int)buffer[3]))&0x0FFF;
			if (cliente->invert_x) {
				x2=cliente->resx-x2-1;
			}
			if (cliente->invert_y) {
				y2=cliente->resy-y2-1;
			}
			xm=(x1+x2)/2;
			ym=(y1+y2)/2;
			int xt,yt;
			xt = (x1>x2) ? x1-x2 : x2-x1;
			yt = (y1>y2) ? y1-y2 : y2-y1;
			dist=xt*xt+yt*yt;
		} else {
			x2=x1;
			y2=y1;
			xm=x1;
			ym=y1;
			dist=0;
		}
	}

	switch(cstatus) {
	case RS_idle:
		if (touches==1) {
			old_x=x1;
			old_y=y1;
			cstatus=RS_one_A;
			return;
		}
		if (touches==2) {
			old_x=xm;
			old_y=ym;
			old_dist=dist;
			move_to(cliente,old_x,old_y);
			cstatus=RS_two_A;
			return;
		}
		if (touches==3) {
			cstatus=RS_three_A;
			menu_ctrl(cliente);
			return;
		}
	break;
	case RS_one_A:
		if (touches==3) {
			cstatus=RS_three_A;
			menu_ctrl(cliente);
			return;
		}
		if (touches==2) {
			old_x=xm;
			old_y=ym;
			old_dist=dist;
			move_to(cliente,old_x,old_y);
			cstatus=RS_two_A;
			return;
		}
		if (touches==0) {
			move_to(cliente,old_x,old_y);
			click(cliente,1);
			click(cliente,0);
			cstatus=RS_idle;
			return;
		}
		if (touches==1) {
			if (cliente->new_scroll) {
				int d;
				d=(old_x-xm)/X_THRESHOLD;
				if (d!=0) {
					scrollh(cliente,d);
					old_x=xm;
					cstatus=RS_one_B;
				}
				d=(ym-old_y)/Y_THRESHOLD;
				if (d!=0) {
					scroll(cliente,d);
					old_y=ym;
					cstatus=RS_one_B;
				}
				if ((cstatus==RS_one_A)&&(time_passed>1000)) {
					move_to(cliente,old_x+SINGLE_CLICK_OFFSET,old_y+SINGLE_CLICK_OFFSET);
					click(cliente,1);
					old_x=x1;
					old_y=x2;
					cstatus=RS_one_C;
				}
			} else {
				if ((old_x!=x1)||(old_y!=y1)) {
					move_to(cliente,old_x,old_y);
					click(cliente,1);
					old_x=x1;
					old_y=x2;
					cstatus=RS_one_B;
				}
			}
			return;
		}
	break;
	case RS_one_B:
		if (touches==1) {
			if ((old_x!=x1)||(old_y!=y1)) {
				if (cliente->new_scroll) {
					int d;
					d=(old_x-xm)/X_THRESHOLD;
					if (d!=0) {
						scrollh(cliente,d);
						old_x=xm;
					}
					d=(ym-old_y)/Y_THRESHOLD;
					if (d!=0) {
						scroll(cliente,d);
						old_y=ym;
					}
				} else {
					move_to(cliente,old_x,old_y);
					old_x=x1;
					old_y=y1;
				}
			}
			return;
		}
		if (touches==0) {
			click(cliente,0);
			cstatus=RS_idle;
			return;
		}
	break;
	case RS_one_C:
		if (touches==1) {
			move_to(cliente,old_x+SINGLE_CLICK_OFFSET,old_y+SINGLE_CLICK_OFFSET);
			old_x=x1;
			old_y=y1;
			return;
		}
		if (touches==0) {
			click(cliente,0);
			cstatus=RS_idle;
			return;
		}
	break;
	case RS_two_A:
		if (touches==3) {
			cstatus=RS_three_A;
			menu_ctrl(cliente);
			return;
		}
		if (touches==1) {
			old_x=x1;
			old_y=y1;
			move_to(cliente,old_x,old_y);
			cstatus=RS_right_A;
			return;
		}
	case RS_two_B:
		if (touches==2) {
			int d;
			if (!cliente->new_scroll) {
				d=(old_x-xm)/X_THRESHOLD;
				if (d!=0) {
					cstatus=RS_two_B;
					scrollh(cliente,d);
					old_x=xm;
				}
				d=(ym-old_y)/Y_THRESHOLD;
				if (d!=0) {
					cstatus=RS_two_B;
					scroll(cliente,d);
					old_y=ym;
				}
			}
			d=(dist-old_dist)/Z_THRESHOLD;
			if (d!=0) {
				if (d>0) {
					zoom(cliente,(int)(sqrt(d)));
				} else {
					zoom(cliente,-((int)(sqrt(-d))));
				}
				old_dist=dist;
				cstatus=RS_two_B;
			}
		}
		if (touches==0) {
			cstatus=RS_idle;
		}
	break;
	case RS_right_A:
		if (touches==0) {
			cstatus=RS_idle;
			return;
		}
		if ((x1!=old_x)||(y1!=old_y)) {
			old_x=x1;
			old_y=y1;
			move_to(cliente,old_x,old_y);
		}
		if (touches==2) {
			click_r(cliente,1);
			cstatus=RS_right_B;
			return;
		}
	break;
	case RS_right_B:
		if (touches==0) {
			cstatus=RS_idle;
			return;
		}
		if ((x1!=old_x)||(y1!=old_y)) {
			old_x=x1;
			old_y=y1;
			move_to(cliente,old_x,old_y);
		}
		if (touches==1) {
			click_r(cliente,0);
			cstatus=RS_right_A;
			return;
		}
	break;
	case RS_three_A:
		if (touches==0) {
			cstatus=RS_idle;
			return;
		}
	break;
	}
}

int main(int argc, char **argv) {

	struct i2c_client cliente;
	int retval;
	struct uinput_user_dev uidev;
	
	if (argc<3) {
		printf("Version 4\n");
		printf("Format: driver [-res XxY] [-gpio PATH] [-invert_x] [-invert_y] DEVICE FW_FILE\n\n");
		printf("-res XxY: specifies that the screen resolution is X width and Y height (default: 800x480)\n");
		printf("-gpio PATH: sets the path to the GPIO device that enables and disables the touch chip\n");
		printf("-invert_x: inverts the X coordinates\n");
		printf("-invert_y: inverts the Y coordinates\n");
		printf("-new_scroll: do scroll with a single finger\n");
		printf("DEVICE: path to the I2C device where the GSLx680 chip is connected\n");
		printf("FW_FILE: path to the firmware file for the GSLx680 chip\n");
		return 0;
	}

	char *adapter=NULL;
	char *firmware=NULL;
	char *option;
	cliente.invert_x=0;
	cliente.invert_y=0;
	cliente.gpio="/sys/devices/virtual/misc/sun4i-gpio/pin/pb3";
	cliente.resx=SCREEN_MAX_X;
	cliente.resy=SCREEN_MAX_Y;

#ifdef USE_FB
	int fb_dev;
	fb_dev=open("/dev/fb0",O_RDWR);
	if (fb_dev>0) {
		struct fb_var_screeninfo vinfo;
		if (0==ioctl (fb_dev, FBIOGET_VSCREENINFO, &vinfo)) {
			cliente.resx=vinfo.xres;
			cliente.resy=vinfo.yres;
	    }
	    close(fb_dev);
	}
#endif

	int loop=1;
	while(loop<argc) {
		option=argv[loop];
		loop++;
		if (option[0]=='-') {
			if (!strcmp(option,"-invert_x")) {
				cliente.invert_x=1;
				continue;
			}
			if (!strcmp(option,"-invert_y")) {
				cliente.invert_y=1;
				continue;
			}
			if (!strcmp(option,"-new_scroll")) {
				cliente.new_scroll=1;
				continue;
			}
			if (loop==argc) {
				printf("Error: option %s doesn't have parameters\n",option);
				return -1;
			}
			if (!strcmp(option,"-res")) {
				if (2!=sscanf(argv[loop],"%dx%d",&cliente.resx,&cliente.resy)) {
					printf("Error: resolution %s has an incorrect format\n",argv[loop]);
					return -1;
				}
				loop++;
				continue;
			}
			if (!strcmp(option,"-gpio")) {
				cliente.gpio=strdup(argv[loop]);
				loop++;
				continue;
			}
			printf("Unknown option %s\n",option);
			return -1;
		}
		if (adapter==NULL) {
			adapter=strdup(option);
			continue;
		}
		if (firmware==NULL) {
			firmware=strdup(option);
			continue;
		}
		printf("Too many parameters\n");
		return -1;
	}

	if (adapter==NULL) {
		printf("Missing adapter path\n");
		return -1;
	}

	if (firmware==NULL) {
		printf("Missing firmware path\n");
		return -1;
	}

	printf("Connecting to device %s, firmware %s\n",adapter,firmware);
	
	cliente.adapter=open(adapter,O_RDWR);
	if (cliente.adapter<0) {
		printf("Can't open device %s\n",adapter);
		return -1;
	}

	send_value(0,&cliente);
	usleep(100000);
	send_value(1,&cliente);

	if (ioctl(cliente.adapter, I2C_SLAVE, GSLX680_I2C_ADDR) < 0) {
		printf("Error selecting device %d\n",GSLX680_I2C_ADDR);
		return -2;
	}

	cliente.ufile=open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (cliente.ufile<0) {
		cliente.ufile=open("/dev/input/uinput", O_WRONLY | O_NONBLOCK);
		if (cliente.ufile<0) {
			printf("Can't connect to UINPUT interface.\n");
			return -2;
		}
	}

	retval = ioctl(cliente.ufile, UI_SET_EVBIT, EV_KEY);
	retval = ioctl(cliente.ufile, UI_SET_KEYBIT, BTN_TOUCH);

	retval = ioctl(cliente.ufile, UI_SET_EVBIT, EV_ABS);
	retval = ioctl(cliente.ufile, UI_SET_ABSBIT, ABS_X);
	retval = ioctl(cliente.ufile, UI_SET_ABSBIT, ABS_Y);
	
	
	memset(&uidev, 0, sizeof(uidev));

	snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "gsl1680-uinput");
	uidev.id.bustype = BUS_I2C;
	uidev.id.vendor  = 0x1;
	uidev.id.product = 0x1;
	uidev.id.version = 1;
	uidev.absmin[ABS_X] = 0;
	uidev.absmax[ABS_X] = cliente.resx-1;
	uidev.absmin[ABS_Y] = 0;
	uidev.absmax[ABS_Y] = cliente.resy-1;
	retval = write(cliente.ufile, &uidev, sizeof(uidev));
	
	retval = ioctl(cliente.ufile, UI_DEV_CREATE);
	retval = ioctl(cliente.ufile, UI_SET_PROPBIT,INPUT_PROP_DIRECT);
	retval = ioctl(cliente.ufile, UI_SET_PROPBIT,INPUT_PROP_POINTER);

	cliente.mfile=open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (cliente.mfile<0) {
		cliente.mfile=open("/dev/input/uinput", O_WRONLY | O_NONBLOCK);
		if (cliente.mfile<0) {
			printf("Can't connect to UINPUT interface.\n");
			return -2;
		}
	}

	/* When a device uses ABSolute pointing, the X server doesn't allows to also use RELative pointing.
	 * But we need it to allow scrolling and zooming, so we define another device, this time with only
	 * relative pointing.
	 * It also can emit the LEFT CONTROL key to emulate zoom in and zoom out (CTRL+vertical scroll)
	 * Finally, it allows to emit CONTROL+MENU key to interface with TabletWM
	 */

	retval = ioctl(cliente.mfile, UI_SET_EVBIT, EV_KEY);
	retval = ioctl(cliente.mfile, UI_SET_KEYBIT, BTN_LEFT);
	retval = ioctl(cliente.mfile, UI_SET_KEYBIT, BTN_RIGHT);
	retval = ioctl(cliente.mfile, UI_SET_KEYBIT, KEY_LEFTCTRL);
	retval = ioctl(cliente.mfile, UI_SET_KEYBIT, KEY_COMPOSE);

	retval = ioctl(cliente.mfile, UI_SET_EVBIT, EV_REL);
	retval = ioctl(cliente.mfile, UI_SET_RELBIT, REL_X);
	retval = ioctl(cliente.mfile, UI_SET_RELBIT, REL_Y);
	retval = ioctl(cliente.mfile, UI_SET_RELBIT, REL_WHEEL);
	retval = ioctl(cliente.mfile, UI_SET_RELBIT, REL_HWHEEL);
	
	memset(&uidev, 0, sizeof(uidev));
	snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "gsl1680-2-uinput");
	uidev.id.bustype = BUS_I2C;
	uidev.id.vendor  = 0x1;
	uidev.id.product = 0x2;
	uidev.id.version = 1;
	retval = write(cliente.mfile, &uidev, sizeof(uidev));
	
	retval = ioctl(cliente.mfile, UI_SET_PROPBIT,INPUT_PROP_POINTER);
	
	retval = ioctl(cliente.mfile, UI_DEV_CREATE);

	init_chip(&cliente,firmware);

	while(1) {
		read_coords(&cliente);
		usleep(20000); // do 50 reads per second
	}
}

