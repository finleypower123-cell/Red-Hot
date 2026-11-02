#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/PepperEngine.h"
#include "Presets.h"

class PepperAudioProcessor final : public juce::AudioProcessor
{
public:
    PepperAudioProcessor();
    ~PepperAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PEPPER"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState state;
    pepper::PresetManager presets;

    /** Peak levels since the editor last read them (read-and-reset from the GUI). */
    float takeInputPeak() noexcept  { return inputPeak.exchange (0.0f); }
    float takeOutputPeak() noexcept { return outputPeak.exchange (0.0f); }

private:
    pepper::PepperEngine engine;

    std::atomic<float>* pPepper = nullptr;
    std::atomic<float>* pHeat = nullptr;
    std::atomic<float>* pTone = nullptr;
    std::atomic<float>* pTight = nullptr;
    std::atomic<float>* pFuzz = nullptr;
    std::atomic<float>* pOutput = nullptr;

    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };

    // latency-matched dry delay so bypass stays sample-aligned
    juce::AudioBuffer<float> bypassDelay;
    int bypassPos = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PepperAudioProcessor)
};
