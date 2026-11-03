#include "PepperEngine.h"

namespace pepper
{
PepperEngine::PepperEngine()
{
    modes[0] = std::make_unique<Jalapeno>();
    modes[1] = std::make_unique<Serrano>();
    modes[2] = std::make_unique<Cayenne>();
    modes[3] = std::make_unique<BirdsEye>();
    modes[4] = std::make_unique<Habanero>();
    modes[5] = std::make_unique<Reaper>();
}

void PepperEngine::prepare (double sampleRate, int maxBlockSize)
{
    juce::ignoreUnused (maxBlockSize);
    fs = sampleRate;

    for (auto& m : modes)
        m->prepare (fs, kChunk);

    latency = modes[0]->getLatencySamples();

    preBuf.setSize (kMaxChannels, kChunk);
    bufA.setSize (kMaxChannels, kChunk);
    bufB.setSize (kMaxChannels, kChunk);
    bassBuf.setSize (kMaxChannels, kChunk);

    const double smoothSecs = 0.05;
    for (auto* s : { &heat, &tone, &tight, &fuzz, &transientAmt, &gateAmt, &thumpDb, &bassAmt })
        s->reset (fs, smoothSecs);
    outGain.reset (fs, 0.03);
    tightHz.reset (fs, 0.05);

    gateEnv.setTimes (fs, 0.3, 40.0);
    fastEnv.setTimes (fs, 0.5, 20.0);
    slowEnv.setTimes (fs, 20.0, 150.0);
    gateOpenCoef  = (float) std::exp (-1.0 / (fs * 0.0005));
    gateCloseCoef = (float) std::exp (-1.0 / (fs * 0.060));
    transSmoothCoef = (float) std::exp (-1.0 / (fs * 0.002));

    rumbleHp.setHighpass (fs, 35.0, 0.6);
    bassHp.setHighpass (fs, 40.0, 0.7);
    bassLp1.setLowpass (fs, 160.0, 0.6);
    bassLp2.setLowpass (fs, 160.0, 0.6);
    reset();
}

void PepperEngine::reset()
{
    current = juce::jlimit (0, kNumPeppers - 1, target.pepper);
    previous = -1;
    fadePos = 1.0f;
    fadeStep = 1.0f / (float) (0.03 * fs / kChunk);

    for (auto& m : modes) m->reset();

    const auto& m = *modes[(size_t) current];
    heat.setCurrentAndTargetValue (target.heat);
    tone.setCurrentAndTargetValue (target.tone);
    tight.setCurrentAndTargetValue (target.tight);
    fuzz.setCurrentAndTargetValue (target.fuzz);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (target.outputDb, -100.0f));
    transientAmt.setCurrentAndTargetValue (m.transientAmount());
    gateAmt.setCurrentAndTargetValue (m.gateAmount());
    thumpDb.setCurrentAndTargetValue (m.thumpAmount() * 2.0f);
    tightHz.setCurrentAndTargetValue (20.0f * std::pow (12.5f, target.tight) * m.tightScale());
    bassAmt.setCurrentAndTargetValue (bassCatchLevel (m.thumpAmount(), target.tight));

    for (auto* b : { &tightHp, &thumpShelf, &toneShelf, &tonePeak, &toneLp, &rumbleHp, &bassHp, &bassLp1, &bassLp2 })
        b->reset();
    gateEnv.reset(); fastEnv.reset(); slowEnv.reset();
    gateGain = 1.0f; transGain = 1.0f;
    lastTightHz = -1; lastThump = -100; lastTone = -1;
    updateFilters (true);
}

float PepperEngine::bassCatchLevel (float thump, float tightness) noexcept
{
    // More Tightness = less bass into the clipper, so more clean bass is caught around it
    return thump * dbToGain (lerp (18.0f, 26.0f, tightness));
}

