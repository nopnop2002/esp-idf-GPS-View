/* GPS NMEA Viewer

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "ili9340.h"
#include "fontx.h"

static const char *TAG = "NMEA";

static const int RX_BUF_SIZE = 1024;

#define TXD_GPIO	0
// You have to set these CONFIG value using menuconfig.
#if 0
#define CONFIG_UART_RXD_GPIO	16
#endif

#define SCREEN_WIDTH	320
#define SCREEN_HEIGHT	240
#define CS_GPIO			14
#define DC_GPIO			27
#define RESET_GPIO		33
#define BL_GPIO			32
#define DISPLAY_LENGTH	26
#define GPIO_INPUT_A	GPIO_NUM_39
#define GPIO_INPUT_B	GPIO_NUM_38
#define GPIO_INPUT_C	GPIO_NUM_37

#define MAX_PAYLOAD		256
typedef struct {
	uint16_t command;
	size_t	 length;
	uint8_t  payload[MAX_PAYLOAD];
	TaskHandle_t taskHandle;
} CMD_t;

#define CMD_START		100
#define CMD_STOP		200
#define CMD_NMEA		300
#define CMD_SELECT		400
#define CMD_RMC			500
#define CMD_GGA			520
#define CMD_VTG			540
#define CMD_NET			600
#define CMD_CONNECT		800
#define CMD_DISCONNECT	820

typedef struct {
	bool	enable;
	size_t	length;
	uint8_t	payload[10];
} TYPE_t;

#define FORMATTED_RMC	100
#define FORMATTED_GGA	200
#define FORMATTED_VTG	300
#define FORMATTED_NET	400

#define	MAX_NMEA		10
typedef struct {
	size_t	typeNum;
	TYPE_t	type[MAX_NMEA];
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

static QueueHandle_t xQueueCmd;
static QueueHandle_t xQueueTcp;
static QueueHandle_t uart0_queue;
static NETWORK_t myNetwork;

/* This project use WiFi configuration that you can set via 'make menuconfig'.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/

#define EXAMPLE_ESP_WIFI_SSID	   CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS	   CONFIG_ESP_WIFI_PASSWORD

#if CONFIG_AP_MODE
#define EXAMPLE_MAX_STA_CONN	   CONFIG_ESP_MAX_STA_CONN
#endif
#if CONFIG_ST_MODE
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY
#endif

#if CONFIG_ST_MODE
/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

/* The event group allows multiple bits for each event, but we only care about one event 
 * - are we connected to the AP with an IP? */
const int WIFI_CONNECTED_BIT = BIT0;
#endif

static void event_handler(void* arg, esp_event_base_t event_base, 
								int32_t event_id, void* event_data)
{
	if (event_base == WIFI_EVENT) ESP_LOGI(TAG, "WIFI_EVENT event_id=%d", event_id);
	if (event_base == IP_EVENT) ESP_LOGI(TAG, "IP_EVENT event_id=%d", event_id);

#if CONFIG_AP_MODE
	if (event_id == WIFI_EVENT_AP_STACONNECTED) {
		wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
		ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
				 MAC2STR(event->mac), event->aid);
	} else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
		wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
		ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
				 MAC2STR(event->mac), event->aid);
	}
#endif

#if CONFIG_ST_MODE
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
			esp_wifi_connect();
			xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
			s_retry_num++;
			ESP_LOGI(TAG, "retry to connect to the AP");
		}
		ESP_LOGI(TAG,"connect to the AP fail");
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
#if 0
		ESP_LOGI(TAG, "got ip:%s", ip4addr_ntoa(&event->ip_info.ip));
#endif
		s_retry_num = 0;
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
	}
#endif
}

#if CONFIG_AP_MODE
void wifi_init_softap()
{
#if 0
	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_create_default());
#endif
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	//ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

	wifi_config_t wifi_config = {
		.ap = {
			.ssid = EXAMPLE_ESP_WIFI_SSID,
			.ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
			.password = EXAMPLE_ESP_WIFI_PASS,
			.max_connection = EXAMPLE_MAX_STA_CONN,
			.authmode = WIFI_AUTH_WPA_WPA2_PSK
		},
	};
	if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
		wifi_config.ap.authmode = WIFI_AUTH_OPEN;
	}

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s",
			 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
}
#endif

#if CONFIG_ST_MODE
bool parseAddress(int * ip, char * text) {
	ESP_LOGD(TAG, "parseAddress text=[%s]",text);
	int len = strlen(text);
	int octet = 0;
	char buf[4];
	int index = 0;
	for(int i=0;i<len;i++) {
		char c = text[i];
		if (c == '.') {
			ESP_LOGD(TAG, "buf=[%s] octet=%d", buf, octet);
			ip[octet] = strtol(buf, NULL, 10);
			octet++;
			index = 0;
		} else {
			if (index == 3) return false;
			if (c < '0' || c > '9') return false;
			buf[index++] = c;
			buf[index] = 0;
		}
	}

	if (strlen(buf) > 0) {
		ESP_LOGD(TAG, "buf=[%s] octet=%d", buf, octet);
		ip[octet] = strtol(buf, NULL, 10);
		octet++;
	}
	if (octet != 4) return false;
	return true;

}

void wifi_init_sta()
{
	s_wifi_event_group = xEventGroupCreate();

#if 0
	tcpip_adapter_init();
#endif
	ESP_ERROR_CHECK(esp_netif_init());

#if CONFIG_STATIC_IP

	ESP_LOGI(TAG, "CONFIG_STATIC_IP_ADDRESS=[%s]",CONFIG_STATIC_IP_ADDRESS);
	ESP_LOGI(TAG, "CONFIG_STATIC_GW_ADDRESS=[%s]",CONFIG_STATIC_GW_ADDRESS);
	ESP_LOGI(TAG, "CONFIG_STATIC_NM_ADDRESS=[%s]",CONFIG_STATIC_NM_ADDRESS);

	int ip[4];
	bool ret = parseAddress(ip, CONFIG_STATIC_IP_ADDRESS);
	ESP_LOGI(TAG, "parseAddress ret=%d ip=%d.%d.%d.%d", ret, ip[0], ip[1], ip[2], ip[3]);
	if (!ret) {
		ESP_LOGE(TAG, "CONFIG_STATIC_IP_ADDRESS [%s] not correct", CONFIG_STATIC_IP_ADDRESS);
	while(1) { vTaskDelay(1); }
	}

	int gw[4];
	ret = parseAddress(gw, CONFIG_STATIC_GW_ADDRESS);
	ESP_LOGI(TAG, "parseAddress ret=%d gw=%d.%d.%d.%d", ret, gw[0], gw[1], gw[2], gw[3]);
	if (!ret) {
		ESP_LOGE(TAG, "CONFIG_STATIC_GW_ADDRESS [%s] not correct", CONFIG_STATIC_GW_ADDRESS);
	while(1) { vTaskDelay(1); }
	}

	int nm[4];
	ret = parseAddress(nm, CONFIG_STATIC_NM_ADDRESS);
	ESP_LOGI(TAG, "parseAddress ret=%d nm=%d.%d.%d.%d", ret, nm[0], nm[1], nm[2], nm[3]);
	if (!ret) {
		ESP_LOGE(TAG, "CONFIG_STATIC_NM_ADDRESS [%s] not correct", CONFIG_STATIC_NM_ADDRESS);
	while(1) { vTaskDelay(1); }
	}

	tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA);

	/* Set STATIC IP Address */
	tcpip_adapter_ip_info_t ipInfo;
	//IP4_ADDR(&ipInfo.ip, 192,168,10,100);
	IP4_ADDR(&ipInfo.ip, ip[0], ip[1], ip[2], ip[3]);
	IP4_ADDR(&ipInfo.gw, gw[0], gw[1], gw[2], gw[3]);
	IP4_ADDR(&ipInfo.netmask, nm[0], nm[1], nm[2], nm[3]);
	tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA, &ipInfo);

