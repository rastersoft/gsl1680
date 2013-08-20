#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <linux/i2c-dev.h>
#include "gslx680_ts.h"
#include "gslX680.h"
#include "gslx680_1.h"
#include "gslx680_2.h"
#include "gslx680_3.h"
#include "gt811.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
       
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

int fw;

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

static struct gsl_ts_data devices[] = {
	{
		.x_index = 6,
		.y_index = 4,
		.z_index = 5,
		.id_index = 7,
		.data_reg = GSL_DATA_REG,
		.status_reg = GSL_STATUS_REG,
		.update_data = 0x4,
		.touch_bytes = 4,
		.touch_meta_data = 4,
		.finger_size = 70,
	},
};

static u32 id_sign[MAX_CONTACTS+1] = {0};
static u8 id_state_flag[MAX_CONTACTS+1] = {0};
static u8 id_state_old_flag[MAX_CONTACTS+1] = {0};
static u16 x_new = 0;
static u16 y_new = 0;
static u16 x_old[MAX_CONTACTS+1] = {0};
static u16 y_old[MAX_CONTACTS+1] = {0};

static inline u16 join_bytes(u8 a, u8 b) {
	u16 ab = 0;
	ab = ab | a;
	ab = ab << 8 | b;
	return ab;
}


static void record_point(u16 x, u16 y , u8 id) {

	u16 x_err =0;
	u16 y_err =0;

	id_sign[id]=id_sign[id]+1;
	
	if(id_sign[id]==1) {
		x_old[id]=x;
		y_old[id]=y;
	}

	x = (x_old[id] + x)/2;
	y = (y_old[id] + y)/2;
		
	if(x>x_old[id]) {
		x_err=x -x_old[id];
	} else {
		x_err=x_old[id]-x;
	}

	if(y>y_old[id]) {
		y_err=y -y_old[id];
	} else {
		y_err=y_old[id]-y;
	}

	if((x_err > 6 && y_err > 2) || (x_err > 2 && y_err > 6)){
		x_new = x;
		x_old[id] = x;
		
		y_new = y;
		y_old[id] = y;
	} else {
		if(x_err > 6) {
			x_new = x;
			x_old[id] = x;
		} else {
			x_new = x_old[id];
		}
		if(y_err> 6) {
			y_new = y;
			y_old[id] = y;
		} else {
			y_new = y_old[id];
		}
	}

	if(id_sign[id]==1) {
		x_new= x_old[id];
		y_new= y_old[id];
	}
}


static void report_data(struct gsl_ts *ts, u16 x, u16 y, u8 pressure, u8 id) {

	swap(x, y);

	printf("#####id=%d,x=%d,y=%d######\n",id,x,y);

	if(x>=SCREEN_MAX_X||y>=SCREEN_MAX_Y) {
		return;
	}

	if(SCREEN_SWITCH_XY) {
		x=SCREEN_MAX_X-x;
		y=SCREEN_MAX_Y-y;
	}

	printf("X: %d  Y: %d  track_id: %d   pressure: %d\n",x,y,id,pressure);

#if 0
	input_report_abs(ts->input, ABS_MT_TRACKING_ID, id);
	input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, pressure);
	input_report_abs(ts->input, ABS_MT_POSITION_X,x);
	input_report_abs(ts->input, ABS_MT_POSITION_Y, y);
	input_report_abs(ts->input, ABS_MT_WIDTH_MAJOR, 1);
	input_mt_sync(ts->input);
#endif
}


static int gslX680_shutdown_low(void) {
	/*gpio_direction_output(WAKE_PORT, 0);
	gpio_set_value(WAKE_PORT,GPIO_LOW);*/
	system("echo 0 > /sys/devices/virtual/misc/sun4i-gpio/pin/pb3");
	return 0;
}

static int gslX680_shutdown_high(void) {
/*	gpio_direction_output(WAKE_PORT, 0);
	gpio_set_value(WAKE_PORT,GPIO_HIGH);*/
	system("echo 1 > /sys/devices/virtual/misc/sun4i-gpio/pin/pb3");
	return 0;
}

