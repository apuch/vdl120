/*
*
*  vdl120: The Voltcraft DL-120TH tool
*
*   + write configuration
*   + read configuration
*   + read log data
*   + store log data for plotting
*
*  DEPENDENCIES
*
*   + libusb-0.1
*
*  TODO
*
*   + port to libusb-1.x
*   + dont rely on the host's byte order (endianness)
*   + clean up: wrap usb_bulk_* functions + error handling
*   + find a better 'num2bin' algorithm
*   + more config options (?)
*
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <usb.h>
#include <time.h>

#include "num2bin.c"


/* hardware specs */

#define VID 0x10c4 /* Cygnal Integrated Products, Inc. */
#define PID 0x0003 /* Silabs C8051F320 USB Board */
#define PID2 0xea61
//#define EP_IN  0x81
//#define EP_OUT 0x02
int EP_IN = 0;
int EP_OUT = 0;
#define BUFSIZE 64 /* wMaxPacketSize = 1x 64 bytes */
#define TIMEOUT 5000
#define TEMP_MIN -40 /* same with celsius and fahrenheit */
#define TEMP_MAX_C 70
#define TEMP_MAX_F 158
#define RH_MIN 0
#define RH_MAX 100

#define ERR(...) do { fprintf(stderr, "ERR: %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); } while (0)


/* struct definitions */

struct config {
/*  0- 3 */  int config_begin; /* 0xce = set config, 0x00 = logger is active */
/*  4- 7 */  int num_data_conf; /* number of data configured */
/*  8-11 */  int num_data_rec; /* number of data recorded */
/* 12-15 */  int interval; /* log interval in seconds */
/* 16-19 */  int time_year;
/* 20-21 */  short int padding20;
/* 22-23 */  short int thresh_temp_low;
/* 24-25 */  short int padding24;
/* 26-27 */  short int thresh_temp_high;
/* 28    */  char time_mon; /* start time, local (!) timezone */
/* 29    */  char time_mday;
/* 30    */  char time_hour;
/* 31    */  char time_min;
/* 32    */  char time_sec;
/* 33    */  char temp_is_fahrenheit;
/* 34    */  char led_conf; /* bit 0: alarm on/off, bits 1-2: 10 (?), bits 3-7: flash frequency in seconds */
// 35 ?!
/* 35-50 */  char name[16]; /* config name. actually just 16 bytes: 35-50 */
/* 51    */  char start; /* 0x02 = start logging immediately; 0x01 = start logging manually */
/* 52-53 */  short int padding52;
/* 54-55 */  short int thresh_rh_low;
/* 56-57 */  short int padding56;
/* 58-59 */  short int thresh_rh_high;
/* 60-63 */  int config_end; /* = config_begin */
};

struct data {
	short int temp; /* temperature in °C or °F, check cfg->temp_is_fahrenheit */
	short int rh; /* relative humidity in % */
	time_t time; /* timestamp, unix time, GMT (!) timezone */
	struct data *next; /* next data set or NULL */
};


/* function prototypes */

struct config *                     /* return value: config struct */
read_config(
	struct usb_dev_handle *dev_hdl  /* usb dev handle */
);

int                                 /* return value: success (bool) */
write_config(
	struct usb_dev_handle *dev_hdl, /* usb dev handle */
	struct config *cfg              /* config struct */
);

struct config *                     /* return value: config struct */
build_config(
	char *name,                     /* logger name */
	int num_data,                   /* number of data points to collect */
	int interval,                   /* log frequency in seconds */
	int thresh_temp_low,
	int thresh_temp_high,
	int thresh_rh_low,
	int thresh_rh_high,
	int temp_is_fahrenheit,         /* bool: temp is fahrenheit */
	int led_alarm,                  /* bool: led alarm */
	int led_freq,                   /* led frequency in seconds */
	int start                       /* start loggin: 1 = manually, 2 = automatically */
);

void
print_config(
	struct config *cfg, /* config struct */
	char *line_prefix   /* prefix to print before each line */
);

int                    /* return value: 0 = valid config, 1 = invalid config */
check_config(
	struct config *cfg /* config struct */
);

struct data *                       /* return value: first data struct */
read_data(
	struct usb_dev_handle *dev_hdl, /* usb dev handle */
	struct config *cfg              /* config struct */
);

void
print_data(
	struct data *data_first
);

void
store_data(
	struct config *cfg,
	struct data *data_first
);


