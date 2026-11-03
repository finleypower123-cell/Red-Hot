/*
    PEPPER — the six pepper voices.

    Each pepper is its own small "circuit": its own pre-EQ, its own clipping
    topology running oversampled, and its own post-EQ voicing. Heat moves
    gain *and* character inside each one, so switching peppers changes the kind of
    distortion, not just the amount.

        Jalapeño        TS-style mid-push into a warm, biased amp stage
        Serrano         biased triode -> cathode-bypass high-pass -> gritty sine clipper
        Cayenne         op-amp gain with gain-dependent bandwidth into hard diodes (thick)
        Bird's Eye      boosted, high-passed 3-stage cascade, tight lows, sharp attack
        Habanero        4-stage cascade with power-supply sag compression (dense, modern)
        Carolina Reaper sine-clip -> Fracture expansion -> wavefold blend -> starved stage

    The Fuzz control blends a parallel fuzz voice (two-stage clip, octave-up
    rectifier, scooped mids) into whichever pepper is active.
*/
#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include "DspBlocks.h"

namespace pepper
{
/** The nonlinear stages run at ~350-400 kHz whatever the session rate:
    8x at 44.1/48k, 4x at 88.2/96k, 2x at 176.4/192k. High-gain clipping genuinely
    needs this to keep fizzy aliasing out of the top end. */
inline int oversamplingOrderFor (double sampleRate)
{
   #ifdef PEPPER_OS_ORDER
    juce::ignoreUnused (sampleRate);
    return PEPPER_OS_ORDER; // test override
   #else
    return sampleRate <= 50000.0 ? 3 : (sampleRate <= 100000.0 ? 2 : 1);
   #endif
}
constexpr int kNumPeppers = 6;

enum class PepperType { jalapeno = 0, serrano, cayenne, birdsEye, habanero, reaper };

/** A value that is linearly interpolated across one processing block. */
struct BlockParam
{
    float start = 0, end = 0;
    void snap (float v) noexcept { start = end = v; }
    void next (float v) noexcept { start = end; end = v; }
    inline float at (float t) const noexcept { return start + (end - start) * t; }
};


//==============================================================================
/** Parallel fuzz voice blended in by the Fuzz control. Runs at the oversampled rate. */
struct FuzzVoice
{
    OnePole inHp, midLp, interHp, avgAbs, outLp;
    Biquad scoop;
    DriveSlew front, second;

    void prepare (double osFs)
    {
        inHp.setCutoff (osFs, 70.0);
        midLp.setCutoff (osFs, 5200.0);
        interHp.setCutoff (osFs, 110.0);
        avgAbs.setCutoff (osFs, 30.0);
        outLp.setCutoff (osFs, 7500.0);
        scoop.setPeak (osFs, 750.0, 0.6, -6.0);
        front.prepare (osFs, 4.0f, 30.0e-6);
        second.prepare (osFs, 2.0f, 25.0e-6);
    }

    void reset()
    {
        inHp.reset(); midLp.reset(); interHp.reset(); avgAbs.reset(); outLp.reset(); scoop.reset();
        front.reset(); second.reset();
    }

    /** drive is a linear gain; fold (0..1) adds wavefolding for the Reaper. */
    inline float process (float x, int ch, float drive, float fold) noexcept
    {
        float s1 = fastTanh (front.process (inHp.hp (x, ch) * drive, ch)); // first transistor
        s1 = midLp.lp (s1, ch);                                           // woolly between-stage filter

        // octave-up: full-wave rectified, DC removed (the "ring" in a good fuzz)
        const float rect = std::abs (s1);
        const float oct = rect - avgAbs.lp (rect, ch);

        // starved, biased second transistor
        const float v2 = second.process (interHp.hp (s1, ch) * 7.0f, ch) + 0.12f;
        const float s2 = cubicClip (v2) - cubicClip (0.12f);
        float y = s2 * 0.82f + oct * 0.55f;

        if (fold > 0.0f)
            y = y * (1.0f - fold) + fastSin (y * 2.6f) * fold;

        y = scoop.process (y, ch);
        return outLp.lp (y, ch) * 0.75f;
    }
};

//==============================================================================
/** Base class for a pepper. Owns its oversampler, pre/post EQ, fuzz voice and level trim. */
class PepperMode
{
public:
    PepperMode() = default;

