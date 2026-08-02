/* Scan 01 — golden pack fixture generator (task T8)
 *
 * Builds a canonical pack through the REAL firmware pack layer (the same
 * code the radio runs) and dumps the EEPROM image for the packtool's
 * cross-language round-trip test. The fixture is committed; regenerate
 * with tools/build-golden.sh when the format changes (and update the
 * packtool together with the firmware — the test proves they match).
 */
#include <stdio.h>
#include <string.h>
#include "settings_pack.h"
extern void SIM_EEPROM_Reset(void);
extern void SIM_EEPROM_Get(void *out, uint16_t size);

static void add_car(const char *num, const char *name, const char *team,
                    uint32_t hz, uint8_t tone, uint8_t ct, uint8_t venue, bool fav)
{
    PackCar_t c;
    memset(&c, 0, sizeof(c));
    strncpy(c.number, num, 3);
    strncpy(c.name, name, 10);
    strncpy(c.team, team, 6);
    c.freq_hz = hz;
    c.tone_index = tone;
    c.code_type = ct;
    c.narrow = true;
    c.venue = venue;
    c.favorite = fav;
    if (!PACK_AddCapture(&c)) {
        printf("golden: add_car %s FAILED\n", num);
        return;
    }
}

int main(void)
{
    uint8_t image[0x2000];

    SIM_EEPROM_Reset();
    if (!PACK_Init()) {
        printf("golden: PACK_Init FAILED\n");
        return 1;
    }
    /* venue-ordered so the packtool's parse->build round-trip is stable */
    add_car("24", "BYRON W", "HMS", 450887500u, 10, PACK_CT_CTCSS, 0, true);
    add_car("48", "JOHNSON J", "HMS", 451112500u, 0, PACK_CT_NONE, 0, false);
    add_car("3", "DILLON A", "RCR", 452100000u, 0, PACK_CT_NONE, 1, false);
    if (!PACK_SaveLockout(0, true)) {          /* lock out car "24" */
        printf("golden: lockout FAILED\n");
        return 1;
    }
    if (!PACK_SetMyDriver("24")) {
        printf("golden: my-driver FAILED\n");
        return 1;
    }
    if (!PACK_SetSealed(true)) {
        printf("golden: seal FAILED\n");
        return 1;
    }
    SIM_EEPROM_Get(image, sizeof(image));
    FILE *f = fopen("tests/fixtures/golden_eeprom.bin", "wb");
    if (!f) {
        printf("golden: cannot write fixture\n");
        return 1;
    }
    fwrite(image, 1, sizeof(image), f);
    fclose(f);
    printf("golden: wrote tests/fixtures/golden_eeprom.bin\n");
    return 0;
}