/* function implementations */

struct config *                     /* return value: config struct */
read_config(
	struct usb_dev_handle *dev_hdl  /* usb dev handle */
) {
	
	char buf[BUFSIZE];
	int ret;
	struct config *cfg;
	
	/* 00 10 01 --> read config */
	
	buf[0] = 0x00;
	buf[1] = 0x10;
	buf[2] = 0x01;
	
	ret = usb_bulk_write(
		dev_hdl,
		EP_OUT,
		buf,
		3,
		TIMEOUT
	);
	if (ret < 0)
	{
		printf("usb_bulk_write failed with code %i: %s\n", ret, usb_strerror());
		return NULL;
	}
	
	
	/* read response header (3 bytes) */
	
	ret = usb_bulk_read(
		dev_hdl,
		EP_IN,
		buf,
		3,
		TIMEOUT
	);
	if (ret < 0)
	{
		ERR("usb_bulk_read failed with code %i: %s\n", ret, usb_strerror());
		return NULL;
	}
/*
	printf("read_config: response header:");
	for (i=0; i<ret; i++)
	{
		printf(" %02x", 0xFF & buf[i]);
	}
	printf("\n");
*/
	
	// buf[1:2] --> logger status?
	// 02 00 00 = no data, ready to log
	// 02 b8 01 = still logging, data available
	// 02 58 13 = ditto
	// 02 c8 00 = done logging, data available
	
	/* read response data (64 byte) */
	
	cfg = malloc(sizeof(struct config));
	
	ret = usb_bulk_read(
		dev_hdl,
		EP_IN,
		(char *)cfg,
		64,
		TIMEOUT
	);
	if (ret < 0)
	{
		ERR("usb_bulk_read failed with code %i: %s\n", ret, usb_strerror());
		return NULL;
	}
	
	return cfg;
}

int                                 /* return value: 0 = success */
write_config(
	struct usb_dev_handle *dev_hdl, /* usb dev handle */
	struct config *cfg              /* config struct */
) {
	char buf[BUFSIZE];
	int ret;
	
	/* write config header */
	
	/* 01 40 00 --> write config */
	
	buf[0] = 0x01;
	buf[1] = 0x40;
	buf[2] = 0x00;
	
	ret = usb_bulk_write(
		dev_hdl,
		EP_OUT,
		buf,
		3,
		TIMEOUT
	);
	if (ret < 0)
	{
		printf("usb_bulk_write failed with code %i: %s\n", ret, usb_strerror());
		return 1;
	}
	
	
	/* write config data */
	
/*
	printf("writing config data:");
	for (i=0; i<64; i++)
	{
		if (i % 8 == 0)
			printf("\n\t");
		printf("%02x ", 0xFF & *(((char *)cfg)+i));
	}
	printf("\n");
*/
	
	ret = usb_bulk_write(
		dev_hdl,
		EP_OUT,
		(char *)cfg,
		64,
		TIMEOUT
	);
	if (ret < 0)
	{
		printf("usb_bulk_write failed with code %i: %s\n", ret, usb_strerror());
		return 1;
	}
	
	/* read response code (1 byte) */
	
	ret = usb_bulk_read(
		dev_hdl,
		EP_IN,
		buf,
		1,
		TIMEOUT
	);
	if (ret < 0)
	{
        ERR("usb_bulk_read failed with code %i: %s\n", ret, usb_strerror());
		return 1;
	}
	
	if ((buf[0] & 0xff) != 0xff)
	{
		printf("write_config failed, response code: %02x\n", (buf[0] & 0xff));
		return 1;
	}
	
	return 0;
}