    virtual ~PepperMode() = default;

    void prepare (double sampleRate, int maxBlockSize)
    {
        fs = sampleRate;
        const int order = oversamplingOrderFor (sampleRate);
        if (oversampler == nullptr || order != osOrder)
        {
            osOrder = order;
            oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
                kMaxChannels, (size_t) order, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        }
        osFs = sampleRate * (double) (1 << osOrder);
        oversampler->initProcessing ((size_t) maxBlockSize);
        fuzzVoice.prepare (osFs);
        for (auto& b : pre)  b.setIdentity();
        for (auto& b : post) b.setIdentity();
        configure();
        lastTight = -1.0f;
        reset();
    }

    void reset()
    {
        if (oversampler != nullptr) oversampler->reset();
        fuzzVoice.reset();
        for (auto& b : pre)  b.reset();
        for (auto& b : post) b.reset();
        resetCore();
        primed = false;
    }

    /** Call once per block before process(). */
    void setParams (float heat, float tight, float fuzz)
    {
        if (std::abs (tight - lastTight) > 0.001f)
        {
            lastTight = tight;
            tightChanged (tight);
        }

        const float fuzzDrive = dbToGain (lerp (30.0f, 46.0f, heat) + 6.0f * fuzz);
        const float trim = dbToGain (trimDb (heat));

        if (! primed)
        {
            heatParam.snap (heat);
            fuzzAmt.snap (fuzz);
            fuzzGain.snap (fuzzDrive);
            trimGain.snap (trim);
            heatChanged (heat, true);
            primed = true;
        }
        else
        {
            heatParam.next (heat);
            fuzzAmt.next (fuzz);
            fuzzGain.next (fuzzDrive);
            trimGain.next (trim);
            heatChanged (heat, false);
        }
    }

    /** In-place, base sample rate. */
    void process (juce::dsp::AudioBlock<float>& block)
    {
        const int nch = (int) block.getNumChannels();
        const int n = (int) block.getNumSamples();

        for (int ch = 0; ch < nch; ++ch)
        {
            auto* d = block.getChannelPointer ((size_t) ch);
            for (int i = 0; i < n; ++i)
            {
                float x = d[i];
                for (int k = 0; k < numPre; ++k) x = pre[(size_t) k].process (x, ch);
                d[i] = x;
            }
        }

        auto up = oversampler->processSamplesUp (block);
        const int osN = (int) up.getNumSamples();
        const float invN = 1.0f / (float) osN;

        for (int ch = 0; ch < nch; ++ch)
        {
            auto* d = up.getChannelPointer ((size_t) ch);
            for (int i = 0; i < osN; ++i)
            {
                const float t = (float) (i + 1) * invN;
                const float x = d[i];
                float y = processSample (x, ch, t);

                const float f = fuzzAmt.at (t);
                if (f > 0.0005f)
                {
                    const float fz = fuzzVoice.process (x, ch, fuzzGain.at (t), fuzzFold() * f);
                    y = y * (1.0f - 0.6f * f) + fz * f * 1.1f;
                }
                d[i] = y;
            }
        }

        oversampler->processSamplesDown (block);

        const float invBase = 1.0f / (float) n;
        for (int ch = 0; ch < nch; ++ch)
        {
            auto* d = block.getChannelPointer ((size_t) ch);
            for (int i = 0; i < n; ++i)
            {
                float x = d[i];
                for (int k = 0; k < numPost; ++k) x = post[(size_t) k].process (x, ch);
                d[i] = x * trimGain.at ((float) (i + 1) * invBase);
            }
        }
    }

    int getLatencySamples() const { return oversampler ? (int) std::round (oversampler->getLatencyInSamples()) : 0; }

    // Voicing hints used by the shared stages in the processor
    virtual float transientAmount() const = 0;   // pick-attack emphasis
    virtual float thumpAmount() const = 0;       // post-clip low-end restoration
    virtual float gateAmount() const = 0;        // hidden downward expander depth
    virtual float tightScale() const = 0;        // scales the Tightness pre-HPF

protected:
    virtual void configure() = 0;                                  // set pre/post EQ, fixed filters
    virtual void resetCore() = 0;
    virtual void heatChanged (float heat, bool snap) = 0;
    virtual void tightChanged (float tight) = 0;
    virtual float processSample (float x, int ch, float t) noexcept = 0;
    virtual float trimDb (float heat) const = 0;
    virtual float fuzzFold() const { return 0.0f; }

