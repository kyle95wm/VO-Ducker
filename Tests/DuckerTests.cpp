// Headless tests for the ducking engine. Run with: ctest --test-dir build --output-on-failure
#include "DuckerEngine.h"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using voducker::DuckerEngine;
using voducker::DuckerParams;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) {                                               \
            ++failures;                                               \
            std::printf ("FAIL %s:%d: ", __FILE__, __LINE__);         \
            std::printf (__VA_ARGS__);                                \
            std::printf ("\n");                                       \
        }                                                             \
    } while (0)

static bool near (float a, float b, float tol) { return std::abs (a - b) <= tol; }
static float toDb (float g) { return 20.0f * std::log10 (std::max (g, 1.0e-12f)); }

static void testStaticCurve()
{
    const float t = -30.0f;
    CHECK (DuckerEngine::computeReductionDb (-50.0f, t) == 0.0f, "below knee should be 0 dB");
    CHECK (DuckerEngine::computeReductionDb (-39.0f, t) == 0.0f, "knee start should be 0 dB");
    CHECK (near (DuckerEngine::computeReductionDb (-30.0f, t), 0.75f * 81.0f / 36.0f, 1e-4f), "at threshold: knee midpoint");
    CHECK (near (DuckerEngine::computeReductionDb (-21.0f, t), 6.75f, 1e-4f), "knee end should meet 4:1 line");
    CHECK (near (DuckerEngine::computeReductionDb (-10.0f, t), 15.0f, 1e-4f), "20 dB over at 4:1 = 15 dB GR");
    CHECK (DuckerEngine::computeReductionDb (200.0f, -60.0f) == DuckerEngine::kMaxReductionDb, "GR capped at 60 dB");
}

// Runs the engine over whole signals in blocks of 512 and returns the processed main signal.
static std::vector<float> run (DuckerEngine& e, std::vector<float> main, const std::vector<float>& key)
{
    const int block = 512;
    for (size_t pos = 0; pos < main.size(); pos += block)
    {
        const int n = (int) std::min<size_t> (block, main.size() - pos);
        float* m[1] = { main.data() + pos };
        const float* k[1] = { key.data() + pos };
        e.process (m, 1, k, 1, n);
    }
    return main;
}

static void testNeverAddsGain()
{
    DuckerEngine e;
    e.prepare (48000.0, 1);
    DuckerParams p; p.thresholdDb = -40.0f; p.attackMs = 1.0f; p.releaseMs = 5.0f; p.lookaheadMs = 0.0f;
    e.setParams (p, true);

    std::mt19937 rng (1);
    std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    std::vector<float> main (96000), key (96000);
    for (size_t i = 0; i < main.size(); ++i) { main[i] = d (rng); key[i] = d (rng) * (i % 20000 < 10000 ? 1.0f : 0.0f); }

    const auto out = run (e, main, key);
    for (size_t i = 0; i < out.size(); ++i)
        if (std::abs (out[i]) > std::abs (main[i]) + 1e-6f) { CHECK (false, "gain > 0 dB at sample %zu", i); break; }
}

static void testLookaheadAndHold (float holdMs)
{
    const double fs = 48000.0;
    DuckerEngine e;
    e.prepare (fs, 1);
    DuckerParams p;
    p.thresholdDb = -30.0f; p.attackMs = 5.0f; p.releaseMs = 50.0f; p.holdMs = holdMs; p.lookaheadMs = 100.0f;
    e.setParams (p, true);

    const int L = e.getLatencySamples();
    CHECK (L == 4800, "latency should be 100 ms = 4800 samples, got %d", L);

    const int onset = 48000, offset = 72000, total = 192000;
    std::vector<float> main (total, 0.5f), key (total, 0.0f);
    for (int i = onset; i < offset; ++i) key[i] = (i % 2 ? 1.0f : -1.0f); // 0 dBFS
    const auto out = run (e, main, key);

    const float expectedGr = DuckerEngine::computeReductionDb (0.0f, p.thresholdDb); // 22.5 dB
    auto grAt = [&] (int i) { return -toDb (out[(size_t) i] / 0.5f); };

    // Before the key arrives (in output time, minus the lookahead) there is no ducking.
    CHECK (grAt (onset - 1) < 0.01f, "ducked too early: %.3f dB", grAt (onset - 1));
    // Ducking begins `lookahead` early and is complete by the time the delayed audio reaches the onset.
    CHECK (grAt (onset + L / 2) > 0.5f * expectedGr, "not ducking ahead of the voice: %.2f dB", grAt (onset + L / 2));
    CHECK (near (grAt (onset + L), expectedGr, 0.1f), "not fully ducked at the voice onset: %.2f dB", grAt (onset + L));
    // Stays fully ducked until the voice really ends in the delayed signal (+ hold).
    const int holdSamples = (int) std::lround (holdMs * 0.001 * fs);
    CHECK (near (grAt (offset + L - 1), expectedGr, 0.1f), "released before voice ended: %.2f dB", grAt (offset + L - 1));
    if (holdSamples > 0)
        CHECK (near (grAt (offset + L + holdSamples - 10), expectedGr, 0.1f), "hold not respected: %.2f dB",
               grAt (offset + L + holdSamples - 10));
    // Then it releases.
    const int afterRelease = offset + L + holdSamples + (int) (0.05 * fs * 8);
    CHECK (grAt (afterRelease) < 0.05f, "did not release: %.3f dB", grAt (afterRelease));
}

