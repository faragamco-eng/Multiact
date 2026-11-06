/*
 * ui_seg.c - Display, LED, key and buzzer driver of the basic panel (hardware/panel).
 * NOT COMPILED HERE (no ESP-IDF). Pin map = hardware/panel/schematic.py sheet 2.
 *
 *   segments a..g,dp : IO4 IO5 IO6 IO7 IO15 IO16 IO8 IO9  (high = on, 150 R)
 *   phases           : IO10..IO13 digits 1..4, IO14 status LEDs (high = on, 2N7002)
 *   keys             : IO38 POWER, IO39 UP, IO40 DOWN, IO41 MODE, IO42 TIMER (low = pressed)
 *   buzzer           : IO21 (LEDC 2.7 kHz)
 * Multiplex 5 phases x 1 ms (200 Hz refresh, 20 % duty). Blink = 2 Hz on alarms.
 */
#include <string.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "seg7.h"
#include "ui_seg.h"

static const gpio_num_t SEG_PIN[8] = { 4, 5, 6, 7, 15, 16, 8, 9 };
static const gpio_num_t PHASE_PIN[5] = { 10, 11, 12, 13, 14 };
static const gpio_num_t KEY_PIN[5] = { 38, 39, 40, 41, 42 };
static volatile uint8_t frame[5];          /* segment bits per phase */
static uint8_t phase;

static void IRAM_ATTR mux_cb(void *arg)
{
    gpio_set_level(PHASE_PIN[phase], 0);   /* blank before changing segments (no ghosting) */
    phase = (uint8_t)((phase + 1u) % 5u);
    uint8_t s = frame[phase];
    for (int i = 0; i < 8; i++) gpio_set_level(SEG_PIN[i], (s >> i) & 1u);
    gpio_set_level(PHASE_PIN[phase], 1);
}

void ui_seg_init(void)
{
    uint64_t out = 0, in = 0;
    for (int i = 0; i < 8; i++) out |= 1ULL << SEG_PIN[i];
    for (int i = 0; i < 5; i++) { out |= 1ULL << PHASE_PIN[i]; in |= 1ULL << KEY_PIN[i]; }
    gpio_config_t o = { .pin_bit_mask = out, .mode = GPIO_MODE_OUTPUT };
    gpio_config_t k = { .pin_bit_mask = in, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&o); gpio_config(&k);
    ledc_timer_config_t t = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
                              .timer_num = LEDC_TIMER_0, .freq_hz = 2700, .clk_cfg = LEDC_AUTO_CLK };
    ledc_timer_config(&t);
    ledc_channel_config_t c = { .gpio_num = 21, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0,
                                .timer_sel = LEDC_TIMER_0, .duty = 0 };
    ledc_channel_config(&c);
    const esp_timer_create_args_t a = { .callback = mux_cb, .dispatch_method = ESP_TIMER_ISR, .name = "mux" };
    esp_timer_handle_t h;
    esp_timer_create(&a, &h);
    esp_timer_start_periodic(h, 1000);
}

void ui_seg_show(const ui_view_t *v, uint32_t now_ms)
{
    char txt[5];
    seg7_text(v, txt);
    bool dark = v->blink && (now_ms % 500u) < 150u;
    for (int i = 0; i < 4; i++) frame[i] = dark ? 0 : seg7_char(txt[i]);
    if (v->speed_step && v->timer_left_min && !dark) frame[1] |= SEG_DP;   /* dot = timer running */
    frame[4] = seg7_leds(v);
}

void ui_seg_beep(uint32_t ms)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    vTaskDelay(pdMS_TO_TICKS(ms));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/* returns a key event (debounced, 3 s = long press) or -1; *long_press set accordingly */
int ui_seg_key(uint32_t now_ms, bool *long_press)
{
    static uint32_t down_at[5];
    static bool down[5], long_sent[5];
    for (int i = 0; i < 5; i++) {
        bool p = gpio_get_level(KEY_PIN[i]) == 0;
        if (p && !down[i]) { down[i] = true; down_at[i] = now_ms; long_sent[i] = false; }
        if (p && down[i] && !long_sent[i] && now_ms - down_at[i] > 3000u) { long_sent[i] = true; *long_press = true; return i; }
        if (!p && down[i]) {
            down[i] = false;
            if (!long_sent[i] && now_ms - down_at[i] > 30u) { *long_press = false; return i; }
        }
    }
    return -1;
}