    /** Piecewise-linear lookup over 5 evenly spaced heat points. */
    static float table5 (const float (&v)[5], float heat) noexcept
    {
        const float p = std::clamp (heat, 0.0f, 1.0f) * 4.0f;
        const int i = std::min (3, (int) p);
        return lerp (v[i], v[i + 1], p - (float) i);
    }

    void setGain (BlockParam& p, float v, bool snap) { snap ? p.snap (v) : p.next (v); }

    double fs = 44100.0, osFs = 176400.0;
    std::array<Biquad, 3> pre;
    std::array<Biquad, 5> post;
    int numPre = 0, numPost = 0;

private:
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int osOrder = -1;
    FuzzVoice fuzzVoice;
    BlockParam heatParam, fuzzAmt, fuzzGain, trimGain;
    float lastTight = -1.0f;
    bool primed = false;
};


//==============================================================================
// 1. JALAPEÑO — warm, crunchy overdrive (TS-style mid push into a biased amp stage)
//==============================================================================
class Jalapeno final : public PepperMode
{
public:
    float transientAmount() const override { return 0.25f; }
    float thumpAmount() const override     { return 0.7f; }
    float gateAmount() const override      { return 0.0f; }
    float tightScale() const override      { return 0.8f; }

protected:
    void configure() override
    {
        numPre = 1;
        pre[0].setPeak (fs, 720.0, 0.7, 2.5);
        numPost = 3;
        post[0].setPeak (fs, 220.0, 0.8, 3.5);     // warmth
        post[1].setPeak (fs, 3800.0, 1.2, -1.5);   // soften the edge
        post[2].setLowpass (fs, 7200.0, 0.7);
        tsHp.setCutoff (osFs, 720.0);
        tsLp.setCutoff (osFs, 6000.0);
        ampFront.prepare (osFs, 4.0f, 12.0e-6);
    }

    void resetCore() override { tsHp.reset(); tsLp.reset(); ampFront.reset(); }

    void heatChanged (float heat, bool snap) override
    {
        setGain (drive, dbToGain (lerp (6.0f, 36.0f, heat)), snap);
        setGain (amp, dbToGain (lerp (3.0f, 13.0f, heat)), snap);
    }

    void tightChanged (float) override {}

    float processSample (float x, int ch, float t) noexcept override
    {
        // the overdrive only clips what's above ~720 Hz, then sums back with the clean signal
        const float clipped = tr1 (tsHp.hp (x, ch) * drive.at (t));
        const float y = tsLp.lp (x * 0.55f + clipped * 0.5f, ch);
        // gentle, slightly asymmetric amp stage for the crunch
        return tr2 (ampFront.process (y * amp.at (t), ch)) * 0.8f;
    }

    float trimDb (float heat) const override { return table5 (trim, heat); }

private:
    const Triode tr1 { 0.08f }, tr2 { 0.18f };
    static constexpr float trim[5] { 6.6f, -0.1f, -6.2f, -10.2f, -11.6f }; // auto-level (dB)
    OnePole tsHp, tsLp;
    DriveSlew ampFront;
    BlockParam drive, amp;
};

//==============================================================================
// 2. SERRANO — aggressive crunch, gritty upper mids, punchy rock
//==============================================================================
class Serrano final : public PepperMode
{
public:
    float transientAmount() const override { return 0.35f; }
    float thumpAmount() const override     { return 0.9f; }
    float gateAmount() const override      { return 0.3f; }
    float tightScale() const override      { return 1.0f; }

protected:
    void configure() override
    {
        numPre = 1;
        pre[0].setPeak (fs, 1600.0, 0.8, 4.0);
        numPost = 4;
        post[0].setPeak (fs, 300.0, 0.8, 3.5);     // punch
        post[1].setPeak (fs, 2300.0, 1.0, 2.0);    // gritty upper-mid bite
        post[2].setPeak (fs, 1100.0, 0.9, -2.0);   // keep it from honking
        post[3].setLowpass (fs, 6800.0, 0.7);
        interLp.setCutoff (osFs, 9000.0);
        front1.prepare (osFs, 4.0f, 15.0e-6);
        front2.prepare (osFs, (float) kPi, 22.0e-6);
    }

