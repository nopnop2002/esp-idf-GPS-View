/* tft task

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
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

#include "driver/gpio.h"

#include "ili9340.h"
#include "fontx.h"

#include "cmd.h"

static const char *TAG = "TFT";

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define GPIO_MOSI 23
#define GPIO_SCLK 18
#define GPIO_CS 14
#define GPIO_DC 27
#define GPIO_RESET 33
#define GPIO_BL 32
#define GPIO_MISO -1
#define GPIO_XPT_CS -1
#define GPIO_XPT_IRQ -1
#define DISPLAY_LENGTH	26
#define GPIO_INPUT_A GPIO_NUM_39
#define GPIO_INPUT_B GPIO_NUM_38
#define GPIO_INPUT_C GPIO_NUM_37

extern QueueHandle_t xQueueCmd;
extern QueueHandle_t xQueueTcp;

void buttonA(void *pvParameters)
{
	ESP_LOGI(pcTaskGetName(0), "Start");
	CMD_t cmdBuf;
	cmdBuf.command = CMD_START;
	cmdBuf.taskHandle = xTaskGetCurrentTaskHandle();

	// set the GPIO as a input
	gpio_reset_pin(GPIO_INPUT_A);
	gpio_set_direction(GPIO_INPUT_A, GPIO_MODE_DEF_INPUT);

	if (xQueueSendToFront(xQueueCmd, &cmdBuf, portMAX_DELAY) != pdPASS) {
		ESP_LOGE(pcTaskGetName(0), "xQueueSend Fail");
	}
	cmdBuf.command = CMD_STOP;

	while(1) {
		int level = gpio_get_level(GPIO_INPUT_A);
		if (level == 0) {
			ESP_LOGI(pcTaskGetName(0), "Push Button");
			while(1) {
				level = gpio_get_level(GPIO_INPUT_A);
				if (level == 1) break;
				vTaskDelay(1);
			}

			// Post an item to the front of a queue.
			if (xQueueSendToFront(xQueueCmd, &cmdBuf, portMAX_DELAY) != pdPASS) {
				ESP_LOGE(pcTaskGetName(0), "xQueueSend Fail");
			}
			if (cmdBuf.command == CMD_START) {
				cmdBuf.command = CMD_STOP;
			} else {
				cmdBuf.command = CMD_START;
			}
		}
		vTaskDelay(1);
	} // end while

	// nerver reach here
	vTaskDelete(NULL);
}

void buttonB(void *pvParameters)
{
	ESP_LOGI(pcTaskGetName(0), "Start");
	CMD_t cmdBuf;
	cmdBuf.command = CMD_SELECT;
	cmdBuf.taskHandle = xTaskGetCurrentTaskHandle();

	// set the GPIO as a input
	gpio_reset_pin(GPIO_INPUT_B);
	gpio_set_direction(GPIO_INPUT_B, GPIO_MODE_DEF_INPUT);

	while(1) {
		int level = gpio_get_level(GPIO_INPUT_B);
		if (level == 0) {
			ESP_LOGI(pcTaskGetName(0), "Push Button");
			while(1) {
				level = gpio_get_level(GPIO_INPUT_B);
				if (level == 1) break;
				vTaskDelay(1);
			}

			// Post an item to the front of a queue.
			if (xQueueSendToFront(xQueueCmd, &cmdBuf, portMAX_DELAY) != pdPASS) {
				ESP_LOGE(pcTaskGetName(0), "xQueueSend Fail");
			}
		}
		vTaskDelay(1);
	} // end while

	// nerver reach here
	vTaskDelete(NULL);
}

void buttonC(void *pvParameters)
{
	ESP_LOGI(pcTaskGetName(0), "Start");
	int CMD_TABLE[] = {CMD_RMC, CMD_GGA, CMD_VTG, CMD_NET};
	int cmdIndex = 0;
	CMD_t cmdBuf;
	//cmdBuf.command = CMD_RMC;
	cmdBuf.command = CMD_TABLE[cmdIndex];
	cmdBuf.taskHandle = xTaskGetCurrentTaskHandle();

	// set the GPIO as a input
	gpio_reset_pin(GPIO_INPUT_C);
	gpio_set_direction(GPIO_INPUT_C, GPIO_MODE_DEF_INPUT);

	while(1) {
		int level = gpio_get_level(GPIO_INPUT_C);
		if (level == 0) {
			ESP_LOGI(pcTaskGetName(0), "Push Button");
			while(1) {
				level = gpio_get_level(GPIO_INPUT_C);
				if (level == 1) break;
				vTaskDelay(1);
			}

			// Post an item to the front of a queue.
			if (xQueueSendToFront(xQueueCmd, &cmdBuf, portMAX_DELAY) != pdPASS) {
				ESP_LOGE(pcTaskGetName(0), "xQueueSend Fail");
			}
			cmdIndex++;
			if (cmdIndex == 4) cmdIndex = 0;
			cmdBuf.command = CMD_TABLE[cmdIndex];
		}
		vTaskDelay(1);
	} // end while

	// nerver reach here
	vTaskDelete(NULL);
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
	ESP_LOGD(TAG, "[%s] typeString=%.*s", __FUNCTION__, typeLength, payload);
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
	ESP_LOGD(TAG, "[%s] typeString=%.*s", __FUNCTION__, typeLength, payload);
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
	ESP_LOGD(TAG, "[%s] typeString=%.*s", __FUNCTION__, typeLength, payload);
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
	NETWORK_t *task_parameter = pvParameters;;
	NETWORK_t myNetwork;
	memcpy((char *)&myNetwork, task_parameter, sizeof(NETWORK_t));
	
	ESP_LOGI(pcTaskGetName(0), "Start");
	ESP_LOGI(pcTaskGetName(0), "myNetwork._ip=[%s]", myNetwork._ip);
	ESP_LOGI(pcTaskGetName(0), "myNetwork._netmask=[%s]", myNetwork._netmask);
	ESP_LOGI(pcTaskGetName(0), "myNetwork._gw=[%s]", myNetwork._gw);

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
	ESP_LOGI(pcTaskGetName(0), "fontWidth=%d fontHeight=%d",fontWidth,fontHeight);

	// Initialize NMEA table
	NMEA_t nmea;
	nmea.typeNum = 0;
	for(int i=0;i<MAX_NMEA;i++) {
		nmea.type[i].enable = false;
	}

	// Setup Screen
	TFT_t dev;
	spi_master_init(&dev, GPIO_MOSI, GPIO_SCLK, GPIO_CS, GPIO_DC, GPIO_RESET, GPIO_BL, GPIO_MISO, GPIO_XPT_CS, GPIO_XPT_IRQ);
	lcdInit(&dev, 0x9341, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0);
	ESP_LOGI(pcTaskGetName(0), "Setup Screen done");

	int lines = (SCREEN_HEIGHT - fontHeight) / fontHeight;
	ESP_LOGD(pcTaskGetName(0), "SCREEN_HEIGHT=%d fontHeight=%d lines=%d", SCREEN_HEIGHT, fontHeight, lines);
	int ymax = (lines+1) * fontHeight;
	ESP_LOGD(pcTaskGetName(0), "ymax=%d",ymax);

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
	int watchdogCounter = 0;

#if 0
	get_nmea_type(target_type, &nmea, ascii);
	lcdDrawFillRect(&dev, xstatus, 0, SCREEN_WIDTH-1, fontHeight-1, BLACK);
	lcdDrawString(&dev, fxG, xstatus, fontHeight-1, ascii, GREEN);
	enabled = true;
#endif

	while(1) {
		xQueueReceive(xQueueCmd, &cmdBuf, portMAX_DELAY);
		ESP_LOGD(pcTaskGetName(0),"cmdBuf.command=%d connected=%d", cmdBuf.command, connected);
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
			watchdogCounter++;
			if (watchdogCounter == 10) {
				vTaskDelay(1); // Avoid WatchDog trigger
				watchdogCounter = 0;
			}
			build_nmea_type(&nmea, cmdBuf.payload, cmdBuf.length);
			if (connected) xQueueSend(xQueueTcp, &cmdBuf, 0);
			if (!enabled) continue;
			ESP_LOGD(pcTaskGetName(0), "target_type=%d", target_type);

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

	// nerver reach here
	vTaskDelete(NULL);
}