#endif

	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

	wifi_config_t wifi_config = {
		.sta = {
			.ssid = EXAMPLE_ESP_WIFI_SSID,
			.password = EXAMPLE_ESP_WIFI_PASS
		},
	};
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
	ESP_ERROR_CHECK(esp_wifi_start() );

	ESP_LOGI(TAG, "wifi_init_sta finished.");
	ESP_LOGI(TAG, "connect to ap SSID:%s password:%s",
			 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);

	// wait for IP_EVENT_STA_GOT_IP
	while(1) {
		/* Wait forever for WIFI_CONNECTED_BIT to be set within the event group.
		   Clear the bits beforeexiting. */
		EventBits_t uxBits = xEventGroupWaitBits(s_wifi_event_group,
		   WIFI_CONNECTED_BIT, /* The bits within the event group to waitfor. */
		   pdTRUE,		  /* WIFI_CONNECTED_BIT should be cleared before returning. */
		   pdFALSE,		  /* Don't waitfor both bits, either bit will do. */
		   portMAX_DELAY);/* Wait forever. */
	   if ( ( uxBits & WIFI_CONNECTED_BIT ) == WIFI_CONNECTED_BIT ){
		   ESP_LOGI(TAG, "WIFI_CONNECTED_BIT");
		   break;
	   }
	}
	ESP_LOGI(TAG, "Got IP Address.");
}
#endif


static void uart_event_task(void *pvParameters)
{
	ESP_LOGI(pcTaskGetTaskName(0), "Start");
	esp_log_level_set(pcTaskGetTaskName(0), ESP_LOG_WARN);

	uart_event_t event;
	size_t buffered_size;
	uint8_t* data = (uint8_t*) malloc(RX_BUF_SIZE);
	CMD_t cmdBuf;
	cmdBuf.command = CMD_NMEA;
	cmdBuf.taskHandle = xTaskGetCurrentTaskHandle();

	for(;;) {
		//Waiting for UART event.
		if(xQueueReceive(uart0_queue, (void * )&event, (portTickType)portMAX_DELAY)) {
			bzero(data, RX_BUF_SIZE);
			ESP_LOGI(pcTaskGetTaskName(0), "uart[%d] event:", UART_NUM_1);
			switch(event.type) {
				//Event of UART receving data
				/*We'd better handler data event fast, there would be much more data events than
				other types of events. If we take too much time on data event, the queue might
				be full.*/
				case UART_DATA:
					ESP_LOGI(pcTaskGetTaskName(0), "[UART DATA]: %d", event.size);
					break;
				//Event of HW FIFO overflow detected
				case UART_FIFO_OVF:
					ESP_LOGW(pcTaskGetTaskName(0), "hw fifo overflow");
					// If fifo overflow happened, you should consider adding flow control for your application.
					// The ISR has already reset the rx FIFO,
					// As an example, we directly flush the rx buffer here in order to read more data.
					uart_flush_input(UART_NUM_1);
					xQueueReset(uart0_queue);
					break;
				//Event of UART ring buffer full
				case UART_BUFFER_FULL:
					ESP_LOGW(pcTaskGetTaskName(0), "ring buffer full");
					// If buffer full happened, you should consider encreasing your buffer size
					// As an example, we directly flush the rx buffer here in order to read more data.
					uart_flush_input(UART_NUM_1);
					xQueueReset(uart0_queue);
					break;
				//Event of UART RX break detected
				case UART_BREAK:
					ESP_LOGW(pcTaskGetTaskName(0), "uart rx break");
					break;
				//Event of UART parity check error
				case UART_PARITY_ERR:
					ESP_LOGW(pcTaskGetTaskName(0), "uart parity error");
					break;
				//Event of UART frame error
				case UART_FRAME_ERR:
					ESP_LOGW(pcTaskGetTaskName(0), "uart frame error");
					break;
				//UART_PATTERN_DET
				case UART_PATTERN_DET:
					uart_get_buffered_data_len(UART_NUM_1, &buffered_size);
					int pos = uart_pattern_pop_pos(UART_NUM_1);
					ESP_LOGI(pcTaskGetTaskName(0), "[UART PATTERN DETECTED] pos: %d, buffered size: %d", pos, buffered_size);
					if (pos == -1) {
						// There used to be a UART_PATTERN_DET event, but the pattern position queue is full so that it can not
						// record the position. We should set a larger queue size.
						// As an example, we directly flush the rx buffer here.
						uart_flush_input(UART_NUM_1);
					} else {
						uart_read_bytes(UART_NUM_1, data, buffered_size, 100 / portTICK_PERIOD_MS);
						ESP_LOGI(pcTaskGetTaskName(0), "read data: %s", data);
						cmdBuf.length = buffered_size;
						memcpy((char *)cmdBuf.payload, (char *)data, buffered_size); 
						cmdBuf.payload[buffered_size] = 0;
						xQueueSend(xQueueCmd, &cmdBuf, 0);
					}
					break;
				//Others
				default:
					ESP_LOGW(pcTaskGetTaskName(0), "uart event type: %d", event.type);
					break;
			}
		}
	}
	// never reach
	free(data);
	data = NULL;
	vTaskDelete(NULL);
}

void buttonA(void *pvParameters)
{
	ESP_LOGI(pcTaskGetTaskName(0), "Start");
	CMD_t cmdBuf;
	cmdBuf.command = CMD_START;
	cmdBuf.taskHandle = xTaskGetCurrentTaskHandle();

	// set the GPIO as a input
	gpio_pad_select_gpio(GPIO_INPUT_A);
	gpio_set_direction(GPIO_INPUT_A, GPIO_MODE_DEF_INPUT);

	while(1) {
		int level = gpio_get_level(GPIO_INPUT_A);
		if (level == 0) {
			ESP_LOGI(pcTaskGetTaskName(0), "Push Button");
			while(1) {
				level = gpio_get_level(GPIO_INPUT_A);
				if (level == 1) break;
				vTaskDelay(1);
			}
			xQueueSend(xQueueCmd, &cmdBuf, 0);
			if (cmdBuf.command == CMD_START) {
				cmdBuf.command = CMD_STOP;
			} else {
				cmdBuf.command = CMD_START;
			}
		}
		vTaskDelay(1);
	}
}

