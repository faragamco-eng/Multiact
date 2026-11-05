/*
 * main.c - Application: ISR glue, bus pre-charge, user interface.
 *
 * UART commands (115200 8N1, through the isolated interface):
 *   run | stop | clr | spd <rpm> | dir <0|1> | pot <0|1> | ?
 * Telemetry line every 200 ms:
 *   st=<state> f=<faults hex> rpm=<est> ref=<ref> d=<duty %> i=<mA> vb=<V> T=<C> zc=<n> fc=<n>
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "app_config.h"
#include "bsp.h"
#include "sixstep.h"
#include "stm32g4xx.h"

static ss_t motor;
static volatile uint32_t ms_ticks;
static bool pot_mode = true;

void bsp_adc_isr_read(ss_meas_t *m);

/* ---------------- interrupt handlers ---------------- */
void ADC1_2_IRQHandler(void)
{
    ss_meas_t m;
    bsp_adc_isr_read(&m);
    ss_on_adc(&motor, &m, bsp_now_us());
}

void TIM2_IRQHandler(void)
{
    if ((TIM2->SR & TIM_SR_CC1IF) && (TIM2->DIER & TIM_DIER_CC1IE)) {
        TIM2->SR = ~TIM_SR_CC1IF;
        TIM2->DIER &= ~TIM_DIER_CC1IE;          /* one-shot */
        ss_on_comm_timer(&motor, bsp_now_us());
    }
}

void TIM1_BRK_TIM15_IRQHandler(void)
{
    if (TIM1->SR & TIM_SR_BIF) {
        TIM1->SR = ~TIM_SR_BIF;                 /* MOE already cleared by HW */
        ss_on_hw_fault(&motor);
    }
}

void SysTick_Handler(void)
{
    ms_ticks++;
    ss_tick_1ms(&motor, bsp_now_us());
}

/* Hard faults: kill the bridge first, then hang for the watchdog. */
void HardFault_Handler(void)
{
    TIM1->BDTR &= ~TIM_BDTR_MOE;
    for (;;) {}
}