void PepperEngine::startPepperChange (int newPepper) noexcept
{
    if (newPepper == current) return;
    previous = current;
    current = newPepper;
    modes[(size_t) current]->reset();
    fadePos = 0.0f;
}

void PepperEngine::updateFilters (bool force) noexcept
{
    const float th = tightHz.getCurrentValue();
    if (force || std::abs (th - lastTightHz) > 0.05f)
    {
        tightHp.setHighpass (fs, th, 0.6);
        lastTightHz = th;
    }

    const float tdb = thumpDb.getCurrentValue();
    if (force || std::abs (tdb - lastThump) > 0.01f)
    {
        thumpShelf.setLowShelf (fs, 110.0, tdb);
        lastThump = tdb;
    }

    const float t = tone.getCurrentValue();
    if (force || std::abs (t - lastTone) > 0.0005f)
    {
        toneShelf.setLowShelf (fs, 260.0, (0.5f - t) * 4.0f);      // +2 dB warmth .. -2 dB
        tonePeak.setPeak (fs, 3200.0, 0.9, (t - 0.5f) * 8.0f);    // -4 .. +4 dB presence
        toneLp.setLowpass (fs, 3200.0 * std::pow (5.0, (double) t), 0.65); // 3.2 kHz .. 16 kHz
        lastTone = t;
    }
}

void PepperEngine::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    numChannels = juce::jmin (numChannels, kMaxChannels);
    inPeak = outPeak = 0.0f;
    if (numChannels <= 0) return;

    for (int offset = 0; offset < numSamples; offset += kChunk)
        processChunk (channels, numChannels, offset, juce::jmin (kChunk, numSamples - offset));
}

