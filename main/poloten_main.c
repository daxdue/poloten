#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "driver/ledc.h"
#include "driver/timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "esp_blufi_api.h"
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "blufi.h"


#define LED_STANDBY 		(4)
#define LED_0_CH 			LEDC_CHANNEL_0
#define LED_POWER_STATE_1 	(16)
#define LED_1_CH 			LEDC_CHANNEL_1
#define LED_POWER_STATE_2 	(19)
#define LED_2_CH 			LEDC_CHANNEL_2
#define LED_POWER_STATE_3 	(18)
#define LED_3_CH 			LEDC_CHANNEL_3
#define LED_POWER_STATE_4 	(5)
#define LED_4_CH 			LEDC_CHANNEL_4
#define LED_POWER_STATE_5 	(17)
#define LED_5_CH 			LEDC_CHANNEL_5
#define SEM_PIN 			(23)
#define SEM_CH 				LEDC_CHANNEL_6
#define LCD_CHANNEL 		LEDC_CHANNEL_7

#define LEDC_HS_TIMER 		LEDC_TIMER_0
#define LEDC_HS_MODE 		LEDC_HIGH_SPEED_MODE
#define TIMER_RES 			LEDC_TIMER_13_BIT
#define ENCODER_PIN_32 		32
#define ENCODER_PIN_33 		33
#define GPIO_INPUT_PIN_SEL 	((1LL<<ENCODER_PIN_32) | (1ULL<<ENCODER_PIN_33))
#define ESP_INTR_FLAG_DEFAULT 0
#define TAG 				3

#define WEB_SERVER 			"clickbag.ru"
#define WEB_PORT 			80
#define WEB_URL  			"http://clickbag.ru/check_product.php?action=check&uid=1234"
#define BLUFI_DEVICE_NAME	"Poloten 1.0"
#define WIFI_LIST_NUM   	10

static const char *TAG2 = "test";

static const char *REQUEST = "GET " WEB_URL " HTTP/1.0\r\n"
    "Host: "WEB_SERVER"\r\n"
    "User-Agent: Poloten/1.0 esp32\r\n"
    "\r\n";

uint8_t encValue = 0;
uint8_t returnFlag = 0;
uint8_t cFlag = 0;
uint8_t flag_state = 0;
uint8_t power_mode = 0;

static uint8_t service_uuid128[32] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    //first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
};
const int CONNECTED_BIT = BIT0;

static bool gl_sta_connected = false;
static uint8_t gl_sta_bssid[6];
static uint8_t gl_sta_ssid[32];
static int gl_sta_ssid_len;

static uint8_t server_if;
static uint16_t conn_id;

char st_0[] = "7";
char st_1[] = "1";
char st_2[] = "2";
char st_3[] = "3";
char st_4[] = "4";
char st_5[] = "5";
char recievedValue[1];

nvs_handle my_handle;


long previousMillis = 0;
unsigned int led_flag = 0;
static xQueueHandle encoder_evt_queue = NULL;

static void IRAM_ATTR encoder_isr_handler(void* arg) {
	uint32_t gpio_num = (uint32_t) arg;
	xQueueSendFromISR(encoder_evt_queue, &gpio_num, NULL);
}

static void blufi_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param);