/* ---------------- tiny formatting (no printf) ---------------- */
static char *put_u(char *p, uint32_t v)
{
    char t[11]; int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) *p++ = t[--n];
    return p;
}
static char *put_i(char *p, int32_t v)
{
    if (v < 0) { *p++ = '-'; v = -v; }
    return put_u(p, (uint32_t)v);
}
static char *put_s(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *put_hex(char *p, uint32_t v)
{
    static const char h[] = "0123456789abcdef";
    for (int i = 7; i >= 0; i--) *p++ = h[(v >> (4 * i)) & 0xFu];
    return p;
}

static void telemetry(void)
{
    char b[160], *p = b;
    p = put_s(p, "st=");  p = put_s(p, ss_state_name(motor.state));
    p = put_s(p, " f=");  p = put_hex(p, motor.faults);
    p = put_s(p, " rpm="); p = put_i(p, (int32_t)motor.speed_rpm);
    p = put_s(p, " ref="); p = put_i(p, (int32_t)motor.speed_ref_rpm);
    p = put_s(p, " d=");  p = put_i(p, (int32_t)(motor.duty * 100.0f));
    p = put_s(p, " i=");  p = put_i(p, (int32_t)(motor.i_phase_a * 1000.0f));
    p = put_s(p, " vb="); p = put_i(p, (int32_t)motor.vbus_v);
    p = put_s(p, " T=");  p = put_i(p, (int32_t)motor.temp_c);
    p = put_s(p, " zc="); p = put_u(p, motor.zc_count);
    p = put_s(p, " fc="); p = put_u(p, motor.forced_count);
    p = put_s(p, "\r\n"); *p = 0;
    bsp_uart_write(b);
}

/* ---------------- command line ---------------- */
static int32_t parse_int(const char *s)
{
    int32_t v = 0, sg = 1;
    while (*s == ' ') s++;
    if (*s == '-') { sg = -1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v * sg;
}

static void command(const char *c)
{
    if (!strcmp(c, "run"))                { if (!ss_start(&motor)) bsp_uart_write("start refused\r\n"); }
    else if (!strcmp(c, "stop"))          ss_stop(&motor);
    else if (!strcmp(c, "clr"))           ss_clear_faults(&motor);
    else if (!strncmp(c, "spd", 3))       { pot_mode = false; ss_set_speed(&motor, (float)parse_int(c + 3)); }
    else if (!strncmp(c, "dir", 3))       ss_set_reverse(&motor, parse_int(c + 3) != 0);
    else if (!strncmp(c, "pot", 3))       pot_mode = parse_int(c + 3) != 0;
    else if (!strcmp(c, "?"))             telemetry();
    else                                   bsp_uart_write("cmd: run stop clr spd <rpm> dir <0|1> pot <0|1> ?\r\n");
}

static void uart_poll(void)
{
    static char line[32];
    static unsigned n;
    int ch;
    while ((ch = bsp_uart_getc()) >= 0) {
        if (ch == '\r' || ch == '\n') {
            if (n) { line[n] = 0; command(line); n = 0; }
        } else if (n < sizeof line - 1u) {
            line[n++] = (char)ch;
        }
    }
}

/* ---------------- DC-link pre-charge ---------------- */
/* The bulk capacitors charge through the inrush NTC. Close the bypass
 * relay only when Vbus is high and has stopped rising. */
static void precharge_bus(void)
{
    float prev = 0.0f;
    uint32_t stable = 0;
    for (;;) {
        bsp_wdg_kick();
        bsp_delay_ms(20);
        float v = motor.vbus_v;            /* filtered by the ADC ISR */
        if (v > VBUS_START_MIN_V && (v - prev) < 2.0f) {
            if (++stable >= 10u) break;    /* 200 ms flat */
        } else {
            stable = 0;
        }
        prev = v;
    }
    bsp_relay(true);
    bsp_delay_ms(50);                      /* relay contact bounce */
}

int main(void)
{
    bsp_init();
    ss_init(&motor, &bsp_hal);
    bsp_uart_write("\r\nBLDC-310 sensorless six-step\r\n");

    precharge_bus();
    bsp_uart_write("bus ready\r\n");

    uint32_t t_tel = 0, t_slow = 0, t_btn = 0;
    bool btn_prev = false;

    for (;;) {
        bsp_wdg_kick();
        uart_poll();
        uint32_t now = ms_ticks;

        if (now - t_slow >= 100u) {                       /* 10 Hz */
            t_slow = now;
            ss_meas_t m;
            bsp_read_meas(&m);
            ss_update_temperature(&motor, m.ntc);
            if (pot_mode) {
                float rpm = (float)bsp_pot_raw() / ADC_FS * MOTOR_MAX_RPM;
                ss_set_speed(&motor, rpm);
            }
            /* thermal derating */
            if (motor.temp_c > T_WARN_C && motor.speed_ref_rpm > MOTOR_MIN_RPM)
                ss_set_speed(&motor, motor.speed_ref_rpm * 0.9f);
        }

        if (now - t_btn >= 20u) {                         /* debounced button */
            t_btn = now;
            bool b = bsp_button();
            if (b && !btn_prev) {
                if (motor.state == SS_IDLE)       ss_start(&motor);
                else if (motor.state == SS_FAULT) ss_clear_faults(&motor);
                else                              ss_stop(&motor);
            }
            btn_prev = b;
        }

        bsp_led_run(motor.state == SS_RUN || ((now >> 7) & 1u && motor.state != SS_IDLE && motor.state != SS_FAULT));
        bsp_led_fault(motor.state == SS_FAULT);

        if (now - t_tel >= 200u) {
            t_tel = now;
            telemetry();
        }
    }
}
