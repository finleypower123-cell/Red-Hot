#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

using namespace pepper;

PepperAudioProcessor::PepperAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "PEPPER", params::createLayout()),
      presets (state)
{
    pPepper = state.getRawParameterValue (params::pepper);
    pHeat   = state.getRawParameterValue (params::heat);
    pTone   = state.getRawParameterValue (params::tone);
    pTight  = state.getRawParameterValue (params::tight);
    pFuzz   = state.getRawParameterValue (params::fuzz);
    pOutput = state.getRawParameterValue (params::output);
}

bool PepperAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();

    if (out != mono && out != stereo) return false;
    return in == out || (in == mono && out == stereo);
}

void PepperAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    EngineParams p;
    p.pepper = (int) pPepper->load();
    p.heat = pHeat->load(); p.tone = pTone->load(); p.tight = pTight->load();
    p.fuzz = pFuzz->load(); p.outputDb = pOutput->load();
    engine.setParams (p);
    engine.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (engine.getLatencySamples());

    bypassDelay.setSize (2, juce::jmax (1, engine.getLatencySamples()));
    bypassDelay.clear();
    bypassPos = 0;
}

void PepperAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, n);

    EngineParams p;
    p.pepper = (int) pPepper->load();
    p.heat = pHeat->load();
    p.tone = pTone->load();
    p.tight = pTight->load();
    p.fuzz = pFuzz->load();
    p.outputDb = pOutput->load();
    engine.setParams (p);

    const int procChannels = juce::jmin (numIn, 2);
    engine.process (buffer.getArrayOfWritePointers(), procChannels, n);

    if (numIn == 1 && numOut >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    // keep the highest peak until the editor collects it
    auto keepMax = [] (std::atomic<float>& a, float v)
    {
        float prev = a.load();
        while (v > prev && ! a.compare_exchange_weak (prev, v)) {}
    };
    keepMax (inputPeak, engine.getInputPeak());
    keepMax (outputPeak, engine.getOutputPeak());
}

void PepperAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // pass the dry signal through, delayed by the same latency we report
    const int len = bypassDelay.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int chans = juce::jmin (2, buffer.getNumChannels());

    if (getLatencySamples() > 0)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            for (int ch = 0; ch < chans; ++ch)
            {
                const float in = buffer.getSample (ch, i);
                buffer.setSample (ch, i, bypassDelay.getSample (ch, bypassPos));
                bypassDelay.setSample (ch, bypassPos, in);
            }
            bypassPos = (bypassPos + 1) % len;
        }
    }

    if (numIn == 1 && numOut >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());

    inputPeak = 0.0f;
    outputPeak = 0.0f;
}

//==============================================================================
int PepperAudioProcessor::getNumPrograms()
{
    return (int) PresetManager::factoryPresets().size();
}

int PepperAudioProcessor::getCurrentProgram()
{
    return juce::jmax (0, presets.getCurrentFactoryIndex());
}

void PepperAudioProcessor::setCurrentProgram (int index)
{
    presets.loadFactory (index);
}

const juce::String PepperAudioProcessor::getProgramName (int index)
{
    const auto& list = PresetManager::factoryPresets();
    return juce::isPositiveAndBelow (index, (int) list.size()) ? juce::String (list[(size_t) index].name) : juce::String();
}

void PepperAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto tree = state.copyState();
    tree.setProperty ("presetName", presets.getCurrentName(), nullptr);
    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, dest);
}

void PepperAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        if (xml->hasTagName (state.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            const auto name = tree.getProperty ("presetName", "Default").toString();
            state.replaceState (tree);
            presets.setCurrentName (name);
        }
    }
}

juce::AudioProcessorEditor* PepperAudioProcessor::createEditor()
{
    return new PepperAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PepperAudioProcessor();
}