static void encoder_task(void* arg) {
	uint32_t io_num;
	uint16_t currentValue = 0;
	uint16_t oldValue = 0;

	for(;;) {

		if(xQueueReceive(encoder_evt_queue, &io_num, portMAX_DELAY)) {

			if(gpio_get_level(ENCODER_PIN_32) == 0 && gpio_get_level(ENCODER_PIN_33) == 0)
				currentValue = 0;
			if(gpio_get_level(ENCODER_PIN_32) == 0 && gpio_get_level(ENCODER_PIN_33) == 1)
				currentValue = 1;
			if(gpio_get_level(ENCODER_PIN_32) == 1 && gpio_get_level(ENCODER_PIN_33) == 0)
				currentValue = 2;
			if(gpio_get_level(ENCODER_PIN_32) == 1 && gpio_get_level(ENCODER_PIN_33) == 1)
				currentValue = 3;

			switch (oldValue) {

				case 0:
					if(currentValue == 2 && encValue < 35)
			    		encValue++;
			    	if(currentValue == 1 && encValue > 0)
			    		encValue--;
			    break;

				case 1:
			    	if(currentValue == 0 && encValue < 35)
			    		encValue++;
			    	if(currentValue == 3 && encValue > 0)
			    		encValue--;
			    break;

				case 2:
			   		if(currentValue == 3 && encValue < 35)
			   			encValue++;
			   		if(currentValue == 0 && encValue > 0)
			   			encValue--;
			   	break;

				case 3:
			    	if(currentValue == 1 && encValue < 35)
			    		encValue++;
			    	if(currentValue == 2 && encValue > 0)
			    		encValue--;
			    break;

			    	}

			    oldValue = currentValue;
			    printf("Current encoder value: %d\n", encValue);
			    	if(0 == encValue) {
			    		power_mode = 0;
			    		printf("Power mode: standby\n");
			    		returnFlag = 1;
			    	}

			    	if(5 <= encValue && 10 >= encValue) {
			    		power_mode = 1;
			    		printf("Power mode: 1\n");
			    		returnFlag = 1;
			    	}

			    	if(11 <= encValue && 16 >= encValue) {
			    		power_mode = 2;
			    		printf("Power mode: 2\n");
			    		returnFlag = 1;
			    	}

			    	if(17 <= encValue && 22 >= encValue) {
			    		power_mode = 3;
			    		printf("Power mode: 3\n");
			    		returnFlag = 1;
			    	}

			    	if(23 <= encValue && 28 >= encValue) {
			    		power_mode = 4;
			    		printf("Power mode: 4\n");
			    		returnFlag = 1;
			    	}

			    	if(29 <= encValue && 34 >= encValue) {
			    		power_mode = 5;
			    		printf("Power mode: 5\n");
			    		returnFlag = 1;
			    	}
		}
	}
}

void gpio_init(void) {
	gpio_config_t gpio_Config;
	gpio_Config.pin_bit_mask = GPIO_INPUT_PIN_SEL;
	gpio_Config.mode = GPIO_MODE_INPUT;
	gpio_Config.pull_up_en = GPIO_PULLUP_ENABLE;
	gpio_Config.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpio_Config.intr_type = GPIO_INTR_ANYEDGE;
	gpio_config(&gpio_Config);

	gpio_set_intr_type(GPIO_NUM_33,GPIO_INTR_ANYEDGE);
	gpio_set_intr_type(GPIO_NUM_32, GPIO_INTR_ANYEDGE);

	gpio_intr_enable(GPIO_NUM_32);
	gpio_intr_enable(GPIO_NUM_33);

}

void gpioCallback(void* arg)
{
    uint32_t gpio_num = 0;
    uint32_t gpio_intr_status = READ_PERI_REG(GPIO_STATUS_REG);
    uint32_t gpio_intr_status_h = READ_PERI_REG(GPIO_STATUS1_REG);
    SET_PERI_REG_MASK(GPIO_STATUS_W1TC_REG, gpio_intr_status);
    SET_PERI_REG_MASK(GPIO_STATUS1_W1TC_REG, gpio_intr_status_h);
    do {
      if(gpio_num < 32)
      {
        if(gpio_intr_status & BIT(gpio_num))
        	ets_printf("Interrupt GPIO%d ,value: %d\n",gpio_num,gpio_get_level(gpio_num));
      }
      else
      {
        if(gpio_intr_status_h & BIT(gpio_num - 32))
        	ets_printf("Interrupt GPIO%d, value: %d\n",gpio_num,gpio_get_level(gpio_num));
      }

    } while(++gpio_num < GPIO_PIN_COUNT);
 }

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x100,
    .max_interval = 0x100,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data =  NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 16,
    .p_service_uuid = service_uuid128,
    .flag = 0x6,
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x100,
    .adv_int_max        = 0x100,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    //.peer_addr            =
    //.peer_addr_type       =
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static wifi_config_t sta_config;
static EventGroupHandle_t wifi_event_group;


