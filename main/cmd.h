
#define MAX_PAYLOAD	256
typedef struct {
	uint16_t command;
	size_t length;
	uint8_t payload[MAX_PAYLOAD];
	TaskHandle_t taskHandle;
} CMD_t;

#define CMD_START	100
#define CMD_STOP 200
#define CMD_NMEA 300
#define CMD_SELECT 400
#define CMD_RMC	500
#define CMD_GGA	520
#define CMD_VTG	540
#define CMD_NET	600
#define CMD_CONNECT	800
#define CMD_DISCONNECT 820

typedef struct {
	bool enable;
	size_t length;
	uint8_t	payload[10];
} TYPE_t;

#define FORMATTED_RMC	100
#define FORMATTED_GGA	200
#define FORMATTED_VTG	300
#define FORMATTED_NET	400

#define	MAX_NMEA 10
typedef struct {
	size_t typeNum;
	TYPE_t type[MAX_NMEA];
} NMEA_t;


typedef struct {
	uint8_t	_time[20];
	uint8_t _valid[10];
	uint8_t _lat1[20];
	uint8_t _lat2[10];
	uint8_t _lon1[20];
	uint8_t _lon2[10];
	uint8_t _speed[10];
	uint8_t _orient[10];
	uint8_t	_date[20];
} RMC_t;

typedef struct {
	uint8_t _time[20];
	uint8_t _lat1[20];
	uint8_t _lat2[10];
	uint8_t _lon1[20];
	uint8_t _lon2[10];
	uint8_t _quality[10];
	uint8_t _satellites[10];
	uint8_t _droprate[10];
	uint8_t _sealevel[10];
	uint8_t _geoidheight[10];
} GGA_t;

typedef struct {
	uint8_t _direction1[10];
	uint8_t _direction2[10];
	uint8_t _speed1[10];
	uint8_t _speed2[10];
} VTG_t;

typedef struct {
	char _ip[32];
	char _netmask[32];
	char _gw[32];
} NETWORK_t;


