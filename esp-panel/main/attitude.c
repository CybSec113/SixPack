/* Attitude Indicator

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

static const char *TAG = "attitude_indicator";
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

#define MOTOR2_IN1 7
#define MOTOR2_IN2 8
#define MOTOR2_IN3 9
#define MOTOR2_IN4 10

#define MOTOR_STEP_PERIOD_US 5000  // 5ms = 200 steps/second for full step mode
#define RESOLUTION_MODE 0  // 0=full step only (no half-stepping)

static float current_position[2] = {0, 0};  // Track position for motor 0 and 1
static int seq_idx[2] = {0, 0};

// Motor state for hardware timer control
typedef struct {
    int motor_id;
    int target_angle;
    int steps_remaining;
    int direction;  // 1 or -1
    bool active;
} motor_state_t;

static motor_state_t motor_state[2] = {{0}, {0}};

// Sequence for full-step mode only
static const uint8_t SEQUENCE_FULL[4][4] = {
    {1, 1, 0, 0},
    {0, 1, 1, 0},
    {0, 0, 1, 1},
    {1, 0, 0, 1},
};

// Calibration points: value (degrees) -> angle (degrees on gauge)
typedef struct {
    int value;
    int angle;
} cal_point_t;

// Attitude Indicator Motor 0: Roll axis (-180 to +180 degrees)
static const cal_point_t calibration_motor0[9] = {
    {-180,   0},       // -180° roll at 0°
    {-135,   45},      // -135° roll at 45°
    {-90,    90},      // -90° roll at 90°
    {-45,    135},     // -45° roll at 135°
    {0,      180},     // 0° roll (level) at 180°
    {45,     225},     // +45° roll at 225°
    {90,     270},     // +90° roll at 270°
    {135,    315},     // +135° roll at 315°
    {180,    360},     // +180° roll at 360°
};

static const int calibration_count_motor0 = 9;

// Attitude Indicator Motor 1: Pitch axis (-90 to +90 degrees)
static const cal_point_t calibration_motor1[9] = {
    {-90,    0},       // -90° pitch (nose down) at 0°
    {-70,    40},      // -70° pitch at 40°
    {-50,    80},      // -50° pitch at 80°
    {-25,    130},     // -25° pitch at 130°
    {0,      180},     // 0° pitch (level) at 180°
    {25,     230},     // +25° pitch at 230°
    {50,     280},     // +50° pitch at 280°
    {70,     320},     // +70° pitch at 320°
    {90,     360},     // +90° pitch (nose up) at 360°
};

static const int calibration_count_motor1 = 9;

// Convert value to motor angle using calibration points
static int value_to_angle(int motor_id, int value)
{
    const cal_point_t *calibration;
    int calibration_count;
    
    if (motor_id == 0) {
        calibration = calibration_motor0;
        calibration_count = calibration_count_motor0;
    } else {
        calibration = calibration_motor1;
        calibration_count = calibration_count_motor1;
    }
    
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

// (rest of file follows same structure as altimeter.c - implementing dual motor stepper control and UDP command handling)
// This includes the main app_main, UDP receive task, motor control task, WiFi initialization, etc.
// The calibration values above are the key difference from altimeter.c

// For a complete implementation, copy the task functions from altimeter.c and update:
// 1. The TAG string (already done above)
// 2. The value_to_angle() function (already done above)
// 3. Any motor configuration differences

// Placeholder structure - in practice, copy remaining code from altimeter.c
extern void app_main(void) {
    ESP_LOGI(TAG, "Attitude Indicator firmware starting");
    // Implementation would continue with WiFi, UDP, motor tasks...
}