    void resetCore() override { interHp.reset(); interLp.reset(); front1.reset(); front2.reset(); }

    void heatChanged (float heat, bool snap) override
    {
        const float total = lerp (12.0f, 44.0f, heat);
        setGain (g1, dbToGain (total * 0.55f), snap);
        setGain (g2, dbToGain (total * 0.45f) * 1.1f, snap);
    }

    void tightChanged (float tight) override { interHp.setCutoff (osFs, 120.0 * (0.7 + 0.8 * tight)); }

    float processSample (float x, int ch, float t) noexcept override
    {
        float a = tr1 (front1.process (x * g1.at (t), ch)); // biased preamp triode
        a = interLp.lp (interHp.hp (a, ch), ch);                       // cathode-bypass style high-pass
        return sinClamp (front2.process (a * g2.at (t), ch)) * 0.85f;  // hard-knee grit
    }

    float trimDb (float heat) const override { return table5 (trim, heat); }

private:
    const Triode tr1 { 0.25f };
    static constexpr float trim[5] { -3.1f, -8.9f, -12.2f, -13.3f, -13.3f }; // auto-level (dB)
    OnePole interHp, interLp;
    DriveSlew front1, front2;
    BlockParam g1, g2;
};

//==============================================================================
// 3. CAYENNE — classic heavy distortion, thick and cutting (op-amp + hard diodes)
//==============================================================================
class Cayenne final : public PepperMode
{
public:
    float transientAmount() const override { return 0.25f; }
    float thumpAmount() const override     { return 1.1f; }
    float gateAmount() const override      { return 0.5f; }
    float tightScale() const override      { return 0.9f; }

protected:
    void configure() override
    {
        numPre = 2;
        pre[0].setPeak (fs, 1000.0, 0.5, 2.0);
        pre[1].setLowShelf (fs, 120.0, 1.0);
        numPost = 3;
        post[0].setPeak (fs, 250.0, 0.7, 2.5);     // body
        post[1].setPeak (fs, 1200.0, 0.9, 1.5);    // cut
        post[2].setLowpass (fs, 5600.0, 0.6);      // the classic "filter" roll-off
        lowHp.setCutoff (osFs, 300.0);
        // the op-amp's slew limit is a big part of this sound
        opAmp.prepare (osFs, (float) kPi, 35.0e-6);
    }

    void resetCore() override { lowHp.reset(); opLp.reset(); opAmp.reset(); }

    void heatChanged (float heat, bool snap) override
    {
        const float g = dbToGain (lerp (18.0f, 54.0f, heat));
        setGain (gain, g, snap);
        // op-amp gain-bandwidth: more gain = less top end = thicker
        opLp.setCutoff (osFs, std::clamp (1.0e6 / (double) g, 2600.0, 20000.0));
    }

    void tightChanged (float) override {}

    float processSample (float x, int ch, float t) noexcept override
    {
        float v = (x * 0.35f + lowHp.hp (x, ch)) * gain.at (t);  // lows get less gain
        v = opAmp.process (opLp.lp (v, ch), ch);
        const float c = sinClamp (v * 0.8f);                    // silicon diodes to ground
        return fastTanh (c * 1.8f) * 0.8f;                       // rounded, thick body
    }

    float trimDb (float heat) const override { return table5 (trim, heat); }

private:
    static constexpr float trim[5] { -9.5f, -12.5f, -13.4f, -13.4f, -13.3f }; // auto-level (dB)
    OnePole lowHp, opLp;
    DriveSlew opAmp;
    BlockParam gain;
};

//==============================================================================
// 4. BIRD'S EYE — tight, aggressive high gain, sharp attack, defined palm mutes
//==============================================================================
class BirdsEye final : public PepperMode
{
public:
    float transientAmount() const override { return 0.55f; }
    float thumpAmount() const override     { return 1.1f; }
    float gateAmount() const override      { return 1.0f; }
    float tightScale() const override      { return 1.2f; }

protected:
    void configure() override
    {
        numPre = 2;
        pre[0].setHighpass (fs, 120.0, 0.6);       // the boost-pedal tightening trick
        pre[1].setPeak (fs, 900.0, 0.9, 4.0);
        numPost = 5;
        post[0].setLowShelf (fs, 240.0, 4.5);      // body back after the tight front end
        post[1].setPeak (fs, 500.0, 1.2, -1.0);    // de-mud
        post[2].setPeak (fs, 1900.0, 1.2, 2.5);    // attack and bite
        post[3].setPeak (fs, 4500.0, 2.0, -1.5);   // fizz notch
        post[4].setLowpass (fs, 7800.0, 0.7);
        lp1.setCutoff (osFs, 11000.0);
        lp2.setCutoff (osFs, 9000.0);
        front1.prepare (osFs, 4.0f, 12.0e-6);
        front2.prepare (osFs, 4.0f, 15.0e-6);
        front3.prepare (osFs, 4.0f, 20.0e-6);
    }