static __inline__ void fw2buf(u8 *buf, const u32 *fw) {
	u32 *u32_buf = (int *)buf;
	*u32_buf = *fw;
}

static u32 gsl_read_interface(struct i2c_client *client, u8 reg, u8 *buf, u32 num) {

	struct i2c_msg xfer_msg[2];

	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = 1;
	xfer_msg[0].flags = client->flags & I2C_M_TEN;
	xfer_msg[0].buf = &reg;

	xfer_msg[1].addr = client->addr;
	xfer_msg[1].len = num;
	xfer_msg[1].flags |= I2C_M_RD;
	xfer_msg[1].buf = buf;

	struct i2c_rdwr_ioctl_data datos;
	datos.msgs=xfer_msg;
	datos.nmsgs=ARRAY_SIZE(xfer_msg);

	if (reg < 0x80) {
		ioctl(client->adapter, I2C_RDWR, datos);
		usleep(5000);
	}

	return ioctl(client->adapter, I2C_RDWR, datos) == ARRAY_SIZE(xfer_msg) ? 0 : -EFAULT;
}

static u32 gsl_write_interface(struct i2c_client *client, const u8 reg, u8 *buf, u32 num) {

	struct i2c_msg xfer_msg[1];

	buf[0] = reg;

	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = num + 1;
	xfer_msg[0].flags = client->flags & I2C_M_TEN;
	xfer_msg[0].buf = buf;

	struct i2c_rdwr_ioctl_data datos;
	datos.msgs=xfer_msg;
	datos.nmsgs=ARRAY_SIZE(xfer_msg);

	return (ioctl(client->adapter, I2C_RDWR, datos) == 1) ? 0 : -EFAULT;
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

static void gsl_load_fw2(struct i2c_client *client) {

	u32 source_len;
	u32 source_line;
	u8  source_data;
	u8  buf[6];
	
	switch(fw) {
	case 7:
		source_data=firm_1;
		source_len=ARRAY_SIZE(firm_1);
	break;
	case 8:
		source_data=firm_2;
		source_len=ARRAY_SIZE(firm_2);
	break;
	case 9:
		source_data=firm_3;
		source_len=ARRAY_SIZE(firm_3);
	break;

	printf("=============gsl_load_fw start 2==============\n");

	
	buf[0]=GSL_PAGE_REG;
	buf[1]=0x00;
	buf[2]=0x00;
	buf[3]=0x00;
	buf[4]=0x00;
	retval=gsl_ts_write(client, buf[0], buf+1, 4);
	for (source_line = 0; source_line < source_len; source_line++) 	{
		buf[2]=source_data[source_line];
		retval=gsl_ts_write(client, buf[1], buf+2, 1);
		if(retval!=2) {
			errno=retval;
			perror("Error al enviar datos 2\n");
		}
	}

	printf("=============gsl_load_fw end 2==============\n");


}

static void gsl_load_fw(struct i2c_client *client) {

	u8 buf[DMA_TRANS_LEN*4 + 1] = {0};
	u8 send_flag = 1;
	u8 *cur = buf + 1;
	u32 source_line = 0;
	u32 source_len;
	int retval;

	if (fw>6) {
		gsl_load_fw2(client);
		return;
	}

	static const struct fw_data *local_GSLX680_FW;
	
	switch(fw) {
	case 0:
		local_GSLX680_FW=GSLX680_FW;
		source_len = ARRAY_SIZE(GSLX680_FW);
	break;
	case 1:
		local_GSLX680_FW=GSL1680_FW;
		source_len = ARRAY_SIZE(GSL1680_FW);
	break;
	case 2:
		local_GSLX680_FW=GSL1680_1_FW;
		source_len = ARRAY_SIZE(GSL1680_1_FW);
	break;
	case 3:
		local_GSLX680_FW=GSL2680_FW;
		source_len = ARRAY_SIZE(GSL2680_FW);
	break;
	case 4:
		local_GSLX680_FW=GSL1680E_FW;
		source_len = ARRAY_SIZE(GSL1680E_FW);
	break;
	case 5:
		local_GSLX680_FW=GSL1680D_FW;
		source_len = ARRAY_SIZE(GSL1680D_FW);
	break;
	case 6:
		local_GSLX680_FW=GSL1680E2_FW;
		source_len = ARRAY_SIZE(GSL1680E2_FW);
	break;
	default:
		return;
	break;
	}

	printf("=============gsl_load_fw start==============\n");

	for (source_line = 0; source_line < source_len; source_line++) 	{
		/* init page trans, set the page val */
		if (GSL_PAGE_REG == local_GSLX680_FW[source_line].offset) {
			fw2buf(buf, &local_GSLX680_FW[source_line].val);
			gsl_ts_write(client, GSL_PAGE_REG, buf, 4);
		} else {
			fw2buf(buf+1, &local_GSLX680_FW[source_line].val);
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

	//test_i2c(client);
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

static void test_i2c(struct i2c_client *client) {
	u8 read_buf = 0;
	u8 write_buf = 0x12;
	int ret;
	ret = gsl_ts_read( client, 0xf0, &read_buf, sizeof(read_buf) );
	if (ret  < 0) {
		printf("I2C transfer error!\n");
	} else {
		printf("I read reg 0xf0 is %x\n", read_buf);
	}
	usleep(10000);

	ret = gsl_ts_write(client, 0xf0, &write_buf, sizeof(write_buf));
	if (ret  < 0) {
		printf("I2C transfer error!\n");
	} else {
		printf("I write reg 0xf0 0x12\n");
	}
	usleep(10000);

	ret = gsl_ts_read( client, 0xf0, &read_buf, sizeof(read_buf) );
	if (ret  <  0) {
		printf("I2C transfer error!\n");
	} else {
		printf("I read reg 0xf0 is 0x%x\n", read_buf);
	}
	usleep(10000);
}

static void check_mem_data(struct i2c_client *client) {
	char write_buf;
	char read_buf[4]  = {0};
	
	usleep(30000);
	write_buf = 0x00;
	gsl_ts_write(client,0xf0, &write_buf, sizeof(write_buf));
	gsl_ts_read(client,0x00, read_buf, sizeof(read_buf));
	gsl_ts_read(client,0x00, read_buf, sizeof(read_buf));
	if (read_buf[3] != 0x1 || read_buf[2] != 0 || read_buf[1] != 0 || read_buf[0] != 0) {
		printf("!!!!!!!!!!!page: %x offset: %x val: %x %x %x %x\n",0x0, 0x0, read_buf[3], read_buf[2], read_buf[1], read_buf[0]);
		init_chip(client);
	}
}

static int check_mem_data_init(struct i2c_client *client) {
	char write_buf;
	char read_buf[4]  = {0};
	
	usleep(30000);
	write_buf = 0x00;
	gsl_ts_write(client,0xf0, &write_buf, sizeof(write_buf));
	gsl_ts_read(client,0x00, read_buf, sizeof(read_buf));
	gsl_ts_read(client,0x00, read_buf, sizeof(read_buf));
	if (read_buf[3] != 0x1 || read_buf[2] != 0 || read_buf[1] != 0 || read_buf[0] != 0)	{
		printf("!!!!!!!!!!!page: %x offset: %x val: %x %x %x %x\n",0x0, 0x0, read_buf[3], read_buf[2], read_buf[1], read_buf[0]);
		return -1;
	}
	
	return 0;
}


static int gsl_ts_init_ts(struct i2c_client *client, struct gsl_ts *ts) {

	int i, rc = 0;
	
	printf("[GSLX680] Enter %s\n", __func__);

	ts->dd = &devices[ts->device_id];

	if (ts->device_id == 0) {
		ts->dd->data_size = MAX_FINGERS * ts->dd->touch_bytes + ts->dd->touch_meta_data;
		ts->dd->touch_index = 0;
	}

	ts->touch_data = malloc(ts->dd->data_size);
	if (!ts->touch_data) {
		printf("%s: Unable to allocate memory\n", __func__);
		return -ENOMEM;
	}

	ts->prev_touches = 0;

	ts->client=client;

	return rc;
}


static void process_gslX680_data(struct gsl_ts *ts)
{
	u8 id, touches;
	u16 x, y;
	int i = 0;

	touches = ts->touch_data[ts->dd->touch_index];
	for(i=1;i<=MAX_CONTACTS;i++) {
		if(touches == 0) {
			id_sign[i] = 0;
		}
		id_state_flag[i] = 0;
	}
	for(i= 0;i < (touches > MAX_FINGERS ? MAX_FINGERS : touches);i ++) {
		x = join_bytes( ( ts->touch_data[ts->dd->x_index  + 4 * i + 1] & 0xf),ts->touch_data[ts->dd->x_index + 4 * i]);
		y = join_bytes(ts->touch_data[ts->dd->y_index + 4 * i + 1],ts->touch_data[ts->dd->y_index + 4 * i ]);
		id = ts->touch_data[ts->dd->id_index + 4 * i] >> 4;

		if(1 <=id && id <= MAX_CONTACTS) {
			record_point(x, y , id);
			report_data(ts, x_new, y_new, 10, id);		
			id_state_flag[id] = 1;
		}
	}
	for(i=1;i<=MAX_CONTACTS;i++) {
		if( (0 == touches) || ((0 != id_state_old_flag[i]) && (0 == id_state_flag[i])) ) {
			id_sign[i]=0;
		}
		id_state_old_flag[i] = id_state_flag[i];
	}

	ts->prev_touches = touches;
}



static void gsl_ts_xy_worker(struct gsl_ts *ts) {

	int rc;
	u8 read_buf[4] = {0};

	if (ts->is_suspended == true) {
		printf("TS is supended\n");
		ts->int_pending = true;
		goto schedule;
	}

	/* read data from DATA_REG */
	rc = gsl_ts_read(ts->client, 0x80, ts->touch_data, ts->dd->data_size);
	printf("---touches: %d ---\r",ts->touch_data[0]);

		
	if (rc < 0) {
		printf("read failed\n");
		goto schedule;
	}

	if (ts->touch_data[ts->dd->touch_index] == 0xff) {
		goto schedule;
	}

	rc = gsl_ts_read( ts->client, 0xbc, read_buf, sizeof(read_buf));
	if (rc < 0) {
		printf("read 0xbc failed\n");
		goto schedule;
	}
	if (ts->touch_data[0]!=0) {
		printf("\n//////// reg %x : %x %x %x %x\n",0xbc, read_buf[3], read_buf[2], read_buf[1], read_buf[0]);
	}
		
	if (read_buf[3] == 0 && read_buf[2] == 0 && read_buf[1] == 0 && read_buf[0] == 0) {
		process_gslX680_data(ts);
	} else {
		reset_chip(ts->client);
		startup_chip(ts->client);
	}
schedule:
	return;
}

void read_coords(struct i2c_client *cliente) {

	u8 buffer[10];
	int retval;
	unsigned int x,y;

	retval=gsl_ts_read(cliente, 0x80, buffer, 1);
	if (retval<=0) {
		printf("error leyendo coordenadas %d\n",retval);
		return;
	}
	
	if (buffer[0]==0) {
		return;
	}
	
	retval=gsl_ts_read(cliente,0x84,buffer,4);
	
	x=(((unsigned int)buffer[0])+256*((unsigned int)buffer[1]))&0x0FFF;
	y=(((unsigned int)buffer[2])+256*((unsigned int)buffer[3]))&0x0FFF;
	printf("Pulsacion en %dx%d\n",x,y);
}

int main(int argc, char **argv) {

	struct i2c_client cliente;
	
	if (argc!=4) {
		printf("Format: driver DEVICE ADDRESS FW_VERSION\n");
		return 0;
	}

	int addr = atoi(argv[2]); /* The I2C address */
	
	fw=atoi(argv[3]); // firmware version
	
	printf("Connecting to device %s, address %d, firmware %d\n",argv[1],addr,fw);
	
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
	//test_i2c(&cliente);

	//reset_chip(&cliente);
	while(1) {
		read_coords(&cliente);
	}

	struct gsl_ts ts;
	ts.device_id=0;
	/*gsl_ts_init_ts(&cliente,&ts);
	while(1) {
		gsl_ts_xy_worker(&ts);
	}*/

}
