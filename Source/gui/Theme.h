#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace pepper::theme
{
// Each pepper's colour, from mild to murderous
inline juce::Colour pepperColour (int index)
{
    static const juce::Colour colours[] {
        juce::Colour (0xff4caf50), // Jalapeño   — green
        juce::Colour (0xffa8c83a), // Serrano    — lime
        juce::Colour (0xfff26b2a), // Cayenne    — orange-red
        juce::Colour (0xffe8283a), // Bird's Eye — red
        juce::Colour (0xffff9020), // Habanero   — orange
        juce::Colour (0xffb5122c)  // Carolina Reaper — deep crimson
    };
    return colours[juce::jlimit (0, 5, index)];
}

inline const juce::Colour background  { 0xff0e0b0c };
inline const juce::Colour panel       { 0xff171314 };
inline const juce::Colour panelEdge   { 0xff2a2223 };
inline const juce::Colour text        { 0xfff2ece8 };
inline const juce::Colour textDim     { 0xff8d8280 };
inline const juce::Colour textFaint   { 0xff5a5150 };
inline const juce::Colour ember       { 0xffff5a2c };

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}
} // namespace pepper::theme