void buttonB(void *pvParameters)
{
	ESP_LOGI(pcTaskGetTaskName(0), "Start");
	CMD_t cmdBuf;
	cmdBuf.command = CMD_SELECT;
	cmdBuf.taskHandle = xTaskGetCurrentTaskHandle();

	// set the GPIO as a input
	gpio_pad_select_gpio(GPIO_INPUT_B);
	gpio_set_direction(GPIO_INPUT_B, GPIO_MODE_DEF_INPUT);

	while(1) {
		int level = gpio_get_level(GPIO_INPUT_B);
		if (level == 0) {
			ESP_LOGI(pcTaskGetTaskName(0), "Push Button");
			while(1) {
				level = gpio_get_level(GPIO_INPUT_B);
				if (level == 1) break;
				vTaskDelay(1);
			}
			xQueueSend(xQueueCmd, &cmdBuf, 0);
		}
		vTaskDelay(1);
	}
}

void buttonC(void *pvParameters)
{
	ESP_LOGI(pcTaskGetTaskName(0), "Start");
	int CMD_TABLE[] = {CMD_RMC, CMD_GGA, CMD_VTG, CMD_NET};
	int cmdIndex = 0;
	CMD_t cmdBuf;
	//cmdBuf.command = CMD_RMC;
	cmdBuf.command = CMD_TABLE[cmdIndex];
	cmdBuf.taskHandle = xTaskGetCurrentTaskHandle();

	// set the GPIO as a input
	gpio_pad_select_gpio(GPIO_INPUT_C);
	gpio_set_direction(GPIO_INPUT_C, GPIO_MODE_DEF_INPUT);

	while(1) {
		int level = gpio_get_level(GPIO_INPUT_C);
		if (level == 0) {
			ESP_LOGI(pcTaskGetTaskName(0), "Push Button");
			while(1) {
				level = gpio_get_level(GPIO_INPUT_C);
				if (level == 1) break;
				vTaskDelay(1);
			}
			xQueueSend(xQueueCmd, &cmdBuf, 0);
#if 0
			if (cmdBuf.command == CMD_RMC) {
				cmdBuf.command = CMD_GGA;
			} else if (cmdBuf.command == CMD_GGA) {
				cmdBuf.command = CMD_VTG;
			} else {
				cmdBuf.command = CMD_RMC;
			}
#endif
			cmdIndex++;
			if (cmdIndex == 4) cmdIndex = 0;
			cmdBuf.command = CMD_TABLE[cmdIndex];
		}
		vTaskDelay(1);
	}
}


void build_nmea_type(NMEA_t * nmea, uint8_t * payload, size_t length) {
	if (length < 3) return;
	if (payload[0] != '$') return;
	if (payload[1] != 'G') return;
	if (payload[2] != 'P') return;

	int typeLength = 0;
	for(int i=0;i<length;i++) {
		if (payload[i] == ',') break;
		typeLength++; 
	}
	ESP_LOGD(TAG, "[%s] typeString=%.*s", __FUNCTION__, typeLength, payload);
	ESP_LOGD(TAG, "[%s] nmea->typeNum=%d", __FUNCTION__, nmea->typeNum);
	TYPE_t type;
	for(int i=0;i<nmea->typeNum;i++) {
		type = nmea->type[i];
		ESP_LOGD(TAG,"type.payload[%d]=[%s]",i,type.payload);
		if (strncmp((char *)type.payload, (char *)payload, typeLength) == 0) return;
	}
	type.enable = true;
	type.length = typeLength;
	memcpy((char *)type.payload, (char *)payload, typeLength);
	type.payload[typeLength] = 0;
	nmea->type[nmea->typeNum] = type;
	ESP_LOGD(TAG,"type.payload=[%s]",nmea->type[nmea->typeNum].payload);
	nmea->typeNum++;
}

bool check_nmea_type(int target, NMEA_t * nmea, uint8_t * payload, size_t length) {
	if (target == 0) return true;
	int typeLength = 0;
	for(int i=0;i<length;i++) {
		if (payload[i] == ',') break;
		typeLength++; 
	}
	ESP_LOGD(TAG, "[%s] typeString=%.*s", __FUNCTION__, typeLength, payload);
	ESP_LOGD(TAG, "[%s] nmea->typeNum=%d", __FUNCTION__, nmea->typeNum);
	TYPE_t type;
	type = nmea->type[target-1];
	ESP_LOGD(TAG,"[%s] type.payload=[%s]",__FUNCTION__, type.payload);
	if (strncmp((char *)type.payload, (char *)payload, typeLength) == 0) return true;
	return false;
}

void get_nmea_type(int target, NMEA_t * nmea, uint8_t * ascii) {
	TYPE_t type;
	if (target == 0) {
		strcpy((char *)ascii, "ALL");
	} else if (target == FORMATTED_RMC) {
		strcpy((char *)ascii, "FORMATTED RMC");
	} else if (target == FORMATTED_GGA) {
		strcpy((char *)ascii, "FORMATTED GGA");
	} else if (target == FORMATTED_VTG) {
		strcpy((char *)ascii, "FORMATTED VTG");
	} else if (target == FORMATTED_NET) {
		strcpy((char *)ascii, "NETWORK INFO");
	} else {
		ESP_LOGI(TAG, "[%s] nmea->typeNum=%d", __FUNCTION__, nmea->typeNum);
		if (target <= nmea->typeNum) {
			type = nmea->type[target-1];
			strcpy((char *)ascii, (char *)type.payload);
		}
	}
}

bool parse_nmea_rmc(RMC_t *rmc, uint8_t * payload, size_t length) {
	int typeLength = 0;
	for(int i=0;i<length;i++) {
		if (payload[i] == ',') break;
		typeLength++; 
	}
	ESP_LOGI(TAG, "[%s] typeString=%.*s", __FUNCTION__, typeLength, payload);
	if (strncmp((char *)payload, "$GPRMC", typeLength) != 0) return false;

	int index = 0;
	int offset = 0;
	rmc->_time[0] = 0;
	rmc->_valid[0] = 0;
	rmc->_lat1[0] = 0;
	rmc->_lat2[0] = 0;
	rmc->_lon1[0] = 0;
	rmc->_lon2[0] = 0;
	rmc->_speed[0] = 0;
	rmc->_orient[0] = 0;
	rmc->_date[0] = 0;
	for(int i=0;i<length;i++) {
		if (payload[i] == ',') {
			index++;
			offset = 0;
		} else {
			if (index == 0) {

			} else if (index == 1) {
				rmc->_time[offset++] = payload[i];
				rmc->_time[offset] = 0;

			} else if (index == 2) {
				rmc->_valid[offset++] = payload[i];
				rmc->_valid[offset] = 0;

			} else if (index == 3) {
				rmc->_lat1[offset++] = payload[i];
				rmc->_lat1[offset] = 0;

			} else if (index == 4) {
				rmc->_lat2[offset++] = payload[i];
				rmc->_lat2[offset] = 0;

			} else if (index == 5) {
				rmc->_lon1[offset++] = payload[i];
				rmc->_lon1[offset] = 0;

			} else if (index == 6) {
				rmc->_lon2[offset++] = payload[i];
				rmc->_lon2[offset] = 0;

			} else if (index == 7) {
				rmc->_speed[offset++] = payload[i];
				rmc->_speed[offset] = 0;

			} else if (index == 8) {
				rmc->_orient[offset++] = payload[i];
				rmc->_orient[offset] = 0;

			} else if (index == 9) {
				rmc->_date[offset++] = payload[i];
				rmc->_date[offset] = 0;
			}
		}
	}
	return true;
}

