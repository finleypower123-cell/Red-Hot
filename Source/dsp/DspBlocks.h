/*
    PEPPER — small, allocation-free DSP building blocks.

    Everything here is header-only, has fixed-size state for up to two channels and
    never allocates, so it is safe to call from the audio thread.

    Some of the clipping curves are adapted from ideas in the Airwindows plug-ins
    by Chris Johnson (MIT licence, https://github.com/airwindows/airwindows):
      - sinClamp()      : the "Density" style sine soft-clip that reaches the rail
                          with zero slope, so it never produces a hard corner.
      - expandStage()   : the "Fracture" expansion stage, x * (|x| + 1).
      - DriveSlew / the ultrasonic filtering philosophy follow the Airwindows amp sims.
*/
#pragma once

#include <cmath>
#include <algorithm>

namespace pepper
{
constexpr int kMaxChannels = 2;
constexpr double kPi = 3.14159265358979323846;
constexpr float kHalfPi = 1.57079632679f;

//==============================================================================
// Waveshapers
//==============================================================================

/** Rational tanh approximation. Exactly ±1 with zero slope at |x| = 3, so no corner. */
inline float fastTanh (float x) noexcept
{
    x = std::clamp (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/** Sine soft-clip: linear-ish near zero, reaches ±1 at ±pi/2 with zero slope (Airwindows Density). */
inline float sinClamp (float x) noexcept
{
    if (x >= kHalfPi)  return 1.0f;
    if (x <= -kHalfPi) return -1.0f;
    return std::sin (x);
}

/** Cubic soft clip: harder knee than tanh, ±1 with zero slope at |x| = 1. */
inline float cubicClip (float x) noexcept
{
    x = std::clamp (x, -1.0f, 1.0f);
    return 1.5f * x - 0.5f * x * x * x;
}

/** Biased tanh — a triode-ish stage. Asymmetry adds even harmonics; slope at 0 is 1. */
inline float triode (float x, float bias) noexcept
{
    const float tb = fastTanh (bias);
    const float norm = 1.0f / std::max (0.2f, 1.0f - tb * tb);
    return (fastTanh (x + bias) - tb) * norm;
}

/** Airwindows "Fracture" expansion stage: grows faster than linear, feeds a folder. */
inline float expandStage (float x) noexcept { return x * (std::abs (x) + 1.0f); }

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

//==============================================================================
// Filters
//==============================================================================

/** RBJ biquad, transposed direct form II, double-precision state. */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[kMaxChannels] {}, z2[kMaxChannels] {};

    void reset() noexcept
    {
        std::fill (std::begin (z1), std::end (z1), 0.0);
        std::fill (std::begin (z2), std::end (z2), 0.0);
    }

    inline float process (float in, int ch) noexcept
    {
        const double x = in;
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return (float) y;
    }

    void setIdentity() noexcept { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    void setLowpass (double fs, double f, double q) noexcept
    {
        const auto w = omega (fs, f); const double c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }

    void setHighpass (double fs, double f, double q) noexcept
    {
        const auto w = omega (fs, f); const double c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }

    void setPeak (double fs, double f, double q, double db) noexcept
    {
        const auto w = omega (fs, f); const double c = std::cos (w), al = std::sin (w) / (2 * q);
        const double A = std::pow (10.0, db / 40.0);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }

    void setLowShelf (double fs, double f, double db, double slope = 0.8) noexcept
    {
        const auto w = omega (fs, f); const double c = std::cos (w), s = std::sin (w);
        const double A = std::pow (10.0, db / 40.0);
        const double al = s / 2 * std::sqrt ((A + 1 / A) * (1 / slope - 1) + 2);
        const double sa = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
             (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
    }

    void setHighShelf (double fs, double f, double db, double slope = 0.8) noexcept
    {
        const auto w = omega (fs, f); const double c = std::cos (w), s = std::sin (w);
        const double A = std::pow (10.0, db / 40.0);
        const double al = s / 2 * std::sqrt ((A + 1 / A) * (1 / slope - 1) + 2);
        const double sa = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
             (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
    }

private:
    static double omega (double fs, double f) noexcept
    {
        return 2.0 * kPi * std::clamp (f, 5.0, fs * 0.49) / fs;
    }

    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }
};

/** One-pole low/high pass. */
struct OnePole
{
    double a = 0.0;                    // feedback coefficient
    double z[kMaxChannels] {};

    void reset() noexcept { std::fill (std::begin (z), std::end (z), 0.0); }

    void setCutoff (double fs, double f) noexcept
    {
        a = std::exp (-2.0 * kPi * std::clamp (f, 1.0, fs * 0.49) / fs);
    }

    inline float lp (float x, int ch) noexcept
    {
        z[ch] = x + a * (z[ch] - x);
        return (float) z[ch];
    }

    inline float hp (float x, int ch) noexcept { return x - lp (x, ch); }
};

/** Peak envelope follower with separate attack / release. */
struct Envelope
{
    float att = 0, rel = 0;
    float z[kMaxChannels] {};

    void reset() noexcept { std::fill (std::begin (z), std::end (z), 0.0f); }

    void setTimes (double fs, double attackMs, double releaseMs) noexcept
    {
        att = (float) std::exp (-1.0 / (fs * attackMs * 0.001));
        rel = (float) std::exp (-1.0 / (fs * releaseMs * 0.001));
    }

    inline float process (float x, int ch) noexcept
    {
        const float in = std::abs (x);
        const float c = in > z[ch] ? att : rel;
        z[ch] = in + c * (z[ch] - in);
        return z[ch];
    }
};

/** Slew-rate limiter (the speed limit of a real op-amp or transistor stage).
    Leaves normal guitar signals alone but turns the infinitely sharp edges of a
    hard-clipped wave into short ramps, which is both smoother-sounding and far
    less prone to aliasing than a bare clipper. */
struct SlewLimiter
{
    float maxStep = 1.0f;
    float z[kMaxChannels] {};

    void reset() noexcept { std::fill (std::begin (z), std::end (z), 0.0f); }

    /** riseTime = seconds to swing from -1 to +1. */
    void setRiseTime (double fs, double riseTimeSec) noexcept
    {
        maxStep = (float) (2.0 / std::max (1.0, riseTimeSec * fs));
    }

    inline float process (float x, int ch) noexcept
    {
        z[ch] += std::clamp (x - z[ch], -maxStep, maxStep);
        return z[ch];
    }
};

/** The front end of a high-gain stage: soft supply rails followed by a slew limit,
    sized to the clipper that comes next. A real gain stage can't swing infinitely
    fast, so the clipper sees ramps instead of vertical edges — this keeps the
    harmonics musical and stops them folding back as aliasing. */
struct DriveSlew
{
    SlewLimiter slew;
    float rail = 6.0f, invRail = 1.0f / 6.0f;

    /** clipWindow: input span over which the next clipper goes from -1 to +1.
        crossTime: how long the stage takes to cross that span (seconds). */
    void prepare (double fs, float clipWindow, double crossTime, float railMultiple = 3.0f) noexcept
    {
        rail = clipWindow * railMultiple / 3.0f * 2.0f;
        invRail = 1.0f / rail;
        slew.maxStep = (float) (clipWindow / std::max (1.0, crossTime * fs));
    }

    void reset() noexcept { slew.reset(); }

    inline float process (float v, int ch) noexcept
    {
        return slew.process (rail * fastTanh (v * invRail), ch);
    }
};

/** Linear per-sample ramp between block-rate targets (zipper-free gain changes). */
struct Ramp
{
    float current = 0, target = 0, step = 0;

    void snap (float v) noexcept { current = target = v; step = 0; }

    void setTarget (float v, int numSamples) noexcept
    {
        target = v;
        step = numSamples > 0 ? (target - current) / (float) numSamples : 0.0f;
        if (numSamples <= 0) current = v;
    }

    inline float next() noexcept { current += step; return current; }
    void finish() noexcept { current = target; step = 0; }
};
} // namespace pepper
