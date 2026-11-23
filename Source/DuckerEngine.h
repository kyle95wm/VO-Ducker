#pragma once

// Framework-free DSP core for VO Ducker. Kept independent of JUCE so it can be
// unit-tested headlessly (see Tests/DuckerTests.cpp).
//
// Signal flow, per sample n:
//
//   key (sidechain) --> peak level (stereo-linked) --> gain computer
//        (4:1, 18 dB soft knee, threshold)   = target reduction g[n] in dB
//   g --> sliding maximum over (lookahead + hold) samples      = held target
//   held target --> attack / release smoothing (dB domain)    = env[n]
//   main --> delay by lookahead samples --> * 10^(-env/20) --> out
//   (bypass crossfades that gain to 1 over 20 ms; the delay stays)
//
// Why the sliding maximum matters: delaying the main signal alone makes the
// gain reduction *start* early, but it also makes it *end* early (the gain
// would begin to release `lookahead` ms before the voice actually stops).
// Holding the target for the full lookahead window keeps the reduction engaged
// until the delayed audio has caught up, so the duck both starts ahead of the
// voice and stays down until the voice really ends. The user Hold time is then
// added on top, after which Release takes over.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace voducker
{

struct DuckerParams
{
    float thresholdDb = -30.0f;  // -60 .. 0
    float attackMs    = 10.0f;   // 1 .. 250
    float releaseMs   = 250.0f;  // 1 .. 5000
    float holdMs      = 0.0f;    // 0 .. 500
    float lookaheadMs = 0.0f;    // 0 .. 1000
};

class DuckerEngine
{
public:
    static constexpr float kRatio          = 4.0f;
    static constexpr float kKneeDb         = 18.0f;
    static constexpr float kMaxReductionDb = 60.0f;
    static constexpr float kMaxLookaheadMs = 1000.0f;
    static constexpr float kMaxHoldMs      = 500.0f;
    static constexpr float kBypassFadeMs   = 20.0f;

    // Static gain curve: input level (dBFS) -> gain reduction in dB (>= 0).
    static float computeReductionDb (float levelDb, float thresholdDb) noexcept
    {
        const float over = levelDb - thresholdDb;
        const float slope = 1.0f - 1.0f / kRatio; // 0.75 for 4:1
        float reduction;

        if (2.0f * over <= -kKneeDb)
            reduction = 0.0f;
        else if (2.0f * over < kKneeDb)
        {
            const float x = over + kKneeDb * 0.5f;
            reduction = slope * x * x / (2.0f * kKneeDb);
        }
        else
            reduction = slope * over;

        return std::min (reduction, kMaxReductionDb);
    }

    void prepare (double sampleRate, int numMainChannels)
    {
        fs = sampleRate;
        numChannels = std::max (1, numMainChannels);

        maxLookaheadSamples = msToSamples (kMaxLookaheadMs);
        const int maxWindow = maxLookaheadSamples + msToSamples (kMaxHoldMs) + 1;

        delaySize = maxLookaheadSamples + 1;
        delay.assign ((size_t) (numChannels * delaySize), 0.0f);

        dequeCap = maxWindow + 1;
        dequeIdx.assign ((size_t) dequeCap, 0);
        dequeVal.assign ((size_t) dequeCap, 0.0f);

        fadeStep = (float) (1.0 / std::max (1.0, kBypassFadeMs * 0.001 * fs));

        reset();
        setParams (params, true);
    }

    void reset()
    {
        std::fill (delay.begin(), delay.end(), 0.0f);
        writePos = 0;
        dqHead = dqTail = 0;
        sampleCounter = 0;
        envDb = 0.0f;
        wetMix = bypassed ? 0.0f : 1.0f;
        meterReductionDb.store (0.0f);
    }

    // Returns true if the latency (lookahead) changed.
    bool setParams (const DuckerParams& p, bool force = false)
    {
        const bool timingChanged = force
            || differs (p.attackMs, params.attackMs) || differs (p.releaseMs, params.releaseMs)
            || differs (p.holdMs, params.holdMs) || differs (p.lookaheadMs, params.lookaheadMs);

        params = p;

        if (! timingChanged || fs <= 0.0)
            return false;

        attackCoeff  = timeToCoeff (p.attackMs);
        releaseCoeff = timeToCoeff (p.releaseMs);

        const int newLookahead = std::clamp (msToSamples (p.lookaheadMs), 0, maxLookaheadSamples);
        const int holdSamples  = std::clamp (msToSamples (p.holdMs), 0, msToSamples (kMaxHoldMs));
        windowSamples = newLookahead + holdSamples + 1;

        const bool latencyChanged = newLookahead != lookaheadSamples;
        lookaheadSamples = newLookahead;
        return latencyChanged;
    }

    int getLatencySamples() const noexcept { return lookaheadSamples; }

    // Bypass fades the ducking out (or back in) over kBypassFadeMs so it never clicks.
    // The detector keeps running while bypassed, so switching back on lands straight on
    // the right amount of ducking, and the lookahead delay stays so timing never shifts.
    void setBypassed (bool shouldBypass) noexcept { bypassed = shouldBypass; }
    bool isBypassed() const noexcept { return bypassed; }

    // main: numMain channels, processed in place.
    // key:  numKey sidechain channels. With numKey == 0 nothing is ducked: the main signal
    //       is only delayed by the lookahead, so timing stays the same as when keyed.
    void process (float* const* main, int numMain, const float* const* key, int numKey, int numSamples) noexcept
    {
        numMain = std::min (numMain, numChannels);
        float blockMax = 0.0f;

        for (int n = 0; n < numSamples; ++n)
        {
            // 1. Detector: stereo-linked peak of the key signal.
            //    No sidechain = silent key = no ducking (the track passes through, only delayed).
            float peak = 0.0f;
            for (int c = 0; c < numKey; ++c)
                peak = std::max (peak, std::abs (key[c][n]));

            const float levelDb = peak > 1.0e-9f ? 20.0f * std::log10 (peak) : -180.0f;
            const float target = computeReductionDb (levelDb, params.thresholdDb);

            // 2. Sliding max over the lookahead + hold window.
            const float held = pushAndGetMax (target);

            // 3. Attack / release ballistics in the dB domain.
            const float coeff = held > envDb ? attackCoeff : releaseCoeff;
            envDb = held + coeff * (envDb - held);
            if (envDb < 1.0e-6f) envDb = 0.0f;

            // 4. Bypass crossfade: blend the ducking gain towards unity.
            const float mixTarget = bypassed ? 0.0f : 1.0f;
            if (differs (wetMix, mixTarget))
                wetMix = bypassed ? std::max (0.0f, wetMix - fadeStep) : std::min (1.0f, wetMix + fadeStep);

            const float duckGain = std::pow (10.0f, -envDb * 0.05f);
            const float gain = 1.0f + wetMix * (duckGain - 1.0f);
            blockMax = std::max (blockMax, envDb * wetMix);

            // 5. Delay the main signal by the lookahead and apply the gain.
            const int readPos = wrap (writePos - lookaheadSamples);
            for (int c = 0; c < numMain; ++c)
            {
                float* line = delay.data() + (size_t) c * (size_t) delaySize;
                line[writePos] = main[c][n];
                main[c][n] = line[readPos] * gain;
            }
            writePos = wrap (writePos + 1);
        }

        meterReductionDb.store (blockMax, std::memory_order_relaxed);
    }

    // Bypass path: keep the same latency so bypassing does not shift timing.
    void processDelayOnly (float* const* main, int numMain, int numSamples) noexcept
    {
        numMain = std::min (numMain, numChannels);
        for (int n = 0; n < numSamples; ++n)
        {
            const int readPos = wrap (writePos - lookaheadSamples);
            for (int c = 0; c < numMain; ++c)
            {
                float* line = delay.data() + (size_t) c * (size_t) delaySize;
                line[writePos] = main[c][n];
                main[c][n] = line[readPos];
            }
            writePos = wrap (writePos + 1);
        }
        meterReductionDb.store (0.0f, std::memory_order_relaxed);
    }

    float getCurrentReductionDb() const noexcept { return meterReductionDb.load (std::memory_order_relaxed); }

private:
    static bool differs (float a, float b) noexcept { return std::abs (a - b) > 0.0f; }

    int msToSamples (float ms) const noexcept { return (int) std::lround (ms * 0.001 * fs); }

    float timeToCoeff (float ms) const noexcept
    {
        const double samples = std::max (1.0, ms * 0.001 * fs);
        return (float) std::exp (-1.0 / samples);
    }

    int wrap (int i) const noexcept
    {
        i %= delaySize;
        return i < 0 ? i + delaySize : i;
    }

    // Monotonic deque (values non-increasing from head to tail) giving the max
    // of the last `windowSamples` targets in O(1) amortised per sample.
    float pushAndGetMax (float v) noexcept
    {
        const int64_t now = sampleCounter++;

        while (dqHead != dqTail && dequeVal[(size_t) prevSlot (dqTail)] <= v)
            dqTail = prevSlot (dqTail);

        dequeIdx[(size_t) dqTail] = now;
        dequeVal[(size_t) dqTail] = v;
        dqTail = nextSlot (dqTail);

        while (dequeIdx[(size_t) dqHead] <= now - windowSamples)
            dqHead = nextSlot (dqHead);

        return dequeVal[(size_t) dqHead];
    }

    int nextSlot (int i) const noexcept { return i + 1 == dequeCap ? 0 : i + 1; }
    int prevSlot (int i) const noexcept { return i == 0 ? dequeCap - 1 : i - 1; }

    double fs = 0.0;
    int numChannels = 2;
    DuckerParams params;

    int maxLookaheadSamples = 0;
    int lookaheadSamples = 0;
    int windowSamples = 1;

    std::vector<float> delay;
    int delaySize = 1;
    int writePos = 0;

    std::vector<int64_t> dequeIdx;
    std::vector<float> dequeVal;
    int dequeCap = 2;
    int dqHead = 0, dqTail = 0;
    int64_t sampleCounter = 0;

    float attackCoeff = 0.0f, releaseCoeff = 0.0f;
    float envDb = 0.0f;

    bool bypassed = false;
    float wetMix = 1.0f;   // 1 = ducking applied, 0 = bypassed
    float fadeStep = 0.001f;

    std::atomic<float> meterReductionDb { 0.0f };
};

} // namespace voducker