bool parse_nmea_gga(GGA_t *gga, uint8_t * payload, size_t length) {
	int typeLength = 0;
	for(int i=0;i<length;i++) {
		if (payload[i] == ',') break;
		typeLength++; 
	}
	ESP_LOGI(TAG, "[%s] typeString=%.*s", __FUNCTION__, typeLength, payload);
	if (strncmp((char *)payload, "$GPGGA", typeLength) != 0) return false;

	int index = 0;
	int offset = 0;
	gga->_time[0] = 0;
	gga->_lat1[0] = 0;
	gga->_lat2[0] = 0;
	gga->_lon1[0] = 0;
	gga->_lon2[0] = 0;
	gga->_quality[0] = 0;
	gga->_satellites[0] = 0;
	gga->_droprate[0] = 0;
	gga->_sealevel[0] = 0;
	gga->_geoidheight[0] = 0;
	for(int i=0;i<length;i++) {
		if (payload[i] == ',') {
			index++;
			offset = 0;
		} else {
			if (index == 0) {

			} else if (index == 1) {
				gga->_time[offset++] = payload[i];
				gga->_time[offset] = 0;

			} else if (index == 2) {
				gga->_lat1[offset++] = payload[i];
				gga->_lat1[offset] = 0;

			} else if (index == 3) {
				gga->_lat2[offset++] = payload[i];
				gga->_lat2[offset] = 0;

			} else if (index == 4) {
				gga->_lon1[offset++] = payload[i];
				gga->_lon1[offset] = 0;

			} else if (index == 5) {
				gga->_lon2[offset++] = payload[i];
				gga->_lon2[offset] = 0;

			} else if (index == 6) {
				gga->_quality[offset++] = payload[i];
				gga->_quality[offset] = 0;

			} else if (index == 7) {
				gga->_satellites[offset++] = payload[i];
				gga->_satellites[offset] = 0;

			} else if (index == 8) {
				gga->_droprate[offset++] = payload[i];
				gga->_droprate[offset] = 0;

			} else if (index == 9) {
				gga->_sealevel[offset++] = payload[i];
				gga->_sealevel[offset] = 0;

			} else if (index == 10) {

			} else if (index == 11) {
				gga->_geoidheight[offset++] = payload[i];
				gga->_geoidheight[offset] = 0;
			}
		}
	}
	return true;
}

bool parse_nmea_vtg(VTG_t *vtg, uint8_t * payload, size_t length) {
	int typeLength = 0;
	for(int i=0;i<length;i++) {
		if (payload[i] == ',') break;
		typeLength++; 
	}
	ESP_LOGI(TAG, "[%s] typeString=%.*s", __FUNCTION__, typeLength, payload);
	if (strncmp((char *)payload, "$GPVTG", typeLength) != 0) return false;

	int index = 0;
	int offset = 0;
	vtg->_direction1[0] = 0;
	vtg->_direction2[0] = 0;
	vtg->_speed1[0] = 0;
	vtg->_speed2[0] = 0;
	for(int i=0;i<length;i++) {
		if (payload[i] == ',') {
			index++;
			offset = 0;
		} else {
			if (index == 0) {

			} else if (index == 1) {
				vtg->_direction1[offset++] = payload[i];
				vtg->_direction1[offset] = 0;

			} else if (index == 2) {

			} else if (index == 3) {
				vtg->_direction2[offset++] = payload[i];
				vtg->_direction2[offset] = 0;

			} else if (index == 4) {

			} else if (index == 5) {
				vtg->_speed1[offset++] = payload[i];
				vtg->_speed1[offset] = 0;

			} else if (index == 6) {

			} else if (index == 7) {
				vtg->_speed2[offset++] = payload[i];
				vtg->_speed2[offset] = 0;

			}
		}
	}
	return true;
}


