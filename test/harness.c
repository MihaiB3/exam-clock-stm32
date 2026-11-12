/* Harness de test pe PC: aceeasi logica ca in firmware (extrasa din main.c), hardware simulat. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifndef CONFIG_HEADER
#define CONFIG_HEADER "config_extract.h"
#endif
#include CONFIG_HEADER

static uint8_t leds[LED_COUNT][3];
static uint8_t brightness = DEFAULT_BRIGHT;
static uint32_t g_ms;
static uint32_t millis(void) { return g_ms; }

static char rxq[8192]; static int rxh, rxt;
static char txlog[8192]; static int txn;
static int uart_getc(void) { return rxh == rxt ? -1 : (unsigned char)rxq[rxt++]; }
static void uart_puts(const char *s) { int l = (int)strlen(s); memcpy(txlog + txn, s, (size_t)l); txn += l; txlog[txn] = 0; }
static void ws_show(void) {}

#include "logic_extract.inc"

/* ---- decodor independent: citeste LED-urile pe baza pozitiilor fizice ---- */
static int start_of(int d) { int base = d*28; if (d>=2) base+=2; if (d>=4) base+=2; return base; }
static int seg_lit(int d, int seg) {
    int n = 0; for (int i = 0; i < 4; i++) { int k = start_of(d)+seg*4+i; if (leds[k][0]||leds[k][1]||leds[k][2]) n++; }
    if (n != 0 && n != 4) { printf("SEGMENT PARTIAL!\n"); exit(2); }
    return n == 4;
}
static int decode_digit(int d) {
    static const char *pat[10] = {"abcdef","bc","abdeg","abcdg","bcfg","acdfg","acdefg","abc","abcdefg","abcdfg"};
    char s[8] = {0}; int n = 0;
    for (int seg = 0; seg < 7; seg++) if (seg_lit(d, seg)) s[n++] = "abcdefg"[seg];
    if (n == 0) return -1;
    for (int v = 0; v < 10; v++) if (!strcmp(s, pat[v])) return v;
    printf("PATTERN NECUNOSCUT '%s'\n", s); exit(2);
}
static int colons_lit(void) { return leds[56][0]&&leds[57][0]&&leds[114][0]&&leds[115][0]; }
static const char *color_name(void) {
    for (int i = 0; i < LED_COUNT; i++) if (i!=56&&i!=57&&i!=114&&i!=115 && (leds[i][0]||leds[i][1]||leds[i][2])) {
        int r=leds[i][0], g=leds[i][1], b=leds[i][2];
        if (r==0   && g==255 && b==0)   return "VERDE";
        if (r==255 && g==180 && b==0)   return "GALBEN";
        if (r==255 && g==80  && b==0)   return "PORTOCALIU";
        if (r==255 && g==0   && b==0)   return "ROSU";
        if (r==0   && g==150 && b==255) return "ALBASTRU";
        if (r==255 && g==255 && b==255) return "ALB";
        return "?";
    }
    return "-";
}
static void snapshot(char *out, size_t sz, const char **col, int *colons) {
    int dg[6]; int blank = 1;
    for (int d = 0; d < 6; d++) { dg[d] = decode_digit(d); if (dg[d] >= 0) blank = 0; }
    if (blank) snprintf(out, sz, "(stins)"); else snprintf(out, sz, "%d%d%d%d%d%d", dg[0],dg[1],dg[2],dg[3],dg[4],dg[5]);
    *col = color_name(); *colons = colons_lit();
}

/* culoarea "din specificatie", calculata independent de firmware */
static const char *spec_color(uint32_t rem, uint32_t total) {
#if COLOR_BY_THIRDS
    if (rem*3 < total) return "ROSU"; if (rem*3 < total*2) return "PORTOCALIU"; return "VERDE";
#else
    (void)total;
    if (rem <= T_RED_SEC) return "ROSU"; if (rem <= T_ORANGE_SEC) return "PORTOCALIU";
    if (rem <= T_YELLOW_SEC) return "GALBEN"; return "VERDE";
#endif
}

