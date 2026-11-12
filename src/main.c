/*
 * CEAS CU CRONOMETRU DE EXAMEN
 * STM32F103C8 + 172 LED-uri WS2812B (6 digiti x 28 LED + 2x2 LED separatori) + HC-05
 *
 *  - Ceas normal (HH:MM:SS), setat de pe telefon
 *  - Cronometru (numaratoare inversa) pentru examen, culoarea depinde de timpul ramas:
 *        verde > 30 min | galben <= 30 min | portocaliu <= 10 min | rosu <= 1 min
 *    La 00:00:00 clipeste rosu, apoi revine singur la ceas.
 *  - Program de examen START=HH:MM / END=HH:MM: porneste singur la ora START si
 *    ajunge la zero exact la ora END.
 *  - Control prin Bluetooth (HC-05, 9600 baud) cu comenzi text, vezi HELP.
 *    Intelege comenzile aplicatiei Android "Exam Clock Controller" (T=, C=, START=, END=, BRIGHT=, MSG=).
 *
 * Fisier unic, fara HAL / CubeMX: doar registrii STM32 (CMSIS).
 * Date LED: PA6 (TIM3_CH1 + DMA1 canal 6). HC-05: PA9 (TX) / PA10 (RX), USART1.
 */

#include "stm32f1xx.h"
#include <stdint.h>

/* ============================ 1. CONFIGURARE ============================ */

#define HSE_MHZ         6       /* cristalul placii: 6 = PCB-ul din lucrare, 8 = Blue Pill */
#define LED_COUNT       172     /* numarul total de LED-uri din lant */
#define BAUD            9600    /* viteza HC-05 (implicit 9600) */
#define DEFAULT_BRIGHT  50      /* luminozitate la pornire, 0..255 (50 = ~20%) */
#define END_BLINK_SEC   10      /* cat clipeste 00:00:00 la finalul cronometrului */
#define REFRESH_MS      100     /* cat de des se retrimite imaginea la LED-uri */

/* Culorile cronometrului. 0 = praguri fixe (ca in aplicatie), 1 = treimi din durata (ca in lucrare) */
#define COLOR_BY_THIRDS 0
#define T_YELLOW_SEC    1800    /* galben cand raman cel mult 30 min */
#define T_ORANGE_SEC    600     /* portocaliu cand raman cel mult 10 min */
#define T_RED_SEC       60      /* rosu cand raman cel mult 1 min */

#define SYSCLK_HZ       72000000UL
#if (72 % HSE_MHZ) != 0 || (72 / HSE_MHZ) < 2 || (72 / HSE_MHZ) > 16
#error "HSE_MHZ trebuie sa fie un divizor al lui 72 (de exemplu 6, 8, 9 sau 12)"
#endif

/* ============================ 2. HARDWARE ============================== */

/* ---- timp: SysTick la 1 ms ---- */
static volatile uint32_t g_ms;
void SysTick_Handler(void) { g_ms++; }
static uint32_t millis(void) { return g_ms; }

/* ---- ceas de sistem: HSE -> PLL -> 72 MHz ---- */
static void clock_init(void)
{
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {}

    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;       /* 2 wait states la 72 MHz */

    /* PLL = HSE * (72 / HSE_MHZ); APB1 = 36 MHz (maxim permis), APB2 = 72 MHz */
    RCC->CFGR = RCC_CFGR_PLLSRC
              | ((uint32_t)(72 / HSE_MHZ - 2) << RCC_CFGR_PLLMULL_Pos)
              | RCC_CFGR_PPRE1_DIV2;

    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}

    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
}

/* ---- LED-uri WS2812B: TIM3_CH1 (PWM) + DMA1 canal 6, pe PA6 ----
 * Un bit = o perioada de 1,25 us (90 de tacte la 72 MHz):
 *   bit 0 -> semnal sus 1/3 din perioada (~0,42 us)
 *   bit 1 -> semnal sus 2/3 din perioada (~0,83 us)
 * Dupa cei 24*LED_COUNT biti urmeaza RESET_SLOTS perioade cu semnal jos (reset).
 */
#define T_PERIOD     (SYSCLK_HZ / 800000UL)
#define T_BIT0       (T_PERIOD / 3)
#define T_BIT1       ((T_PERIOD * 2) / 3)
#define RESET_SLOTS  50

