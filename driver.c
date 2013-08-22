#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <linux/i2c-dev.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#include "driver.h"

#define GSLX680_I2C_NAME 	"gslX680"
#define GSLX680_I2C_ADDR 	0x40
#define IRQ_PORT			RK30_PIN4_PC2
#define WAKE_PORT		    RK30_PIN4_PD0

#define GSL_DATA_REG		0x80
#define GSL_STATUS_REG		0xe0
#define GSL_PAGE_REG		0xf0

#define PRESS_MAX  			255
#define MAX_FINGERS			10
#define MAX_CONTACTS 		10
#define DMA_TRANS_LEN		0x20

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define swap(x, y) do { typeof(x) z = x; x = y; y = z; } while (0)

char *fw_file;

struct i2c_client {
	int addr;
	int adapter;
	int flags;
};

struct gsl_ts {
	struct i2c_client *client;
	struct workqueue_struct *wq;
	struct gsl_ts_data *dd;
	u8 *touch_data;
	u8 device_id;
	u8 prev_touches;
	bool is_suspended;
	bool int_pending;
	int irq;
};

struct gsl_ts_data {
	u8 x_index;
	u8 y_index;
	u8 z_index;
	u8 id_index;
	u8 touch_index;
	u8 data_reg;
	u8 status_reg;
	u8 data_size;
	u8 touch_bytes;
	u8 update_data;
	u8 touch_meta_data;
	u8 finger_size;
};


static inline u16 join_bytes(u8 a, u8 b) {
	u16 ab = 0;
	ab = ab | a;
	ab = ab << 8 | b;
	return ab;
}


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
	gsl_ts_write(client, 0xe0, buf, 1);
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

static void gsl_load_fw(struct i2c_client *client) {

	u8 buf[DMA_TRANS_LEN*4 + 1] = {0};
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
	gsl_ts_write(client, 0xe0, &tmp, 1);
	usleep(10000);	
}

static void init_chip(struct i2c_client *client) {

	reset_chip(client);
	gsl_load_fw(client);
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

	retval=gsl_ts_read(cliente, 0x80, buffer, 1);
	if (retval<=0) {
		printf("error leyendo coordenadas %d\n",retval);
		return;
	}
	
	if (buffer[0]==0) {
		return;
	}
	
	total=buffer[0];
	
	retval=gsl_ts_read(cliente,0x84,buffer,4);
	
	x=(((unsigned int)buffer[0])+256*((unsigned int)buffer[1]));
	y=(((unsigned int)buffer[2])+256*((unsigned int)buffer[3]));
	vx=(x>>12)&0x000F;
	vy=(y>>12)&0x000F;
	printf("Pulsacion en %dx%d, vx %d (%d pulsaciones)\r",x&0x0FFF,y&0x0FFF,vx,total);
}


int main(int argc, char **argv) {

	struct i2c_client cliente;
	
	if (argc!=4) {
		printf("Format: driver DEVICE ADDRESS FW_FILE\n");
		return 0;
	}

	int addr = atoi(argv[2]); /* The I2C address */
	
	fw_file=argv[3]; // firmware file
	
	printf("Connecting to device %s, address %d, firmware %s\n",argv[1],addr,fw_file);
	
	cliente.adapter=open(argv[1],O_RDWR);
	if (cliente.adapter<0) {
		printf("Can't open device\n");
		return -1;
	}

	cliente.addr=addr;

	system("echo 0 > /sys/devices/virtual/misc/sun4i-gpio/pin/pb3");
	sleep(1);
	system("echo 1 > /sys/devices/virtual/misc/sun4i-gpio/pin/pb3");

	if (ioctl(cliente.adapter, I2C_SLAVE, addr) < 0) {
		/* ERROR HANDLING; you can check errno to see what went wrong */
		printf("Error selecting device %d\n",addr);
		return -2;
	}

	init_chip(&cliente);

	while(1) {
		read_coords(&cliente);
	}
}
