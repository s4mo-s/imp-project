#include <stdio.h>
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_http_server.h"

#define GPIO_LED_RED		2
#define GPIO_LED1		13
#define GPIO_LED2		12
#define GPIO_LED3		14
#define GPIO_RED		27
#define GPIO_GREEN		17
#define GPIO_BLUE		16

QueueHandle_t queue = NULL;

typedef struct {
	int led1;
	int led2;
	int led3;
	int red;
	int green;
	int blue;
	int speed;
	int rounds;
} led_options_t;

#define ESP_WIFI_SSID      "esp-wifi"
#define ESP_WIFI_PASS      "mypassword"
#define ESP_WIFI_CHANNEL   1
#define MAX_STA_CONN       1

inline int MIN(int a, int b) { return a > b ? b : a; }

static const char *TAG = "HTTPS";

/* TOGGLE MODE/SPEED */
void set_options(led_options_t* options, int mode, int speed)
{
	switch (mode){
		case 1:
			options->led1 = 1;
			options->led2 = 2;
			options->led3 = 4;
			options->red = 8;
			options->green = 0;
			options->blue = 8;
			options->rounds = 17;
			break;
		case 2:
			options->led1 = 1;
			options->led2 = 0;
			options->led3 = 1;
			options->red = 1;
			options->green = 0;
			options->blue = 0;
			options->rounds = 21;
			break;
		case 3:
			options->led1 = 4;
			options->led2 = 2;
			options->led3 = 1;
			options->red = 8;
			options->green = 8;
			options->blue = 0;
			options->rounds = 17;
			break;
		case 4:
			options->led1 = 1;
			options->led2 = 1;
			options->led3 = 1;
			options->red = 0;
			options->green = 1;
			options->blue = 1;
			options->rounds = 21;
			break;
	}

	switch (speed){
		case 1:
			options->speed = 1000;
			break;
		case 2:
			options->speed = 500;
			break;
		case 3:
			options->speed = 100;
			break;
		}
}

/* TASK */
void blink_task() {
	while (1){
        led_options_t options = {};
        int cnt = 0;

        // Get structure from queue or waiting if queue is empty
        while (1){
            if(xQueueReceive(queue, &options, (TickType_t)(1000/portTICK_PERIOD_MS))){
                break;
            }
            vTaskDelay(500/portTICK_PERIOD_MS);
        }

        // Process sequence
        while(options.rounds) {
            ESP_ERROR_CHECK(gpio_set_level(GPIO_LED1, cnt & options.led1));
            ESP_ERROR_CHECK(gpio_set_level(GPIO_LED2, cnt & options.led2));
            ESP_ERROR_CHECK(gpio_set_level(GPIO_LED3, cnt & options.led3));
            ESP_ERROR_CHECK(gpio_set_level(GPIO_RED, cnt & options.red));
            ESP_ERROR_CHECK(gpio_set_level(GPIO_GREEN, cnt & options.green));
            ESP_ERROR_CHECK(gpio_set_level(GPIO_BLUE, cnt & options.blue));
            vTaskDelay(options.speed / portTICK_RATE_MS);
            options.rounds--;
            cnt++;
        }
	}
}

/* WIFI INIT */
void wifi_init(void)
{
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_ap();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	wifi_config_t wifi_config = {
		.ap = {
            .ssid = ESP_WIFI_SSID,
            .channel = ESP_WIFI_CHANNEL,
            .password = ESP_WIFI_PASS,
            .max_connection = MAX_STA_CONN,
        },
	};

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());
}


/* REQUEST HANDLERS */
static esp_err_t get_handler(httpd_req_t *req){
	const char *response = req->user_ctx;
	httpd_resp_send(req, response, strlen(response));
	return ESP_OK;
}

esp_err_t post_handler(httpd_req_t *req){
	char content[100];
	size_t recv_size = MIN(req->content_len, sizeof(content));
	int ret = httpd_req_recv(req, content, recv_size);

	if (ret <= 0)
	{
		if (ret == HTTPD_SOCK_ERR_TIMEOUT)
		{
			httpd_resp_send_408(req);
		}
		return ESP_FAIL;
	}

	int mode = content[5] - '0';
	int speed = content[13] - '0';
	ESP_LOGI(TAG, "mode: %i, speed %i\n", mode, speed);

	char *response = req->user_ctx;
	httpd_resp_send(req, response, strlen(response));

	led_options_t options;
	set_options(&options, mode, speed);

	xQueueSend(queue, (void*)&options, (TickType_t)0);
	return ESP_OK;
}