static uint8_t  leds[LED_COUNT][3];                 /* R, G, B (inainte de luminozitate) */
static uint8_t  brightness = DEFAULT_BRIGHT;         /* 0..255 */
static uint16_t pwm_buf[LED_COUNT * 24 + RESET_SLOTS];

static void ws_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    RCC->AHBENR  |= RCC_AHBENR_DMA1EN;

    /* PA6: functie alternativa, push-pull, 50 MHz */
    GPIOA->CRL = (GPIOA->CRL & ~(0xFu << 24)) | (0xBu << 24);

    TIM3->PSC   = 0;
    TIM3->ARR   = T_PERIOD - 1;
    TIM3->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;   /* PWM mode 1, preload */
    TIM3->CCER  = TIM_CCER_CC1E;
    TIM3->CCR1  = 0;

    /* DMA: memorie -> TIM3->CCR1, 16 biti, incrementeaza adresa memoriei */
    DMA1_Channel6->CPAR = (uint32_t)&TIM3->CCR1;
    DMA1_Channel6->CCR  = DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_PSIZE_0
                        | DMA_CCR_MSIZE_0 | DMA_CCR_PL_1;
}

/* Trimite imaginea din leds[] la LED-uri (dureaza ~5 ms, CPU-ul doar asteapta). */
static void ws_show(void)
{
    uint32_t k = 0;
    for (int i = 0; i < LED_COUNT; i++) {
        uint32_t r = (leds[i][0] * (uint32_t)brightness + 127) / 255;
        uint32_t g = (leds[i][1] * (uint32_t)brightness + 127) / 255;
        uint32_t b = (leds[i][2] * (uint32_t)brightness + 127) / 255;
        uint32_t c = (g << 16) | (r << 8) | b;               /* WS2812B vrea ordinea G-R-B */
        for (int bit = 23; bit >= 0; bit--)
            pwm_buf[k++] = ((c >> bit) & 1u) ? T_BIT1 : T_BIT0;
    }
    for (int i = 0; i < RESET_SLOTS; i++)
        pwm_buf[k++] = 0;

    DMA1_Channel6->CCR  &= ~DMA_CCR_EN;
    DMA1->IFCR           = DMA_IFCR_CGIF6;                   /* sterge flag-urile canalului 6 */
    DMA1_Channel6->CMAR  = (uint32_t)pwm_buf;
    DMA1_Channel6->CNDTR = k;
    TIM3->CNT = 0;
    TIM3->SR  = 0;
    DMA1_Channel6->CCR  |= DMA_CCR_EN;
    TIM3->DIER = TIM_DIER_CC1DE;                             /* fiecare perioada cere un nou bit */
    TIM3->CR1 |= TIM_CR1_CEN;

    uint32_t t0 = g_ms;                                      /* asteapta sfarsitul transferului */
    while (!(DMA1->ISR & DMA_ISR_TCIF6) && (g_ms - t0) < 50) {}

    TIM3->CR1 &= ~TIM_CR1_CEN;
    TIM3->DIER = 0;
    TIM3->EGR  = TIM_EGR_UG;                                 /* iesirea ramane sigur pe 0 */
    DMA1_Channel6->CCR &= ~DMA_CCR_EN;
}

/* ---- UART (HC-05): USART1, receptie prin intrerupere intr-un buffer circular ---- */
#define RX_SZ 64
static volatile uint8_t rx_buf[RX_SZ];
static volatile uint8_t rx_head, rx_tail;

static void uart_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;

    /* PA9 (TX): functie alternativa push-pull; PA10 (RX): intrare cu pull-up */
    GPIOA->CRH = (GPIOA->CRH & ~(0xFFu << 4)) | (0xBu << 4) | (0x8u << 8);
    GPIOA->BSRR = (1u << 10);

    USART1->BRR = (SYSCLK_HZ + BAUD / 2) / BAUD;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    NVIC_EnableIRQ(USART1_IRQn);
}

void USART1_IRQHandler(void)
{
    uint32_t sr = USART1->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
        uint8_t c = (uint8_t)USART1->DR;                     /* citirea curata si eroarea ORE */
        if (sr & USART_SR_RXNE) {
            uint8_t next = (uint8_t)((rx_head + 1) & (RX_SZ - 1));
            if (next != rx_tail) { rx_buf[rx_head] = c; rx_head = next; }
        }
    }
}

