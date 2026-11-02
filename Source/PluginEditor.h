#pragma once

#include "PluginProcessor.h"
#include "gui/PepperLookAndFeel.h"
#include "gui/PepperSelector.h"
#include "gui/LevelMeter.h"

class PepperAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PepperAudioProcessorEditor (PepperAudioProcessor&);
    ~PepperAudioProcessorEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

    static constexpr int baseWidth = 860, baseHeight = 520;

private:
    /** Everything is laid out at a fixed design size inside this, then scaled to fit. */
    class Content final : public juce::Component
    {
    public:
        explicit Content (PepperAudioProcessorEditor& o) : owner (o) {}
        void paint (juce::Graphics& g) override { owner.paintContent (g); }
    private:
        PepperAudioProcessorEditor& owner;
    };

    struct Knob
    {
        juce::Slider slider;
        juce::String name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void paintContent (juce::Graphics&);
    void layoutContent();
    void timerCallback() override;
    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name, const juce::String& tip);
    void showPresetMenu();
    void savePreset();
    void updateAccent (int pepperIndex);

    PepperAudioProcessor& processor;
    pepper::PepperLookAndFeel lnf;
    Content content { *this };

    pepper::PepperSelector selector;
    Knob heat, tone, tight, fuzz, output;
    pepper::LevelMeter inMeter { "IN" }, outMeter { "OUT" };

    juce::TextButton prevButton { "<" }, nextButton { ">" }, presetButton, saveButton { "SAVE" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };

    juce::Colour accent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PepperAudioProcessorEditor)
};
