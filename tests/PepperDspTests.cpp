/*
    Offline checks for the PEPPER engine:
      - level of every pepper across the Heat range (for auto-level calibration)
      - tonal balance per band (mud / mids / fizz)
      - aliasing with a high sine
      - parameter-automation torture test (NaN / blow-ups / any block size)
      - click check on pepper changes
      - CPU cost
*/
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/PepperEngine.h"
#include <random>
#include <cstdio>

using namespace pepper;

static double kFs = 48000.0;
static const char* kNames[] = { "Jalapeno", "Serrano", "Cayenne", "BirdsEye", "Habanero", "Reaper" };

//==============================================================================
// Karplus-Strong guitar DI so the tests see something guitar-shaped
struct KSString
{
    std::vector<float> line; size_t pos = 0; float damp = 0.996f, bright = 0.5f, last = 0;
    void pluck (double f, float amp, bool palmMute, std::mt19937& rng)
    {
        line.assign ((size_t) std::max (2.0, kFs / f), 0.0f);
        std::uniform_real_distribution<float> u (-1.0f, 1.0f);
        float lp = 0;
        for (auto& s : line) { lp += 0.6f * (u (rng) - lp); s = lp * amp; }
        damp = palmMute ? 0.93f : 0.9985f;
        bright = palmMute ? 0.25f : 0.5f;
        pos = 0; last = 0;
    }
    float tick()
    {
        if (line.empty()) return 0.0f;
        const size_t n = line.size();
        const float a = line[pos], b = line[(pos + 1) % n];
        const float v = damp * (bright * a + (1.0f - bright) * b);
        line[pos] = v; pos = (pos + 1) % n;
        return a;
    }
};

static std::vector<float> makeRiff()
{
    std::mt19937 rng (7);
    const int len = (int) (kFs * 7.0);
    std::vector<float> out ((size_t) len, 0.0f);
    std::array<KSString, 3> str;
    const double E2 = 82.41, B2 = 123.47, E3 = 164.81, D2 = 73.42, A2 = 110.0, D3 = 146.83;
    const double sixteenth = 60.0 / 140.0 / 4.0;

    struct Ev { double t; std::vector<double> f; bool mute; float amp; };
    std::vector<Ev> ev;
    double t = 0.0;
    for (int i = 0; i < 16; ++i, t += sixteenth)               // palm-muted chugs with accents
        ev.push_back ({ t, (i % 4 == 3) ? std::vector<double> { E2, B2, E3 } : std::vector<double> { E2 }, i % 4 != 3, 1.0f });
    ev.push_back ({ 2.0, { E2, B2, E3 }, false, 1.0f });      // open power chords
    ev.push_back ({ 3.0, { D2, A2, D3 }, false, 1.0f });
    const double lead[] = { 329.63, 392.0, 440.0, 493.88, 440.0, 392.0, 329.63, 293.66 };
    for (int i = 0; i < 8; ++i)                                 // single-note lead line
        ev.push_back ({ 4.0 + i * 0.25, { lead[i] }, false, 0.8f });

    size_t next = 0;
    for (int i = 0; i < len; ++i)
    {
        const double now = i / kFs;
        while (next < ev.size() && ev[next].t <= now)
        {
            for (size_t s = 0; s < str.size(); ++s)
                if (s < ev[next].f.size()) str[s].pluck (ev[next].f[s], ev[next].amp, ev[next].mute, rng);
                else str[s].line.clear();
            ++next;
        }
        float v = 0;
        for (auto& s : str) v += s.tick();
        out[(size_t) i] = now < 6.0 ? v : 0.0f;
    }

    float peak = 0; for (auto v : out) peak = std::max (peak, std::abs (v));
    std::normal_distribution<float> noise (0.0f, 1.0f);
    for (auto& v : out) v = v / peak * 0.355f + noise (rng) * 0.00006f; // -9 dBFS peak, ~-84 dBFS hiss
    return out;
}

static std::vector<float> render (const std::vector<float>& in, const EngineParams& p, int block = 256)
{
    PepperEngine e; e.setParams (p); e.prepare (kFs, block);
    std::vector<float> l (in), r (in);
    for (size_t off = 0; off < in.size(); off += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, in.size() - off);
        float* ch[2] { l.data() + off, r.data() + off };
        e.setParams (p);
        e.process (ch, 2, n);
    }
    return l;
}

static double rmsDb (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0; for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i];
    return 10.0 * std::log10 (s / (double) (b - a) + 1e-30);
}