/* WEB SERVER */
static httpd_handle_t start_webserver(void){
	httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

	ESP_LOGI(TAG, "Starting HTTP Server");

	esp_err_t ret = httpd_start(&server, &config);

	if (ESP_OK != ret)
	{
		ESP_LOGI(TAG, "Error starting server!");
		return NULL;
	}

	const httpd_uri_t uri_get = {
		.uri = "/",
		.method = HTTP_GET,
		.handler = get_handler,
		.user_ctx ="<!DOCTYPE html>\
                        <html>\
                            <head>\
                                <title>ESP32 controller</title>\
                                <meta charset=\"utf-8\"/>\
                                <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/>\
                                <style>\
                                    .button {\
                                        border: none;\
                                        border-radius: 15px;\
                                        padding: 15px 30px;\
                                        text-align: center;\
                                        font-size: 30px;\
                                        margin-top: 50px;\
                                        cursor: pointer;\
                                        color: white;\
                                        background-color: blue;\
                                    }\
                                    body {\
                                        font-family: Open Sans, Helvetica, Arial, sans-serif;\
                                        font-size: 30px;\
                                        text-align: center;\
                                        background-color: #8fddff;\
                                    }\
                                    h1 {\
                                        color: blue;\
                                    }\
                                    h2 {\
                                        margin-bottom: 30px;\
                                    }\
                                    input, select {\
                                        font-size: 4vh;\
                                        margin-bottom: 10px;\
                                    }\
                                </style>\
                            </head>\
                            <body>\
                                <h1>CONTROL</h1>\
                                <form action=\"\" method=\"post\">\
                                    <h2>Mode</h2>\
                                    <select name=\"mode\">\
                                        <option value=\"1\">1</option>\
                                        <option value=\"2\">2</option>\
                                        <option value=\"3\">3</option>\
                                        <option value=\"4\">4</option></select\
                                    ><br/>\
                                    <h2>Speed</h2>\
                                    <select name=\"speed\">\
                                        <option value=\"1\">Slow</option>\
                                        <option value=\"2\">Medium</option>\
                                        <option value=\"3\">Fast</option></select\
                                    ><br/>\
                                    <input type=\"submit\" class=\"button\" value=\"Submit\"/>\
                                </form>\
                            </body>\
                        </html>"
	};

	const httpd_uri_t uri_post = {
		.uri = "/",
		.method = HTTP_POST,
		.handler = post_handler,
		.user_ctx ="<!DOCTYPE html>\
                        <html>\
                            <head>\
                                <title>ESP32 controller</title>\
                                <meta charset=\"utf-8\"/>\
                                <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/>\
                                <style>\
                                    .button {\
                                        border: none;\
                                        border-radius: 15px;\
                                        padding: 15px 30px;\
                                        text-align: center;\
                                        font-size: 30px;\
                                        margin-top: 50px;\
                                        cursor: pointer;\
                                        color: white;\
                                        background-color: blue;\
                                    }\
                                    body {\
                                        font-family: Open Sans, Helvetica, Arial, sans-serif;\
                                        font-size: 30px;\
                                        text-align: center;\
                                        background-color: #8fddff;\
                                    }\
                                    h1 {\
                                        color: blue;\
                                    }\
                                    h2 {\
                                        margin-bottom: 30px;\
                                    }\
                                    input, select {\
                                        font-size: 4vh;\
                                        margin-bottom: 10px;\
                                    }\
                                </style>\
                            </head>\
                            <body>\
                                <h1>CONTROL</h1>\
                                <form action=\"\" method=\"post\">\
                                    <h2>Mode</h2>\
                                    <select name=\"mode\">\
                                        <option value=\"1\">1</option>\
                                        <option value=\"2\">2</option>\
                                        <option value=\"3\">3</option>\
                                        <option value=\"4\">4</option></select\
                                    ><br/>\
                                    <h2>Speed</h2>\
                                    <select name=\"speed\">\
                                        <option value=\"1\">Slow</option>\
                                        <option value=\"2\">Medium</option>\
                                        <option value=\"3\">Fast</option></select\
                                    ><br/>\
                                    <input type=\"submit\" class=\"button\" value=\"Submit\"/>\
                                </form>\
                            </body>\
                        </html>"
	};

	// Set URI handlers
	ESP_LOGI(TAG, "Registering URI handlers");
	httpd_register_uri_handler(server, &uri_get);
	httpd_register_uri_handler(server, &uri_post);
	return server;
}

/* QUEUE INIT */
void queue_init() {
	queue = xQueueCreate(20, sizeof(led_options_t));

	if (queue == NULL){
		printf("Failed to create queue\n");
		exit(EXIT_FAILURE);
	}
}

/* MAIN */
void app_main(void)
{
    gpio_pad_select_gpio(GPIO_LED1);
    gpio_pad_select_gpio(GPIO_LED2);
    gpio_pad_select_gpio(GPIO_LED3);
    gpio_pad_select_gpio(GPIO_RED);
    gpio_pad_select_gpio(GPIO_GREEN);
    gpio_pad_select_gpio(GPIO_BLUE);

    // Set the GPIO as a push/pull output
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_LED_RED, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_LED1, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_LED2, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_LED3, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_RED, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_GREEN, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_BLUE, GPIO_MODE_OUTPUT));

    // Initialize NVS
    ESP_ERROR_CHECK(nvs_flash_init());

    // Initialize Wifi
    wifi_init();

    // Initialize mDNS and set hostname
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("esp32"));

    // Initialize queue
    queue_init();

    // Start webserver
    start_webserver();

    xTaskCreate(&blink_task, "blink_task", 2048, NULL, 5, NULL);
}
