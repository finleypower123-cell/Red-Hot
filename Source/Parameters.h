#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace pepper::params
{
inline const juce::String pepper { "pepper" };
inline const juce::String heat   { "heat" };
inline const juce::String tone   { "tone" };
inline const juce::String tight  { "tight" };
inline const juce::String fuzz   { "fuzz" };
inline const juce::String output { "output" };

inline const juce::StringArray pepperNames { juce::String (juce::CharPointer_UTF8 ("Jalape\xc3\xb1o")), "Serrano", "Cayenne",
                                             "Bird's Eye", "Habanero", "Carolina Reaper" };

/** Upper-case names for the GUI (spelled out so the Ñ survives on every system). */
inline const juce::StringArray pepperDisplayNames { juce::String (juce::CharPointer_UTF8 ("JALAPE\xc3\x91O")), "SERRANO", "CAYENNE",
                                                    "BIRD'S EYE", "HABANERO", "CAROLINA REAPER" };

inline juce::StringArray makeTaglines()
{
    const juce::String dot (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
    return { "WARM" + dot + "CRUNCHY" + dot + "CONTROLLED",
             "GRITTY" + dot + "PUNCHY" + dot + "UPPER-MID BITE",
             "THICK" + dot + "SATURATED" + dot + "CUTTING",
             "TIGHT" + dot + "SHARP ATTACK" + dot + "DEFINED CHUGS",
             "DENSE" + dot + "BITING MIDS" + dot + "COMPRESSED",
             "BLOWN-OUT" + dot + "CHAOTIC" + dot + "FILTHY FUZZ" };
}
inline const juce::StringArray pepperTaglines = makeTaglines();

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto percent = AudioParameterFloatAttributes()
                       .withStringFromValueFunction ([] (float v, int) { return String (juce::roundToInt (v * 100.0f)) + "%"; })
                       .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue() / 100.0f; });

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pepper, 1 }, "Pepper", pepperNames, 2));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { heat, 1 },  "Heat",      NormalisableRange<float> (0.0f, 1.0f), 0.6f,  percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { tone, 1 },  "Tone",      NormalisableRange<float> (0.0f, 1.0f), 0.5f,  percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { tight, 1 }, "Tightness", NormalisableRange<float> (0.0f, 1.0f), 0.5f,  percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { fuzz, 1 },  "Fuzz",      NormalisableRange<float> (0.0f, 1.0f), 0.0f,  percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { output, 1 }, "Output",   NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                    AudioParameterFloatAttributes()
                        .withLabel ("dB")
                        .withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1) + " dB"; })
                        .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue(); })));
    return layout;
}
} // namespace pepper::params
