/* Blink Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
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
#include "driver/gptimer.h"
#include "config.h"
#include "esp_http_client.h"

static const char *TAG = "udp_receiver";
#define UDP_PORT       49003
#define HEARTBEAT_PORT 49002
#define BUFFER_SIZE    1024
#define HEARTBEAT_INTERVAL 5000
#define LOG_PORT       9998
#define LOG_BUFFER_SIZE 1024

#define MOTOR_IN1 3
#define MOTOR_IN2 4
#define MOTOR_IN3 5
#define MOTOR_IN4 6
#define MOTOR_STEP_PERIOD_US 5000  // 5ms = 200 steps/second for full step mode
#define RESOLUTION_MODE 0  // 0=full step only (no half-stepping)

static float current_position = 0;
static int seq_idx = 0;

// Motor state for hardware timer control
typedef struct {
    int target_angle;
    int steps_remaining;
    int direction;  // 1 or -1
    bool active;
} motor_state_t;

static motor_state_t motor_state = {0};

// Sequence for full-step mode only
static const uint8_t SEQUENCE_FULL[4][4] = {
    {1, 1, 0, 0},
    {0, 1, 1, 0},
    {0, 0, 1, 1},
    {1, 0, 0, 1},
};

// Calibration points: value (knots) -> angle (degrees)
typedef struct {
    int value;
    int angle;
} cal_point_t;

// Airspeed: 0-200 knots
static const cal_point_t calibration[10] = {
    {40,    32},     // 40 knots at 32° (minimum)
    {50,    52},
    {60,    72},
    {70,    94},
    {80,    116},
    {90,    138},
    {100,   161},
    {110,   182},
    {120,   203},
    {200,   315},    // 200 knots at 315° (maximum)
};

static const int calibration_count = 10;

// Convert airspeed value to motor angle using calibration points
static int value_to_angle(int value)
{
    // Clamp to calibration range
    if (value <= calibration[0].value) {
        return calibration[0].angle;
    }
    if (value >= calibration[calibration_count - 1].value) {
        return calibration[calibration_count - 1].angle;
    }
    
    // Find surrounding calibration points and interpolate
    for (int i = 0; i < calibration_count - 1; i++) {
        if (value >= calibration[i].value && value <= calibration[i + 1].value) {
            int v1 = calibration[i].value;
            int v2 = calibration[i + 1].value;
            int a1 = calibration[i].angle;
            int a2 = calibration[i + 1].angle;
            
            // Linear interpolation
            float ratio = (float)(value - v1) / (v2 - v1);
            int angle = (int)(a1 + ratio * (a2 - a1));
            return angle;
        }
    }
    
    return calibration[0].angle;
}

// Timer interrupt handler for motor stepping
static bool motor_timer_callback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    if (!motor_state.active || motor_state.steps_remaining <= 0) {
        return false;  // Don't re-arm
    }
    
    // Perform one step
    int seq_len = 4;  // Full step mode only
    const uint8_t (*sequence)[4] = SEQUENCE_FULL;
    
    gpio_set_level(MOTOR_IN1, sequence[seq_idx][0]);
    gpio_set_level(MOTOR_IN2, sequence[seq_idx][1]);
    gpio_set_level(MOTOR_IN3, sequence[seq_idx][2]);
    gpio_set_level(MOTOR_IN4, sequence[seq_idx][3]);
    
    // Update sequence index
    if (motor_state.direction > 0) {
        seq_idx = (seq_idx + 1) % seq_len;
    } else {
        seq_idx = (seq_idx - 1 + seq_len) % seq_len;
    }
    
    motor_state.steps_remaining--;
    
    if (motor_state.steps_remaining <= 0) {
        motor_state.active = false;
        current_position = motor_state.target_angle;
        ESP_LOGI(TAG, "Motor reached target: %d°", motor_state.target_angle);
        return false;  // Stop timer
    }
    
    return true;  // Keep timer running
}

static gptimer_handle_t motor_timer = NULL;

static void motor_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MOTOR_IN1) | (1ULL << MOTOR_IN2) | (1ULL << MOTOR_IN3) | (1ULL << MOTOR_IN4),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(MOTOR_IN1, 0);
    gpio_set_level(MOTOR_IN2, 0);
    gpio_set_level(MOTOR_IN3, 0);
    gpio_set_level(MOTOR_IN4, 0);
    
    // Configure hardware timer
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,  // 1 MHz for microsecond precision
    };
    
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &motor_timer));
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = MOTOR_STEP_PERIOD_US,  // Set alarm period
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(motor_timer, &alarm_config));
    
    gptimer_event_callbacks_t cbs = {
        .on_alarm = motor_timer_callback,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(motor_timer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(motor_timer));
    
    ESP_LOGI(TAG, "Motor timer initialized with %d µs step period", MOTOR_STEP_PERIOD_US);
}

typedef struct {
    int target_angle;
    int min_angle;
    int max_angle;
} motor_cmd_t;

static void motor_move_to(int target_angle, int min_angle, int max_angle)
{
    // Clamp target to range
    if (target_angle < min_angle) target_angle = min_angle;
    if (target_angle > max_angle) target_angle = max_angle;
    
    int current = (int)current_position;
    int diff = target_angle - current;
    
    if (diff == 0) {
        ESP_LOGI(TAG, "Motor already at target: %d°", target_angle);
        return;
    }
    
    // Full step mode: 2048 steps per 360 degrees
    int steps = (int)(abs(diff) / (360.0 / 2048));
    int direction = (diff >= 0) ? 1 : -1;
    
    ESP_LOGI(TAG, "Motor START: current=%d°, target=%d° (diff: %d°, steps: %d, dir: %s)", 
             current, target_angle, diff, steps, (direction > 0) ? "CW" : "CCW");
    
    // Stop any existing movement
    if (motor_state.active) {
        ESP_ERROR_CHECK(gptimer_stop(motor_timer));
        motor_state.active = false;
    }
    
    // Set up new movement
    motor_state.target_angle = target_angle;
    motor_state.steps_remaining = steps;
    motor_state.direction = direction;
    motor_state.active = true;
    
    // Reset timer count and start
    ESP_ERROR_CHECK(gptimer_set_raw_count(motor_timer, 0));
    ESP_ERROR_CHECK(gptimer_start(motor_timer));
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

static int wifi_log_vprintf(const char *fmt, va_list args)
{
    static char log_buffer[LOG_BUFFER_SIZE];
    int len = vsnprintf(log_buffer, LOG_BUFFER_SIZE - 1, fmt, args);
    
    if (log_socket >= 0 && len > 0) {
        send(log_socket, (uint8_t *)log_buffer, len, 0);
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
            ESP_LOGI(TAG, "WiFi logging client connected: %s", inet_ntoa(client_addr.sin_addr));
            
            // Keep connection open until client disconnects
            char dummy[256];
            while (recv(client_socket, dummy, sizeof(dummy), 0) > 0) {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            
            ESP_LOGI(TAG, "WiFi logging client disconnected");
            close(client_socket);
            log_socket = -1;
        }
    }
}

static void heartbeat_task(void *pvParameters)
{
    esp_task_wdt_add(NULL);
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
    
    ESP_LOGI(TAG, "Heartbeat task started, sending to %s:%d", RPI_IP, HEARTBEAT_PORT);

    char heartbeat_msg[64];
    int heartbeat_count = 0;

    while (1) {
        esp_task_wdt_reset();
        uint32_t uptime = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
        snprintf(heartbeat_msg, sizeof(heartbeat_msg), "HEARTBEAT:%s:%lu", ESP_ID, (unsigned long)uptime);
        
        int ret = sendto(sock, heartbeat_msg, strlen(heartbeat_msg), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (ret < 0) {
            ESP_LOGW(TAG, "Heartbeat send failed: errno %d, to %s:%d", errno, RPI_IP, HEARTBEAT_PORT);
        } else {
            heartbeat_count++;
            if (heartbeat_count % 6 == 0) {  // Log every 30 seconds (6 x 5 second intervals)
                ESP_LOGI(TAG, "Heartbeat sent (%d sent, msg: %s)", heartbeat_count, heartbeat_msg);
            }
        }
        
        esp_task_wdt_reset();
        vTaskDelay(HEARTBEAT_INTERVAL / portTICK_PERIOD_MS);
    }
}

static void udp_receiver_task(void *pvParameters)
{
    char rx_buffer[BUFFER_SIZE];
    struct sockaddr_in dest_addr;
    
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(UDP_PORT);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Socket bound, listening on port %d", UDP_PORT);

    while (1) {
        struct sockaddr_in source_addr;
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);
        
        if (len < 0) {
            ESP_LOGW(TAG, "recvfrom failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        rx_buffer[len] = 0;
        ESP_LOGI(TAG, "Received: %s", rx_buffer);
        
        if (strncmp(rx_buffer, "VALUE:", 6) == 0) {
            int motor_id = 0, value = 0;
            if (sscanf(&rx_buffer[6], "%d:%d", &motor_id, &value) == 2 || sscanf(&rx_buffer[6], "%d", &value) == 1) {
                if (sscanf(&rx_buffer[6], "%d:%d", &motor_id, &value) != 2) {
                    motor_id = 0;  // Default to motor 0 if not specified
                }
                int angle = value_to_angle(value);
                ESP_LOGI(TAG, "Motor %d: Converted value %d to angle %d degrees", motor_id, value, angle);
                motor_move_to(angle, 0, 360);
            } else {
                ESP_LOGW(TAG, "Failed to parse value from: %s", &rx_buffer[6]);
            }
        } else if (strncmp(rx_buffer, "ANGLE:", 6) == 0) {
            int motor_id = 0, angle = 0;
            if (sscanf(&rx_buffer[6], "%d:%d", &motor_id, &angle) == 2 || sscanf(&rx_buffer[6], "%d", &angle) == 1) {
                if (sscanf(&rx_buffer[6], "%d:%d", &motor_id, &angle) != 2) {
                    motor_id = 0;  // Default to motor 0 if not specified
                }
                ESP_LOGI(TAG, "Motor %d: Parsed angle: %d degrees", motor_id, angle);
                motor_move_to(angle, 0, 360);
            } else {
                ESP_LOGW(TAG, "Failed to parse angle from: %s", &rx_buffer[6]);
            }
        } else if (strncmp(rx_buffer, "MOVE:", 5) == 0) {
            int motor_id = 0, angle = 0, min_angle = 0, max_angle = 360;
            sscanf(&rx_buffer[5], "%d:%d:%d:%d", &motor_id, &angle, &min_angle, &max_angle);
            ESP_LOGI(TAG, "Motor %d -> %d degrees (range: %d-%d)", motor_id, angle, min_angle, max_angle);
            motor_move_to(angle, min_angle, max_angle);
        } else if (strncmp(rx_buffer, "ZERO:", 5) == 0) {
            current_position = 0;
            seq_idx = 0;
            ESP_LOGI(TAG, "Motor zeroed to 0 degrees");
        }
    }

    close(sock);
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting UDP receiver on port %d", UDP_PORT);
    
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 60000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true,
    };
    esp_task_wdt_init(&wdt_config);
    
    motor_init();
    wifi_init();
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    
    // Start WiFi logging before other tasks
    xTaskCreate(wifi_logging_task, "wifi_log", 4096, NULL, 2, NULL);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    esp_log_set_vprintf(wifi_log_vprintf);
    
    xTaskCreate(heartbeat_task, "heartbeat", 4096, NULL, 5, NULL);
    xTaskCreate(udp_receiver_task, "udp_receiver", 8192, NULL, 3, NULL);
    
    // Initialize needle to 0° at startup
    vTaskDelay(100 / portTICK_PERIOD_MS);  // Brief delay to let motor timer start
    motor_move_to(0, 0, 360);
    vTaskDelay(2000 / portTICK_PERIOD_MS);  // Wait for movement to complete
    ESP_LOGI(TAG, "Initialization complete. Needle at 0°, ready for commands.");
    
    // Main task just sleeps, no need to monitor it
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

#ifdef CONFIG_INSTRUMENT_AIRSPEED

// Airspeed-specific implementation only compiles when CONFIG_INSTRUMENT_AIRSPEED=y

#endif // CONFIG_INSTRUMENT_AIRSPEED