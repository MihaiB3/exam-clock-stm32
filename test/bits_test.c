#include <stdint.h>
#include <stdio.h>
#define LED_COUNT 172
#define SYSCLK_HZ 72000000UL
#define T_PERIOD     (SYSCLK_HZ / 800000UL)
#define T_BIT0       (T_PERIOD / 3)
#define T_BIT1       ((T_PERIOD * 2) / 3)
#define RESET_SLOTS  50
static uint8_t leds[LED_COUNT][3]; static uint8_t brightness; static uint16_t pwm_buf[LED_COUNT*24+RESET_SLOTS];
static uint32_t fill(void) {
#include "fill_extract.inc"
    return k;
}
static unsigned decode(const uint16_t *p) { unsigned v = 0; for (int i = 0; i < 24; i++) { v = (v<<1) | (p[i] == T_BIT1); if (p[i]!=T_BIT0 && p[i]!=T_BIT1) { printf("valoare PWM invalida %u\n", p[i]); return 0xFFFFFFFF; } } return v; }
int main(void) {
    int bad = 0;
    printf("T_PERIOD=%lu (ARR=%lu)  T_BIT0=%lu (%.3f us)  T_BIT1=%lu (%.3f us)  perioada=%.3f us\n", T_PERIOD, T_PERIOD-1, T_BIT0, T_BIT0/72.0, T_BIT1, T_BIT1/72.0, T_PERIOD/72.0);
    brightness = 255; leds[0][0]=255; leds[1][1]=255; leds[2][2]=255; leds[3][0]=0x12; leds[3][1]=0x34; leds[3][2]=0x56;
    uint32_t n = fill();
    if (n != LED_COUNT*24 + RESET_SLOTS) { printf("lungime gresita %u\n", n); bad++; }
    unsigned e0 = 0x00FF00, e1 = 0xFF0000, e2 = 0x0000FF, e3 = 0x341256; /* GRB */
    if (decode(&pwm_buf[0])!=e0) { printf("LED0 (rosu) -> %06X, asteptat %06X\n", decode(&pwm_buf[0]), e0); bad++; }
    if (decode(&pwm_buf[24])!=e1){ printf("LED1 (verde) -> %06X, asteptat %06X\n", decode(&pwm_buf[24]), e1); bad++; }
    if (decode(&pwm_buf[48])!=e2){ printf("LED2 (albastru) -> %06X, asteptat %06X\n", decode(&pwm_buf[48]), e2); bad++; }
    if (decode(&pwm_buf[72])!=e3){ printf("LED3 (12,34,56) -> %06X, asteptat %06X\n", decode(&pwm_buf[72]), e3); bad++; }
    for (int i = LED_COUNT*24; i < LED_COUNT*24+RESET_SLOTS; i++) if (pwm_buf[i] != 0) { bad++; printf("reset slot %d nenul\n", i); break; }
    for (int i = 4*24; i < LED_COUNT*24; i++) if (pwm_buf[i] != T_BIT0) { bad++; printf("LED nefolosit are bit 1 la %d\n", i); break; }
    /* luminozitate 20%: rosu 255 -> 51 (0x33) */
    brightness = 51; fill();
    if (decode(&pwm_buf[0]) != 0x003300) { printf("brightness 51 rosu -> %06X, asteptat 003300\n", decode(&pwm_buf[0])); bad++; }
    /* durata unui cadru */
    printf("cadru: %u perioade = %.2f ms\n", n, n*1.25/1000);
    printf(bad ? "BITSTREAM: %d probleme\n" : "BITSTREAM: OK (GRB, MSB primul, %d erori)\n", bad);
    return bad;
}