void tft(void *pvParameters)
{
	ESP_LOGI(pcTaskGetTaskName(0), "Start");
	// set font file
	FontxFile fxG[2];
	InitFontx(fxG,"/spiffs/ILGH24XB.FNT",""); // 12x24Dot Gothic
	FontxFile fxM[2];
	InitFontx(fxM,"/spiffs/ILMH24XB.FNT",""); // 12x24Dot Mincyo

	// get font width & height
	uint8_t buffer[FontxGlyphBufSize];
	uint8_t fontWidth;
	uint8_t fontHeight;
	GetFontx(fxG, 0, buffer, &fontWidth, &fontHeight);
	ESP_LOGI(pcTaskGetTaskName(0), "fontWidth=%d fontHeight=%d",fontWidth,fontHeight);

	// Initialize NMEA table
	NMEA_t nmea;
	nmea.typeNum = 0;
	for(int i=0;i<MAX_NMEA;i++) {
		nmea.type[i].enable = false;
	}

	// Setup Screen
	TFT_t dev;
	spi_master_init(&dev, CS_GPIO, DC_GPIO, RESET_GPIO, BL_GPIO);
	lcdInit(&dev, 0x9341, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0);
	ESP_LOGI(pcTaskGetTaskName(0), "Setup Screen done");

	int lines = (SCREEN_HEIGHT - fontHeight) / fontHeight;
	ESP_LOGD(pcTaskGetTaskName(0), "SCREEN_HEIGHT=%d fontHeight=%d lines=%d", SCREEN_HEIGHT, fontHeight, lines);
	int ymax = (lines+1) * fontHeight;
	ESP_LOGD(pcTaskGetTaskName(0), "ymax=%d",ymax);

	// Initial Screen
	//uint8_t ascii[DISPLAY_LENGTH+1];
	uint8_t ascii[44];
	lcdFillScreen(&dev, BLACK);
	lcdSetFontDirection(&dev, 0);

	// Reset scroll area
	lcdSetScrollArea(&dev, 0, 0x0140, 0);

	strcpy((char *)ascii, "NMEA View");
	lcdDrawString(&dev, fxG, 0, fontHeight-1, ascii, YELLOW);
	uint16_t xstatus = 13*fontWidth;
	//strcpy((char *)ascii, "Stop");
	//lcdDrawString(&dev, fxG, xstatus, fontHeight-1, ascii, RED);

	uint16_t vsp = fontHeight*2;
	uint16_t ypos = (fontHeight*2) - 1;
	uint16_t current = 0;
	bool enabled = false;
	bool formatted_mode = false;
	int target_type = 0;
	CMD_t cmdBuf;
	RMC_t rmcBuf;
	GGA_t ggaBuf;
	VTG_t vtgBuf;
	bool check;
	bool connected = false;
	bool updateConnected = false;

	while(1) {
		xQueueReceive(xQueueCmd, &cmdBuf, portMAX_DELAY);
		ESP_LOGD(pcTaskGetTaskName(0),"cmdBuf.command=%d connected=%d", cmdBuf.command, connected);
		if (cmdBuf.command == CMD_START) {
			get_nmea_type(target_type, &nmea, ascii);
			lcdDrawFillRect(&dev, xstatus, 0, SCREEN_WIDTH-1, fontHeight-1, BLACK);
			lcdDrawString(&dev, fxG, xstatus, fontHeight-1, ascii, GREEN);
			enabled = true;

		} else if (cmdBuf.command == CMD_STOP) {
			get_nmea_type(target_type, &nmea, ascii);
			lcdDrawFillRect(&dev, xstatus, 0, SCREEN_WIDTH-1, fontHeight-1, BLACK);
			lcdDrawString(&dev, fxG, xstatus, fontHeight-1, ascii, YELLOW);
			enabled = false;

		} else if (cmdBuf.command == CMD_CONNECT) {
			connected = true;
			updateConnected = true;

		} else if (cmdBuf.command == CMD_DISCONNECT) {
			connected = false;
			updateConnected = true;

		} else if (cmdBuf.command == CMD_NMEA) {
			build_nmea_type(&nmea, cmdBuf.payload, cmdBuf.length);
			if (connected) xQueueSend(xQueueTcp, &cmdBuf, 0);
			if (!enabled) continue;
			ESP_LOGD(TAG, "[%s] target_type=%d",__FUNCTION__, target_type);

			if (target_type == FORMATTED_RMC) {
				check = parse_nmea_rmc(&rmcBuf, cmdBuf.payload, cmdBuf.length);
				if (!check) continue;
				//lcdDrawFillRect(&dev, 0, fontHeight, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, BLACK);
				uint16_t yrmc = fontHeight*2-1;
				lcdDrawFillRect(&dev, 5*fontWidth, yrmc-fontHeight, SCREEN_WIDTH-1, yrmc, BLACK);
				sprintf((char *)ascii, "%s",rmcBuf._date);
				lcdDrawString(&dev, fxG, 5*fontWidth, yrmc, ascii, CYAN);
				yrmc = yrmc + fontHeight;
				lcdDrawFillRect(&dev, 5*fontWidth, yrmc-fontHeight, SCREEN_WIDTH-1, yrmc, BLACK);
				sprintf((char *)ascii, "%s",rmcBuf._time);
				lcdDrawString(&dev, fxG, 5*fontWidth, yrmc, ascii, CYAN);
				yrmc = yrmc + fontHeight;
				lcdDrawFillRect(&dev, 6*fontWidth, yrmc-fontHeight, SCREEN_WIDTH-1, yrmc, BLACK);
				if (rmcBuf._valid[0] == 'V') sprintf((char *)ascii, "V(Invalid)");
				if (rmcBuf._valid[0] == 'A') sprintf((char *)ascii, "A(Valid)");
				lcdDrawString(&dev, fxG, 6*fontWidth, yrmc, ascii, CYAN);
				//if (rmcBuf._valid[0] == 'V') continue;
				yrmc = yrmc + fontHeight;
				lcdDrawFillRect(&dev, 4*fontWidth, yrmc-fontHeight, SCREEN_WIDTH-1, yrmc, BLACK);
				if (rmcBuf._valid[0] == 'A') {
					sprintf((char *)ascii, "%s %s",rmcBuf._lat1, rmcBuf._lat2);
					lcdDrawString(&dev, fxG, 4*fontWidth, yrmc, ascii, CYAN);
				}
				yrmc = yrmc + fontHeight;
				lcdDrawFillRect(&dev, 4*fontWidth, yrmc-fontHeight, SCREEN_WIDTH-1, yrmc, BLACK);
				if (rmcBuf._valid[0] == 'A') {
					sprintf((char *)ascii, "%s %s",rmcBuf._lon1, rmcBuf._lon2);
					lcdDrawString(&dev, fxG, 4*fontWidth, yrmc, ascii, CYAN);
				}
				yrmc = yrmc + fontHeight;
				lcdDrawFillRect(&dev, 13*fontWidth, yrmc-fontHeight, SCREEN_WIDTH-1, yrmc, BLACK);
				if (rmcBuf._valid[0] == 'A') {
					sprintf((char *)ascii, "%s",rmcBuf._speed);
					lcdDrawString(&dev, fxG, 13*fontWidth, yrmc, ascii, CYAN);
				}
				yrmc = yrmc + fontHeight;
				lcdDrawFillRect(&dev, 17*fontWidth, yrmc-fontHeight, SCREEN_WIDTH-1, yrmc, BLACK);
				if (rmcBuf._valid[0] == 'A') {
					sprintf((char *)ascii, "%s",rmcBuf._orient);
					lcdDrawString(&dev, fxG, 17*fontWidth, yrmc, ascii, CYAN);
				}

			} else if (target_type == FORMATTED_GGA) {
				check = parse_nmea_gga(&ggaBuf, cmdBuf.payload, cmdBuf.length);
				if (!check) continue;
				uint16_t yrmc = fontHeight*2-1;
				lcdDrawFillRect(&dev, 5*fontWidth, yrmc-fontHeight, SCREEN_WIDTH-1, yrmc, BLACK);
				sprintf((char *)ascii, "%s",ggaBuf._time);
				lcdDrawString(&dev, fxG, 5*fontWidth, yrmc, ascii, CYAN);
				yrmc = yrmc + fontHeight;
				lcdDrawFillRect(&dev, 8*fontWidth, yrmc-fontHeight, SCREEN_WIDTH-1, yrmc, BLACK);
				if (ggaBuf._quality[0] == '0') sprintf((char *)ascii, "0(Invalid)");
				if (ggaBuf._quality[0] == '1') sprintf((char *)ascii, "1(SPS Mode)");
				if (ggaBuf._quality[0] == '2') sprintf((char *)ascii, "2(differenctial GPS Mode)");
				lcdDrawString(&dev, fxG, 8*fontWidth, yrmc, ascii, CYAN);
				yrmc = yrmc + fontHeight;
				lcdDrawFillRect(&dev, 4*fontWidth, yrmc-fontHeight, SCREEN_WIDTH-1, yrmc, BLACK);
				if (ggaBuf._quality[0] != '0') {
					sprintf((char *)ascii, "%s %s",ggaBuf._lat1, ggaBuf._lat2);
					lcdDrawString(&dev, fxG, 4*fontWidth, yrmc, ascii, CYAN);
				}
				yrmc = yrmc + fontHeight;
				lcdDrawFillRect(&dev, 4*fontWidth, yrmc-fontHeight, SCREEN_WIDTH-1, yrmc, BLACK);
				if (ggaBuf._quality[0] != '0') {
					sprintf((char *)ascii, "%s %s",ggaBuf._lon1, ggaBuf._lon2);
					lcdDrawString(&dev, fxG, 4*fontWidth, yrmc, ascii, CYAN);
				}
				yrmc = yrmc + fontHeight;
				lcdDrawFillRect(&dev, 21*fontWidth, yrmc-fontHeight, SCREEN_WIDTH-1, yrmc, BLACK);
				if (ggaBuf._quality[0] != '0') {
					sprintf((char *)ascii, "%s",ggaBuf._satellites);
					lcdDrawString(&dev, fxG, 21*fontWidth, yrmc, ascii, CYAN);
				}
				yrmc = yrmc + fontHeight;
				lcdDrawFillRect(&dev, 10*fontWidth, yrmc-fontHeight, SCREEN_WIDTH-1, yrmc, BLACK);
				if (ggaBuf._quality[0] != '0') {
					sprintf((char *)ascii, "%s",ggaBuf._droprate);
					lcdDrawString(&dev, fxG, 10*fontWidth, yrmc, ascii, CYAN);
				}
				yrmc = yrmc + fontHeight;
				lcdDrawFillRect(&dev, 16*fontWidth, yrmc-fontHeight, SCREEN_WIDTH-1, yrmc, BLACK);
				if (ggaBuf._quality[0] != '0') {
					sprintf((char *)ascii, "%s",ggaBuf._sealevel);
					lcdDrawString(&dev, fxG, 16*fontWidth, yrmc, ascii, CYAN);
				}
				yrmc = yrmc + fontHeight;
				lcdDrawFillRect(&dev, 13*fontWidth, yrmc-fontHeight, SCREEN_WIDTH-1, yrmc, BLACK);
				if (ggaBuf._quality[0] != '0') {
					sprintf((char *)ascii, "%s",ggaBuf._geoidheight);
					lcdDrawString(&dev, fxG, 13*fontWidth, yrmc, ascii, CYAN);
				}

			} else if (target_type == FORMATTED_VTG) {
				check = parse_nmea_vtg(&vtgBuf, cmdBuf.payload, cmdBuf.length);
				if (!check) continue;
				uint16_t yrmc = fontHeight*2-1;
				lcdDrawFillRect(&dev, 15*fontWidth, yrmc-fontHeight, SCREEN_WIDTH-1, yrmc, BLACK);
				sprintf((char *)ascii, "%s",vtgBuf._direction1);
				lcdDrawString(&dev, fxG, 15*fontWidth, yrmc, ascii, CYAN);
				yrmc = yrmc + fontHeight;
				lcdDrawFillRect(&dev, 22*fontWidth, yrmc-fontHeight, SCREEN_WIDTH-1, yrmc, BLACK);
				sprintf((char *)ascii, "%s",vtgBuf._direction2);
				lcdDrawString(&dev, fxG, 22*fontWidth, yrmc, ascii, CYAN);
				yrmc = yrmc + fontHeight;
				lcdDrawFillRect(&dev, 20*fontWidth, yrmc-fontHeight, SCREEN_WIDTH-1, yrmc, BLACK);
				sprintf((char *)ascii, "%s",vtgBuf._speed1);
				lcdDrawString(&dev, fxG, 20*fontWidth, yrmc, ascii, CYAN);
				yrmc = yrmc + fontHeight;
				lcdDrawFillRect(&dev, 20*fontWidth, yrmc-fontHeight, SCREEN_WIDTH-1, yrmc, BLACK);
				sprintf((char *)ascii, "%s",vtgBuf._speed2);
				lcdDrawString(&dev, fxG, 20*fontWidth, yrmc, ascii, CYAN);

			} else if (target_type == FORMATTED_NET) {
				if (!updateConnected) continue;
				uint16_t yrmc = fontHeight*5-1;
				lcdDrawFillRect(&dev, 7*fontWidth, yrmc-fontHeight, SCREEN_WIDTH-1, yrmc, BLACK);
				sprintf((char *)ascii, "Not Conected");
				if (connected) sprintf((char *)ascii, "Connect to u-center");
				lcdDrawString(&dev, fxG, 7*fontWidth, yrmc, ascii, CYAN);
				updateConnected = false;

			} else {
				check = check_nmea_type(target_type, &nmea, cmdBuf.payload, cmdBuf.length);
				if (!check) continue;

				int loop = 1;
				if (target_type != 0) loop = (cmdBuf.length+DISPLAY_LENGTH) / DISPLAY_LENGTH;
				int index = 0;
				for(int i=0;i<loop;i++) {
					memcpy((char *)ascii, (char *)&(cmdBuf.payload[index]), DISPLAY_LENGTH);
					ascii[DISPLAY_LENGTH] = 0;
					index = index + DISPLAY_LENGTH;

					if (current < lines) {
						//lcdDrawString(&dev, fxM, 0, ypos, cmdBuf.payload, CYAN);
						lcdDrawString(&dev, fxM, 0, ypos, ascii, CYAN);
					} else {
						lcdDrawFillRect(&dev, 0, ypos-fontHeight, SCREEN_WIDTH-1, ypos, BLACK);
						lcdSetScrollArea(&dev, fontHeight, (SCREEN_HEIGHT-fontHeight), 0);
						lcdScroll(&dev, vsp);
						vsp = vsp + fontHeight;
						if (vsp > ymax) vsp = fontHeight*2;
						//lcdDrawString(&dev, fxM, 0, ypos, cmdBuf.payload, CYAN);
						lcdDrawString(&dev, fxM, 0, ypos, ascii, CYAN);
					}
					current++;
					ypos = ypos + fontHeight;
					if (ypos > ymax) ypos = (fontHeight*2) - 1;
				}
			}

		} else if (cmdBuf.command == CMD_SELECT) {
			if (!enabled) continue;
			target_type++;
			if (target_type > nmea.typeNum) target_type = 0;
			get_nmea_type(target_type, &nmea, ascii);
			lcdDrawFillRect(&dev, xstatus, 0, SCREEN_WIDTH-1, fontHeight-1, BLACK);
			lcdDrawString(&dev, fxG, xstatus, fontHeight-1, ascii, GREEN);
			if (formatted_mode) lcdDrawFillRect(&dev, 0, fontHeight, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, BLACK);
			formatted_mode = false;

		} else if (cmdBuf.command == CMD_RMC) {
			// Reset scroll area
			lcdSetScrollArea(&dev, 0, 0x0140, 0);
			vsp = fontHeight*2;
			ypos = (fontHeight*2) - 1;
			current = 0;

			target_type = FORMATTED_RMC;
			get_nmea_type(target_type, &nmea, ascii);
			lcdDrawFillRect(&dev, xstatus, 0, SCREEN_WIDTH-1, fontHeight-1, BLACK);
			lcdDrawString(&dev, fxG, xstatus, fontHeight-1, ascii, GREEN);
			lcdDrawFillRect(&dev, 0, fontHeight, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, BLACK);

			// Show initial screen
			uint16_t yrmc = fontHeight*2-1;
			sprintf((char *)ascii, "Date:");
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			yrmc = yrmc + fontHeight;
			sprintf((char *)ascii, "Time:");
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			yrmc = yrmc + fontHeight;
			sprintf((char *)ascii, "Valid:");
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			yrmc = yrmc + fontHeight;
			sprintf((char *)ascii, "Lat:");
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			yrmc = yrmc + fontHeight;
			sprintf((char *)ascii, "Lon:");
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			yrmc = yrmc + fontHeight;
			sprintf((char *)ascii, "Moving Speed:");
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			yrmc = yrmc + fontHeight;
			sprintf((char *)ascii, "True Orientation:");
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			formatted_mode = true;

		} else if (cmdBuf.command == CMD_GGA) {
			// Reset scroll area
			lcdSetScrollArea(&dev, 0, 0x0140, 0);
			vsp = fontHeight*2;
			ypos = (fontHeight*2) - 1;
			current = 0;

			target_type = FORMATTED_GGA;
			get_nmea_type(target_type, &nmea, ascii);
			lcdDrawFillRect(&dev, xstatus, 0, SCREEN_WIDTH-1, fontHeight-1, BLACK);
			lcdDrawString(&dev, fxG, xstatus, fontHeight-1, ascii, GREEN);
			lcdDrawFillRect(&dev, 0, fontHeight, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, BLACK);

			// Show initial screen
			uint16_t yrmc = fontHeight*2-1;
			sprintf((char *)ascii, "Time:");
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			yrmc = yrmc + fontHeight;
			sprintf((char *)ascii, "Quality:");
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			yrmc = yrmc + fontHeight;
			sprintf((char *)ascii, "Lat:");
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			yrmc = yrmc + fontHeight;
			sprintf((char *)ascii, "Lon:");
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			yrmc = yrmc + fontHeight;
			sprintf((char *)ascii, "Number of satellites:");
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			yrmc = yrmc + fontHeight;
			sprintf((char *)ascii, "Drop rate:");
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			yrmc = yrmc + fontHeight;
			sprintf((char *)ascii, "Above sea level:");
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			yrmc = yrmc + fontHeight;
			sprintf((char *)ascii, "Geoid height:");
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			formatted_mode = true;

		} else if (cmdBuf.command == CMD_VTG) {
			// Reset scroll area
			lcdSetScrollArea(&dev, 0, 0x0140, 0);
			vsp = fontHeight*2;
			ypos = (fontHeight*2) - 1;
			current = 0;

			target_type = FORMATTED_VTG;
			get_nmea_type(target_type, &nmea, ascii);
			lcdDrawFillRect(&dev, xstatus, 0, SCREEN_WIDTH-1, fontHeight-1, BLACK);
			lcdDrawString(&dev, fxG, xstatus, fontHeight-1, ascii, GREEN);
			lcdDrawFillRect(&dev, 0, fontHeight, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, BLACK);

			// Show initial screen
			uint16_t yrmc = fontHeight*2-1;
			sprintf((char *)ascii, "True direction:");
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			yrmc = yrmc + fontHeight;
			sprintf((char *)ascii, "Magnetic orientation:");
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			yrmc = yrmc + fontHeight;
			sprintf((char *)ascii, "Ground speed (knot):");
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			yrmc = yrmc + fontHeight;
			sprintf((char *)ascii, "Ground speed (km/h):");
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			formatted_mode = true;

		} else if (cmdBuf.command == CMD_NET) {
			// Reset scroll area
			lcdSetScrollArea(&dev, 0, 0x0140, 0);
			vsp = fontHeight*2;
			ypos = (fontHeight*2) - 1;
			current = 0;

			target_type = FORMATTED_NET;
			get_nmea_type(target_type, &nmea, ascii);
			lcdDrawFillRect(&dev, xstatus, 0, SCREEN_WIDTH-1, fontHeight-1, BLACK);
			lcdDrawString(&dev, fxG, xstatus, fontHeight-1, ascii, GREEN);
			lcdDrawFillRect(&dev, 0, fontHeight, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, BLACK);

			// Show initial screen
			uint16_t yrmc = fontHeight*2-1;
			sprintf((char *)ascii, "IP Adress:%s", myNetwork._ip);
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			yrmc = yrmc + fontHeight;
			sprintf((char *)ascii, "Subnet mask:%s", myNetwork._netmask);
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			yrmc = yrmc + fontHeight;
			sprintf((char *)ascii, "Gateway:%s", myNetwork._gw);
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			yrmc = yrmc + fontHeight;
			if (!connected) sprintf((char *)ascii, "Status:Not Conected");
			if (connected) sprintf((char *)ascii, "Status:Connect to u-center");
			lcdDrawString(&dev, fxG, 0, yrmc, ascii, CYAN);
			formatted_mode = true;

		}
	}

	// nerver reach
	while (1) {
		vTaskDelay(2000 / portTICK_PERIOD_MS);
	}
}


