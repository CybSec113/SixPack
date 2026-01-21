#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include "rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/netif.h"
#include "driver/gpio.h"
#include "config.h"

static const char *TAG = "inputs";
#define HEARTBEAT_PORT 49002
#define COMMAND_PORT   49003
#define LOG_PORT       9997
#define LOG_BUFFER_SIZE 1024
#define ENCODER_PORT   49004

// EC11 Encoder configurations
#define EC11_HDG_BUG_CLK   2  // Button input
#define EC11_HDG_BUG_DT    3
#define EC11_HDG_BUG_BTN   10

typedef struct {
    const char *name;
    int pin_clk;
    int pin_dt;
    int pin_btn;
    int value;
    int last_clk_state;
    int last_btn_state;
    int btn_debounce_count;
    bool button_pressed;
} encoder_t;

static encoder_t encoders[] = {
    {
        .name = "EC11_HdgBug",
        .pin_clk = EC11_HDG_BUG_CLK,
        .pin_dt = EC11_HDG_BUG_DT,
        .pin_btn = EC11_HDG_BUG_BTN,
        .value = 0,
        .last_clk_state = 0,
        .last_btn_state = 1,
        .btn_debounce_count = 0,
        .button_pressed = false,
    },
};

static const int encoder_count = sizeof(encoders) / sizeof(encoder_t);

static void encoder_init(void)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    for (int i = 0; i < encoder_count; i++) {
        io_conf.pin_bit_mask = (1ULL << encoders[i].pin_clk) | 
                               (1ULL << encoders[i].pin_dt) | 
                               (1ULL << encoders[i].pin_btn);
        gpio_config(&io_conf);
        encoders[i].last_clk_state = gpio_get_level(encoders[i].pin_clk);
        encoders[i].last_btn_state = gpio_get_level(encoders[i].pin_btn);
    }
    
    // Verify GPIO4 specifically
    int gpio_btn = gpio_get_level(EC11_HDG_BUG_BTN);
    int clk_level = gpio_get_level(EC11_HDG_BUG_CLK);
    int dt_level = gpio_get_level(EC11_HDG_BUG_DT);
    
    ESP_LOGI(TAG, "GPIO init: BTN(GPIO%d)=%d CLK(GPIO%d)=%d DT(GPIO%d)=%d", 
        EC11_HDG_BUG_BTN, gpio_btn, EC11_HDG_BUG_CLK, clk_level, EC11_HDG_BUG_DT, dt_level);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static int log_socket = -1;

// Keep reference to default vprintf for fallback
static vprintf_like_t default_vprintf = NULL;

static int wifi_log_vprintf(const char *fmt, va_list args)
{
    static char log_buffer[LOG_BUFFER_SIZE];
    int len = vsnprintf(log_buffer, LOG_BUFFER_SIZE - 1, fmt, args);
    
    // Send to WiFi if connected
    if (log_socket >= 0 && len > 0) {
        send(log_socket, (uint8_t *)log_buffer, len, 0);
    }
    
    // Always also send to default (USB/UART) for debugging
    if (default_vprintf && len > 0) {
        default_vprintf(fmt, args);
    }
    
    return len;
}