struct data *                       /* return value: first data struct */
read_data(
	struct usb_dev_handle *dev_hdl, /* usb dev handle */
	struct config *cfg              /* config struct */
) {
	
	char buf[BUFSIZE];
	//char buf[1024];
	int ret, i, num_data;
	
	struct data *data_first = NULL;
	struct data *data_last  = NULL;
	struct data *data_curr  = NULL;
	
	/* try to read config */
	if (cfg == NULL)
	{
		cfg = read_config(dev_hdl);
		
		if (cfg == NULL)
		{
			printf("read_data: failed to read config\n");
			return NULL;
		}
	}
	
	if (cfg->num_data_rec == 0)
	{
		printf("read_data: no data to read\n");
		return NULL;
	}
	
	buf[0] = 0x00;
	buf[1] = 0x00;
	buf[2] = 0x40;
	
	ret = usb_bulk_write(
		dev_hdl,
		EP_OUT,
		buf,
		3,
		TIMEOUT
	);
	if (ret < 0)
	{
		printf("usb_bulk_write failed with code %i: %s\n", ret, usb_strerror());
		return NULL;
	}
	
	struct tm *time_start;
	time_start = malloc(sizeof(struct tm));
	memset(time_start, 0, sizeof(struct tm));
	time_start->tm_year = -1900 + cfg->time_year;
	time_start->tm_mon  = -1 + cfg->time_mon;
	time_start->tm_mday = cfg->time_mday;
	time_start->tm_hour = cfg->time_hour;
	time_start->tm_min  = cfg->time_min;
	time_start->tm_sec  = cfg->time_sec;
	time_start->tm_isdst = -1;
	
	time_t time_start_stamp;
	
	// create GMT timestamps for Gnuplot
	setenv("TZ", "GMT", 1);
	time_start_stamp = mktime(time_start);
	unsetenv("TZ");
	
	num_data = 0;
	while (num_data < cfg->num_data_rec)
	{
		
		/* send (random?) keep-alive packet every 1024 bytes */
		/* the logger sends another response header before further data */
		
		if (num_data > 0 && num_data % 1024 == 0)
		{
			buf[0] = 0x00;
			buf[1] = 0x01;
			buf[2] = 0x40;
			ret = usb_bulk_write(
				dev_hdl,
				EP_OUT,
				buf,
				3,
				TIMEOUT
			);
			if (ret < 0)
			{
				printf("usb_bulk_write failed with code %i: %s\n", ret, usb_strerror());
				return NULL;
			}
		}
		
		/* read response header (3 bytes) */
		
		if (num_data % 1024 == 0)
		{
			ret = usb_bulk_read(
				dev_hdl,
				EP_IN,
				buf,
				3,
				TIMEOUT
			);
			if (ret < 0)
			{
				ERR("usb_bulk_read failed with code %i: %s\n", ret, usb_strerror());
				return NULL;
			}
/*
			printf("read_data: response header:");
			for (i=0; i<ret; i++)
			{
				printf(" %02x", 0xFF & buf[i]);
			}
			printf("\n");
*/
		}
		
		
		/* read response data (64 byte) */
		
		ret = usb_bulk_read(
			dev_hdl,
			EP_IN,
			buf,
			64, // 1024
			TIMEOUT
		);
		if (ret < 0)
		{
			ERR("usb_bulk_read failed with code %i: %s\n", ret, usb_strerror());
			return NULL;
		}

/*
		printf("read_data: response data:");
		for (i=0; i<ret; i++)
		{
			if (i % 8 == 0)
				printf("\n   ");
			printf(" %02x", 0xFF & buf[i]);
		}
		printf("\n");
*/
		
		
		/* parse data: 4 bytes per data point (64/4=16) */
		
		//for (i = 0; i < 16; i++)
		for (i = 0; i < ret/4; i++)
		{
			data_curr = malloc(sizeof(struct data));
			memcpy((char *)data_curr, (char *)buf+i*4, 4);
			data_curr->time = time_start_stamp + num_data * cfg->interval;
			data_curr->next = NULL;
			
			if (num_data == 0)
				data_first = data_curr;
			else
				data_last->next = data_curr;
			data_last = data_curr;
			
			num_data++;
			if (num_data == cfg->num_data_rec)
				break;
		}
	}

printf("num_data = %i\n", num_data);
	
	return data_first;
}	


void
print_data(
	struct data *data_first
) {
	struct data *data_curr;
	data_curr = data_first;
	
	if (data_curr == NULL)
		return;
	
	do {
		
		printf("%i %.1f %.1f\n", (int)data_curr->time, data_curr->temp/10.0, data_curr->rh/10.0);
		data_curr = data_curr->next;
		
	} while (data_curr != NULL);
}


