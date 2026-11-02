#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

namespace pepper
{
/** Draws one of the six peppers (0 = Jalapeño ... 5 = Carolina Reaper) to fit 'area'. */
void drawPepper (juce::Graphics&, int type, juce::Rectangle<float> area, juce::Colour colour, float alpha = 1.0f);

/** The centrepiece: six peppers, click one to choose the distortion character. */
class PepperSelector final : public juce::Component
{
public:
    explicit PepperSelector (juce::RangedAudioParameter& pepperParam);

    int getSelected() const noexcept { return selected; }
    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    int slotAt (juce::Point<float>) const;
    juce::Rectangle<float> slotBounds (int index) const;
    void choose (int index);

    juce::ParameterAttachment attachment;
    int selected = 2, hovered = -1;
    float wheelAccum = 0.0f;
};
} // namespace pepper
