/*
 * LED blink with FreeRTOS
 */
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include "ssd1306.h"
#include "gfx.h"

#include "pico/stdlib.h"
#include <stdio.h>

const uint BTN_1_OLED = 28;
const uint LED_1_OLED = 20;
const uint LED_2_OLED = 21;
const uint LED_3_OLED = 22;

int TRIG_PIN = 17;
int ECHO_PIN = 16;

QueueHandle_t xQueueDistance;
QueueHandle_t xQueueTime;
SemaphoreHandle_t xSemaphoreTrigger;

typedef struct {
    int time_init;
    int time_end;
    int64_t last_valid_read_time;
} Time;

// pin_callback: Função callback do pino do echo.
void gpio_callback(uint gpio, uint32_t events) {
    static int time_init;
    Time time;
    if (events == 0x8) {
        time_init = to_us_since_boot(get_absolute_time());
    } else if (events == 0x4) {
        time.time_init = time_init;
        time.time_end = to_us_since_boot(get_absolute_time());
        time.last_valid_read_time = time.time_end;
        xQueueSendFromISR(xQueueTime, &time, NULL);
    }
}

// trigger_task: Task responsável por gerar o trigger.
void trigger_task(void *p) {
    gpio_init(TRIG_PIN);
    gpio_set_dir(TRIG_PIN, GPIO_OUT);

    while (1) {
        xSemaphoreTake(xSemaphoreTrigger, portMAX_DELAY);
        gpio_put(TRIG_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_put(TRIG_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

// echo_task: Task que faz a leitura do tempo que o pino echo ficou levantado.
void echo_task(void *p) {
    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(ECHO_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

    Time time;
    time.last_valid_read_time = 0;
    int distance;
    while (1) {
        int64_t current_time = to_us_since_boot(get_absolute_time());
        if (xQueueReceive(xQueueTime, &time, 0)) {
            if (time.time_end > time.time_init) {
                distance = (time.time_init - time.time_end) / 58;
                distance = abs(distance);
                if (distance > 200) {
                    distance = 200;
                }
                xQueueSend(xQueueDistance, &distance, portMAX_DELAY);
            }
        } else {
            time.last_valid_read_time = current_time;
            printf("Erro\n");
            distance = -1;
            xQueueSend(xQueueDistance, &distance, portMAX_DELAY);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// oled_task: Task que exibe a informação da distancia no display. Faz uso de dois recursos, xSemaphoreTrigger e xQueueDistance
void oled_task(void *p) {
    ssd1306_init();
    ssd1306_t disp;
    gfx_init(&disp, 128, 32);

    int distance = 0;
    int previous_distance = 0;
    char str[20];
    while (1) {
        if (1){
            xSemaphoreGive(xSemaphoreTrigger);
            xQueueReceive(xQueueDistance, &distance, portMAX_DELAY);
        
            int steps = abs(distance - previous_distance);
            for (int i = 0; i <= steps; i++){
                int current_distance = previous_distance + ((distance - previous_distance) * i / steps);
                gfx_clear_buffer(&disp);
                if (distance == -1 || distance >= 200) {
                    sprintf(str, "Erro");
                    gfx_draw_string(&disp, 0, 10, 1, str);
                    gfx_draw_line(&disp, 15, 27, 200, 27);
                } 
                else {
                    sprintf(str, "Distancia: %d cm", current_distance);
                    gfx_draw_string(&disp, 0, 0, 1, str);
                    gfx_draw_string(&disp, 0, 10, 1, "");
                    gfx_draw_line(&disp, 15, 27, 20 + current_distance, 27);
                }
                gfx_show(&disp);
                vTaskDelay(pdMS_TO_TICKS(5));
            }
            previous_distance = distance;
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            gfx_clear_buffer(&disp);
            gfx_show(&disp);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


int main() {
    stdio_init_all();

    xQueueDistance = xQueueCreate(1, sizeof(int));
    xQueueTime = xQueueCreate(1, sizeof(Time));
    xSemaphoreTrigger = xSemaphoreCreateBinary();

    xTaskCreate(trigger_task, "Trigger", 4095, NULL, 1, NULL);
    xTaskCreate(echo_task, "Echo", 4095, NULL, 1, NULL);
    xTaskCreate(oled_task, "OLED", 4095, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true)
        ;
}