// band energy split using one big FFT
static std::array<double, 5> bands (const std::vector<float>& x, size_t a)
{
    const int order = 18, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) N * 2, 0.0f);
    for (int i = 0; i < N && a + (size_t) i < x.size(); ++i)
        buf[(size_t) i] = x[a + (size_t) i] * (0.5f - 0.5f * std::cos (2.0f * 3.14159265f * i / N));
    fft.performFrequencyOnlyForwardTransform (buf.data());
    const double edges[] = { 0, 150, 800, 3000, 6000, 24000 };
    std::array<double, 5> e {}; double tot = 0;
    for (int k = 1; k < N / 2; ++k)
    {
        const double f = k * kFs / N, p = (double) buf[(size_t) k] * buf[(size_t) k];
        for (int b = 0; b < 5; ++b) if (f >= edges[b] && f < edges[b + 1]) e[(size_t) b] += p;
        tot += p;
    }
    for (auto& v : e) v = 100.0 * v / tot;
    return e;
}

// energy that is NOT at a harmonic of f0 (i.e. aliasing + noise), relative to total
static double aliasDb (int pepperIdx, float heat, float fuzz, double f0 = 1318.5)
{
    std::vector<float> in ((size_t) kFs * 2);
    for (size_t i = 0; i < in.size(); ++i) in[i] = 0.25f * (float) std::sin (2 * 3.14159265358979 * f0 * i / kFs);
    EngineParams p; p.pepper = pepperIdx; p.heat = heat; p.fuzz = fuzz;
    auto out = render (in, p);
    const int order = 15, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) N * 2, 0.0f);
    for (int i = 0; i < N; ++i)
        buf[(size_t) i] = out[(size_t) (kFs / 2) + (size_t) i] * (0.5f - 0.5f * std::cos (2.0f * 3.14159265f * i / N));
    fft.performFrequencyOnlyForwardTransform (buf.data());
    double harm = 0, other = 0;
    for (int k = 2; k < N / 2; ++k)
    {
        const double f = k * kFs / N, p2 = (double) buf[(size_t) k] * buf[(size_t) k];
        const double h = f / f0, d = std::abs (h - std::round (h)) * f0;
        (d < 30.0 ? harm : other) += p2;
    }
    return 10.0 * std::log10 (other / (harm + other) + 1e-30);
}

