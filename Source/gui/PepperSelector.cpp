#include "PepperSelector.h"
#include "../Parameters.h"

namespace pepper
{
using namespace juce;

//==============================================================================
// Pepper artwork — vector paths drawn in a 100 x 160 design space
//==============================================================================
namespace
{
struct PepperShape
{
    Path body;
    float topY = 34.0f;      // where the calyx sits
    float calyxW = 36.0f;
};

PepperShape makeShape (int type)
{
    PepperShape s;
    Path& p = s.body;
    switch (type)
    {
        case 0: // Jalapeño — fat, blunt, glossy
            p.startNewSubPath (30, 34);
            p.cubicTo (20, 62, 26, 112, 46, 146);
            p.cubicTo (52, 156, 62, 154, 66, 145);
            p.cubicTo (80, 110, 82, 62, 70, 34);
            p.cubicTo (60, 28, 40, 28, 30, 34);
            s.calyxW = 42; break;

        case 1: // Serrano — slimmer, longer, pointed
            p.startNewSubPath (38, 34);
            p.cubicTo (31, 72, 40, 120, 55, 154);
            p.cubicTo (57, 158, 59, 157, 60, 153);
            p.cubicTo (67, 118, 70, 70, 62, 34);
            p.cubicTo (56, 30, 44, 30, 38, 34);
            s.calyxW = 28; break;

        case 2: // Cayenne — long, thin, curling hook
            p.startNewSubPath (42, 34);
            p.cubicTo (37, 82, 46, 126, 72, 151);
            p.cubicTo (76, 155, 80, 152, 77, 147);
            p.cubicTo (60, 126, 56, 82, 58, 34);
            p.cubicTo (54, 31, 46, 31, 42, 34);
            s.calyxW = 22; break;

        case 3: // Bird's Eye — tiny and sharp
            p.startNewSubPath (44, 54);
            p.cubicTo (41, 82, 45, 110, 52, 130);
            p.cubicTo (53, 133, 55, 133, 56, 130);
            p.cubicTo (61, 110, 61, 82, 56, 54);
            p.cubicTo (53, 51, 47, 51, 44, 54);
            s.topY = 54; s.calyxW = 18; break;

        case 4: // Habanero — squat lantern with lobes
            p.startNewSubPath (26, 56);
            p.cubicTo (14, 74, 15, 106, 29, 124);
            p.cubicTo (35, 132, 41, 126, 46, 132);
            p.cubicTo (50, 138, 55, 136, 57, 130);
            p.cubicTo (63, 125, 67, 132, 73, 124);
            p.cubicTo (86, 106, 86, 74, 74, 56);
            p.cubicTo (62, 48, 38, 48, 26, 56);
            s.topY = 55; s.calyxW = 40; break;

        default: // Carolina Reaper — wrinkled, bulbous, scorpion tail
            p.startNewSubPath (24, 52);
            p.cubicTo (9, 72, 15, 108, 33, 120);
            p.cubicTo (40, 125, 44, 118, 50, 126);
            p.cubicTo (56, 132, 60, 140, 57, 158);
            p.cubicTo (69, 144, 68, 128, 71, 120);
            p.cubicTo (86, 108, 91, 72, 76, 52);
            p.cubicTo (64, 42, 36, 42, 24, 52);
            s.topY = 51; s.calyxW = 42; break;
    }
    p.closeSubPath();
    return s;
}
} // namespace

void drawPepper (Graphics& g, int type, Rectangle<float> area, Colour colour, float alpha)
{
    const auto shape = makeShape (type);
    const auto xf = RectanglePlacement (RectanglePlacement::centred)
                        .getTransformToFit ({ 0.0f, 0.0f, 100.0f, 160.0f }, area);
    const float scale = std::sqrt (std::abs (xf.getDeterminant()));

    Graphics::ScopedSaveState save (g);
    g.addTransform (xf);
    g.setOpacity (alpha);

    const auto& body = shape.body;
    const auto bb = body.getBounds();

    // body with a rounded, lit-from-the-left gradient
    ColourGradient grad (colour.brighter (0.45f), bb.getX(), bb.getY() + bb.getHeight() * 0.3f,
                         colour.darker (0.65f), bb.getRight(), bb.getY() + bb.getHeight() * 0.5f, false);
    grad.addColour (0.45, colour);
    g.setGradientFill (grad);
    g.fillPath (body);

    {
        Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (body);

        // texture: wrinkles for the Reaper, lobes for the Habanero
        g.setColour (colour.darker (0.7f).withAlpha (0.55f));
        if (type == 5)
        {
            for (auto [x0, y0, x1, y1] : { std::array<float, 4> { 30, 66, 38, 92 }, { 44, 60, 48, 98 }, { 62, 64, 58, 96 }, { 72, 70, 66, 100 }, { 40, 100, 52, 112 } })
            {
                Path w; w.startNewSubPath (x0, y0); w.quadraticTo ((x0 + x1) * 0.5f + 4.0f, (y0 + y1) * 0.5f, x1, y1);
                g.strokePath (w, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
            }
        }
        else if (type == 4)
        {
            for (float x : { 38.0f, 50.0f, 62.0f })
            {
                Path w; w.startNewSubPath (x, 58); w.quadraticTo (x + (x - 50) * 0.4f, 96, x, 128);
                g.strokePath (w, PathStrokeType (1.4f));
            }
        }

        // glossy highlight
        Path hl;
        const float hx = bb.getX() + bb.getWidth() * 0.28f;
        hl.startNewSubPath (hx, bb.getY() + bb.getHeight() * 0.10f);
        hl.quadraticTo (hx - bb.getWidth() * 0.12f, bb.getY() + bb.getHeight() * 0.35f,
                        hx - bb.getWidth() * 0.02f, bb.getY() + bb.getHeight() * 0.62f);
        g.setColour (Colours::white.withAlpha (0.38f));
        g.strokePath (hl, PathStrokeType (jmax (2.0f, bb.getWidth() * 0.07f), PathStrokeType::curved, PathStrokeType::rounded));
    }

    g.setColour (colour.darker (0.9f).withAlpha (0.9f));
    g.strokePath (body, PathStrokeType (1.4f));

    // stem and calyx
    const float cx = 50.0f, top = shape.topY, w = shape.calyxW;
    const Colour green (type == 0 ? 0xff2f5f26 : 0xff3f7a2e);

    Path stem;
    stem.startNewSubPath (cx, top);
    stem.quadraticTo (cx + 1.0f, top - 16.0f, cx + 9.0f, top - 26.0f);
    g.setColour (Colour (0xff6b8a3a));
    g.strokePath (stem, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));

    Path calyx;
    calyx.startNewSubPath (cx - w * 0.5f, top + 3.0f);
    calyx.quadraticTo (cx - w * 0.3f, top - 7.0f, cx, top - 3.0f);
    calyx.quadraticTo (cx + w * 0.3f, top - 7.0f, cx + w * 0.5f, top + 3.0f);
    calyx.quadraticTo (cx + w * 0.22f, top + 8.0f, cx, top + 5.0f);
    calyx.quadraticTo (cx - w * 0.22f, top + 8.0f, cx - w * 0.5f, top + 3.0f);
    calyx.closeSubPath();
    g.setColour (green);
    g.fillPath (calyx);
    g.setColour (green.darker (0.6f));
    g.strokePath (calyx, PathStrokeType (1.0f));
    juce::ignoreUnused (scale);
}

//==============================================================================
PepperSelector::PepperSelector (RangedAudioParameter& param)
    : attachment (param, [this] (float v)
                  {
                      selected = jlimit (0, 5, roundToInt (v));
                      repaint();
                      if (onChange) onChange (selected);
                  })
{
    setWantsKeyboardFocus (true);
    setMouseCursor (MouseCursor::PointingHandCursor);
    setTitle ("Pepper selector");
    attachment.sendInitialUpdate();
}

Rectangle<float> PepperSelector::slotBounds (int index) const
{
    // slots sit inside a side margin so the selected pepper's glow never gets clipped
    const float margin = (float) getWidth() * 0.035f;
    const float w = ((float) getWidth() - 2.0f * margin) / 6.0f;
    return { margin + w * (float) index, 0.0f, w, (float) getHeight() };
}

int PepperSelector::slotAt (Point<float> p) const
{
    for (int i = 0; i < 6; ++i)
        if (slotBounds (i).contains (p)) return i;
    return -1;
}

void PepperSelector::choose (int index)
{
    index = jlimit (0, 5, index);
    if (index == selected) return;
    attachment.setValueAsCompleteGesture ((float) index);
}

void PepperSelector::paint (Graphics& g)
{
    const float h = (float) getHeight();
    const float nameH = h * 0.12f, dotsH = h * 0.08f;

    for (int i = 0; i < 6; ++i)
    {
        const auto slot = slotBounds (i);
        const auto colour = theme::pepperColour (i);
        const bool sel = i == selected, hov = i == hovered;

        auto art = slot.withTrimmedBottom (nameH + dotsH + h * 0.04f).reduced (slot.getWidth() * 0.08f, h * 0.03f);

        if (sel)
        {
            // heat glow behind the selected pepper
            const auto c = art.getCentre();
            const float r = jmin (art.getHeight() * 0.62f, slot.getWidth() * 0.8f);
            ColourGradient glow (colour.withAlpha (0.45f), c.x, c.y, colour.withAlpha (0.0f), c.x + r, c.y, true);
            g.setGradientFill (glow);
            g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        }
        else
        {
            art = art.reduced (art.getWidth() * 0.09f, art.getHeight() * 0.09f);
        }

        drawPepper (g, i, art, sel ? colour : colour.darker (hov ? 0.1f : 0.35f).withMultipliedSaturation (hov ? 0.9f : 0.75f),
                    sel ? 1.0f : (hov ? 0.9f : 0.62f));

        // name
        auto label = Rectangle<float> (slot.getX(), slot.getBottom() - nameH - dotsH, slot.getWidth(), nameH);
        g.setColour (sel ? theme::text : (hov ? theme::text.withAlpha (0.8f) : theme::textDim));
        g.setFont (theme::font (jmin (nameH * 0.62f, slot.getWidth() * 0.12f), sel));
        g.drawFittedText (params::pepperDisplayNames[i], label.toNearestInt(), Justification::centred, 1, 0.8f);

        // heat dots: 1 to 6
        const float dot = dotsH * 0.42f, gap = dot * 0.7f;
        const float total = 6.0f * dot + 5.0f * gap;
        float x = slot.getCentreX() - total * 0.5f;
        const float y = slot.getBottom() - dotsH * 0.5f - dot * 0.5f;
        for (int d = 0; d < 6; ++d, x += dot + gap)
        {
            g.setColour (d <= i ? (sel ? colour : colour.withAlpha (0.45f)) : Colour (0xff2c2526));
            g.fillEllipse (x, y, dot, dot);
        }

        if (sel)
        {
            g.setColour (colour.withAlpha (0.9f));
            g.fillRoundedRectangle (slot.getCentreX() - slot.getWidth() * 0.18f, slot.getBottom() - 2.5f,
                                    slot.getWidth() * 0.36f, 2.5f, 1.2f);
        }
    }
}

void PepperSelector::mouseMove (const MouseEvent& e)
{
    const int s = slotAt (e.position);
    if (s != hovered) { hovered = s; repaint(); }
}

void PepperSelector::mouseExit (const MouseEvent&)
{
    hovered = -1;
    repaint();
}

void PepperSelector::mouseDown (const MouseEvent& e)
{
    const int s = slotAt (e.position);
    if (s >= 0) choose (s);
}

void PepperSelector::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    wheelAccum += w.deltaY;
    if (std::abs (wheelAccum) > 0.2f)
    {
        choose (selected + (wheelAccum > 0 ? 1 : -1));
        wheelAccum = 0.0f;
    }
}

bool PepperSelector::keyPressed (const KeyPress& k)
{
    if (k.getKeyCode() == KeyPress::rightKey) { choose (selected + 1); return true; }
    if (k.getKeyCode() == KeyPress::leftKey)  { choose (selected - 1); return true; }
    return false;
}
} // namespace pepper