static int uart_getc(void)                                   /* -1 daca nu e nimic de citit */
{
    if (rx_head == rx_tail) return -1;
    uint8_t c = rx_buf[rx_tail];
    rx_tail = (uint8_t)((rx_tail + 1) & (RX_SZ - 1));
    return c;
}

static void uart_puts(const char *s)
{
    while (*s) {
        while (!(USART1->SR & USART_SR_TXE)) {}
        USART1->DR = (uint8_t)*s++;
    }
}

/* ===================== 3. LOGICA APLICATIEI (fara hardware) ============= */
/* LOGIC BEGIN */
#define DAY_SEC 86400UL

typedef struct { uint8_t r, g, b; } rgb_t;
static const rgb_t COL_CLOCK  = {   0, 150, 255 };  /* ceas normal: albastru */
static const rgb_t COL_OK     = {   0, 255,   0 };  /* verde */
static const rgb_t COL_YELLOW = { 255, 180,   0 };  /* galben */
static const rgb_t COL_WARN   = { 255,  80,   0 };  /* portocaliu */
static const rgb_t COL_ALERT  = { 255,   0,   0 };  /* rosu */
static const rgb_t COL_WHITE  = { 255, 255, 255 };  /* separatorii dintre cifre */

enum { MODE_CLOCK, MODE_COUNTDOWN, MODE_PAUSED, MODE_END };

static uint8_t  mode = MODE_CLOCK;
static uint32_t clock_sec;      /* ora curenta, in secunde de la miezul noptii */
static uint32_t clock_last;     /* momentul (ms) la care a inceput secunda curenta a ceasului */
static uint32_t total_sec;      /* durata cronometrului */
static uint32_t remaining;      /* secunde ramase */
static uint32_t cd_last;        /* momentul (ms) la care a inceput secunda curenta a cronometrului */
static uint32_t end_start;      /* momentul (ms) la care cronometrul a ajuns la 0 */
static uint32_t last_draw;
static uint8_t  dirty = 1;

/* programul de examen: START=HH:MM si END=HH:MM (secunde de la miezul noptii) */
static uint32_t sched_start, sched_end;
static uint8_t  have_start, have_end;

/* ---- desenare pe cele 7 segmente ----
 * Fiecare digit are 28 LED: 7 segmente (a,b,c,d,e,f,g) x 4 LED, in aceasta ordine.
 * Ordinea digitilor: H H : M M : S S ; intre perechi sunt cate 2 LED separatori.
 */