void
store_data(
	struct config *cfg,
	struct data *data_first
) {
	struct data *data_curr;
	char dumpfile_path[1024];
	FILE *dumpfile = NULL;
	
	data_curr = data_first;
	if (data_curr == NULL)
		return;
	
	sprintf(dumpfile_path, "%s.dat", cfg->name),
	dumpfile = fopen(dumpfile_path, "a");
	if (dumpfile == NULL)
	{
		printf("store_data: failed to fopen(\"%s\", \"a+\")", dumpfile_path);
		return;
	}
	printf("writing log data to %s\n", dumpfile_path);
	
	fprintf(dumpfile, "# [%04i-%02i-%02i %02i:%02i:%02i] %i points @ %i sec\n",
		cfg->time_year,
		cfg->time_mon,
		cfg->time_mday,
		cfg->time_hour,
		cfg->time_min,
		cfg->time_sec,
		cfg->num_data_rec,
		cfg->interval
	);
	do {
		fprintf(dumpfile, "%i %.1f %.1f\n", (int)data_curr->time, data_curr->temp/10.0, data_curr->rh/10.0);
		data_curr = data_curr->next;
	} while (data_curr != NULL);

printf("data_curr->next = %p\n", data_curr->next);
	
	fclose(dumpfile);
}


void
print_config(
	struct config *cfg, /* config struct */
	char *line_prefix   /* prefix to print before each line */
) {
	//printf("%sconfig_begin =       0x%02x\n", line_prefix, cfg->config_begin);
	printf("%sname =               %s\n",   line_prefix, cfg->name);
	printf("%snum_data_conf =      %i\n",   line_prefix, cfg->num_data_conf);
	printf("%snum_data_rec =       %i\n",   line_prefix, cfg->num_data_rec);
	printf("%sinterval =           %i\n",   line_prefix, cfg->interval);
	printf("%stime_year =          %i\n",   line_prefix, cfg->time_year);
	printf("%stime_mon =           %i\n",   line_prefix, cfg->time_mon);
	printf("%stime_mday =          %i\n",   line_prefix, cfg->time_mday);
	printf("%stime_hour =          %i\n",   line_prefix, cfg->time_hour);
	printf("%stime_min =           %i\n",   line_prefix, cfg->time_min);
	printf("%stime_sec =           %i\n",   line_prefix, cfg->time_sec);
	printf("%stemp_is_fahrenheit = %i\n",   line_prefix, cfg->temp_is_fahrenheit);
	printf("%sled_conf =           0x%02x (freq=%i, alarm=%i)\n",
		line_prefix, cfg->led_conf, (cfg->led_conf & 0x1F), (cfg->led_conf & 0x80 >> 7));
	printf("%sstart =              0x%02x", line_prefix, cfg->start);
	if (cfg->start == 1)
		printf(" (manual)");
	if (cfg->start == 2)
		printf(" (automatic)");
	printf("\n");
	printf("%sthresh_temp_low =    %i\n",   line_prefix, bin2num(cfg->thresh_temp_low));
	printf("%sthresh_temp_high =   %i\n",   line_prefix, bin2num(cfg->thresh_temp_high));
	printf("%sthresh_rh_low =      %i\n",   line_prefix, bin2num(cfg->thresh_rh_low));
	printf("%sthresh_rh_high =     %i\n",   line_prefix, bin2num(cfg->thresh_rh_high));
	//printf("%sconfig_end =         0x%02x\n", line_prefix, cfg->config_end);
}


struct config *                     /* return value: config struct */
build_config(
	char *name,                     /* logger name */
	int num_data,                   /* number of data points to collect */
	int interval,                   /* log frequency in seconds */
	int thresh_temp_low,
	int thresh_temp_high,
	int thresh_rh_low,
	int thresh_rh_high,
	int temp_is_fahrenheit,         /* bool: temp is fahrenheit */
	int led_alarm,                  /* bool: led alarm */
	int led_freq,                   /* led frequency in seconds */
	int start                       /* start loggin: 1 = manually, 2 = automatically */
) {
	struct config *cfg = NULL;
	cfg = malloc(sizeof(struct config));
	if (cfg == NULL)
	{
		printf("build_config: failed to malloc struct config\n");
		return NULL;
	}
	memset(cfg, 0, sizeof(*cfg));
	
	cfg->config_begin = 0xce;
	cfg->config_end = 0xce;
	
	cfg->start = start;
	
	cfg->num_data_conf = num_data;
	cfg->interval = interval;
	
	time_t now_stamp = 0;
	now_stamp = time(NULL);
	struct tm *now = NULL;
	now = localtime(&now_stamp);
	if (now == NULL)
	{
		printf("build_config: failed to get localtime\n");
		return NULL;
	}
	cfg->time_year = now->tm_year + 1900;
	cfg->time_mon  = now->tm_mon + 1;
	cfg->time_mday = now->tm_mday;
	cfg->time_hour = now->tm_hour;
	cfg->time_min  = now->tm_min;
	cfg->time_sec  = now->tm_sec;
	
	cfg->thresh_temp_low  = num2bin(thresh_temp_low);
	cfg->thresh_temp_high = num2bin(thresh_temp_high);
	
	cfg->temp_is_fahrenheit = temp_is_fahrenheit & 1;
	
	cfg->led_conf = (led_alarm & 1 << 7) | (led_freq & 0x1F);
	
	strncpy(cfg->name, name, 16);
	
	cfg->thresh_rh_low  = num2bin(thresh_rh_low);
	cfg->thresh_rh_high = num2bin(thresh_rh_high);
	
	return cfg;
}


