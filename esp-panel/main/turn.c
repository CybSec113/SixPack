/* Turn Coordinator/Indicator

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

static const char *TAG = "turn_indicator";
#define UDP_PORT       49003
#define HEARTBEAT_PORT 49002
#define BUFFER_SIZE    1024
#define HEARTBEAT_INTERVAL 5000
#define LOG_PORT       9999
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

// Calibration points: value (turn rate in degrees per second) -> angle (degrees)
typedef struct {
    int value;
    int angle;
} cal_point_t;

// Turn Coordinator: -3 to +3 degrees per second turn rate
// Needle deflects to show turn direction and rate
static const cal_point_t calibration[7] = {
    {-3,     0},       // -3 deg/sec (full left) at 0°
    {-2,     60},      // -2 deg/sec at 60°
    {-1,     120},     // -1 deg/sec at 120°
    {0,      180},     // 0 deg/sec (centered) at 180°
    {1,      240},     // +1 deg/sec at 240°
    {2,      300},     // +2 deg/sec at 300°
    {3,      360},     // +3 deg/sec (full right) at 360°
};

static const int calibration_count = 7;

// Convert turn rate value to motor angle using calibration points
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

// (rest of file follows same structure as airspeed.c - implementing single motor stepper control and UDP command handling)
// This includes the main app_main, UDP receive task, motor control task, WiFi initialization, etc.
// The calibration values above are the key difference from airspeed.c

// Placeholder structure - in practice, copy remaining code from airspeed.c and update:
// 1. The TAG string (already done above)
// 2. The value_to_angle() function (already done above)

// For a complete implementation, copy remaining code from airspeed.c with minor adjustments
extern void app_main(void) {
    ESP_LOGI(TAG, "Turn Indicator firmware starting");
    // Implementation would continue with WiFi, UDP, motor tasks...
}