/* ---- infrastructura ---- */
static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { if (++fails <= 25) { printf("  FAIL @%ums: ", g_ms); printf(__VA_ARGS__); printf("\n"); } } } while (0)
static void send(const char *s) { for (; *s; s++) rxq[rxh++] = *s; }
static void advance(uint32_t ms) { while (ms--) { g_ms++; if (g_ms % 3 == 0) app_loop(); } app_loop(); }
static void clear_tx(void) { txn = 0; txlog[0] = 0; }
static void expect_tx(const char *needle) { CHECK(strstr(txlog, needle) != NULL, "raspuns lipsa '%s' (am primit '%s')", needle, txlog); clear_tx(); }
static void expect_screen(const char *time, const char *col) {
    char s[32]; const char *c; int colons; snapshot(s, sizeof s, &c, &colons);
    CHECK(!strcmp(s, time), "afisaj '%s', asteptat '%s'", s, time);
    if (col) CHECK(!strcmp(c, col), "culoare %s, asteptat %s (afisaj %s)", c, col, s);
}
static void hms_str(uint32_t s, char *o) { snprintf(o, 8, "%02u%02u%02u", (unsigned)(s/3600), (unsigned)((s/60)%60), (unsigned)(s%60)); }
static void expect_countdown(uint32_t rem, uint32_t total) { char w[8]; hms_str(rem, w); expect_screen(w, spec_color(rem, total)); }
static void expect_clock_screen(void) { char w[8]; hms_str(clock_sec, w); expect_screen(w, "ALBASTRU"); }
static void reset_state(void) {
    mode = MODE_CLOCK; clock_sec = 0; remaining = total_sec = 0; dirty = 1; rxh = rxt = 0; clear_tx();
    have_start = have_end = 0; sched_start = sched_end = 0; brightness = DEFAULT_BRIGHT;
    clock_last = last_draw = g_ms;
}
static void run_to_blink_end(void) { for (int i = 0; i < 2000 && mode == MODE_END; i++) advance(10); }
static int circ_diff(uint32_t a, uint32_t b) { uint32_t d = (a + DAY_SEC - b) % DAY_SEC; return (int)(d <= DAY_SEC/2 ? d : DAY_SEC - d); }

#define SECTION(name) printf("[%s] ", name)
#define ENDSEC() do { printf("%s\n", fails>f0 ? "FAIL":"OK"); f0 = fails; } while (0)