    void resetCore() override
    {
        hp1.reset(); hp2.reset(); lp1.reset(); lp2.reset(); front1.reset(); front2.reset(); front3.reset();
    }

    void heatChanged (float heat, bool snap) override
    {
        const float total = lerp (26.0f, 62.0f, heat);
        setGain (g1, dbToGain (total * 0.40f), snap);
        setGain (g2, dbToGain (total * 0.35f), snap);
        setGain (g3, dbToGain (total * 0.25f), snap);
    }

    void tightChanged (float tight) override
    {
        hp1.setCutoff (osFs, 180.0 * (0.7 + 0.8 * tight));
        hp2.setCutoff (osFs, 100.0 * (0.7 + 0.8 * tight));
    }

    float processSample (float x, int ch, float t) noexcept override
    {
        float a = tr1 (front1.process (x * g1.at (t), ch));
        a = lp1.lp (hp1.hp (a, ch), ch);
        float b = tr2 (front2.process (a * g2.at (t), ch));
        b = lp2.lp (hp2.hp (b, ch), ch);
        return fastTanh (front3.process (b * g3.at (t), ch)) * 0.8f;
    }

    float trimDb (float heat) const override { return table5 (trim, heat); }

private:
    const Triode tr1 { 0.2f }, tr2 { -0.15f };
    static constexpr float trim[5] { -11.5f, -14.0f, -14.4f, -14.2f, -13.7f }; // auto-level (dB)
    OnePole hp1, hp2, lp1, lp2;
    DriveSlew front1, front2, front3;
    BlockParam g1, g2, g3;
};

//==============================================================================
// 5. HABANERO — modern metal: dense saturation, biting mids, controlled compression
//==============================================================================
class Habanero final : public PepperMode
{
public:
    float transientAmount() const override { return 0.3f; }
    float thumpAmount() const override     { return 1.0f; }
    float gateAmount() const override      { return 1.0f; }
    float tightScale() const override      { return 1.1f; }

protected:
    void configure() override
    {
        numPre = 2;
        pre[0].setHighpass (fs, 100.0, 0.6);
        pre[1].setPeak (fs, 750.0, 0.7, 3.0);
        numPost = 5;
        post[0].setLowShelf (fs, 160.0, 2.5);      // modern thump
        post[1].setPeak (fs, 800.0, 0.9, 1.5);     // mid density
        post[2].setPeak (fs, 3000.0, 1.4, 2.0);    // bite
        post[3].setPeak (fs, 5500.0, 1.5, -2.5);   // anti-fizz
        post[4].setLowpass (fs, 8200.0, 0.6);
        lp1.setCutoff (osFs, 12000.0);
        lp2.setCutoff (osFs, 9000.0);
        lp3.setCutoff (osFs, 8000.0);
        sagEnv.setTimes (osFs, 8.0, 150.0);
        for (auto* f : { &front1, &front2, &front3, &front4 })
            f->prepare (osFs, 4.0f, 18.0e-6);
    }

    void resetCore() override
    {
        hp1.reset(); hp2.reset(); hp3.reset(); lp1.reset(); lp2.reset(); lp3.reset(); sagEnv.reset();
        for (auto* f : { &front1, &front2, &front3, &front4 }) f->reset();
    }

    void heatChanged (float heat, bool snap) override
    {
        const float stage = lerp (32.0f, 70.0f, heat) * 0.25f;
        setGain (gs, dbToGain (stage), snap);
        setGain (sagDepth, lerp (0.3f, 0.9f, heat), snap);
    }

