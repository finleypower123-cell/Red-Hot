#pragma once

#include "Theme.h"

namespace pepper
{
class PepperLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PepperLookAndFeel();

    void setAccent (juce::Colour c) { accent = c; }
    juce::Colour getAccent() const  { return accent; }

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;

    juce::Font getPopupMenuFont() override { return theme::font (15.0f); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;

private:
    juce::Colour accent { theme::ember };
};
} // namespace pepper