int                    /* return value: 0 = valid config, 1 = invalid config */
check_config(
	struct config *cfg /* config struct */
) {
	
	if (0 == strlen(cfg->name))
	{
		printf("check_config: empty name\n");
		return 1;
	}
	
	if (cfg->num_data_conf <= 0 || 16000 < cfg->num_data_conf)
	{
		printf("check_config: invalid num_data_conf, valid range: [1:16000]\n");
		return 1;
	}
	
	if (cfg->interval <= 0 || 86400 < cfg->interval)
	{
		printf("check_config: invalid interval, valid range: [1:86400]\n");
		return 1;
	}
	
	if (cfg->start != 1 && cfg->start != 2)
	{
		printf("check_config: invalid start flag\n");
		return 1;
	}
	
	if (cfg->temp_is_fahrenheit)
	{
		if (bin2num(cfg->thresh_temp_low) < TEMP_MIN || TEMP_MAX_F < bin2num(cfg->thresh_temp_low))
		{
			printf("check_config: invalid thresh_temp_low\n");
			return 1;
		}
		if (bin2num(cfg->thresh_temp_high) < TEMP_MIN || TEMP_MAX_F < bin2num(cfg->thresh_temp_high))
		{
			printf("check_config: invalid thresh_temp_high\n");
			return 1;
		}
	} else {
		if (bin2num(cfg->thresh_temp_low) < TEMP_MIN || TEMP_MAX_C < bin2num(cfg->thresh_temp_low))
		{
			printf("check_config: invalid thresh_temp_low\n");
			return 1;
		}
		if (bin2num(cfg->thresh_temp_high) < TEMP_MIN || TEMP_MAX_C < bin2num(cfg->thresh_temp_high))
		{
			printf("check_config: invalid thresh_temp_high\n");
			return 1;
		}
	}
	
	if (bin2num(cfg->thresh_temp_high) < bin2num(cfg->thresh_temp_low))
	{
		printf("check_config: invalid thresh_temp_low/high\n");
		return 1;
	}
	
	if (bin2num(cfg->thresh_rh_low) < RH_MIN || RH_MAX < bin2num(cfg->thresh_rh_low))
	{
		printf("check_config: invalid thresh_rh_low\n");
		return 1;
	}
	
	if (bin2num(cfg->thresh_rh_high) < RH_MIN || RH_MAX < bin2num(cfg->thresh_rh_high))
	{
		printf("check_config: invalid thresh_rh_high\n");
		return 1;
	}
	
	if (bin2num(cfg->thresh_rh_high) < bin2num(cfg->thresh_rh_low))
	{
		printf("check_config: invalid thresh_rh_low/high\n");
		return 1;
	}
	
	int led_freq = cfg->led_conf & 0x1F;
	if (led_freq != 10 && led_freq != 20 && led_freq != 30)
	{
		printf("check_config: invalid led_conf (freq)\n");
		return 1;
	}
	
	return 0;
}

