/*
    PEPPER — factory presets plus user presets saved as small XML files in
    ~/Documents/PEPPER/Presets (easy to find, back up and share with the band).
*/
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace pepper
{
struct FactoryPreset
{
    const char* name;
    int pepper;
    float heat, tone, tight, fuzz, outputDb;
};

class PresetManager : private juce::AudioProcessorValueTreeState::Listener
{
public:
    static const std::vector<FactoryPreset>& factoryPresets();
    static constexpr const char* fileExtension = ".pepper";

    explicit PresetManager (juce::AudioProcessorValueTreeState&);
    ~PresetManager() override;

    juce::File getUserPresetFolder() const;
    juce::Array<juce::File> getUserPresets() const;

    void loadFactory (int index);
    bool loadUser (const juce::File&);
    bool saveUser (const juce::File&);

    /** Steps through factory presets then user presets, wrapping round. */
    void loadNext (int direction);

    juce::String getCurrentName() const    { return currentName; }
    void setCurrentName (const juce::String& n) { currentName = n; modified = false; }
    bool isModified() const noexcept       { return modified.load(); }

    int getCurrentFactoryIndex() const     { return factoryIndex; }

private:
    void applyValues (const std::map<juce::String, float>& values);
    void parameterChanged (const juce::String&, float) override;

    juce::AudioProcessorValueTreeState& state;
    juce::String currentName { "Default" };
    int factoryIndex = -1;
    juce::File currentFile;
    std::atomic<bool> modified { false };
    std::atomic<bool> applying { false };
};
} // namespace pepper
