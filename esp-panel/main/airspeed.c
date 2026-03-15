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
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "config.h"
#include "esp_http_client.h"

static const char *TAG = "udp_receiver";
#define UDP_PORT       49003
#define HEARTBEAT_PORT 49002
#define BUFFER_SIZE    1024
#define HEARTBEAT_INTERVAL 5000
#define LOG_PORT       9999
#define LOG_BUFFER_SIZE 1024

#define I2C_SDA         5
#define I2C_SCL         6
#define I2C_ADDR        0x3C
#define LCD_W           72
#define LCD_H           40
#define LCD_X_GAP       28
#define LCD_Y_GAP       14
#define LCD_BUF_SIZE    (LCD_W * LCD_H / 8)

#define MOTOR_IN1 7
#define MOTOR_IN2 8
#define MOTOR_IN3 9
#define MOTOR_IN4 10
#define MOTOR_STEP_PERIOD_US 5000  // 5ms = 200 steps/second for full step mode
#define RESOLUTION_MODE 0  // 0=full step only (no half-stepping)

static int current_position = 0;  // Track actual motor position in degrees
static int total_steps_from_zero = 0;  // Total steps taken from 0°

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
static const cal_point_t calibration[7] = {
    {40,    32},     // 40 knots at 32° (minimum)
    {60,    72},
    {80,    116},
    {100,   161},
    {120,   203},
    {160,   265},
    {200,   315},    // 200 knots at 315° (maximum)
};

static const int calibration_count = 7;

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
    const uint8_t (*sequence)[4] = SEQUENCE_FULL;
    
    int seq_idx = total_steps_from_zero % 4;
    if (seq_idx < 0) seq_idx += 4;
    
    gpio_set_level(MOTOR_IN1, sequence[seq_idx][0]);
    gpio_set_level(MOTOR_IN2, sequence[seq_idx][1]);
    gpio_set_level(MOTOR_IN3, sequence[seq_idx][2]);
    gpio_set_level(MOTOR_IN4, sequence[seq_idx][3]);
    
    motor_state.steps_remaining--;
    total_steps_from_zero += motor_state.direction;
    
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

static int log_socket = -1;
static esp_lcd_panel_handle_t oled_panel = NULL;
static char esp_ip_addr[32] = "";

static void motor_move_to(int target_angle, int min_angle, int max_angle)
{
    // Clamp target to range
    if (target_angle < min_angle) target_angle = min_angle;
    if (target_angle > max_angle) target_angle = max_angle;
    
    // Calculate target steps from zero (absolute position)
    int target_steps = (target_angle * 2048) / 360;
    int diff_steps = target_steps - total_steps_from_zero;
    
    if (diff_steps == 0) {
        ESP_LOGI(TAG, "Motor already at target: %d°", target_angle);
        return;
    }
    
    int direction = (diff_steps >= 0) ? 1 : -1;
    int steps = abs(diff_steps);
    
    ESP_LOGI(TAG, "Motor START: current=%d° (%d steps), target=%d° (%d steps), diff=%d steps, dir=%s", 
             current_position, total_steps_from_zero, target_angle, target_steps, diff_steps, (direction > 0) ? "CW" : "CCW");
    
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

static void set_pixel(uint8_t *buf, int x, int y, bool on)
{
    if (x < 0 || x >= LCD_W || y < 0 || y >= LCD_H) return;
    int idx = (y / 8) * LCD_W + x;
    if (on) buf[idx] |=  (1 << (y % 8));
    else    buf[idx] &= ~(1 << (y % 8));
}

static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00}, {0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62}, {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00}, {0x00,0x41,0x22,0x1C,0x00}, {0x14,0x08,0x3E,0x08,0x14}, {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x60,0x60,0x00,0x00}, {0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}, {0x00,0x36,0x36,0x00,0x00}, {0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00}, {0x14,0x14,0x14,0x14,0x14}, {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E}, {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01}, {0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40}, {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63}, {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
};

static void draw_char(uint8_t *buf, int x, int y, char c)
{
    if (c < 32 || c > 90) c = 32;
    const uint8_t *g = font5x7[c - 32];
    for (int col = 0; col < 5; col++)
        for (int row = 0; row < 7; row++)
            set_pixel(buf, x + col, y + row, (g[col] >> row) & 1);
}

static void draw_string(uint8_t *buf, int x, int y, const char *s)
{
    while (*s) { draw_char(buf, x, y, *s++); x += 6; }
}

static void oled_display(const char *l1, const char *l2, const char *l3)
{
    if (!oled_panel) return;
    static uint8_t buf[LCD_BUF_SIZE];
    memset(buf, 0, sizeof(buf));
    if (l1) draw_string(buf, 1,  1, l1);
    if (l2) draw_string(buf, 4, 12, l2);
    if (l3) draw_string(buf, 16, 23, l3);
    esp_lcd_panel_draw_bitmap(oled_panel, 0, 0, LCD_W, LCD_H, buf);
}

static void init_oled(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .glitch_ignore_cnt = 7,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = I2C_ADDR,
        .scl_speed_hz = 400000,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io));

    esp_lcd_panel_dev_config_t panel_cfg = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io, &panel_cfg, &oled_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(oled_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(oled_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(oled_panel, true));

    static uint8_t blank[128 * 64 / 8];
    memset(blank, 0, sizeof(blank));
    esp_lcd_panel_set_gap(oled_panel, 0, 0);
    esp_lcd_panel_draw_bitmap(oled_panel, 0, 0, 128, 64, blank);
    esp_lcd_panel_set_gap(oled_panel, LCD_X_GAP, LCD_Y_GAP);
    
    ESP_LOGI(TAG, "OLED initialized");
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
        snprintf(esp_ip_addr, sizeof(esp_ip_addr), IPSTR, IP2STR(&event->ip_info.ip));
        
        char *dot = strchr(esp_ip_addr, '.');
        char line2[18] = {0}, line3[18] = {0};
        if (dot) {
            int first_part_len = dot - esp_ip_addr;
            strncpy(line2, esp_ip_addr, first_part_len + 4);
            snprintf(line3, sizeof(line3), "  %s", dot + 4);
        }
        oled_display("AIRSPEED", line2, line3);
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
            total_steps_from_zero = 0;
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
    init_oled();
    oled_display("AIRSPEED", "CONNECTING", NULL);
    wifi_init();
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    
    // Start WiFi logging before other tasks
    xTaskCreate(wifi_logging_task, "wifi_log", 4096, NULL, 2, NULL);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    esp_log_set_vprintf(wifi_log_vprintf);
    
    xTaskCreate(heartbeat_task, "heartbeat", 4096, NULL, 5, NULL);
    xTaskCreate(udp_receiver_task, "udp_receiver", 8192, NULL, 3, NULL);
    
    // Don't move the needle on startup - just set internal position
    current_position = 0;
    total_steps_from_zero = 0;
    ESP_LOGI(TAG, "Initialization complete. Ready for commands.");
    
    // Main task just sleeps, no need to monitor it
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

#ifdef CONFIG_INSTRUMENT_AIRSPEED

// Airspeed-specific implementation only compiles when CONFIG_INSTRUMENT_AIRSPEED=y

#endif // CONFIG_INSTRUMENT_AIRSPEED