    void tightChanged (float tight) override
    {
        const double s = 0.7 + 0.8 * tight;
        hp1.setCutoff (osFs, 160.0 * s);
        hp2.setCutoff (osFs, 100.0 * s);
        hp3.setCutoff (osFs, 70.0 * s);
    }

    float processSample (float x, int ch, float t) noexcept override
    {
        const float g = gs.at (t);
        float s = tr1 (front1.process (x * g, ch));
        s = lp1.lp (hp1.hp (s, ch), ch);
        s = tr2 (front2.process (s * g, ch));
        s = lp2.lp (hp2.hp (s, ch), ch);
        s = tr3 (front3.process (s * g, ch));
        s = lp3.lp (hp3.hp (s, ch), ch);
        // power-supply sag: the louder the third stage, the less drive into the last
        const float sag = 1.0f / (1.0f + sagDepth.at (t) * sagEnv.process (s, ch));
        return fastTanh (front4.process (s * g * sag, ch)) * 0.8f;
    }

    float trimDb (float heat) const override { return table5 (trim, heat); }

private:
    const Triode tr1 { 0.3f }, tr2 { -0.2f }, tr3 { 0.25f };
    static constexpr float trim[5] { -12.8f, -14.1f, -14.3f, -13.9f, -13.5f }; // auto-level (dB)
    OnePole hp1, hp2, hp3, lp1, lp2, lp3;
    Envelope sagEnv;
    DriveSlew front1, front2, front3, front4;
    BlockParam gs, sagDepth;
};

//==============================================================================
// 6. CAROLINA REAPER — extreme fuzz: sine clip, Fracture expansion, wavefold chaos
//==============================================================================
class Reaper final : public PepperMode
{
public:
    float transientAmount() const override { return 0.2f; }
    float thumpAmount() const override     { return 1.1f; }
    float gateAmount() const override      { return 0.8f; }
    float tightScale() const override      { return 0.6f; }

protected:
    void configure() override
    {
        numPre = 2;
        pre[0].setLowShelf (fs, 110.0, 3.0);
        pre[1].setPeak (fs, 500.0, 0.7, 2.0);
        numPost = 4;
        post[0].setPeak (fs, 800.0, 0.6, -4.0);    // big scoop
        post[1].setLowShelf (fs, 120.0, 2.0);
        post[2].setPeak (fs, 2500.0, 1.0, 2.0);
        post[3].setLowpass (fs, 6200.0, 0.7);
        lp1.setCutoff (osFs, 4500.0);
        lp2.setCutoff (osFs, 9000.0);
        front.prepare (osFs, (float) kPi, 25.0e-6);
        slewOut.setRiseTime (osFs, 25.0e-6);
    }

    void resetCore() override { hp1.reset(); lp1.reset(); lp2.reset(); front.reset(); slewOut.reset(); }

    void heatChanged (float heat, bool snap) override
    {
        setGain (g1, dbToGain (lerp (30.0f, 56.0f, heat)), snap);
        setGain (g2, lerp (1.2f, 2.6f, heat), snap);
        setGain (chaos, lerp (0.2f, 0.6f, heat), snap);
        heatNow = heat;
    }

    void tightChanged (float tight) override { hp1.setCutoff (osFs, 80.0 * (0.7 + 0.8 * tight)); }

    float processSample (float x, int ch, float t) noexcept override
    {
        float s = sinClamp (front.process (x * g1.at (t), ch));   // slammed first stage
        s = hp1.hp (lp1.lp (s, ch), ch);
        const float v = expandStage (s * g2.at (t));              // Fracture expansion
        const float c = chaos.at (t);
        float y = sinClamp (v) * (1.0f - c) + fastSin (v) * c;    // blend in the wavefold
        y = fastTanh (1.6f * y + 0.2f) - fastTanh (0.2f);         // starved, biased output stage
        return slewOut.process (lp2.lp (y, ch) * 0.85f, ch);
    }

    float fuzzFold() const override { return 0.35f + 0.4f * heatNow; }

    float trimDb (float heat) const override { return table5 (trim, heat); }

private:
    static constexpr float trim[5] { -11.7f, -11.0f, -8.4f, -7.5f, -7.0f }; // auto-level (dB)
    OnePole hp1, lp1, lp2;
    DriveSlew front;
    SlewLimiter slewOut;
    BlockParam g1, g2, chaos;
    float heatNow = 0.5f;
};

} // namespace pepper
