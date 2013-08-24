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

#include "driver.h"

struct i2c_client {
	int adapter;
	int ufile;
};


static int gslX680_shutdown_low(void) {
	system("echo 0 > /sys/devices/virtual/misc/sun4i-gpio/pin/pb3");
	return 0;
}

static int gslX680_shutdown_high(void) {
	system("echo 1 > /sys/devices/virtual/misc/sun4i-gpio/pin/pb3");
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

	for (source_line = 0; !feof(fichero); source_line++) 	{
		fscanf(fichero,"{%x,%x}, ",&offset,&val);
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
   				perror("Error al enviar datos\n");
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
	gslX680_shutdown_low();	
	usleep(50000); 	
	gslX680_shutdown_high();	
	usleep(30000); 		
	gslX680_shutdown_low();	
	usleep(5000); 	
	gslX680_shutdown_high();	
	usleep(20000); 	
	reset_chip(client);
	startup_chip(client);	
}


void read_coords(struct i2c_client *cliente) {

	u8 buffer[10];
	int retval;
	unsigned int x,y,total;
	unsigned int vx,vy;
	struct input_event ev;

	static char pressed=0;

	retval=gsl_ts_read(cliente, GSL_DATA_REG, buffer, 1);
	if (retval<=0) {
		printf("error leyendo coordenadas %d\n",retval);
		return;
	}
	
	if (buffer[0]==0) { // no touch
		if(pressed==1) {
			pressed=0;
			memset(&ev, 0, sizeof(struct input_event));

			ev.type = EV_KEY;
			ev.code = BTN_LEFT;
			ev.value = 0; // Release event
			retval=write(cliente->ufile, &ev, sizeof(struct input_event));

			memset(&ev, 0, sizeof(struct input_event));

			ev.type = EV_SYN;
			ev.code = 0;
			ev.value = 0;
			retval=write(cliente->ufile, &ev, sizeof(struct input_event));
		}
		return;
	}
	
	total=buffer[0];
	
	retval=gsl_ts_read(cliente,0x84,buffer,4);
	
	x=(((unsigned int)buffer[0])+256*((unsigned int)buffer[1]));
	y=(((unsigned int)buffer[2])+256*((unsigned int)buffer[3]));
	vx=(x>>12)&0x000F;
	vy=(y>>12)&0x000F;
	x&=0x0FFF;
	y&=0x0FFF;

	memset(&ev, 0, sizeof(struct input_event));

	ev.type = EV_ABS;
	ev.code = ABS_X;
	ev.value = x;
	retval=write(cliente->ufile, &ev, sizeof(struct input_event));

	memset(&ev, 0, sizeof(struct input_event));

	ev.type = EV_ABS;
	ev.code = ABS_Y;
	ev.value = y;
	retval=write(cliente->ufile, &ev, sizeof(struct input_event));

	memset(&ev, 0, sizeof(struct input_event));

	ev.type = EV_SYN;
	ev.code = 0;
	ev.value = 0;
	retval=write(cliente->ufile, &ev, sizeof(struct input_event));

	if(pressed==0) {
		pressed=1;
		memset(&ev, 0, sizeof(struct input_event));

		ev.type = EV_KEY;
		ev.code = BTN_LEFT;
		ev.value = 1; // press event
		retval=write(cliente->ufile, &ev, sizeof(struct input_event));

		memset(&ev, 0, sizeof(struct input_event));

		ev.type = EV_SYN;
		ev.code = 0;
		ev.value = 0;
		retval=write(cliente->ufile, &ev, sizeof(struct input_event));
	}
}


int main(int argc, char **argv) {

	struct i2c_client cliente;
	int ufile;
	int retval;
	
	if (argc!=3) {
		printf("Format: driver DEVICE FW_FILE\n");
		return 0;
	}
	
	printf("Connecting to device %s, firmware %s\n",argv[1],argv[2]);
	
	cliente.adapter=open(argv[1],O_RDWR);
	if (cliente.adapter<0) {
		printf("Can't open device\n");
		return -1;
	}

	system("echo 0 > /sys/devices/virtual/misc/sun4i-gpio/pin/pb3");
	usleep(100000);
	system("echo 1 > /sys/devices/virtual/misc/sun4i-gpio/pin/pb3");

	if (ioctl(cliente.adapter, I2C_SLAVE, GSLX680_I2C_ADDR) < 0) {
		printf("Error selecting device %d\n",GSLX680_I2C_ADDR);
		return -2;
	}

	ufile=open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (ufile<0) {
		ufile=open("/dev/input/uinput", O_WRONLY | O_NONBLOCK);
		if (ufile<0) {
			printf("Can't connect to UINPUT interface.\n");
			return -2;
		}
	}
	
	
	retval = ioctl(ufile, UI_SET_EVBIT, EV_KEY);
	retval = ioctl(ufile, UI_SET_KEYBIT, BTN_LEFT);
	retval = ioctl(ufile, UI_SET_EVBIT, EV_ABS);
	
	// allow to send absolute coordinates
	retval = ioctl(ufile, UI_SET_ABSBIT, ABS_X);
	retval = ioctl(ufile, UI_SET_ABSBIT, ABS_Y);
	
	struct uinput_user_dev uidev;

	memset(&uidev, 0, sizeof(uidev));

	snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "gsl1680-uinput");
	uidev.id.bustype = BUS_I2C;
	uidev.id.vendor  = 0x1;
	uidev.id.product = 0x1;
	uidev.id.version = 1;
	uidev.absmin[ABS_X] = 0;
	uidev.absmax[ABS_X] = SCREEN_MAX_X-1;
	uidev.absmin[ABS_Y] = 0;
	uidev.absmax[ABS_Y] = SCREEN_MAX_Y-1;
	retval = write(ufile, &uidev, sizeof(uidev));
	
	retval = ioctl(ufile, UI_DEV_CREATE);

	cliente.ufile=ufile;

	init_chip(&cliente,argv[2]);

	while(1) {
		read_coords(&cliente);
		usleep(15000);
	}
}