static esp_err_t net_event_handler(void *ctx, system_event_t *event) {

	wifi_mode_t mode;
    switch (event->event_id) {

    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;

    case SYSTEM_EVENT_STA_GOT_IP: {
        esp_blufi_extra_info_t info;
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        esp_wifi_get_mode(&mode);
        memset(&info, 0, sizeof(esp_blufi_extra_info_t));
        memcpy(info.sta_bssid, gl_sta_bssid, 6);
        info.sta_bssid_set = true;
        info.sta_ssid = gl_sta_ssid;
        info.sta_ssid_len = gl_sta_ssid_len;
        esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
        break;
    }

    case SYSTEM_EVENT_STA_CONNECTED:
        gl_sta_connected = true;
        cFlag = 1;
        memcpy(gl_sta_bssid, event->event_info.connected.bssid, 6);
        memcpy(gl_sta_ssid, event->event_info.connected.ssid, event->event_info.connected.ssid_len);
        gl_sta_ssid_len = event->event_info.connected.ssid_len;
        break;

    case SYSTEM_EVENT_STA_DISCONNECTED:
    	cFlag = 0;
        gl_sta_connected = false;
        memset(gl_sta_ssid, 0, 32);
        memset(gl_sta_bssid, 0, 6);
        gl_sta_ssid_len = 0;
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;


    case SYSTEM_EVENT_SCAN_DONE: {
        uint16_t apCount = 0;
        esp_wifi_scan_get_ap_num(&apCount);

        if (apCount == 0) {
            BLUFI_INFO("Не найдено точек доступа");
            break;
        }

        wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * apCount);
        if (!ap_list) {
            BLUFI_ERROR("malloc error, ap_list is NULL");
            break;
        }

        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&apCount, ap_list));
        esp_blufi_ap_record_t * blufi_ap_list = (esp_blufi_ap_record_t *)malloc(apCount * sizeof(esp_blufi_ap_record_t));
        if (!blufi_ap_list) {
            if (ap_list) {
                free(ap_list);
            }
            BLUFI_ERROR("malloc error, blufi_ap_list is NULL");
            break;
        }

        for (int i = 0; i < apCount; ++i) {
            blufi_ap_list[i].rssi = ap_list[i].rssi;
            memcpy(blufi_ap_list[i].ssid, ap_list[i].ssid, sizeof(ap_list[i].ssid));
        }

        esp_blufi_send_wifi_list(apCount, blufi_ap_list);
        esp_wifi_scan_stop();
        free(ap_list);
        free(blufi_ap_list);
        break;
    }

    default:
        break;
    }
    return ESP_OK;
}

static void initialise_wifi(void) {
	esp_err_t error_wifi;
	esp_err_t error_nvs = nvs_flash_init();
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(net_event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    error_wifi = esp_wifi_start();
}


static void http_get_task(void *pvParameters)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    char recv_buf[64];

    while(1) {

        xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                            false, true, portMAX_DELAY);
        int err = getaddrinfo(WEB_SERVER, "80", &hints, &res);

        if(err != 0 || res == NULL) {
            ESP_LOGE(TAG2, "DNS lookup failed err=%d res=%p", err, res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
           continue;
        }

        addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
        s = socket(res->ai_family, res->ai_socktype, 0);

        if(s < 0) {
            ESP_LOGE(TAG2, "Failed to allocate socket.");
            freeaddrinfo(res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(TAG2, "Socket connect failed errno=%d", errno);
            close(s);
            freeaddrinfo(res);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
           continue;
        }

        freeaddrinfo(res);

        if (write(s, REQUEST, strlen(REQUEST)) < 0) {
            ESP_LOGE(TAG2, "Socket send failed");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }

        struct timeval receiving_timeout;
        receiving_timeout.tv_sec = 5;
        receiving_timeout.tv_usec = 0;

        if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
                sizeof(receiving_timeout)) < 0) {
            ESP_LOGE(TAG2, "Failed to set socket receiving timeout");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }

        do {
            bzero(recv_buf, sizeof(recv_buf));
            r = read(s, recv_buf, sizeof(recv_buf)-1);
            for(int i = 0; i < r; i++) {
            	recievedValue[0] =  putchar(recv_buf[i]);
            }
        } while(r > 0);
        close(s);

        if(strcmp(recievedValue, st_1) == 0) {
        	printf("Power mode: 1\n");
            power_mode = 1;
            encValue = 14;
        }
        if(strcmp(recievedValue, st_2) == 0) {
        	printf("Power mode: 2\n");
        	power_mode = 2;
        	encValue = 20;
        }
        if(strcmp(recievedValue, st_3) == 0) {
        	printf("Power mode: 3\n");
        	power_mode = 3;
        	encValue = 26;
        }
        if(strcmp(recievedValue, st_4) == 0) {
        	printf("Power mode: 4\n");
        	power_mode = 4;
        	encValue = 32;
        }
        if(strcmp(recievedValue, st_5) == 0) {
        	printf("Power mode: 5\n");
        	power_mode = 5;
        	encValue = 37;
        }
        if(strcmp(recievedValue, st_0) == 0) {
        	printf("Power mode: standby\n");
        	power_mode = 0;
        	encValue = 0;
        	}

        for(int countdown = 1; countdown >= 0; countdown--) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
   }
}