int main(void)
{
    int f0 = 0;
    printf("Mod culori: %s\n", COLOR_BY_THIRDS ? "treimi (ca in lucrare)" : "praguri fixe 30/10/1 min (ca in aplicatie)");

    SECTION("0 decodare cifre/pozitii/separatori/172 LED");
    for (int d = 0; d < 6; d++) for (int v = 0; v < 10; v++) {
        clear_all(); draw_digit(d, (uint32_t)v, COL_OK);
        CHECK(decode_digit(d) == v, "digit %d valoare %d decodat %d", d, v, decode_digit(d));
        for (int o = 0; o < 6; o++) if (o != d) CHECK(decode_digit(o) == -1, "digitul %d a 'scurs' in %d", d, o);
    }
    clear_all(); draw_time(88,88,88, COL_WHITE, 1); int lit = 0; for (int i=0;i<LED_COUNT;i++) if (leds[i][0]) lit++;
    CHECK(lit == 172, "88:88:88 trebuie sa aprinda toate cele 172 LED, aprinse: %d", lit);
    ENDSEC();

    SECTION("1 ceas normal, SET, miezul noptii, SET invalid");
    g_ms = 100000; reset_state();
    send("SET 23:59:58\r\n"); advance(10); expect_tx("OK SET");
    expect_screen("235958", "ALBASTRU");
    advance(1000); expect_screen("235959", "ALBASTRU");
    advance(1000); expect_screen("000000", "ALBASTRU");
    advance(59*1000); expect_screen("000059", NULL);
    advance(1000); expect_screen("000100", NULL);
    send("set 13:07:09\n"); advance(5); expect_tx("OK SET"); expect_screen("130709", NULL);
    send("SET 24:00:00\n"); advance(5); expect_tx("ERR");
    send("SET 12:60:00\n"); advance(5); expect_tx("ERR");
    send("SET 12:30\n");    advance(5); expect_tx("ERR");
    send("SET abc\n");      advance(5); expect_tx("ERR");
    expect_screen("130709", NULL);
    { int on=0, off=0; for (int i=0;i<1000;i+=10) { advance(10); if (colons_lit()) on++; else off++; }
      CHECK(on>35 && off>35, "separatorii nu clipesc (on=%d off=%d)", on, off); }
    ENDSEC();

    SECTION("2 cronometru 40 min: culoare + timp corecte la FIECARE secunda");
    reset_state(); send("T=30\n"); advance(5); expect_tx("OK START");
    advance(980); expect_countdown(30, 30);                    /* ~985 ms de la start: inca 30 */
    advance(30);  expect_countdown(29, 30);                    /* ~1015 ms: 29 */
    reset_state(); send("T=2400\n"); advance(10); expect_tx("OK START");
    expect_countdown(2400, 2400);
    for (uint32_t s = 1; s < 2400; s++) { advance(1000); expect_countdown(2400 - s, 2400); }
    advance(1000);
    CHECK(mode == MODE_END, "trebuia MODE_END, e %d", mode);
    ENDSEC();

    SECTION("3 clipire rosie la final, apoi revenire la ceas");
    uint32_t t_end = end_start;
    { int on=0, off=0, wrong=0; for (int i=0;i<9800;i+=10) { advance(10);
          char s[32]; const char *c; int cl; snapshot(s, sizeof s, &c, &cl);
          if (!strcmp(s,"(stins)")) off++; else { on++; if (strcmp(s,"000000")||strcmp(c,"ROSU")) wrong++; } }
      CHECK(on>400 && off>400 && wrong==0, "clipire final on=%d off=%d gresit=%d", on, off, wrong); }
    CHECK(mode == MODE_END, "inca in clipire dupa 9.8 s");
    run_to_blink_end();
    CHECK(mode == MODE_CLOCK, "dupa clipire trebuia ceas, mode=%d", mode);
    CHECK(g_ms - t_end >= END_BLINK_SEC*1000u && g_ms - t_end < END_BLINK_SEC*1000u + 50, "durata clipire %u ms", g_ms - t_end);
    expect_clock_screen();
    ENDSEC();

    SECTION("4 pauza / reluare");
    reset_state(); send("00:01:00\n"); advance(10); clear_tx();
    advance(10000); expect_screen("000050", spec_color(50, 60));
    send("PAUSE\n"); advance(5); expect_tx("OK PAUSE");
    { int on=0, off=0; for (int i=0;i<30000;i+=10) { advance(10); char s[32]; const char *c; int cl; snapshot(s,sizeof s,&c,&cl);
          if (!strcmp(s,"(stins)")) off++; else { on++; CHECK(!strcmp(s,"000050"), "in pauza timpul s-a miscat: %s", s); } }
      CHECK(on>1000 && off>1000, "pauza nu clipeste (on=%d off=%d)", on, off); }
    CHECK(remaining == 50 && mode == MODE_PAUSED, "dupa 30 s pauza remaining=%u mode=%d", remaining, mode);
    send("RESUME\n"); advance(5); expect_tx("OK RESUME");
    advance(500); CHECK(remaining == 50, "resume: nu trebuia sa scada inainte de o secunda intreaga (%u)", remaining);
    advance(600); CHECK(remaining == 49, "dupa 1.1 s de la resume remaining=%u", remaining);
    send("RESUME\n"); advance(5); expect_tx("ERR");
    send("STOP\n"); advance(5); expect_tx("OK STOP"); send("PAUSE\n"); advance(5); expect_tx("ERR");
    ENDSEC();

    SECTION("5 stop: revine la ceas, ora a mers in fundal");
    reset_state(); send("SET 10:00:00\n"); advance(10); clear_tx();
    send("01:00:00\n"); advance(10); clear_tx();
    advance(125*1000); expect_screen("005755", NULL);
    send("0\n"); advance(5); expect_tx("OK STOP");
    CHECK(mode == MODE_CLOCK, "stop nu a revenit la ceas");
    expect_screen("100205", "ALBASTRU");
    ENDSEC();

    SECTION("6 validare comenzi");
    reset_state();
    send("  start 00:00:15\n"); advance(5); expect_tx("ERR");           /* alias scos: START cu spatiu nu mai exista */
    send("00:00:15\n"); advance(5); expect_tx("OK START"); expect_screen("000015", NULL);
    send("99:59:59\n"); advance(5); expect_tx("OK START"); expect_screen("995959", "VERDE");
    send("1:2:3\n");    advance(5); expect_tx("OK START"); expect_screen("010203", NULL);
    send("00:00:00\n"); advance(5); expect_tx("ERR");  expect_screen("010203", NULL);
    send("100:00:00\n");advance(5); expect_tx("ERR");
    send("00:61:00\n"); advance(5); expect_tx("ERR");
    send("00:00:61\n"); advance(5); expect_tx("ERR");
    send("12345\n");    advance(5); expect_tx("ERR");
    send("hello\n");    advance(5); expect_tx("ERR");
    send("00:00:20 x\n");advance(5); expect_tx("ERR");
    send("00:00:::20\n");advance(5); expect_tx("ERR");
    send("::\n");       advance(5); expect_tx("ERR");
    send("00:00:\n");   advance(5); expect_tx("ERR");
    send("00000:00:20\n");advance(5); expect_tx("ERR");
    expect_screen("010203", NULL);
    /* T= (formatul aplicatiei) */
    send("T=90\n");     advance(5); expect_tx("OK START"); CHECK(remaining == 90 && total_sec == 90, "T=90 -> %u/%u", remaining, total_sec);
    send("T=3661\n");   advance(5); expect_tx("OK START"); expect_screen("010101", NULL);
    send("T=359999\n"); advance(5); expect_tx("OK START"); expect_screen("995959", NULL);
    send("t=5\n");      advance(5); expect_tx("OK START"); CHECK(total_sec == 5, "t=5 litera mica");
    send("T=0\n");      advance(5); expect_tx("ERR");
    send("T=360000\n"); advance(5); expect_tx("ERR");
    send("T=1234567\n");advance(5); expect_tx("ERR");
    send("T=\n");       advance(5); expect_tx("ERR");
    send("T=abc\n");    advance(5); expect_tx("ERR");
    send("T=1 2\n");    advance(5); expect_tx("ERR");
    send("T=-5\n");     advance(5); expect_tx("ERR");
    CHECK(total_sec == 5, "o comanda invalida a stricat cronometrul (%u)", total_sec);
    /* C= */
    send("C=25:00:00\n"); advance(5); expect_tx("ERR");
    send("C=12:00\n");    advance(5); expect_tx("ERR");
    send("C=abc\n");      advance(5); expect_tx("ERR");
    send("0\n"); advance(5); clear_tx();
    ENDSEC();

    SECTION("7 BRIGHT (0-255, cu = sau spatiu)");
    reset_state();
    send("BRIGHT=120\n"); advance(5); expect_tx("OK BRIGHT"); CHECK(brightness == 120, "bright=%u", brightness);
    send("BRIGHT 255\n"); advance(5); expect_tx("OK BRIGHT"); CHECK(brightness == 255, "bright=%u", brightness);
    send("BRIGHT=0\n");   advance(5); expect_tx("OK BRIGHT"); CHECK(brightness == 0, "bright=%u", brightness);
    send("BRIGHT=77\n");  advance(5); clear_tx();
    send("BRIGHT=256\n"); advance(5); expect_tx("ERR");
    send("BRIGHT=-1\n");  advance(5); expect_tx("ERR");
    send("BRIGHT=\n");    advance(5); expect_tx("ERR");
    send("BRIGHT=12x\n"); advance(5); expect_tx("ERR");
    send("BRIGHT=1000\n");advance(5); expect_tx("ERR");
    CHECK(brightness == 77, "luminozitatea s-a stricat: %u", brightness);
    ENDSEC();

    SECTION("8 STATUS / HELP (text exact)");
    reset_state(); send("SET 08:15:30\n"); advance(5); clear_tx();
    send("STATUS\n"); advance(5); expect_tx("MODE=CLOCK TIME=08:15:30\r\n");
    send("00:20:00\n"); advance(5); clear_tx(); advance(1500);
    send("STATUS\n"); advance(5); expect_tx("MODE=COUNTDOWN TIME=08:15:31 LEFT=00:19:59 TOTAL=00:20:00\r\n");
    send("PAUSE\n"); advance(5); clear_tx();
    send("STATUS\n"); advance(5); expect_tx("MODE=PAUSED TIME=08:15:31 LEFT=00:19:59 TOTAL=00:20:00\r\n");
    send("HELP\n"); advance(5); expect_tx("PAUSE");
    send("0\n"); advance(5); clear_tx();
    send("START=09:00\n"); advance(5); expect_tx("OK PROGRAM (lipseste END)");
    send("STATUS\n"); advance(5); expect_tx("PROGRAM=09:00---:--");
    send("END=11:30\n"); advance(5); expect_tx("OK PROGRAM ARMAT");
    send("STATUS\n"); advance(5); expect_tx("MODE=CLOCK TIME=08:15:3");
    send("STATUS\n"); advance(5); expect_tx("PROGRAM=09:00-11:30\r\n");
    send("0\n"); advance(5); clear_tx();
    send("STATUS\n"); advance(5); CHECK(!strstr(txlog, "PROGRAM"), "STOP nu a anulat programul: %s", txlog); clear_tx();
    ENDSEC();

    SECTION("9 intrare fragmentata, CR/LF, linii goale/lungi, spatii");
    reset_state();
    send("00:0"); advance(50); CHECK(mode == MODE_CLOCK, "a pornit inainte de sfarsitul liniei");
    send("0:25"); advance(50); CHECK(mode == MODE_CLOCK, "a pornit inainte de sfarsitul liniei (2)");
    send("\r"); advance(5); CHECK(mode == MODE_COUNTDOWN, "CR nu a terminat linia");
    send("\n"); advance(5);
    CHECK(strstr(txlog, "OK START") && !strstr(txlog, "ERR"), "raspuns la CR+LF: '%s'", txlog); clear_tx();
    send("\n\n\r\r\n"); advance(5); CHECK(txn == 0, "linii goale au produs raspuns: '%s'", txlog);
    send("0\n"); advance(5); clear_tx();
    send("00:00:20   \n"); advance(5); expect_tx("OK START"); CHECK(remaining == 20, "spatii la final: comanda respinsa");
    send("STOP  \r\n"); advance(5); expect_tx("OK STOP");
    send("   \n"); advance(5); CHECK(txn == 0, "linie doar cu spatii a produs raspuns: '%s'", txlog);
    send("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"); advance(5); expect_tx("ERR");
    send("00:00:30\n"); advance(5); expect_tx("OK START"); CHECK(remaining == 30, "dupa linie lunga, comanda buna nu merge");
    send("0\n"); advance(5); clear_tx();
    ENDSEC();

    SECTION("10 examen 3 h integral: timp + culoare la fiecare secunda, zero deriva");
    reset_state(); send("SET 09:00:00\n"); advance(10); clear_tx();
    uint32_t A = clock_last;
    send("T=10800\n"); advance(10); clear_tx();
    uint32_t B = cd_last;
    expect_countdown(10800, 10800);
    for (uint32_t sec = 1; sec < 10800; sec++) { advance(1000); expect_countdown(10800 - sec, 10800); }
    advance(1000);
    CHECK(mode == MODE_END, "dupa 3h exact cronometrul trebuia sa fie la final (mode=%d)", mode);
    CHECK(end_start - B >= 10800000u && end_start - B <= 10800003u, "deriva: sfarsit la B+%u ms (asteptat 10800000..03)", end_start - B);
    CHECK(clock_sec == 12*3600, "ceasul dupa 3h: %u (asteptat 43200)", clock_sec);
    CHECK(clock_last - A == 10800000u, "ceasul a derivat: %u", clock_last - A);
    run_to_blink_end(); expect_clock_screen();
    ENDSEC();

    SECTION("11 depasirea contorului millis() (~49 zile)");
    g_ms = 0xFFFFFFFFu - 5500u; reset_state();
    send("SET 10:00:00\n"); advance(10); clear_tx(); A = clock_last;
    send("T=20\n"); advance(10); clear_tx(); B = cd_last;
    advance(12000);
    CHECK(g_ms < 100000u, "testul trebuia sa treaca peste 0 (g_ms=%u)", g_ms);
    { uint32_t k = (clock_last - A) / 1000;
      CHECK(clock_sec == 36000 + k, "ceas dupa wrap: %u asteptat %u", clock_sec, 36000 + k);
      CHECK((g_ms - A) / 1000 - k <= 1, "ceasul ramane in urma peste wrap (k=%u real=%u)", k, (g_ms - A)/1000); }
    CHECK(remaining >= 7 && remaining <= 8, "cronometru dupa wrap: %u", remaining);
    advance(8000); CHECK(mode == MODE_END, "nu a ajuns la final dupa wrap (mode=%d rem=%u)", mode, remaining);
    run_to_blink_end(); CHECK(mode == MODE_CLOCK, "nu a revenit la ceas dupa wrap");
    { uint32_t k = (clock_last - A) / 1000;
      CHECK(clock_sec == 36000 + k && (g_ms - A) / 1000 - k <= 1, "ceas dupa wrap+final: %u (k=%u)", clock_sec, k); }
    (void)B;
    ENDSEC();

    SECTION("12 REPLAY: mesajele EXACTE din aplicatia Android");
    g_ms = 500000; reset_state();
    send("C=14:35:12\n"); advance(10);
    CHECK(clock_sec == 14*3600+35*60+12, "C= nu a setat ceasul: %u", clock_sec); CHECK(txn == 0, "C= a produs raspuns: '%s'", txlog);
    send("T=3661\n"); advance(10); expect_tx("OK START"); CHECK(mode == MODE_COUNTDOWN && total_sec == 3661, "T=3661");
    expect_screen("010101", NULL);
    send("PAUSE\n"); advance(10); expect_tx("OK PAUSE"); CHECK(mode == MODE_PAUSED, "PAUSE");
    send("RESUME\n"); advance(10); expect_tx("OK RESUME"); CHECK(mode == MODE_COUNTDOWN, "RESUME");
    send("MSG=PAUSE\n"); advance(10); expect_tx("OK PAUSE"); CHECK(mode == MODE_PAUSED, "MSG=PAUSE");
    send("RESUME\n"); advance(10); clear_tx();
    send("MSG=EXAMEN\n"); advance(10); expect_tx("OK MSG"); CHECK(mode == MODE_COUNTDOWN, "MSG=EXAMEN a modificat cronometrul");
    send("MSG=STOP\n"); advance(10); expect_tx("OK STOP"); CHECK(mode == MODE_CLOCK, "MSG=STOP");
    send("BRIGHT=120\n"); advance(10); expect_tx("OK BRIGHT"); CHECK(brightness == 120, "BRIGHT=120");
    send("START=09:00\n"); advance(10); expect_tx("OK PROGRAM");
    send("END=11:00\n"); advance(10); expect_tx("OK PROGRAM ARMAT");
    send("STOP\n"); advance(10); expect_tx("OK STOP");
    send("T=900\n"); advance(10); clear_tx(); send("STOP\n"); advance(10); expect_tx("OK STOP"); CHECK(mode == MODE_CLOCK, "STOP");
    ENDSEC();

    SECTION("13 program START/END");
    /* a) se asteapta ora START, porneste singur, se termina exact la END */
    g_ms = 700000; reset_state(); send("SET 08:59:50\n"); advance(10); clear_tx();
    send("START=09:00\nEND=09:01\n"); advance(10); clear_tx();
    CHECK(mode == MODE_CLOCK, "a pornit prea devreme");
    advance(9000); CHECK(mode == MODE_CLOCK, "a pornit inainte de 09:00:00 (clock=%u)", clock_sec);
    advance(1500);
    CHECK(mode == MODE_COUNTDOWN && total_sec == 60, "nu a pornit la START (mode=%d total=%u clock=%u)", mode, total_sec, clock_sec);
    CHECK(!have_start && !have_end, "programul nu s-a consumat");
    for (int i = 0; i < 70000 && mode == MODE_COUNTDOWN; i++) advance(1);
    CHECK(mode == MODE_END && clock_sec == 9*3600+60 && end_start - clock_last <= 4, "nu s-a terminat exact la END (mode=%d clock=%u, decalaj=%u ms)", mode, clock_sec, end_start - clock_last);
    run_to_blink_end();
    /* b) in interiorul intervalului (comanda primita la fraction de secunda): porneste imediat, aliniat la ceas */
    g_ms += 123; reset_state(); send("SET 09:10:00\n"); advance(10); clear_tx(); advance(400);
    send("START=09:00\nEND=09:30\n"); advance(10); clear_tx();
    CHECK(mode == MODE_COUNTDOWN && total_sec == 1800 && remaining == 1200, "start in interval: mode=%d total=%u rem=%u", mode, total_sec, remaining);
    for (int i = 0; i < 1300000 && mode == MODE_COUNTDOWN; i++) advance(1);
    CHECK(mode == MODE_END && clock_sec == 9*3600+30*60 && end_start - clock_last <= 4, "interval pornit tarziu: nealiniat la END (clock=%u, decalaj=%u ms)", clock_sec, end_start - clock_last);
    run_to_blink_end();
    /* c) ordinea inversa: END inainte de START */
    reset_state(); send("SET 07:00:00\n"); advance(10); clear_tx();
    send("END=08:00\n"); advance(10); expect_tx("lipseste START"); CHECK(mode == MODE_CLOCK, "END singur a pornit ceva");
    send("START=07:30\n"); advance(10); expect_tx("ARMAT"); CHECK(mode == MODE_CLOCK, "a pornit inainte de 07:30");
    /* d) STOP anuleaza programul */
    send("STOP\n"); advance(10); clear_tx(); CHECK(!have_start && !have_end, "STOP nu a anulat programul");
    /* e) interval deja trecut: ramane armat, nu porneste */
    reset_state(); send("SET 10:00:00\n"); advance(10); clear_tx();
    send("START=09:00\nEND=09:30\n"); advance(10); clear_tx();
    advance(5000); CHECK(mode == MODE_CLOCK && have_start && have_end, "interval trecut: nu trebuia sa porneasca / sa dispara");
    /* f) peste miezul noptii */
    reset_state(); send("SET 23:59:00\n"); advance(10); clear_tx();
    send("START=23:30\nEND=00:02\n"); advance(10); clear_tx();
    CHECK(mode == MODE_COUNTDOWN && total_sec == 32*60 && remaining == 3*60, "interval peste miezul noptii: total=%u rem=%u", total_sec, remaining);
    send("STOP\n"); advance(10); clear_tx();
    /* f2) peste miezul noptii, ceasul e deja dupa 00:00 (in interiorul intervalului 23:30 - 00:02) */
    reset_state(); send("SET 00:01:00\n"); advance(10); clear_tx();
    send("START=23:30\nEND=00:02\n"); advance(10); clear_tx();
    CHECK(mode == MODE_COUNTDOWN && total_sec == 32*60 && remaining == 60, "interval peste miezul noptii, ceas dupa 00:00: mode=%d total=%u rem=%u", mode, total_sec, remaining);
    send("STOP\n"); advance(10); clear_tx();
    /* f3) peste miezul noptii, ceasul e INAINTE de interval (22:00) -> asteapta */
    reset_state(); send("SET 22:00:00\n"); advance(10); clear_tx();
    send("START=23:30\nEND=00:02\n"); advance(10); clear_tx();
    CHECK(mode == MODE_CLOCK && have_start && have_end, "interval peste miezul noptii, ceas inainte: a pornit gresit (mode=%d)", mode);
    send("STOP\n"); advance(10); clear_tx();
    /* g) START == END e refuzat */
    reset_state(); send("START=09:00\n"); advance(10); clear_tx(); send("END=09:00\n"); advance(10); expect_tx("ERR");
    CHECK(!have_start && !have_end, "START=END ramas armat");
    /* h) format */
    send("START=24:00\n"); advance(5); expect_tx("ERR"); send("START=09:60\n"); advance(5); expect_tx("ERR");
    send("END=9\n"); advance(5); expect_tx("ERR"); send("START=09:00:00\n"); advance(5); expect_tx("ERR");
    send("START=\n"); advance(5); expect_tx("ERR"); send("END=ab:cd\n"); advance(5); expect_tx("ERR");
    /* i) un program armat nu intrerupe un cronometru care ruleaza; porneste dupa ce se termina */
    reset_state(); send("SET 08:59:00\n"); advance(10); clear_tx();
    send("T=100\n"); advance(10); clear_tx();
    send("START=09:00\nEND=09:30\n"); advance(10); clear_tx();
    advance(30000); CHECK(mode == MODE_COUNTDOWN && total_sec == 100, "programul a intrerupt cronometrul manual (total=%u)", total_sec);
    advance(40000); CHECK(clock_sec >= 9*3600 && mode == MODE_COUNTDOWN && total_sec == 100 && remaining > 0 && remaining < 100,
                          "dupa ora START programul a intrerupt cronometrul manual (clock=%u total=%u rem=%u)", clock_sec, total_sec, remaining);
    for (int i = 0; i < 90000 && !(mode == MODE_COUNTDOWN && total_sec == 1800); i++) advance(1);
    CHECK(mode == MODE_COUNTDOWN && total_sec == 1800, "dupa cronometrul manual programul nu a pornit (mode=%d total=%u clock=%u)", mode, total_sec, clock_sec);
    ENDSEC();

    SECTION("14 sincronizare C= la fiecare secunda (cum face aplicatia)");
    g_ms = 900000; reset_state();                              /* placa porneste la 00:00:00 */
    { uint32_t t0 = g_ms, P0 = 14*3600+35*60+12; int resets = 0, maxd = 0; uint32_t prev_last;
      for (int i = 0; i < 400; i++) {
          advance(1005);                                       /* aplicatia: delay(1000) + prelucrare */
          uint32_t phone = (P0 + (g_ms - t0) / 1000) % DAY_SEC;
          char msg[24]; snprintf(msg, sizeof msg, "C=%02u:%02u:%02u\n", (unsigned)(phone/3600), (unsigned)((phone/60)%60), (unsigned)(phone%60));
          prev_last = clock_last; send(msg); advance(5);
          if (clock_last != prev_last && (clock_last - prev_last) % 1000 != 0) resets++;
          if (i > 0) { int d = circ_diff(clock_sec, phone); if (d > maxd) maxd = d; }
          CHECK(txn == 0, "C= a produs raspuns: '%s'", txlog); clear_tx();
      }
      CHECK(resets == 1, "faza ceasului a fost resetata de %d ori (asteptat 1: sincronizarea initiala)", resets);
      CHECK(maxd <= 1, "ceasul placii difera de telefon cu pana la %d s (asteptat <= 1)", maxd); }
    ENDSEC();

    printf("\n%d verificari, %d esecuri\n", checks, fails);
    return fails ? 1 : 0;
}