static const uint8_t seg_mask[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

#define COLON1_LED  56          /* dupa digitii 0 si 1 (2*28) */
#define COLON2_LED  114         /* dupa digitii 2 si 3 (4*28 + 2) */

static void set_led(int i, rgb_t c)
{
    leds[i][0] = c.r; leds[i][1] = c.g; leds[i][2] = c.b;
}

static int digit_start(int d)
{
    return d * 28 + (d >= 2 ? 2 : 0) + (d >= 4 ? 2 : 0);
}

static void draw_digit(int d, uint32_t value, rgb_t c)
{
    int start = digit_start(d);
    for (int seg = 0; seg < 7; seg++)
        if (seg_mask[value] & (1u << seg))
            for (int i = 0; i < 4; i++)
                set_led(start + seg * 4 + i, c);
}

static void clear_all(void)
{
    for (int i = 0; i < LED_COUNT; i++) { leds[i][0] = leds[i][1] = leds[i][2] = 0; }
}

static void draw_time(uint32_t h, uint32_t m, uint32_t s, rgb_t c, int colons)
{
    clear_all();
    draw_digit(0, h / 10, c);  draw_digit(1, h % 10, c);
    draw_digit(2, m / 10, c);  draw_digit(3, m % 10, c);
    draw_digit(4, s / 10, c);  draw_digit(5, s % 10, c);
    if (colons) {
        set_led(COLON1_LED, COL_WHITE);  set_led(COLON1_LED + 1, COL_WHITE);
        set_led(COLON2_LED, COL_WHITE);  set_led(COLON2_LED + 1, COL_WHITE);
    }
}

static void draw_hms(uint32_t secs, rgb_t c, int colons)
{
    draw_time(secs / 3600, (secs / 60) % 60, secs % 60, c, colons);
}

/* Culoarea cronometrului in functie de timpul ramas. */
static rgb_t countdown_color(void)
{
#if COLOR_BY_THIRDS
    if (remaining * 3 < total_sec)          return COL_ALERT;
    if (remaining * 3 < total_sec * 2)      return COL_WARN;
    return COL_OK;
#else
    if (remaining <= T_RED_SEC)             return COL_ALERT;
    if (remaining <= T_ORANGE_SEC)          return COL_WARN;
    if (remaining <= T_YELLOW_SEC)          return COL_YELLOW;
    return COL_OK;
#endif
}

/* Alege imaginea in functie de modul curent. */
static void render(uint32_t now)
{
    switch (mode) {
    case MODE_CLOCK:
        /* separatorii sunt aprinsi in prima jumatate de secunda */
        draw_hms(clock_sec, COL_CLOCK, (now - clock_last) < 500);
        break;

    case MODE_COUNTDOWN:
    case MODE_PAUSED:
        if (mode == MODE_PAUSED && ((now / 500) & 1))
            clear_all();                        /* in pauza tot afisajul clipeste lent */
        else
            draw_hms(remaining, countdown_color(), 1);
        break;

    case MODE_END:
        if (((now - end_start) / 250) & 1)
            clear_all();                        /* 00:00:00 clipeste rosu */
        else
            draw_hms(0, COL_ALERT, 1);
        break;
    }
}

/* ---- interpretarea comenzilor ---- */
static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int starts_with(const char *s, const char *prefix)
{
    while (*prefix) { if (*s++ != *prefix++) return 0; }
    return 1;
}

/* citeste un numar de cel mult maxdigits cifre; intoarce pozitia de dupa el sau 0 daca nu sunt cifre */
static const char *parse_uint(const char *p, uint32_t *out, int maxdigits)
{
    uint32_t v = 0;
    int n = 0;
    while (*p >= '0' && *p <= '9') {
        if (++n > maxdigits) return 0;
        v = v * 10 + (uint32_t)(*p++ - '0');
    }
    if (n == 0) return 0;
    *out = v;
    return p;
}

/* "HH:MM:SS" -> secunde. Intoarce 1 daca formatul si valorile sunt corecte. */
static int parse_hms(const char *p, uint32_t *secs)
{
    uint32_t h, m, s;
    if (!(p = parse_uint(p, &h, 4)) || *p++ != ':') return 0;
    if (!(p = parse_uint(p, &m, 4)) || *p++ != ':') return 0;
    if (!(p = parse_uint(p, &s, 4)) || *p != '\0')  return 0;
    if (h > 99 || m > 59 || s > 59) return 0;
    *secs = h * 3600 + m * 60 + s;
    return 1;
}

/* "HH:MM" (ora 0-23) -> secunde de la miezul noptii */
static int parse_hm(const char *p, uint32_t *secs)
{
    uint32_t h, m;
    if (!(p = parse_uint(p, &h, 2)) || *p++ != ':') return 0;
    if (!(p = parse_uint(p, &m, 2)) || *p != '\0')  return 0;
    if (h > 23 || m > 59) return 0;
    *secs = h * 3600 + m * 60;
    return 1;
}

static void put_hms(char *buf, uint32_t secs)               /* scrie "HH:MM:SS" (8 caractere) */
{
    uint32_t h = secs / 3600, m = (secs / 60) % 60, s = secs % 60;
    buf[0] = (char)('0' + h / 10); buf[1] = (char)('0' + h % 10); buf[2] = ':';
    buf[3] = (char)('0' + m / 10); buf[4] = (char)('0' + m % 10); buf[5] = ':';
    buf[6] = (char)('0' + s / 10); buf[7] = (char)('0' + s % 10);
}

static void put_hm(char *buf, uint32_t secs, int valid)     /* scrie "HH:MM" sau "--:--" (5 caractere) */
{
    uint32_t h = secs / 3600, m = (secs / 60) % 60;
    if (!valid) { buf[0] = buf[1] = buf[3] = buf[4] = '-'; buf[2] = ':'; return; }
    buf[0] = (char)('0' + h / 10); buf[1] = (char)('0' + h % 10); buf[2] = ':';
    buf[3] = (char)('0' + m / 10); buf[4] = (char)('0' + m % 10);
}

static void send_status(void)
{
    static const char *const names[] = { "CLOCK", "COUNTDOWN", "PAUSED", "END" };
    char t[9] = {0};
    uart_puts("MODE=");  uart_puts(names[mode]);
    uart_puts(" TIME=");  put_hms(t, clock_sec); uart_puts(t);
    if (mode == MODE_COUNTDOWN || mode == MODE_PAUSED) {
        uart_puts(" LEFT=");  put_hms(t, remaining); uart_puts(t);
        uart_puts(" TOTAL="); put_hms(t, total_sec); uart_puts(t);
    }
    if (have_start || have_end) {
        t[5] = '\0';
        uart_puts(" PROGRAM=");
        put_hm(t, sched_start, have_start); uart_puts(t);
        uart_puts("-");
        put_hm(t, sched_end, have_end);     uart_puts(t);
    }
    uart_puts("\r\n");
}

static void start_countdown(uint32_t secs, uint32_t now)
{
    total_sec = remaining = secs;
    cd_last = now;
    mode = MODE_COUNTDOWN;
}

/* Programul de examen: daca START si END sunt setate si ora curenta e in intervalul
 * [START, END), porneste cronometrul pentru timpul pana la END. Daca START e in viitor,
 * asteapta. Cronometrul porneste aliniat la secundele ceasului, deci la START exact
 * ajunge la 00:00:00 la ora END. Intervalul poate trece peste miezul noptii. */
static void schedule_check(void)
{
    if (!have_start || !have_end || mode != MODE_CLOCK) return;
    uint32_t dur     = (sched_end + DAY_SEC - sched_start) % DAY_SEC;   /* nenul: verificat la primire */
    uint32_t elapsed = (clock_sec + DAY_SEC - sched_start) % DAY_SEC;
    if (elapsed < dur) {
        total_sec = dur;
        remaining = dur - elapsed;
        cd_last   = clock_last;
        mode      = MODE_COUNTDOWN;
        have_start = have_end = 0;                                      /* programul s-a consumat */
        dirty = 1;
    }
}

static void handle_line(const char *s)
{
    uint32_t now = millis();
    uint32_t v;
    const char *p;

    if (str_eq(s, "0") || str_eq(s, "STOP") || str_eq(s, "MSG=STOP")) {
        mode = MODE_CLOCK;
        have_start = have_end = 0;                          /* STOP anuleaza si programul */
        uart_puts("OK STOP\r\n");
    } else if (str_eq(s, "PAUSE") || str_eq(s, "MSG=PAUSE")) {
        if (mode == MODE_COUNTDOWN) { mode = MODE_PAUSED; uart_puts("OK PAUSE\r\n"); }
        else uart_puts("ERR nu merge cronometrul\r\n");
    } else if (str_eq(s, "RESUME")) {
        if (mode == MODE_PAUSED) { mode = MODE_COUNTDOWN; cd_last = now; uart_puts("OK RESUME\r\n"); }
        else uart_puts("ERR cronometrul nu e in pauza\r\n");
    } else if (starts_with(s, "MSG=")) {
        uart_puts("OK MSG\r\n");                            /* mesajele text nu au efect pe afisaj */
    } else if (starts_with(s, "T=")) {                      /* T=<secunde>: porneste cronometrul */
        p = parse_uint(s + 2, &v, 6);
        if (p && *p == '\0' && v >= 1 && v <= 359999) {
            start_countdown(v, now);
            uart_puts("OK START\r\n");
        } else uart_puts("ERR format: T=secunde (1-359999)\r\n");
    } else if (starts_with(s, "C=")) {                      /* C=HH:MM:SS: sincronizare ceas (de pe telefon) */
        if (parse_hms(s + 2, &v) && v < DAY_SEC) {
            uint32_t d = (v + DAY_SEC - clock_sec) % DAY_SEC;
            if (d >= 2 && d <= DAY_SEC - 2) {               /* corecteaza doar daca diferenta e >= 2 s */
                clock_sec = v; clock_last = now;
            }                                               /* fara raspuns: aplicatia o trimite in fiecare secunda */
        } else uart_puts("ERR format: C=HH:MM:SS\r\n");
    } else if (starts_with(s, "START=") || starts_with(s, "END=")) {
        int is_start = (s[0] == 'S');
        if (parse_hm(s + (is_start ? 6 : 4), &v)) {
            if (is_start) { sched_start = v; have_start = 1; }
            else          { sched_end   = v; have_end   = 1; }
            if (have_start && have_end && sched_start == sched_end) {
                have_start = have_end = 0;
                uart_puts("ERR START si END sunt egale\r\n");
            } else if (have_start && have_end) {
                uart_puts("OK PROGRAM ARMAT\r\n");
                schedule_check();
            } else {
                uart_puts(is_start ? "OK PROGRAM (lipseste END)\r\n" : "OK PROGRAM (lipseste START)\r\n");
            }
        } else uart_puts("ERR format: START=HH:MM / END=HH:MM\r\n");
    } else if (starts_with(s, "SET ")) {                    /* SET HH:MM:SS: seteaza ceasul exact */
        if (parse_hms(s + 4, &v) && v < DAY_SEC) {
            clock_sec = v; clock_last = now;
            uart_puts("OK SET\r\n");
        } else uart_puts("ERR format: SET HH:MM:SS (ora 0-23)\r\n");
    } else if (starts_with(s, "BRIGHT=") || starts_with(s, "BRIGHT ")) {
        p = parse_uint(s + 7, &v, 3);
        if (p && *p == '\0' && v <= 255) {
            brightness = (uint8_t)v;
            uart_puts("OK BRIGHT\r\n");
        } else uart_puts("ERR format: BRIGHT=0-255\r\n");
    } else if (str_eq(s, "STATUS")) {
        send_status();
    } else if (str_eq(s, "HELP")) {
        uart_puts("T=secunde sau HH:MM:SS = porneste cronometrul | 0 sau STOP | PAUSE | RESUME\r\n");
        uart_puts("C=HH:MM:SS = sincronizare ceas | SET HH:MM:SS = seteaza ceasul\r\n");
        uart_puts("START=HH:MM + END=HH:MM = program examen | BRIGHT=0-255 | STATUS\r\n");
    } else if (parse_hms(s, &v) && v > 0) {                 /* "01:30:00" == "T=5400" */
        start_countdown(v, now);
        uart_puts("OK START\r\n");
    } else {
        uart_puts("ERR comanda necunoscuta, scrie HELP\r\n");
    }
    dirty = 1;
}

/* Strange caractere pana la sfarsit de linie (\n sau \r), apoi executa comanda. */
static void poll_commands(void)
{
    static char line[32];
    static uint8_t n;
    static uint8_t too_long;
    int c;

    while ((c = uart_getc()) >= 0) {
        if (c == '\n' || c == '\r') {
            while (n && line[n - 1] == ' ') n--;                       /* spatiile de la final nu conteaza */
            if (too_long) uart_puts("ERR linie prea lunga\r\n");
            else if (n) { line[n] = '\0'; handle_line(line); }
            n = 0; too_long = 0;
        } else if (c == ' ' && n == 0) {
            /* ignora spatiile de la inceput */
        } else if (n < sizeof(line) - 1) {
            line[n++] = (char)((c >= 'a' && c <= 'z') ? c - 32 : c);   /* litere mari */
        } else {
            too_long = 1;
        }
    }
}

static void app_init(void)
{
    uint32_t t0 = millis();
    draw_time(88, 88, 88, COL_WHITE, 1);                    /* test: aprinde toate segmentele */
    ws_show();
    while (millis() - t0 < 1200) {}

    clock_last = last_draw = millis();
    uart_puts("\r\nCeas pornit. Scrie HELP pentru comenzi.\r\n");
}

static void app_loop(void)
{
    poll_commands();

    uint32_t now = millis();                                /* dupa comenzi: now >= clock_last */

    while (now - clock_last >= 1000) {                      /* ceasul normal merge mereu */
        clock_last += 1000;
        if (++clock_sec >= DAY_SEC) clock_sec = 0;
        dirty = 1;
    }
    schedule_check();                                       /* porneste singur examenul la ora START */

    if (mode == MODE_COUNTDOWN) {
        while (now - cd_last >= 1000) {
            cd_last += 1000;
            if (remaining) remaining--;
            dirty = 1;
            if (remaining == 0) { mode = MODE_END; end_start = now; break; }
        }
    }

    if (mode == MODE_END && now - end_start >= END_BLINK_SEC * 1000UL) {
        mode = MODE_CLOCK;                                  /* dupa clipire revine la ceas */
        dirty = 1;
    }

    if (dirty || now - last_draw >= REFRESH_MS) {
        render(now);
        ws_show();
        last_draw = millis();
        dirty = 0;
    }
}

/* LOGIC END */

/* ================================ 4. MAIN =============================== */

int main(void)
{
    clock_init();
    SysTick_Config(SYSCLK_HZ / 1000);       /* intrerupere la fiecare 1 ms */
    ws_init();
    uart_init();
    app_init();
    for (;;)
        app_loop();
}
