/*
    Plug-in level checks: bus layouts, presets, state save/restore, bypass latency,
    and a rendered screenshot of the editor (written to the path given as argv[1]).
*/
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include <cstdio>

using namespace juce;

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok) ++failures;
}

static float paramValue (PepperAudioProcessor& p, const String& id)
{
    auto* prm = p.state.getParameter (id);
    return prm->convertFrom0to1 (prm->getValue());
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;

    // ---- layouts and processing --------------------------------------------------
    for (auto layout : { std::pair { 2, 2 }, std::pair { 1, 1 }, std::pair { 1, 2 } })
    {
        PepperAudioProcessor proc;
        AudioProcessor::BusesLayout l;
        l.inputBuses.add (layout.first == 1 ? AudioChannelSet::mono() : AudioChannelSet::stereo());
        l.outputBuses.add (layout.second == 1 ? AudioChannelSet::mono() : AudioChannelSet::stereo());
        const bool ok = proc.setBusesLayout (l);
        proc.prepareToPlay (44100.0, 512);
        AudioBuffer<float> buf (2, 512);
        MidiBuffer midi;
        bool finite = true; float peak = 0;
        for (int b = 0; b < 200; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float s = 0.3f * std::sin ((float) (b * 512 + i) * 0.0117f);
                for (int ch = 0; ch < 2; ++ch) buf.setSample (ch, i, ch < layout.first ? s : 0.0f);
            }
            proc.processBlock (buf, midi);
            for (int ch = 0; ch < layout.second; ++ch)
                for (int i = 0; i < 512; ++i) { finite &= std::isfinite (buf.getSample (ch, i)); peak = jmax (peak, std::abs (buf.getSample (ch, i))); }
        }
        const bool rightHasSignal = layout.second < 2 || buf.getMagnitude (1, 0, 512) > 0.001f;
        char what[128];
        std::snprintf (what, sizeof what, "%d-in/%d-out layout accepted, finite output (peak %.2f), both outputs live",
                       layout.first, layout.second, peak);
        check (ok && finite && rightHasSignal && peak > 0.01f, what);
    }

    // ---- unsupported layout -------------------------------------------------------
    {
        PepperAudioProcessor proc;
        AudioProcessor::BusesLayout l;
        l.inputBuses.add (AudioChannelSet::stereo());
        l.outputBuses.add (AudioChannelSet::mono());
        check (! proc.checkBusesLayoutSupported (l), "stereo-in/mono-out rejected");
    }

    PepperAudioProcessor proc;
    proc.prepareToPlay (48000.0, 256);
    check (proc.getLatencySamples() > 0 && proc.getLatencySamples() < 16, "reports a small latency");

    // ---- factory presets -----------------------------------------------------------
    check (proc.getNumPrograms() == 5, "five factory presets");
    const char* expected[] = { "Hybrid Crunch", "Meteora Lead", "Modern Metal", "Sempiternal Fuzz", "Carolina Chaos" };
    for (int i = 0; i < 5; ++i)
    {
        proc.setCurrentProgram (i);
        const auto& fp = pepper::PresetManager::factoryPresets()[(size_t) i];
        const bool ok = proc.getProgramName (i) == expected[i]
                        && (int) paramValue (proc, pepper::params::pepper) == fp.pepper
                        && std::abs (paramValue (proc, pepper::params::heat) - fp.heat) < 0.001f
                        && std::abs (paramValue (proc, pepper::params::fuzz) - fp.fuzz) < 0.001f
                        && proc.presets.getCurrentName() == expected[i] && ! proc.presets.isModified();
        check (ok, (String ("factory preset loads: ") + expected[i]).toRawUTF8());
    }

    // ---- modified flag ----------------------------------------------------------------
    proc.state.getParameter (pepper::params::tone)->setValueNotifyingHost (0.9f);
    check (proc.presets.isModified(), "editing a knob marks the preset as modified");

    // ---- host state round trip -----------------------------------------------------------
    {
        proc.setCurrentProgram (4);
        MemoryBlock mb;
        proc.getStateInformation (mb);
        PepperAudioProcessor other;
        other.setStateInformation (mb.getData(), (int) mb.getSize());
        const bool ok = (int) paramValue (other, pepper::params::pepper) == 5
                        && std::abs (paramValue (other, pepper::params::heat) - 0.85f) < 0.001f
                        && std::abs (paramValue (other, pepper::params::output) + 1.0f) < 0.05f
                        && other.presets.getCurrentName() == "Carolina Chaos";
        check (ok, "session state saves and restores (values + preset name)");
    }

    // ---- user preset save / load ------------------------------------------------------------
    {
        auto tmp = File::getSpecialLocation (File::tempDirectory).getChildFile ("pepper_test_preset.pepper");
        proc.setCurrentProgram (1);
        proc.state.getParameter (pepper::params::fuzz)->setValueNotifyingHost (0.33f);
        const bool saved = proc.presets.saveUser (tmp);
        proc.setCurrentProgram (0);
        const bool loaded = proc.presets.loadUser (tmp);
        check (saved && loaded && std::abs (paramValue (proc, pepper::params::fuzz) - 0.33f) < 0.001f
                   && (int) paramValue (proc, pepper::params::pepper) == 2 && proc.presets.getCurrentName() == "pepper_test_preset",
               "user preset saves to file and loads back");
        tmp.deleteFile();
    }

    // ---- bypass is latency-aligned ---------------------------------------------------------------
    {
        AudioBuffer<float> buf (2, 64);
        buf.clear();
        buf.setSample (0, 0, 1.0f);
        MidiBuffer midi;
        proc.processBlockBypassed (buf, midi);
        const int lat = proc.getLatencySamples();
        check (std::abs (buf.getSample (0, lat) - 1.0f) < 1.0e-6f, "bypass delays the dry signal by the reported latency");
    }

    // ---- editor screenshot ---------------------------------------------------------------------
    if (argc > 1)
    {
        for (int presetIndex : { 1, 4, 0, 2 })
        {
            proc.setCurrentProgram (presetIndex);
            std::unique_ptr<AudioProcessorEditor> ed (proc.createEditor());
            const float scale = argc > 2 ? String (argv[2]).getFloatValue() : 1.0f;
            ed->setSize (roundToInt (PepperAudioProcessorEditor::baseWidth * scale), roundToInt (PepperAudioProcessorEditor::baseHeight * scale));
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            File out (String (argv[1]) + "_" + String (presetIndex) + ".png");
            out.deleteFile();
            FileOutputStream fos (out);
            PNGImageFormat png;
            const bool ok = png.writeImageToStream (img, fos);
            check (ok, ("editor screenshot written: " + out.getFileName()).toRawUTF8());
        }
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