static void testDeepReduction()
{
    DuckerEngine e;
    e.prepare (48000.0, 1);
    DuckerParams p; p.thresholdDb = -60.0f; p.attackMs = 1.0f; p.releaseMs = 100.0f;
    e.setParams (p, true);

    std::vector<float> main (48000, 1.0f), key (48000, 1.0f);
    const auto out = run (e, main, key);
    const float gr = -toDb (out.back());
    CHECK (near (gr, 45.0f, 0.1f), "0 dBFS key at -60 threshold should give 45 dB GR, got %.2f", gr);
}

static void testBypassKeepsLatency()
{
    DuckerEngine e;
    e.prepare (48000.0, 1);
    DuckerParams p; p.lookaheadMs = 10.0f;
    e.setParams (p, true);
    std::vector<float> x (2000, 0.0f);
    x[0] = 1.0f;
    float* m[1] = { x.data() };
    e.processDelayOnly (m, 1, (int) x.size());
    CHECK (x[480] == 1.0f && x[0] == 0.0f, "bypass should delay by the lookahead");
}

static void testMaxLookahead()
{
    for (double fs : { 44100.0, 48000.0, 192000.0 })
    {
        DuckerEngine e;
        e.prepare (fs, 2);
        DuckerParams p; p.lookaheadMs = 1000.0f; p.holdMs = 500.0f;
        e.setParams (p, true);
        CHECK (e.getLatencySamples() == (int) fs, "1000 ms lookahead at %.0f Hz should be %d samples, got %d",
               fs, (int) fs, e.getLatencySamples());

        // An impulse must come out exactly one second later.
        std::vector<float> l ((size_t) fs + 10, 0.0f), r = l;
        l[0] = r[0] = 1.0f;
        float* m[2] = { l.data(), r.data() };
        e.processDelayOnly (m, 2, (int) l.size());
        CHECK (l[(size_t) fs] == 1.0f && r[(size_t) fs] == 1.0f, "impulse not delayed by 1 s at %.0f Hz", fs);
    }
}

static void testNoSidechainMeansNoDucking()
{
    DuckerEngine e;
    e.prepare (48000.0, 1);
    DuckerParams p; p.thresholdDb = -60.0f; p.attackMs = 1.0f; p.lookaheadMs = 10.0f;
    e.setParams (p, true);

    // Loud programme, no key channels at all: output must be the input, delayed by the lookahead.
    std::vector<float> x (4800), in (4800);
    for (size_t i = 0; i < x.size(); ++i) x[i] = in[i] = (i % 2 ? 0.9f : -0.9f);
    float* m[1] = { x.data() };
    e.process (m, 1, nullptr, 0, (int) x.size());

    const int L = e.getLatencySamples();
    for (size_t i = (size_t) L; i < x.size(); ++i)
        if (x[i] != in[i - (size_t) L]) { CHECK (false, "signal changed without a sidechain at sample %zu", i); break; }
    CHECK (e.getCurrentReductionDb() == 0.0f, "meter should show no reduction without a sidechain");
}

int main()
{
    testNoSidechainMeansNoDucking();
    testMaxLookahead();
    testStaticCurve();
    testNeverAddsGain();
    testLookaheadAndHold (0.0f);
    testLookaheadAndHold (200.0f);
    testDeepReduction();
    testBypassKeepsLatency();

    if (failures == 0)
        std::printf ("All DuckerEngine tests passed.\n");
    return failures == 0 ? 0 : 1;
}