int main (int argc, char **argv)
{

	if (argc < 2)
	{
		printf("usage:\n"),
		printf("  %s -c LOGNAME NUM_DATA INTERVAL  -->  configure logger\n", argv[0]);
		printf("  %s -i  -->  print config\n", argv[0]);
		printf("  %s -p  -->  print data\n", argv[0]);
		printf("  %s -s  -->  store data in LOGNAME.dat\n", argv[0]);
		return 1;
	}
	
	int ret;
	int i;
	
	struct tm *log_start = NULL;
	
	char *buf = NULL;
	buf = malloc(sizeof(char)*BUFSIZE);
	
	struct usb_bus *busses;
	struct usb_bus *bus_cur;
	struct usb_device *dev_cur;
	struct usb_device *dev = NULL;
	struct usb_dev_handle *dev_hdl = NULL;
	
	// init
	usb_init();
//usb_set_debug(255);
	ret = usb_find_busses();
	if (ret < 0)
	{
		printf("usb_find_busses failed with status %i\n", ret);
		goto cleanup;
	}
	ret = usb_find_devices();
	if (ret < 0)
	{
		printf("usb_find_busses failed with status %i\n", ret);
		goto cleanup;
	}
	busses = usb_get_busses();
	
	// find dev
	for (bus_cur = busses; bus_cur != NULL; bus_cur = bus_cur->next)
	{
		for (dev_cur = bus_cur->devices; dev_cur != NULL; dev_cur = dev_cur->next)
		{
			if (dev_cur->descriptor.idVendor == VID &&
				( dev_cur->descriptor.idProduct == PID  || dev_cur->descriptor.idProduct == PID2))
			{
				dev = dev_cur;
				break;
			}
		}
		if (dev != NULL)
		{
			break;
		}
	}
	if (dev == NULL)
	{
		printf("device %04x:%04x not found\n", VID, PID);
		goto cleanup;
	}
	
	dev_hdl = usb_open(dev);

	if (dev_hdl == NULL)
	{
		printf("usb_open failed: %s\n", usb_strerror());
		goto cleanup;
	}
    EP_OUT = dev->config[0].interface[0].altsetting[0].endpoint[0].bEndpointAddress;
    EP_IN = dev->config[0].interface[0].altsetting[0].endpoint[1].bEndpointAddress;
	
	ret = usb_reset(dev_hdl);
	if (ret < 0)
	{
		printf("usb_reset failed with status %i: %s\n", ret, usb_strerror());
		goto cleanup;
	}
	
	ret = usb_set_configuration(dev_hdl, 1); // bConfigurationValue=1, iConfiguration=0
	if (ret < 0)
	{
		printf("usb_set_configuration failed with status %i\n", ret);
		goto cleanup;
	}
	
	ret = usb_claim_interface(dev_hdl, 0); // bInterfaceNumber=0, bAlternateSetting=0, bNumEndpoints=2
	if (ret < 0)
	{
		printf("usb_claim_interface failed with status %i: %s\n", ret, usb_strerror());
		goto cleanup;
	}
	
	
	
	/* configure logger */
	
	if (0 == strcmp(argv[1], "-c"))
	{
		struct config *cfg = NULL;
		int num_data = atoi(argv[3]);
		int interval = atoi(argv[4]);
		
		/* at this point, the original software would do read_config(), */
		/* which seems not to be necessary for correct operation. */
		//cfg = read_config(dev_hdl);
		
		cfg = build_config(
			argv[2], // name
			num_data, interval,
			0, 40, // temp thresh
			35, 75, // rh thresh
			0, // fahrenheit
			0, 10, // led alarm, frequency
			// 1 // start manual
			2 // start automatic
		);
		print_config(cfg, "config->");
		if (0 != check_config(cfg))
		{
			printf("config invalid!\n");
			goto cleanup;
		}
		write_config(dev_hdl, cfg);
		
		free(cfg); cfg = NULL;
	}
	
	/* print config */
	
	if (0 == strcmp(argv[1], "-i"))
	{
		struct config *cfg = NULL;
		
		cfg = read_config(dev_hdl);
		print_config(cfg, "config->");
		
		free(cfg); cfg = NULL;
	}
	
	/* print data */
	
	if (0 == strcmp(argv[1], "-p"))
	{
		struct config *cfg = NULL;
		struct data *data_first;
		
		cfg = read_config(dev_hdl);
		data_first = read_data(dev_hdl, cfg);
		print_data(data_first);
		
		free(cfg); cfg = NULL;
	}
	
	/* store log data in file */
	if (0 == strcmp(argv[1], "-s"))
	{
		struct config *cfg = NULL;
		cfg = read_config(dev_hdl);
		//print_config(cfg, "config->");
		
		struct data *data_first;
		data_first = read_data(dev_hdl, cfg);
		//print_data(data_first);
		store_data(cfg, data_first);
		free(cfg); cfg = NULL;
	}
	
	goto cleanup;
	
	
	/********************
	 *** get log data ***
	 ********************/
	
	
	
	
	buf[0] = 0x00;
	buf[1] = 0x00;
	buf[2] = 0x40;
	
	ret = usb_bulk_write(
		dev_hdl,
		EP_OUT,
		buf,
		3,
		5000
	);
	if (ret >= 0)
	{
		printf("usb_bulk_write tferred %i bytes:", ret);
		for (i=0; i<ret; i++)
		{
			if (i % 8 == 0)
				printf("\n\t");
			printf(" %02x", buf[i] & 0xFF);
		}
		printf("\n");
	}
	else
	{
		printf("usb_bulk_write failed with code %i: %s\n", ret, usb_strerror());
		goto cleanup;
	}
	
	
	// read response header (3 byte)
	
	ret = usb_bulk_read(
		dev_hdl,
		EP_IN,
		buf,
		64,
		5000
	);
	if (ret >= 0)
	{
		printf("usb_bulk_read tferred %i bytes:", ret);
		for (i = 0; i < ret; i++)
		{
			if (i % 8 == 0)
				printf("\n\t");
			printf(" %02x", buf[i] & 0xFF);
		}
		printf("\n");
	}
	else
	{
		ERR("usb_bulk_read failed with code %i: %s\n", ret, usb_strerror());
		printf("buf: ");
		for (i = 0; i < 64; i++)
		{
			printf(" %02x", buf[i] & 0xFF);
		}
		printf("\n");
		goto cleanup;
	}
	
	// parse data
	
	for (i=0; i<4; i++)
		printf("buf[%i] = %02x\n", i, buf[i] & 0xFF);
	
	int num_data_conf = (buf[4] & 0xFF) + ((buf[5] & 0xFF) << 8) + ((buf[6] & 0xFF) << 16) + ((buf[7] & 0xFF) << 24);
	printf("num_data_conf = %i\n", num_data_conf);
	
	int num_data_rec = (buf[8] & 0xFF) + ((buf[9] & 0xFF) << 8) + ((buf[10] & 0xFF) << 16) + ((buf[11] & 0xFF) << 24);
	printf("num_data_rec = %i\n", num_data_rec);
	
	int log_interval = (buf[12] & 0xFF) + ((buf[13] & 0xFF) << 8) + ((buf[14] & 0xFF) << 16) + ((buf[15] & 0xFF) << 24);
	printf("log_interval = %i\n", log_interval);
	
	log_start = malloc(sizeof(struct tm));
	memset(log_start, 0, sizeof(struct tm));
	//log_start = localtime(time(NULL));
	mktime(log_start);
	printf("log_start = %04i-%02i-%02i %02i:%02i:%02i\n", 1900 + log_start->tm_year, log_start->tm_mon,
		log_start->tm_mday, log_start->tm_hour, log_start->tm_min, log_start->tm_sec);
	
	log_start->tm_year  = -1900 + (buf[16] & 0xFF) + ((buf[17] & 0xFF) << 8) + ((buf[18] & 0xFF) << 16) + ((buf[19] & 0xFF) << 24);
	
	// assert: bytes 20,21,24,25 belong to thresh_temp and are always null
	int log_thresh_temp_low  = bin2num(((buf[22] & 0xFF) << 8) + (buf[23] & 0xFF));
	int log_thresh_temp_high = bin2num(((buf[26] & 0xFF) << 8) + (buf[27] & 0xFF));
	printf("log_thresh_temp: %i low %i high\n", log_thresh_temp_low, log_thresh_temp_high);
	
	log_start->tm_mon   = buf[28] & 0xFF;
	log_start->tm_mday  = buf[29] & 0xFF;
	log_start->tm_hour  = buf[30] & 0xFF;
	log_start->tm_min   = buf[31] & 0xFF;
	log_start->tm_sec   = buf[32] & 0xFF;
	log_start->tm_isdst = -1;
	
	printf("time() = %i\n", (int) time(NULL));
	
	printf("log_start = %04i-%02i-%02i %02i:%02i:%02i\n", 1900 + log_start->tm_year, log_start->tm_mon,
		log_start->tm_mday, log_start->tm_hour, log_start->tm_min, log_start->tm_sec);
	
	char mytmpbuf[100];
	strftime(mytmpbuf, 100, "%s", log_start);
	printf("strftime: %s\n", mytmpbuf);
	
	tzset();
	int log_start_time = mktime(log_start);
	
	printf("log_start_time = %i\n", log_start_time);
	strftime(mytmpbuf, 100, "%s", log_start);
	printf("strftime: %s\n", mytmpbuf);
	
	printf("log_start = %04i-%02i-%02i %02i:%02i:%02i\n", 1900 + log_start->tm_year, log_start->tm_mon,
		log_start->tm_mday, log_start->tm_hour, log_start->tm_min, log_start->tm_sec);
	
	printf("weekday = %i\n", log_start->tm_wday);
	
	// assert: byte 33 is used for fahrenheit flag only
	int log_is_fahrenheit = buf[33] & 1;
	printf("log_is_fahrenheit = %i\n", log_is_fahrenheit);
	
	// assert: bits 2,3 in byte 34 unused
	int log_led_freq = buf[34] & 0x1F; // last 5 bits
	printf("log_led_freq = %i\n", log_led_freq);
	
	int log_led_alarm = (buf[34] & 0x80) >> 7; // 1st bit
	printf("log_led_alarm = %i\n", log_led_alarm);
	
	// unknown
	printf("buf[35] = %02x\n", buf[35] & 0xFF);
	
	// assert: string is NOT zero terminated if strlen == 15
	char log_name[16];
	for (i=0; i<15; i++)
		log_name[i] = buf[35+i] & 0xFF;
	log_name[15] = 0;
	printf("log_name = %s\n", log_name);
	
	// unknown
	for (i=50; i<52; i++)
		printf("buf[%i] = %02x\n", i, buf[i] & 0xFF);
	
	// assert: bytes 52,53,56,57 belong to thresh_hum and are always null
	int log_thresh_hum_low  = bin2num(((buf[54] & 0xFF) << 8) + (buf[55] & 0xFF));
	int log_thresh_hum_high = bin2num(((buf[58] & 0xFF) << 8) + (buf[59] & 0xFF));
	printf("log_thresh_hum: %i low %i high\n", log_thresh_hum_low, log_thresh_hum_high);
	
	// unknown
	for (i=60; i<64; i++)
		printf("buf[%i] = %02x\n", i, buf[i] & 0xFF);
	
	
	// read response data (N * 64 byte + X)
	
	struct log_point {
		float temp;
		float rh;
	};
	
	struct log_point *log_data;
	log_data = malloc(num_data_rec * sizeof(struct log_point));
	int num_data_collected = 0;
	
	while (num_data_collected < num_data_rec)
	{
		ret = usb_bulk_read(
			dev_hdl,
			EP_IN,
			buf,
			64,
			5000
		);
		if (ret >= 0)
		{
			printf("usb_bulk_read tferred %i bytes:", ret);
			for (i = 0; i < ret; i++)
			{
				if (i % 8 == 0)
					printf("\n\t");
				printf(" %02x", buf[i] & 0xFF);
			}
			printf("\n");
		}
		else
		{
			ERR("usb_bulk_read failed with code %i: %s\n", ret, usb_strerror());
			printf("buf: ");
			for (i = 0; i < 64; i++)
			{
				printf(" %02x", buf[i] & 0xFF);
			}
			printf("\n");
			goto cleanup;
		}
		
		for (i=0; i<ret; i += 4)
		{
			log_data[num_data_collected].temp = (buf[i+0] + (buf[i+1]<<8)) / 10.0;
			log_data[num_data_collected].rh   = (buf[i+2] + (buf[i+3]<<8)) / 10.0;
			
			num_data_collected++;
		}
	}
	
	
printf("testing malloc\n");
void *test_ptr;
printf("malloc\n");
test_ptr = malloc(1024);
printf("free\n");
free(test_ptr);
printf("test malloc done\n");

	
printf("about to print log data...\n");

	struct tm *cur_time = NULL;
printf("malloc: struct tm\n");
	cur_time = malloc(sizeof(struct tm));
printf("copying time struct\n");
	memcpy(cur_time, log_start, sizeof(struct tm));
	
	char cur_time_str[100];
	
printf("ok loop\n");
	for (i=0; i<num_data_rec; i++)
	{
		strftime(cur_time_str, 100, "%Y-%m-%d %H:%M:%S", cur_time);
		printf("log point %4i [%s]: %2.1f°C %2.1f%%\n", (i+1), cur_time_str, log_data[i].temp, log_data[i].rh);
		
		cur_time->tm_sec += log_interval;
		mktime(cur_time);
	}
	free(cur_time);
	
cleanup:
	free(buf);
	if (log_start != NULL)
		free(log_start);
	if (dev_hdl != NULL)
		usb_close(dev_hdl);
	return 0;
}
