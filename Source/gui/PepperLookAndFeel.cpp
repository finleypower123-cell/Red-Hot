#include "PepperLookAndFeel.h"

namespace pepper
{
using namespace juce;

PepperLookAndFeel::PepperLookAndFeel()
{
    setColour (PopupMenu::backgroundColourId, theme::panel);
    setColour (PopupMenu::textColourId, theme::text);
    setColour (PopupMenu::headerTextColourId, theme::textDim);
    setColour (PopupMenu::highlightedBackgroundColourId, Colour (0xff2c2223));
    setColour (PopupMenu::highlightedTextColourId, theme::text);
    setColour (TextButton::textColourOffId, theme::text);
    setColour (TextButton::textColourOnId, theme::text);
    setColour (Slider::textBoxTextColourId, theme::text);
    setColour (TooltipWindow::backgroundColourId, theme::panel);
    setColour (TooltipWindow::textColourId, theme::text);
}

void PepperLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                          float startAngle, float endAngle, Slider& slider)
{
    const auto bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float trackW = jmax (3.0f, radius * 0.085f);
    const float arcR = radius - trackW;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool hot = slider.isMouseOverOrDragging();

    // track
    Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (Colour (0xff2a2223));
    g.strokePath (track, PathStrokeType (trackW, PathStrokeType::curved, PathStrokeType::rounded));

    // value arc (bipolar sliders start from their centre value)
    float fromAngle = startAngle;
    if (slider.getProperties().contains ("bipolarAt"))
    {
        const double centreVal = slider.getProperties()["bipolarAt"];
        fromAngle = startAngle + (float) slider.valueToProportionOfLength (centreVal) * (endAngle - startAngle);
    }

    if (std::abs (angle - fromAngle) > 0.001f)
    {
        Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, jmin (fromAngle, angle), jmax (fromAngle, angle), true);
        g.setColour (accent.withAlpha (hot ? 0.32f : 0.22f));
        g.strokePath (value, PathStrokeType (trackW * 2.6f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (value, PathStrokeType (trackW, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // knob body
    const float bodyR = arcR - trackW * 2.2f;
    const auto body = Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);

    g.setColour (Colours::black.withAlpha (0.55f));                       // drop shadow
    g.fillEllipse (body.translated (0.0f, bodyR * 0.08f).expanded (bodyR * 0.04f));

    ColourGradient grad (Colour (0xff3a3233), body.getX(), body.getY(),
                         Colour (0xff141112), body.getRight(), body.getBottom(), false);
    g.setGradientFill (grad);
    g.fillEllipse (body);

    g.setColour (Colour (0xff4a4041).withAlpha (0.9f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.drawEllipse (body.reduced (bodyR * 0.16f), 1.0f);

    // pointer
    const Point<float> p1 = centre.getPointOnCircumference (bodyR * 0.30f, angle);
    const Point<float> p2 = centre.getPointOnCircumference (bodyR * 0.82f, angle);
    g.setColour (hot ? accent.brighter (0.3f) : theme::text);
    g.drawLine ({ p1, p2 }, jmax (2.0f, bodyR * 0.09f));
}

void PepperLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (down ? Colour (0xff2c2223) : highlighted ? Colour (0xff231c1d) : theme::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (highlighted ? accent.withAlpha (0.7f) : theme::panelEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void PepperLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool highlighted, bool)
{
    g.setColour (highlighted ? theme::text : theme::text.withAlpha (0.85f));
    g.setFont (theme::font (jmin (15.0f, (float) b.getHeight() * 0.5f), true));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 0), Justification::centred, 1);
}

void PepperLookAndFeel::drawPopupMenuBackground (Graphics& g, int w, int h)
{
    g.fillAll (theme::panel);
    g.setColour (theme::panelEdge);
    g.drawRect (0, 0, w, h);
}
} // namespace pepper