static esp_blufi_callbacks_t blufi_test_callbacks = {
    .event_cb = blufi_event_callback,
    .negotiate_data_handler = blufi_dh_negotiate_data_handler,
    .encrypt_func = blufi_aes_encrypt,
    .decrypt_func = blufi_aes_decrypt,
    .checksum_func = blufi_crc_checksum,
};

static void blufi_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param)
{
    switch (event) {

    case ESP_BLUFI_EVENT_INIT_FINISH:
        BLUFI_INFO("BLUFI initialization completed\n");
        esp_ble_gap_set_device_name(BLUFI_DEVICE_NAME);
        esp_ble_gap_config_adv_data(&adv_data);
        break;

    case ESP_BLUFI_EVENT_DEINIT_FINISH:
        BLUFI_INFO("BLUFI deinit completed\n");
        break;

    case ESP_BLUFI_EVENT_BLE_CONNECT:
        BLUFI_INFO("BLUFI BLE connect\n");
        server_if = param->connect.server_if;
        conn_id = param->connect.conn_id;
        esp_ble_gap_stop_advertising();
        blufi_security_init();
        break;

    case ESP_BLUFI_EVENT_BLE_DISCONNECT:
        BLUFI_INFO("BLUFI BLE disconnect\n");
        blufi_security_deinit();
        esp_ble_gap_start_advertising(&adv_params);
        break;

    case ESP_BLUFI_EVENT_SET_WIFI_OPMODE:
        BLUFI_INFO("BLUFI Set WIFI operation mode %d\n", param->wifi_mode.op_mode);
        ESP_ERROR_CHECK( esp_wifi_set_mode(param->wifi_mode.op_mode) );
        break;

    case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP:
        BLUFI_INFO("BLUFI Request WIFI connect to AP\n");
        esp_wifi_disconnect();
        esp_wifi_connect();
        break;

    case ESP_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP:
        BLUFI_INFO("BLUFI Request WIFI disconnect from AP\n");
        esp_wifi_disconnect();
        break;

    case ESP_BLUFI_EVENT_REPORT_ERROR:
        BLUFI_ERROR("BLUFI Report error, error code %d\n", param->report_error.state);
        esp_blufi_send_error_info(param->report_error.state);
        break;

    case ESP_BLUFI_EVENT_GET_WIFI_STATUS: {
        wifi_mode_t mode;
        esp_blufi_extra_info_t info;
        esp_wifi_get_mode(&mode);

        if (gl_sta_connected ) {  
            memset(&info, 0, sizeof(esp_blufi_extra_info_t));
            memcpy(info.sta_bssid, gl_sta_bssid, 6);
            info.sta_bssid_set = true;
            info.sta_ssid = gl_sta_ssid;
            info.sta_ssid_len = gl_sta_ssid_len;
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
        }
        else {
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, 0, NULL);
        }
        BLUFI_INFO("BLUFI Get WIFI status from AP\n");
        break;
    }

    case ESP_BLUFI_EVENT_RECV_SLAVE_DISCONNECT_BLE:
        BLUFI_INFO("BLUFI close GATT connection");
        esp_blufi_close(server_if, conn_id);
        break;

	case ESP_BLUFI_EVENT_RECV_STA_BSSID:
        memcpy(sta_config.sta.bssid, param->sta_bssid.bssid, 6);
        sta_config.sta.bssid_set = 1;
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        BLUFI_INFO("Recieved STA BSSID %s\n", sta_config.sta.ssid);
        break;

	case ESP_BLUFI_EVENT_RECV_STA_SSID:
        strncpy((char *)sta_config.sta.ssid, (char *)param->sta_ssid.ssid, param->sta_ssid.ssid_len);
        sta_config.sta.ssid[param->sta_ssid.ssid_len] = '\0';
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        BLUFI_INFO("Recieved STA SSID %s\n", sta_config.sta.ssid);
        break;

	case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
        strncpy((char *)sta_config.sta.password, (char *)param->sta_passwd.passwd, param->sta_passwd.passwd_len);
        sta_config.sta.password[param->sta_passwd.passwd_len] = '\0';
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        BLUFI_INFO("Recieved STA PASSWORD %s\n", sta_config.sta.password);
        break;

    case ESP_BLUFI_EVENT_GET_WIFI_LIST:{
        wifi_scan_config_t scanConf = {
            .ssid = NULL,
            .bssid = NULL,
            .channel = 0,
            .show_hidden = false
        };
        ESP_ERROR_CHECK(esp_wifi_scan_start(&scanConf, true));
        break;
    }
    case ESP_BLUFI_EVENT_RECV_CUSTOM_DATA:
        BLUFI_INFO("Recieved BLE power mode %d\n", param->custom_data.data_len);
        esp_log_buffer_hex("Custom Data", param->custom_data.data, param->custom_data.data_len);
        uint8_t curr_val = param->custom_data.data[1]<<8 | param->custom_data.data[0];
        printf("BLE power mode: %d\n", curr_val);
        returnFlag = 1;
        switch(curr_val) {

        case 48:
        	power_mode = 0;
        	encValue = 7;
            break;
        case 49:
        	power_mode = 1;
        	encValue = 14;
        	break;
        case 50:
        	power_mode = 2;
        	encValue = 20;
        	break;
        case 51:
        	power_mode = 3;
        	break;
        case 52:
        	power_mode = 4;
        	encValue = 26;
        	break;
        case 53:
        	power_mode = 5;
        	encValue = 32;
        	break;
        case 36:
        	power_mode = 6;
        	encValue = 37;
        	break;
        }
        break;
    default:
        break;
    }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&adv_params);
        break;
    default:
        break;
    }
}

