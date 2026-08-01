/* Scan 01 — host unit + property tests for pack_bandlock (task T2 gate)
 *
 * Compile: gcc -Wall -Werror -I. tests/test_bandlock.c pack_bandlock.c -o /tmp/test_bandlock
 * Run:    /tmp/test_bandlock
 *
 * Properties under test (spec/p1-pack-eeprom §8):
 *   P1 soundness:    allowed(f, m)  ⇒ ¬cellular(f) ∧ (entry(f) ∨ (m=PRACTICE ∧ in2m(f)))
 *   P2 completeness: ¬cellular(f) ∧ (entry(f) ∨ (m=PRACTICE ∧ in2m(f))) ⇒ allowed(f, m)
 *   P3 mode monotonicity: allowed(f, RACE) ⇒ allowed(f, PRACTICE)
 *   P4 determinism: same input ⇒ same output (implied by P1/P2 over repeated draws)
 * plus an explicit expected-value boundary table (every band edge ± 1 Hz, both modes).
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "pack_bandlock.h"

static int g_checks = 0;
static int g_failures = 0;

static void expect(bool cond, const char *what, uint32_t freq, PACK_Mode_t mode)
{
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("FAIL %s: freq=%u (%u.%03u MHz) mode=%s\n",
               what, freq, freq / 1000000u, (freq / 1000u) % 1000u,
               mode == PACK_MODE_RACE ? "RACE" : "PRACTICE");
    }
}

/* ---- independent classifier, recomputed from the spec lists ---- */

static bool in_cellular(uint32_t f)
{
    static const uint32_t c[4][2] = {
        {  824000000u,  894000000u },
        { 1710000000u, 1755000000u },
        { 1850000000u, 1990000000u },
        { 2110000000u, 2155000000u },
    };
    for (int i = 0; i < 4; i++)
        if (f >= c[i][0] && f <= c[i][1])
            return true;
    return false;
}

static bool in_entry_union(uint32_t f)
{
    /* 88-108, 108-137, 151-162, 162.4-162.55, 450-470 */
    static const uint32_t b[5][2] = {
        {  88000000u,  108000000u },
        { 108000000u,  137000000u },
        { 151000000u,  162000000u },
        { 162400000u,  162550000u },
        { 450000000u,  470000000u },
    };
    for (int i = 0; i < 5; i++)
        if (f >= b[i][0] && f <= b[i][1])
            return true;
    return false;
}

static bool in_2m(uint32_t f)
{
    return f >= 144000000u && f <= 148000000u;
}

static bool expected(uint32_t f, PACK_Mode_t mode)
{
    if (in_cellular(f))
        return false;
    if (in_entry_union(f))
        return true;
    if (mode == PACK_MODE_PRACTICE && in_2m(f))
        return true;
    return false;
}

/* ---- deterministic xorshift32 ---- */

static uint32_t g_rng = 0xC0FFEE01u;
static uint32_t rng(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

int main(void)
{
    /* ---- explicit cases (real-world frequencies) ---- */
    struct { uint32_t freq; PACK_Mode_t mode; bool want; const char *name; } cases[] = {
        {  101100000u, PACK_MODE_RACE,     true,  "MRN broadcast 101.1" },
        {  118000000u, PACK_MODE_RACE,     true,  "airband 118.0" },
        {  121500000u, PACK_MODE_RACE,     true,  "airband guard 121.5" },
        {  146940000u, PACK_MODE_RACE,     false, "2m Skywarn in RACE" },
        {  146940000u, PACK_MODE_PRACTICE, true,  "2m Skywarn in PRACTICE" },
        {  151000000u, PACK_MODE_RACE,     true,  "dirt-track 151.0" },
        {  156800000u, PACK_MODE_RACE,     true,  "marine 156.8" },
        {  162550000u, PACK_MODE_RACE,     true,  "NOAA 162.55" },
        {  450887500u, PACK_MODE_RACE,     true,  "racing 450.8875" },
        {  462562500u, PACK_MODE_RACE,     true,  "FRS 462.5625" },
        {  470000001u, PACK_MODE_RACE,     false, "above racing band" },
        {   100000000u, PACK_MODE_RACE,    true,  "broadcast 100.0" },
        {   87999999u, PACK_MODE_RACE,     false, "below broadcast" },
        {  824000000u, PACK_MODE_RACE,     false, "cellular 824.0" },
        {  893999999u, PACK_MODE_RACE,     false, "cellular top-1" },
        { 1850000000u, PACK_MODE_PRACTICE, false, "cellular 1850 in PRACTICE" },
        { 1990000000u, PACK_MODE_PRACTICE, false, "cellular 1990 in PRACTICE" },
    };
    for (unsigned int i = 0; i < (sizeof(cases) / sizeof(cases[0])); i++)
        expect(PACK_FreqAllowed(cases[i].freq, cases[i].mode) == cases[i].want,
               cases[i].name, cases[i].freq, cases[i].mode);

    /* ---- boundary table: every band edge ±1 Hz, both modes ---- */
    struct { uint32_t lo, hi; } bands[] = {
        {  88000000u,  108000000u },
        { 108000000u,  137000000u },
        { 144000000u,  148000000u },
        { 151000000u,  162000000u },
        { 162400000u,  162550000u },
        { 450000000u,  470000000u },
        {  824000000u,  894000000u },  /* cellular edges: always reject */
        { 1710000000u, 1755000000u },
        { 1850000000u, 1990000000u },
        { 2110000000u, 2155000000u },
    };
    for (unsigned int b = 0; b < (sizeof(bands) / sizeof(bands[0])); b++) {
        uint32_t edges[4] = {
            bands[b].lo > 0 ? bands[b].lo - 1 : 0,
            bands[b].lo,
            bands[b].hi,
            bands[b].hi + 1,
        };
        for (int m = PACK_MODE_RACE; m <= PACK_MODE_PRACTICE; m++) {
            for (int e = 0; e < 4; e++) {
                bool want = expected(edges[e], (PACK_Mode_t)m);
                expect(PACK_FreqAllowed(edges[e], (PACK_Mode_t)m) == want,
                       "boundary", edges[e], (PACK_Mode_t)m);
            }
        }
    }

    /* ---- property sweep: 400k random freqs over 1 Hz .. 3 GHz, both modes ---- */
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1)
            g_rng = 0x5EED0001u;  /* distinct stream per pass */
        for (int i = 0; i < 200000; i++) {
            uint32_t f = (rng() % 3000000000u) + 1u;
            for (int m = PACK_MODE_RACE; m <= PACK_MODE_PRACTICE; m++) {
                bool got = PACK_FreqAllowed(f, (PACK_Mode_t)m);
                bool want = expected(f, (PACK_Mode_t)m);
                /* P1+P2: equivalence with the classifier */
                expect(got == want, "property equivalence", f, (PACK_Mode_t)m);
                /* P3: mode monotonicity */
                if (m == PACK_MODE_RACE && got)
                    expect(PACK_FreqAllowed(f, PACK_MODE_PRACTICE),
                           "mode monotonicity", f, PACK_MODE_PRACTICE);
            }
        }
    }

    printf("band-lock: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