#define SERVER_PORT 5000

// TCP Server Task
void tcp_server(void *pvParameters)
{
	ESP_LOGI(pcTaskGetTaskName(0), "Start");
	//esp_log_level_set(pcTaskGetTaskName(0), ESP_LOG_WARN);

	/* set up address to connect to */
	struct sockaddr_in srcAddr;
	struct sockaddr_in dstAddr;
	memset(&srcAddr, 0, sizeof(srcAddr));
	//srcAddr.sin_len = sizeof(srcAddr);
	srcAddr.sin_family = AF_INET;
	//srcAddr.sin_port = PP_HTONS(SERVER_PORT);
	srcAddr.sin_port = htons(CONFIG_SERVER_PORT);
	srcAddr.sin_addr.s_addr = INADDR_ANY;

	/* create the socket */
	int srcSocket;
	int dstSocket;
	socklen_t dstAddrSize;
	int ret;

	srcSocket = lwip_socket(AF_INET, SOCK_STREAM, 0);
	LWIP_ASSERT("srcSocket >= 0", srcSocket >= 0);

	/* bind socket */
	ret = lwip_bind(srcSocket, (struct sockaddr *)&srcAddr, sizeof(srcAddr));
	/* should succeed */
	LWIP_ASSERT("ret == 0", ret == 0);

	/* listen socket */
	ret = lwip_listen(srcSocket, 5);
	/* should succeed */
	LWIP_ASSERT("ret == 0", ret == 0);

	CMD_t cmdBuf;
	cmdBuf.taskHandle = xTaskGetCurrentTaskHandle();
	cmdBuf.length = 0;
	int	connected;

	while(1) {
		// Connection acceptance
		ESP_LOGI(pcTaskGetTaskName(0), "Wait from client connect port:%d", CONFIG_SERVER_PORT);
		dstAddrSize = sizeof(dstAddr);
		dstSocket = lwip_accept(srcSocket, (struct sockaddr *)&dstAddr, &dstAddrSize);
		ESP_LOGI(pcTaskGetTaskName(0), "Connect from %s",inet_ntoa(dstAddr.sin_addr));
		cmdBuf.command = CMD_CONNECT;
		xQueueSend(xQueueCmd, &cmdBuf, 0); // Send CONNECT
		connected = 1;

		while(connected) {
			xQueueReceive(xQueueTcp, &cmdBuf, portMAX_DELAY);
			ESP_LOGD(pcTaskGetTaskName(0), "cmdBuf.command=%d", cmdBuf.command);
			if (cmdBuf.command != CMD_NMEA) continue;
			ESP_LOGD(pcTaskGetTaskName(0), "[%s] payload=[%.*s]",__FUNCTION__, cmdBuf.length, cmdBuf.payload);
			/* write something */
			ret = lwip_write(dstSocket, cmdBuf.payload, cmdBuf.length);
#if 0
				uint8_t buffer[64];
				sprintf((char *)buffer,"$GPGSA,A,1,,,,,,,,,,,,,99.99,99.99,99.99*30\r\n");
				ret = lwip_write(dstSocket, buffer, strlen((char *)buffer));
#endif
			ESP_LOGI(pcTaskGetTaskName(0), "lwip_write ret=%d", ret);
			if (ret < 0) {
				cmdBuf.command = CMD_DISCONNECT;
				xQueueSend(xQueueCmd, &cmdBuf, 0); // Send DISCONNECT
				connected = 0;
			}
		} // end while
	} // end for


	/* close (never come here) */
	ret = lwip_close(srcSocket);
	LWIP_ASSERT("ret == 0", ret == 0);
	vTaskDelete( NULL );

}


