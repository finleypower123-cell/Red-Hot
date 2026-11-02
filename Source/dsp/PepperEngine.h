/*
    PEPPER — the full signal chain around the six pepper voices.

        input -> noise floor control -> Tightness high-pass -> [pepper voice, oversampled]
              -> pick-attack emphasis -> + bass catch (lows that skip the clipper)
              -> low-end restoration ("thump")
              -> Tone (tilt + presence + roll-off) -> rumble guard -> Output

    Everything is preallocated in prepare(); process() never allocates or locks.
    Processing runs in 32-sample chunks so filter coefficients follow smoothed
    parameters without zipper noise, and pepper changes crossfade over 30 ms.
*/
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>
#include "PepperModes.h"

namespace pepper
{
struct EngineParams
{
    int pepper = 2;
    float heat = 0.6f, tone = 0.5f, tight = 0.5f, fuzz = 0.0f, outputDb = 0.0f;
};

class PepperEngine
{
public:
    PepperEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Set the latest parameter values (smoothed internally). Call before process(). */
    void setParams (const EngineParams& p) noexcept { target = p; }

    /** In-place. Uses the first min(numChannels, 2) channels. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    int getLatencySamples() const noexcept { return latency; }
    float getInputPeak() const noexcept  { return inPeak; }
    float getOutputPeak() const noexcept { return outPeak; }

    static constexpr int kChunk = 32;

private:
    void processChunk (float* const* channels, int numChannels, int offset, int n) noexcept;
    void startPepperChange (int newPepper) noexcept;
    void updateFilters (bool force) noexcept;
    static float bassCatchLevel (float thump, float tightness) noexcept;

    std::array<std::unique_ptr<PepperMode>, kNumPeppers> modes;
    double fs = 44100.0;
    int latency = 0;

    EngineParams target;
    int current = 2, previous = -1;
    float fadePos = 1.0f, fadeStep = 0.0f;

    juce::SmoothedValue<float> heat, tone, tight, fuzz, outGain;
    juce::SmoothedValue<float> tightHz, thumpDb, transientAmt, gateAmt;

    // shared filters
    Biquad tightHp, thumpShelf, toneShelf, tonePeak, toneLp, rumbleHp;
    Biquad bassHp, bassLp1, bassLp2;            // "bass catch": clean-ish lows around the clipper
    juce::SmoothedValue<float> bassAmt;
    juce::AudioBuffer<float> bassBuf;
    float lastTightHz = -1, lastThump = -100, lastTone = -1;

    // dynamics
    Envelope gateEnv, fastEnv, slowEnv;
    float gateGain = 1.0f, transGain = 1.0f;
    float gateOpenCoef = 0, gateCloseCoef = 0, transSmoothCoef = 0;

    // scratch (allocated in prepare)
    juce::AudioBuffer<float> preBuf, bufA, bufB;
    std::array<float, kChunk> transGainBuf {};

    float inPeak = 0, outPeak = 0;
};
} // namespace pepper
