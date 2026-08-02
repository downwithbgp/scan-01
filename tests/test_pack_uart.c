/* Scan 01 — pack_status text builder tests (task T7, spec §7) */
#include <stdio.h>
#include <string.h>
#include "pack_uart.h"
#include "settings_pack.h"

static int g_checks = 0, g_failures = 0;
static void expect(bool cond, const char *what) {
    g_checks++;
    if (!cond) { g_failures++; printf("FAIL: %s\n", what); }
}

static uint8_t g_eeprom[0x2000];
void EEPROM_ReadBuffer(uint16_t Address, void *pBuffer, uint8_t Size) { memcpy(pBuffer, g_eeprom + Address, Size); }
void EEPROM_WriteBuffer(uint16_t Address, const void *pBuffer) { memcpy(g_eeprom + Address, pBuffer, 8); }

static PackCar_t make_car(const char *num, const char *name, uint32_t hz, uint8_t tone, uint8_t ct) {
    PackCar_t c; memset(&c, 0, sizeof(c));
    strncpy(c.number, num, 3); strncpy(c.name, name, 10);
    c.freq_hz = hz; c.tone_index = tone; c.code_type = ct; c.narrow = true;
    return c;
}

int main(void) {
    memset(g_eeprom, 0xFF, sizeof(g_eeprom));
    memset(g_eeprom, 0, 0x50);
    expect(PACK_Init(), "status: init (demo)");
    PackCar_t c = make_car("24", "NEW", 450887500u, 10, PACK_CT_CTCSS);
    expect(PACK_AddCapture(&c), "status: capture");
    expect(PACK_SetSealed(true), "status: seal");

    char out[256];
    uint16_t n = PACK_UART_BuildStatus(out, sizeof(out));
    expect(n > 0 && n < sizeof(out), "status: bounded length");
    expect(strstr(out, "SC01 v1") != NULL, "status: magic+version");
    expect(strstr(out, "cars=1 stations=7") != NULL, "status: counts");
    expect(strstr(out, "sealed=1 practice=0") != NULL, "status: seal+practice");
    expect(strstr(out, "captured 24 450.8875 NEW") != NULL, "status: captured entry");
    expect(strstr(out, "captured") == strstr(out, "captured 24"), "status: only one captured line");

    uint16_t short_n = PACK_UART_BuildStatus(out, 8);
    expect(short_n <= 8, "status: truncated safely");

    printf("pack uart: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