static void wifi_logging_task(void *pvParameters)
{
    struct sockaddr_in server_addr;
    int server_socket;
    
    server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket < 0) {
        ESP_LOGE(TAG, "Failed to create log server socket");
        vTaskDelete(NULL);
        return;
    }
    
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Set non-blocking mode
    int flags = fcntl(server_socket, F_GETFL, 0);
    fcntl(server_socket, F_SETFL, flags | O_NONBLOCK);
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(LOG_PORT);
    
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind log server socket");
        close(server_socket);
        vTaskDelete(NULL);
        return;
    }
    
    listen(server_socket, 1);
    ESP_LOGI(TAG, "WiFi logging server listening on port %d", LOG_PORT);
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket >= 0) {
            log_socket = client_socket;
            // Set client to non-blocking
            flags = fcntl(client_socket, F_GETFL, 0);
            fcntl(client_socket, F_SETFL, flags | O_NONBLOCK);
            ESP_LOGI(TAG, "WiFi logging client connected");
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void heartbeat_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Heartbeat task started");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr((const char *)RPI_IP);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(HEARTBEAT_PORT);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create heartbeat socket");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Heartbeat sending to %s:%d", RPI_IP, HEARTBEAT_PORT);

    char heartbeat_msg[64];

    while (1) {
        uint32_t uptime = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
        snprintf(heartbeat_msg, sizeof(heartbeat_msg), "HEARTBEAT:%s:%lu", ESP_ID, (unsigned long)uptime);
        
        int ret = sendto(sock, heartbeat_msg, strlen(heartbeat_msg), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (ret < 0) {
            ESP_LOGW(TAG, "Heartbeat send failed: errno %d", errno);
        } else {
            ESP_LOGI(TAG, "Heartbeat OK");
        }
        
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

static void encoder_task(void *pvParameters)
{
    ESP_LOGI(TAG, "encoder_task: starting");
    
    // Shorter delay in chunks to avoid watchdog
    for (int i = 0; i < 3; i++) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "encoder_task: delay %d/3", i+1);
    }
    ESP_LOGI(TAG, "encoder_task: WiFi wait done");
    
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Encoder socket failed");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "encoder_task: socket created");
    
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    
    struct sockaddr_in rpi_addr = {0};
    rpi_addr.sin_addr.s_addr = inet_addr((const char *)RPI_IP);
    rpi_addr.sin_family = AF_INET;
    rpi_addr.sin_port = htons(ENCODER_PORT);
    ESP_LOGI(TAG, "encoder_task: address configured");
    
    int last_button_state[encoder_count];
    int last_clk_state[encoder_count];
    for (int i = 0; i < encoder_count; i++) {
        last_button_state[i] = 1;  // Initialize to released (GPIO HIGH at rest)
        last_clk_state[i] = gpio_get_level(encoders[i].pin_clk);
    }
    
    ESP_LOGI(TAG, "Encoder monitoring active");
    
    while (1) {
        for (int i = 0; i < encoder_count; i++) {
            int clk_state = gpio_get_level(encoders[i].pin_clk);
            int dt_state = gpio_get_level(encoders[i].pin_dt);
            int btn_raw = gpio_get_level(encoders[i].pin_btn);
            bool btn_state = (btn_raw == 0);  // GPIO LOW = pressed, HIGH = released
            
            if (last_clk_state[i] == 1 && clk_state == 0) {
                // CLK falling edge detected - read DT state immediately
                dt_state = gpio_get_level(encoders[i].pin_dt);
                
                if (dt_state == 1) {
                    encoders[i].value++;
                } else {
                    encoders[i].value--;
                }
                
                char msg[64];
                snprintf(msg, sizeof(msg), "ENCODER:%s:%d:%s", 
                    encoders[i].name, encoders[i].value, 
                    btn_state ? "PRESSED" : "released");
                sendto(sock, msg, strlen(msg), 0, 
                    (struct sockaddr *)&rpi_addr, sizeof(rpi_addr));
            }
            
            if (btn_state != last_button_state[i]) {
                // Button state changed - confirm it after minimal debounce
                btn_raw = gpio_get_level(encoders[i].pin_btn);
                bool btn_new = (btn_raw == 0);  // LOW = pressed
                
                if (btn_new != last_button_state[i]) {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "ENCODER:%s:%d:%s", 
                        encoders[i].name, encoders[i].value, 
                        btn_new ? "PRESSED" : "released");
                    sendto(sock, msg, strlen(msg), 0, 
                        (struct sockaddr *)&rpi_addr, sizeof(rpi_addr));
                    last_button_state[i] = btn_new;
                    encoders[i].last_btn_state = btn_new;
                }
            }
            
            last_clk_state[i] = clk_state;
            encoders[i].last_clk_state = clk_state;
            encoders[i].button_pressed = btn_state;
        }
        
        // Poll every 1ms instead of 10ms for more responsive encoder reading
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    close(sock);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Inputs ESP starting");
    
    encoder_init();
    wifi_init();
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    
    xTaskCreate(wifi_logging_task, "wifi_log", 4096, NULL, 2, NULL);
    xTaskCreate(heartbeat_task, "heartbeat", 4096, NULL, 3, NULL);
    xTaskCreate(encoder_task, "encoder", 4096, NULL, 3, NULL);
    
    // Save default vprintf before override, then set dual logging
    default_vprintf = esp_log_set_vprintf(wifi_log_vprintf);
    ESP_LOGI(TAG, "Ready");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

#ifdef CONFIG_INSTRUMENT_INPUTS
// Inputs-specific implementation only compiles when CONFIG_INSTRUMENT_INPUTS=y
#endif // CONFIG_INSTRUMENT_INPUTS
