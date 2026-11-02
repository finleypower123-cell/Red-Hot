#pragma once

#include "Theme.h"

namespace pepper
{
/** Simple vertical peak meter with hold and clip light. Fed from the editor's timer. */
class LevelMeter final : public juce::Component
{
public:
    explicit LevelMeter (juce::String labelText) : label (std::move (labelText)) {}

    void setAccent (juce::Colour c) { accent = c; }

    /** Call at ~30 Hz with the latest block peak (linear). */
    void push (float peak)
    {
        const float db = juce::Decibels::gainToDecibels (peak, -80.0f);
        level = juce::jmax (db, level - 1.6f);                // ~48 dB/s fall
        if (db >= hold) { hold = db; holdFrames = 30; }
        else if (--holdFrames < 0) hold = juce::jmax (level, hold - 1.0f);
        if (peak > 1.0f) clipFrames = 60;
        else if (clipFrames > 0) --clipFrames;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto labelArea = r.removeFromBottom (16.0f);
        auto clipArea = r.removeFromTop (8.0f);
        r.removeFromTop (3.0f);

        g.setColour (juce::Colour (0xff0b0909));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (theme::panelEdge);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);

        const auto toY = [&] (float db) { return juce::jmap (juce::jlimit (minDb, maxDb, db), minDb, maxDb, r.getBottom() - 2.0f, r.getY() + 2.0f); };
        auto bar = r.reduced (3.0f, 2.0f);
        auto fill = bar.withTop (toY (level));
        if (fill.getHeight() > 0.5f)
        {
            juce::ColourGradient grad (accent.darker (0.4f), 0.0f, bar.getBottom(), juce::Colour (0xffff3b30), 0.0f, toY (0.0f), false);
            grad.addColour (0.75, accent);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (fill, 1.5f);
        }

        g.setColour (theme::text.withAlpha (0.75f));
        g.fillRect (bar.getX(), toY (hold) - 1.0f, bar.getWidth(), 1.5f);

        // 0 dB and -18 dB ticks
        g.setColour (theme::textFaint);
        for (float db : { 0.0f, -18.0f })
            g.fillRect (r.getRight() + 2.0f, toY (db) - 0.5f, 4.0f, 1.0f);

        g.setColour (clipFrames > 0 ? juce::Colour (0xffff3b30) : juce::Colour (0xff2a2223));
        g.fillRoundedRectangle (clipArea.reduced (2.0f, 0.0f), 2.0f);

        g.setColour (theme::textDim);
        g.setFont (theme::font (11.0f, true));
        g.drawText (label, labelArea.expanded (8.0f, 0.0f), juce::Justification::centred);
    }

private:
    juce::String label;
    juce::Colour accent { theme::ember };
    float level = -80.0f, hold = -80.0f;
    int holdFrames = 0, clipFrames = 0;
    static constexpr float minDb = -60.0f, maxDb = 6.0f;
};
} // namespace pepper