int main (int argc, char** argv)
{
    const bool calibrate = argc > 1 && juce::String (argv[1]) == "--calibrate";
    juce::ignoreUnused (calibrate);
    auto riff = makeRiff();
    const size_t musicEnd = (size_t) (kFs * 6.0);
    std::printf ("Input riff: RMS %.1f dBFS\n\n", rmsDb (riff, 0, musicEnd));

    // ---- 1. level vs heat ------------------------------------------------------------
    std::printf ("Output RMS (dBFS) vs Heat   [tone .5, tight .5, fuzz 0]\n%-10s", "");
    const float heats[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    for (auto h : heats) std::printf ("  h=%.2f", h);
    std::printf ("\n");
    std::string trims;
    for (int m = 0; m < kNumPeppers; ++m)
    {
        std::printf ("%-10s", kNames[m]);
        trims += std::string (kNames[m]) + ": { ";
        for (auto h : heats)
        {
            EngineParams p; p.pepper = m; p.heat = h;
            auto out = render (riff, p);
            const double lv = rmsDb (out, 0, musicEnd);
            std::printf ("  %6.1f", lv);
            char buf[32]; std::snprintf (buf, sizeof buf, "%.1ff, ", (-17.0 + 2.0 * h) - lv);
            trims += buf;
        }
        std::printf ("\n");
        trims += "}\n";
    }
    if (calibrate) std::printf ("Suggested additional trim (dB) for target -17 + 2*heat:\n%s", trims.c_str());

    // ---- 2. fuzz blend level ------------------------------------------------------------
    std::printf ("\nOutput RMS vs Fuzz  [heat .6]\n");
    for (int m = 0; m < kNumPeppers; ++m)
    {
        std::printf ("%-10s", kNames[m]);
        for (float f : { 0.0f, 0.5f, 1.0f })
        {
            EngineParams p; p.pepper = m; p.heat = 0.6f; p.fuzz = f;
            std::printf ("  f=%.1f %6.1f", f, rmsDb (render (riff, p), 0, musicEnd));
        }
        std::printf ("\n");
    }

    // ---- 3. tonal balance -----------------------------------------------------------------
    std::printf ("\nBand energy %% at heat .7  (<150 | 150-800 | 800-3k | 3k-6k | >6k)  + noise-floor tail\n");
    { auto b = bands (riff, (size_t) (kFs * 0.5));
      std::printf ("%-10s  %5.1f  %5.1f  %5.1f  %5.1f  %5.2f\n", "(input DI)", b[0], b[1], b[2], b[3], b[4]); }
    for (int m = 0; m < kNumPeppers; ++m)
    {
        EngineParams p; p.pepper = m; p.heat = 0.7f;
        auto out = render (riff, p);
        auto b = bands (out, (size_t) (kFs * 0.5));
        std::printf ("%-10s  %5.1f  %5.1f  %5.1f  %5.1f  %5.2f   tail %6.1f dBFS\n", kNames[m], b[0], b[1], b[2], b[3], b[4],
                     rmsDb (out, (size_t) (kFs * 6.3), (size_t) (kFs * 6.9)));
    }

    std::printf ("\nTightness sweep, Habanero heat .7: <150 Hz share and 150-800 share\n");
    for (float tg : { 0.0f, 0.5f, 1.0f })
    {
        EngineParams p; p.pepper = 4; p.heat = 0.7f; p.tight = tg;
        auto b = bands (render (riff, p), (size_t) (kFs * 0.5));
        std::printf ("  tight %.1f: %5.1f%%  %5.1f%%\n", tg, b[0], b[1]);
    }
    std::printf ("Tone sweep, Cayenne heat .7: 3k-6k and >6k share\n");
    for (float tn : { 0.0f, 0.5f, 1.0f })
    {
        EngineParams p; p.pepper = 2; p.heat = 0.7f; p.tone = tn;
        auto b = bands (render (riff, p), (size_t) (kFs * 0.5));
        std::printf ("  tone %.1f: %5.1f%%  %5.2f%%\n", tn, b[3], b[4]);
    }

    // ---- 4. aliasing -----------------------------------------------------------------------
    std::printf ("\nNon-harmonic energy (aliasing), lower is cleaner.  1.32 kHz = 24th fret high E;  2.49 kHz = torture\n");
    for (int m = 0; m < kNumPeppers; ++m)
        std::printf ("%-10s 1.32k: h.5 %6.1f  h1 %6.1f  h1+fz %6.1f   |  2.49k: h.5 %6.1f  h1 %6.1f\n", kNames[m],
                     aliasDb (m, 0.5f, 0.0f), aliasDb (m, 1.0f, 0.0f), aliasDb (m, 1.0f, 1.0f),
                     aliasDb (m, 0.5f, 0.0f, 2489.0), aliasDb (m, 1.0f, 0.0f, 2489.0));

    // ---- 5. automation torture test --------------------------------------------------------
    {
        std::mt19937 rng (123);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        std::uniform_int_distribution<int> blk (1, 1024), pep (0, 5);
        PepperEngine e; e.prepare (kFs, 1024);
        std::vector<float> l (riff), r (riff);
        float peak = 0; bool bad = false; size_t off = 0; int changes = 0;
        while (off < l.size())
        {
            const int n = (int) std::min ((size_t) blk (rng), l.size() - off);
            EngineParams p; p.pepper = pep (rng); p.heat = u (rng); p.tone = u (rng); p.tight = u (rng);
            p.fuzz = u (rng); p.outputDb = -24.0f + 36.0f * u (rng);
            e.setParams (p); ++changes;
            float* ch[2] { l.data() + off, r.data() + off };
            e.process (ch, 2, n);
            for (int i = 0; i < n; ++i) { bad |= ! std::isfinite (l[off + (size_t) i]); peak = std::max (peak, std::abs (l[off + (size_t) i])); }
            off += (size_t) n;
        }
        std::printf ("\nTorture test: %d random param/pepper changes, random block sizes -> %s, peak %.2f (%.1f dBFS)\n",
                     changes, bad ? "NaN/INF FOUND" : "no NaN/INF", peak, 20 * std::log10 (peak + 1e-12));
    }

    // ---- 6. click on pepper change ------------------------------------------------------------
    {
        std::vector<float> chord ((size_t) kFs * 2);
        for (size_t i = 0; i < chord.size(); ++i)
            chord[i] = 0.2f * (float) (std::sin (2 * 3.14159265 * 82.41 * i / kFs) + 0.6 * std::sin (2 * 3.14159265 * 123.47 * i / kFs));
        PepperEngine e; e.prepare (kFs, 256);
        std::vector<float> l (chord), r (chord);
        float maxStepSteady = 0, maxStepSwitch = 0;
        for (size_t off = 0; off < l.size(); off += 256)
        {
            EngineParams p; p.heat = 0.7f; p.pepper = (off < l.size() / 2) ? 1 : 4;
            e.setParams (p);
            float* ch[2] { l.data() + off, r.data() + off };
            e.process (ch, 2, 256);
        }
        const size_t sw = l.size() / 2;
        for (size_t i = sw - 20000; i < sw - 2000; ++i) maxStepSteady = std::max (maxStepSteady, std::abs (l[i] - l[i - 1]));
        float maxStepAfter = 0;
        for (size_t i = sw; i < sw + 4000; ++i) maxStepSwitch = std::max (maxStepSwitch, std::abs (l[i] - l[i - 1]));
        for (size_t i = sw + 20000; i < sw + 38000; ++i) maxStepAfter = std::max (maxStepAfter, std::abs (l[i] - l[i - 1]));
        std::printf ("Pepper switch: max sample step before %.3f, during crossfade %.3f, after %.3f\n", maxStepSteady, maxStepSwitch, maxStepAfter);
    }

    // ---- 7. CPU ----------------------------------------------------------------------------------
    std::printf ("\nCPU (stereo, 48 kHz, 256-sample blocks):\n");
    for (int m = 0; m < kNumPeppers; ++m)
    {
        EngineParams p; p.pepper = m; p.heat = 0.8f; p.fuzz = 0.5f;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto out = render (riff, p);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("%-10s %.2f%% of one core\n", kNames[m], 100.0 * ms / (riff.size() / kFs * 1000.0));
    }

    // ---- sample-rate independence ---------------------------------------------------------------
    std::printf ("\nLevel and latency at other sample rates (heat .7):\n");
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        // same 48 kHz riff, resampled, so only the plug-in's behaviour changes
        std::vector<float> r2 ((size_t) ((double) riff.size() * rate / 48000.0) - 8);
        juce::LagrangeInterpolator interp;
        interp.process (48000.0 / rate, riff.data(), r2.data(), (int) r2.size());
        kFs = rate;
        PepperEngine e; e.prepare (kFs, 256);
        std::printf ("%6.1f kHz  latency %2d samples (%.2f ms)  levels:", rate / 1000.0, e.getLatencySamples(), e.getLatencySamples() * 1000.0 / rate);
        for (int m = 0; m < kNumPeppers; ++m)
        {
            EngineParams p; p.pepper = m; p.heat = 0.7f;
            std::printf (" %6.1f", rmsDb (render (r2, p), 0, (size_t) (kFs * 6.0)));
        }
        std::printf ("\n");
    }
    kFs = 48000.0;

    // ---- 8. write demo renders ----------------------------------------------------------------------
    if (argc > 2)
    {
        juce::File dir (argv[2]); dir.createDirectory();
        auto write = [&] (const juce::String& name, const std::vector<float>& x)
        {
            juce::WavAudioFormat wav;
            auto f = dir.getChildFile (name + ".wav"); f.deleteFile();
            std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
            auto opts = juce::AudioFormatWriterOptions().withSampleRate (kFs).withNumChannels (1).withBitsPerSample (24);
            if (auto w = wav.createWriterFor (os, opts))
            {
                const float* d[1] { x.data() };
                w->writeFromFloatArrays (d, 1, (int) x.size());
            }
        };
        write ("00_DI", riff);
        struct Pr { const char* n; int m; float h, t, ti, f; };
        const Pr ps[] = { { "01_Hybrid_Crunch", 3, 0.55f, 0.5f, 0.7f, 0.05f }, { "02_Meteora_Lead", 2, 0.8f, 0.62f, 0.45f, 0.15f },
                          { "03_Modern_Metal", 4, 0.7f, 0.55f, 0.75f, 0.0f }, { "04_Sempiternal_Fuzz", 4, 0.65f, 0.5f, 0.6f, 0.55f },
                          { "05_Carolina_Chaos", 5, 0.85f, 0.5f, 0.35f, 0.7f } };
        for (auto& pr : ps)
        {
            EngineParams p; p.pepper = pr.m; p.heat = pr.h; p.tone = pr.t; p.tight = pr.ti; p.fuzz = pr.f;
            write (pr.n, render (riff, p));
        }
        std::printf ("Wrote renders to %s\n", dir.getFullPathName().toRawUTF8());
    }
    return 0;
}