void app_main()
{
    esp_err_t ret;
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        BLUFI_ERROR("%s Initialization BT controller failed: %s\n", __func__, esp_err_to_name(ret));
    }
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        BLUFI_ERROR("%s Enable BT controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_init();
    if (ret) {
        BLUFI_ERROR("%s Init Bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        BLUFI_ERROR("%s Init Bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }
    BLUFI_INFO("BD ADDR: "ESP_BD_ADDR_STR"\n", ESP_BD_ADDR_HEX(esp_bt_dev_get_address()));
    BLUFI_INFO("BLUFI VERSION %04x\n", esp_blufi_get_version());
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if(ret){
        BLUFI_ERROR("%s gap register failed, error code = %x\n", __func__, ret);
        return;
    }

    ret = esp_blufi_register_callbacks(&blufi_test_callbacks);
    if(ret){
        BLUFI_ERROR("%s Blufi register failed, error code = %x\n", __func__, ret);
        return;
    }
    esp_blufi_profile_init();

    initialise_wifi();

        	ledc_timer_config_t ledc_timer = {
        	        		.duty_resolution = LEDC_TIMER_10_BIT,
        	        		.freq_hz  = 60,
        	    			.speed_mode = LEDC_HS_MODE,
        	    			.timer_num = LEDC_HS_TIMER
        	        };

        	        ledc_timer_config(&ledc_timer);



        	ledc_channel_config_t ledc_channel[6] = {
        			{

        					.channel = LED_0_CH,
							.duty = 1024,
							.gpio_num = LED_STANDBY,
							.speed_mode = LEDC_HS_MODE,
							.timer_sel = LEDC_HS_TIMER
        			},


					{
							.channel = LED_1_CH,
							.duty = 1024,
							.gpio_num = LED_POWER_STATE_1,
							.speed_mode = LEDC_HS_MODE,
							.timer_sel = LEDC_HS_TIMER
					},

					{
							.channel = LED_2_CH,
							.duty = 1024,
							.gpio_num = LED_POWER_STATE_2,
							.speed_mode = LEDC_HS_MODE,
							.timer_sel = LEDC_HS_TIMER
					},

					{
							.channel = LED_3_CH,
							.duty = 1024,
							.gpio_num  = LED_POWER_STATE_3,
							.speed_mode = LEDC_HS_MODE,
							.timer_sel = LEDC_HS_TIMER
					},

					{
							.channel = LED_4_CH,
							.duty = 1024,
							.gpio_num = LED_POWER_STATE_4,
							.speed_mode = LEDC_HS_MODE,
							.timer_sel = LEDC_HS_TIMER
					},

					{
							.channel = LED_5_CH,
							.duty = 1024,
							.gpio_num = LED_POWER_STATE_5,
							.speed_mode = LEDC_HS_MODE,
							.timer_sel = LEDC_HS_TIMER
					}
        	};

        	for (int ch = 0; ch < 7; ch++) {
        		ledc_channel_config(&ledc_channel[ch]);
        	}

            gpio_config_t gpio_Config;
            gpio_Config.pin_bit_mask = GPIO_INPUT_PIN_SEL;
            gpio_Config.mode = GPIO_MODE_INPUT;
            gpio_Config.pull_up_en = GPIO_PULLUP_ENABLE;
            gpio_Config.pull_down_en = GPIO_PULLDOWN_DISABLE;
            gpio_Config.intr_type = GPIO_INTR_ANYEDGE;
            gpio_config(&gpio_Config);

            encoder_evt_queue = xQueueCreate(10, sizeof(uint32_t));
            xTaskCreatePinnedToCore(encoder_task, "encoder_task", 2048, NULL, 10, NULL, 1);

            gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
            gpio_isr_handler_add(ENCODER_PIN_32, encoder_isr_handler, (void*) ENCODER_PIN_32);
            gpio_isr_handler_add(ENCODER_PIN_33, encoder_isr_handler, (void*) ENCODER_PIN_33);

            wifi_ap_record_t info;
            int8_t rssi_info;

            xTaskCreatePinnedToCore(&http_get_task, "http_get_task", 4096, NULL, 5, NULL, 1);


            void led_num(int led, int state) {
            	ledc_set_duty(ledc_channel[led].speed_mode, ledc_channel[led].channel, state);
            	ledc_update_duty(ledc_channel[led].speed_mode, ledc_channel[led].channel);
            }

            void power_state_led(int led) {
            	int on = 512;
            	int off = 1024;

            	switch(led) {
            		case 0:
            			led_num(0, on);
            			led_num(1, off);
            			led_num(2, off);
            			led_num(3, off);
            			led_num(4, off);
            			led_num(5, off);
                		break;

            		case 1:
            			led_num(0, off);
            			led_num(1, on);
            			led_num(2, off);
            			led_num(3, off);
            			led_num(4, off);
            			led_num(5, off);
                		break;

                	case 2:
                		led_num(0, off);
                		led_num(1, on);
                		led_num(2, on);
                		led_num(3, off);
                		led_num(4, off);
                		led_num(5, off);
                		break;

                	case 3:
                		led_num(0, off);
                		led_num(1, on);
                		led_num(2, on);
                		led_num(3, on);
                		led_num(4, off);
                		led_num(5, off);
                		break;

                	case 4:
                		led_num(0, off);
                		led_num(1, on);
                		led_num(2, on);
                		led_num(3, on);
                		led_num(4, on);
                		led_num(5, off);
                		break;

                	case 5:
                		led_num(0, off);
                		led_num(1, on);
                		led_num(2, on);
                		led_num(3, on);
                		led_num(4, on);
                		led_num(5, on);
                		break;

                	case 6:
                		led_num(0, off);
                		led_num(1, off);
                		led_num(2, off);
                		led_num(3, off);
                		led_num(4, off);
                		led_num(5, off);
                		break;
                		}
                	}

while(1) {

	if(cFlag == 1) {
		ledc_set_duty(ledc_channel[6].speed_mode, ledc_channel[6].channel, 512);
		ledc_update_duty(ledc_channel[6].speed_mode, ledc_channel[6].channel);
	}

	void power_function(int num, int led_n) {

		while(flag_state == 0) {
			if(returnFlag == 1) {
				returnFlag = 0;
				return;
            }
			unsigned long curMillis =  xTaskGetTickCount();
			led_num(0, 0);
            		if(curMillis - previousMillis > num) {
            			previousMillis = curMillis;
            			flag_state = 1;
            		}

         }

		while(flag_state == 1) {
			if(returnFlag == 1){
				returnFlag = 0;
				return;
            }

			unsigned long curMillis =  xTaskGetTickCount();
			led_num(0, 1024);
			if(curMillis - previousMillis > 500 - num) {
				previousMillis = curMillis;
				flag_state = 0;
			}

		}
	}

		switch(power_mode) {
			case 0:
				power_state_led(0);
                break;
			case 1:
                power_state_led(1);
                power_function(100, 1);
                break;
			case 2:
				power_state_led(2);
				power_function(200, 2);
				break;
			case 3:
				power_state_led(3);
				power_function(300, 3);
				break;
			case 4:
				power_state_led(4);
				power_function(400, 4);
				break;
			case 5:
				power_state_led(5);
				power_function(490, 5);
				break;
		}
	}
}
