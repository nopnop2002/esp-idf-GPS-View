/* TCP server task

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

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "cmd.h"

extern QueueHandle_t xQueueCmd;
extern QueueHandle_t xQueueTcp;

#define SERVER_PORT 5000

// TCP Server Task
void tcp_server(void *pvParameters)
{
	ESP_LOGI(pcTaskGetName(0), "Start");
	//esp_log_level_set(pcTaskGetName(0), ESP_LOG_WARN);

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
		ESP_LOGI(pcTaskGetName(0), "Wait from client connect port:%d", CONFIG_SERVER_PORT);
		dstAddrSize = sizeof(dstAddr);
		dstSocket = lwip_accept(srcSocket, (struct sockaddr *)&dstAddr, &dstAddrSize);
		ESP_LOGI(pcTaskGetName(0), "Connect from %s",inet_ntoa(dstAddr.sin_addr));
		cmdBuf.command = CMD_CONNECT;
		xQueueSend(xQueueCmd, &cmdBuf, 0); // Send CONNECT
		connected = 1;

		while(connected) {
			xQueueReceive(xQueueTcp, &cmdBuf, portMAX_DELAY);
			ESP_LOGD(pcTaskGetName(0), "cmdBuf.command=%d", cmdBuf.command);
			if (cmdBuf.command != CMD_NMEA) continue;
			ESP_LOGD(pcTaskGetName(0), "[%s] payload=[%.*s]",__FUNCTION__, cmdBuf.length, cmdBuf.payload);
			/* write something */
			ret = lwip_write(dstSocket, cmdBuf.payload, cmdBuf.length);
#if 0
				uint8_t buffer[64];
				sprintf((char *)buffer,"$GPGSA,A,1,,,,,,,,,,,,,99.99,99.99,99.99*30\r\n");
				ret = lwip_write(dstSocket, buffer, strlen((char *)buffer));
#endif
			ESP_LOGI(pcTaskGetName(0), "lwip_write ret=%d", ret);
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