static void SPIFFS_Directory(char * path) {
	DIR* dir = opendir(path);
	assert(dir != NULL);
	while (true) {
		struct dirent*pe = readdir(dir);
		if (!pe) break;
		ESP_LOGI(TAG,"d_name=%s d_ino=%d d_type=%x", pe->d_name,pe->d_ino, pe->d_type);
	}
	closedir(dir);
}

void app_main()
{
	// Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
	  ESP_ERROR_CHECK(nvs_flash_erase());
	  ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);
	
	// Initialize WiFi
#if CONFIG_AP_MODE
	ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
	wifi_init_softap();
	tcpip_adapter_ip_info_t ip_info;
	ESP_ERROR_CHECK(tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info));
	ESP_LOGI(TAG, "ESP32 is AP MODE");
#endif

#if CONFIG_ST_MODE
	ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
	wifi_init_sta();
	tcpip_adapter_ip_info_t ip_info;
	ESP_ERROR_CHECK(tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info));
	ESP_LOGI(TAG, "ESP32 is STA MODE");
#endif

	/* Print the local IP address */
	ESP_LOGI(TAG, "IP Address:	%s", ip4addr_ntoa(&ip_info.ip));
	ESP_LOGI(TAG, "Subnet mask: %s", ip4addr_ntoa(&ip_info.netmask));
	ESP_LOGI(TAG, "Gateway:		%s", ip4addr_ntoa(&ip_info.gw));
	sprintf(myNetwork._ip, "%s", ip4addr_ntoa(&ip_info.ip));
	sprintf(myNetwork._netmask, "%s", ip4addr_ntoa(&ip_info.netmask));
	sprintf(myNetwork._gw, "%s", ip4addr_ntoa(&ip_info.gw));

	// Initialize SPIFFS
	ESP_LOGI(TAG, "Initializing SPIFFS");
	esp_vfs_spiffs_conf_t conf = {
		.base_path = "/spiffs",
		.partition_label = NULL,
		.max_files = 6,
		.format_if_mount_failed =true
	};

	// Use settings defined above toinitialize and mount SPIFFS filesystem.
	// Note: esp_vfs_spiffs_register is anall-in-one convenience function.
	ret = esp_vfs_spiffs_register(&conf);

	if (ret != ESP_OK) {
		if (ret ==ESP_FAIL) {
			ESP_LOGE(TAG, "Failed to mount or format filesystem");
		} else if (ret== ESP_ERR_NOT_FOUND) {
			ESP_LOGE(TAG, "Failed to find SPIFFS partition");
		} else {
			ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)",esp_err_to_name(ret));
		}
		return;
	}

	size_t total = 0, used = 0;
	ret = esp_spiffs_info(NULL, &total, &used);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG,"Failed to get SPIFFS partition information (%s)",esp_err_to_name(ret));
	} else {
		ESP_LOGI(TAG,"Partition size: total: %d, used: %d", total, used);
	}

	SPIFFS_Directory("/spiffs");
	ESP_LOGI(TAG, "Initializing SPIFFS done");

	ESP_LOGI(TAG, "Initializing UART");
	/* Configure parameters of an UART driver,
	 * communication pins and install the driver */
	uart_config_t uart_config = {
		.baud_rate = 9600,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE
	};
	uart_param_config(UART_NUM_1, &uart_config);

	//Set UART pins (using UART0 default pins ie no changes.)
	uart_set_pin(UART_NUM_1, TXD_GPIO, CONFIG_UART_RXD_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
	//Install UART driver, and get the queue.
	uart_driver_install(UART_NUM_1, RX_BUF_SIZE * 2, 0, 20, &uart0_queue, 0);

	//Set uart pattern detect function.
	//uart_enable_pattern_det_intr(UART_NUM_1, 0x0a, 1, 10000, 10, 10); // pattern is LF
	uart_enable_pattern_det_baud_intr(UART_NUM_1, 0x0a, 1, 9, 0, 0); // pattern is LF
	//Reset the pattern queue length to record at most 20 pattern positions.
	uart_pattern_queue_reset(UART_NUM_1, 20);
	ESP_LOGI(TAG, "Initializing UART done");


	/* Create Queue */
	xQueueCmd = xQueueCreate( 10, sizeof(CMD_t) );
	configASSERT( xQueueCmd );
	xQueueTcp = xQueueCreate( 10, sizeof(CMD_t) );
	configASSERT( xQueueTcp );

	xTaskCreate(buttonA, "START", 1024*2, NULL, 2, NULL);
	xTaskCreate(buttonB, "STOP", 1024*2, NULL, 2, NULL);
	xTaskCreate(buttonC, "SELECT", 1024*2, NULL, 2, NULL);
	xTaskCreate(tft, "TFT", 1024*4, NULL, 5, NULL);
	xTaskCreate(tcp_server, "TCP", 1024*4, NULL, 5, NULL);
	//Create a task to handler UART event from ISR
	xTaskCreate(uart_event_task, "uart_event", 1024*4, NULL, 5, NULL);
}