void PepperEngine::processChunk (float* const* channels, int nch, int offset, int n) noexcept
{
    // ---- parameters for this chunk ------------------------------------------------
    const int wanted = juce::jlimit (0, kNumPeppers - 1, target.pepper);
    if (wanted != current)
    {
        if (fadePos < 1.0f)       // already mid-fade: hard-restart from the current voice
            previous = -1;
        startPepperChange (wanted);
    }

    const auto& voice = *modes[(size_t) current];
    heat.setTargetValue (target.heat);
    tone.setTargetValue (target.tone);
    tight.setTargetValue (target.tight);
    fuzz.setTargetValue (target.fuzz);
    outGain.setTargetValue (juce::Decibels::decibelsToGain (target.outputDb, -100.0f));
    transientAmt.setTargetValue (voice.transientAmount());
    gateAmt.setTargetValue (voice.gateAmount());
    thumpDb.setTargetValue (voice.thumpAmount() * 2.0f);
    tightHz.setTargetValue (20.0f * std::pow (12.5f, target.tight) * voice.tightScale());
    bassAmt.setTargetValue (bassCatchLevel (voice.thumpAmount(), target.tight));

    for (auto* s : { &heat, &tone, &tight, &fuzz, &transientAmt, &gateAmt, &thumpDb, &tightHz, &bassAmt })
        s->skip (n);
    updateFilters (false);

    const float h = heat.getCurrentValue();
    const float tg = tight.getCurrentValue();
    const float fz = fuzz.getCurrentValue();
    const float gateDepth = gateAmt.getCurrentValue() * (0.4f + 0.6f * h);
    const float transAmt = transientAmt.getCurrentValue();
    const float bassLevel = bassAmt.getCurrentValue();
    constexpr float gateThresh = 0.000794f; // -62 dBFS

    // ---- input stage: tightness HPF, noise floor control, attack detection -------
    for (int i = 0; i < n; ++i)
    {
        float linked = 0.0f;
        for (int ch = 0; ch < nch; ++ch)
        {
            const float x = channels[ch][offset + i];
            inPeak = std::max (inPeak, std::abs (x));
            const float y = tightHp.process (x, ch);
            preBuf.setSample (ch, i, y);
            // bass catch (after the Airwindows GrindAmp idea): lows taken *before* the
            // tightness filter, gently saturated and added back around the clipper
            const float b = bassLp2.process (bassLp1.process (bassHp.process (x, ch), ch), ch);
            bassBuf.setSample (ch, i, b);
            linked = std::max (linked, std::abs (y));
        }

        // gentle downward expander under -62 dBFS (keeps high-gain hiss from blooming)
        const float env = gateEnv.process (linked, 0);
        float g = 1.0f;
        if (gateDepth > 0.0f && env < gateThresh)
            g = std::max (0.01f, std::pow (std::max (env, 1.0e-7f) / gateThresh, 2.0f * gateDepth));
        const float c = g > gateGain ? gateOpenCoef : gateCloseCoef;
        gateGain = g + c * (gateGain - g);

        for (int ch = 0; ch < nch; ++ch)
        {
            preBuf.setSample (ch, i, preBuf.getSample (ch, i) * gateGain);
            bassBuf.setSample (ch, i, bassBuf.getSample (ch, i) * gateGain);
        }

        // pick-attack emphasis: fast envelope racing ahead of the slow one
        const float fe = fastEnv.process (linked, 0);
        const float se = slowEnv.process (linked, 0);
        const float r = juce::jlimit (0.0f, 1.5f, (fe - se) / (se + 1.0e-4f));
        const float tgain = 1.0f + transAmt * 0.45f * r;
        transGain = tgain + transSmoothCoef * (transGain - tgain);
        transGainBuf[(size_t) i] = transGain;
    }

    // ---- pepper voices (with crossfade on change) ---------------------------------
    auto runVoice = [&] (int index, juce::AudioBuffer<float>& buf)
    {
        for (int ch = 0; ch < nch; ++ch)
            buf.copyFrom (ch, 0, preBuf, ch, 0, n);
        juce::dsp::AudioBlock<float> block (buf.getArrayOfWritePointers(), (size_t) nch, (size_t) n);
        auto& m = *modes[(size_t) index];
        m.setParams (h, tg, fz);
        m.process (block);
    };

    runVoice (current, bufA);

    float gCurStart = 1.0f, gCurEnd = 1.0f, gPrevStart = 0.0f, gPrevEnd = 0.0f;
    const bool fading = previous >= 0 && fadePos < 1.0f;
    if (fading)
    {
        runVoice (previous, bufB);
        const float p0 = fadePos, p1 = std::min (1.0f, fadePos + fadeStep);
        gCurStart = std::sin (p0 * kHalfPi);  gCurEnd = std::sin (p1 * kHalfPi);
        gPrevStart = std::cos (p0 * kHalfPi); gPrevEnd = std::cos (p1 * kHalfPi);
        fadePos = p1;
        if (fadePos >= 1.0f) previous = -1;
    }
    else
    {
        fadePos = 1.0f;
        previous = -1;
    }

    // ---- shared output stage --------------------------------------------------------
    const float invN = 1.0f / (float) n;
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) (i + 1) * invN;
        const float gc = gCurStart + (gCurEnd - gCurStart) * t;
        const float gp = gPrevStart + (gPrevEnd - gPrevStart) * t;
        const float og = outGain.getNextValue();

        for (int ch = 0; ch < nch; ++ch)
        {
            float y = bufA.getSample (ch, i) * gc;
            if (fading) y += bufB.getSample (ch, i) * gp;

            y *= transGainBuf[(size_t) i];
            y += 0.42f * sinClamp (bassBuf.getSample (ch, i) * bassLevel);
            y = thumpShelf.process (y, ch);
            y = toneShelf.process (y, ch);
            y = tonePeak.process (y, ch);
            y = toneLp.process (y, ch);
            y = rumbleHp.process (y, ch);
            y *= og;

            if (! std::isfinite (y)) y = 0.0f; // never let a bad sample escape

            channels[ch][offset + i] = y;
            outPeak = std::max (outPeak, std::abs (y));
        }
    }
}
} // namespace pepper
