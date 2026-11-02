#include "PluginEditor.h"
#include "Parameters.h"

using namespace juce;
using namespace pepper;

namespace
{
// design-space layout
const Rectangle<int> selectorArea { 0, 76, 860, 224 };
const Rectangle<int> taglineArea  { 30, 304, 800, 22 };
const Rectangle<int> knobPanel    { 20, 336, 820, 168 };
}

PepperAudioProcessorEditor::PepperAudioProcessorEditor (PepperAudioProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      selector (*p.state.getParameter (params::pepper))
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    content.addAndMakeVisible (selector);
    selector.onChange = [this] (int i) { updateAccent (i); };

    setupKnob (heat,   params::heat,   "HEAT",      "How hot: the amount of distortion and saturation");
    setupKnob (tone,   params::tone,   "TONE",      "Darker to brighter");
    setupKnob (tight,  params::tight,  "TIGHTNESS", "Turn up for tighter lows and sharper palm mutes; down for a looser, bigger bottom end");
    setupKnob (fuzz,   params::fuzz,   "FUZZ",      "Blends in a thick fuzz voice with extra harmonics");
    setupKnob (output, params::output, "OUTPUT",    "Final volume");
    output.slider.getProperties().set ("bipolarAt", 0.0);

    for (auto* m : { &inMeter, &outMeter })
        content.addAndMakeVisible (*m);

    for (auto* b : { &prevButton, &nextButton, &presetButton, &saveButton })
        content.addAndMakeVisible (*b);

    prevButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
    presetButton.setTooltip ("Choose a preset");
    saveButton.setTooltip ("Save your settings as a preset");
    prevButton.onClick = [this] { processor.presets.loadNext (-1); };
    nextButton.onClick = [this] { processor.presets.loadNext (+1); };
    presetButton.onClick = [this] { showPresetMenu(); };
    saveButton.onClick = [this] { savePreset(); };

    updateAccent (selector.getSelected());
    timerCallback();

    setResizable (true, true);
    setResizeLimits (baseWidth * 7 / 10, baseHeight * 7 / 10, baseWidth * 8 / 5, baseHeight * 8 / 5);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setSize (baseWidth, baseHeight);

    startTimerHz (30);
}

PepperAudioProcessorEditor::~PepperAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PepperAudioProcessorEditor::setupKnob (Knob& k, const String& paramId, const String& name, const String& tip)
{
    k.name = name;
    k.slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    k.slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    k.slider.setMouseDragSensitivity (220);
    k.slider.setVelocityBasedMode (false);
    k.slider.setTooltip (tip);
    k.slider.setTitle (name);
    k.slider.onValueChange = [this] { content.repaint (knobPanel); };
    content.addAndMakeVisible (k.slider);
    k.attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (processor.state, paramId, k.slider);

    if (auto* param = processor.state.getParameter (paramId))
        k.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
}

void PepperAudioProcessorEditor::updateAccent (int pepperIndex)
{
    accent = theme::pepperColour (pepperIndex);
    lnf.setAccent (accent);
    inMeter.setAccent (accent);
    outMeter.setAccent (accent);
    content.repaint();
}

//==============================================================================
void PepperAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) baseWidth;
    content.setBounds (0, 0, baseWidth, baseHeight);
    content.setTransform (AffineTransform::scale (scale));
    layoutContent();
}

void PepperAudioProcessorEditor::layoutContent()
{
    // preset bar
    auto bar = Rectangle<int> (baseWidth - 24 - 360, 18, 360, 32);
    saveButton.setBounds (bar.removeFromRight (68));
    bar.removeFromRight (8);
    nextButton.setBounds (bar.removeFromRight (32));
    bar.removeFromRight (4);
    prevButton.setBounds (bar.removeFromLeft (32));
    bar.removeFromLeft (4);
    presetButton.setBounds (bar);

    selector.setBounds (selectorArea);

    // knob row: meters at the edges, Heat a little bigger than the rest
    auto row = knobPanel.reduced (18, 12);
    inMeter.setBounds (row.removeFromLeft (22).withTrimmedTop (6).withTrimmedBottom (4));
    outMeter.setBounds (row.removeFromRight (22).withTrimmedTop (6).withTrimmedBottom (4));
    row.reduce (20, 0);

    const int heatW = 168, otherW = (row.getWidth() - heatW) / 4;
    auto place = [&] (Knob& k, int w, int size)
    {
        auto col = row.removeFromLeft (w);
        auto knobArea = col.withTrimmedBottom (40);
        k.slider.setBounds (knobArea.withSizeKeepingCentre (size, size));
    };
    place (heat, heatW, 116);
    place (tone, otherW, 92);
    place (tight, otherW, 92);
    place (fuzz, otherW, 92);
    place (output, otherW, 92);
}

void PepperAudioProcessorEditor::paintContent (Graphics& g)
{
    const auto bounds = Rectangle<float> (0, 0, (float) baseWidth, (float) baseHeight);

    // background: near-black with a faint glow in the selected pepper's colour
    g.fillAll (theme::background);
    {
        const auto c = selectorArea.toFloat().getCentre();
        ColourGradient glow (accent.withAlpha (0.13f), c.x, c.y, accent.withAlpha (0.0f), c.x + 520.0f, c.y, true);
        g.setGradientFill (glow);
        g.fillRect (bounds);
        ColourGradient ember (theme::ember.withAlpha (0.0f), 0, (float) baseHeight - 140.0f,
                              theme::ember.withAlpha (0.05f), 0, (float) baseHeight, false);
        g.setGradientFill (ember);
        g.fillRect (bounds);
    }

    // header: logo + wordmark
    drawPepper (g, 5, { 22.0f, 12.0f, 26.0f, 42.0f }, theme::pepperColour (5));
    g.setColour (theme::text);
    g.setFont (theme::font (30.0f, true).withExtraKerningFactor (0.18f));
    g.drawText ("PEPPER", 56, 12, 220, 34, Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.0f, true).withExtraKerningFactor (0.25f));
    g.drawText ("DISTORTION  &  FUZZ", 58, 44, 220, 14, Justification::centredLeft);

    g.setColour (theme::panelEdge);
    g.fillRect (20, 64, baseWidth - 40, 1);

    // selected pepper tagline
    const int sel = selector.getSelected();
    g.setColour (accent.brighter (0.2f));
    g.setFont (theme::font (13.0f, true).withExtraKerningFactor (0.18f));
    g.drawText (params::pepperTaglines[sel], taglineArea, Justification::centred);

    // knob panel
    const auto panel = knobPanel.toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (panel, 14.0f);
    g.setColour (theme::panelEdge);
    g.drawRoundedRectangle (panel.reduced (0.5f), 14.0f, 1.0f);
    g.setColour (accent.withAlpha (0.55f));
    g.fillRoundedRectangle (panel.getCentreX() - 60.0f, panel.getY() - 1.0f, 120.0f, 2.0f, 1.0f);

    // knob names and values
    for (auto* k : { &heat, &tone, &tight, &fuzz, &output })
    {
        const auto b = k->slider.getBounds();
        auto textArea = Rectangle<int> (b.getCentreX() - 80, knobPanel.getBottom() - 52, 160, 18);
        g.setColour (k->slider.isMouseOverOrDragging() ? theme::text : theme::text.withAlpha (0.88f));
        g.setFont (theme::font (k == &heat ? 15.0f : 13.0f, true).withExtraKerningFactor (0.12f));
        g.drawText (k->name, textArea, Justification::centred);

        const auto valueText = k->slider.getTextFromValue (k->slider.getValue());
        g.setColour (k->slider.isMouseOverOrDragging() ? accent.brighter (0.3f) : theme::textDim);
        g.setFont (theme::font (12.0f));
        g.drawText (valueText, textArea.translated (0, 18), Justification::centred);
    }
}

//==============================================================================
void PepperAudioProcessorEditor::timerCallback()
{
    inMeter.push (processor.takeInputPeak());
    outMeter.push (processor.takeOutputPeak());

    auto name = processor.presets.getCurrentName();
    if (processor.presets.isModified()) name << " *";
    if (presetButton.getButtonText() != name)
        presetButton.setButtonText (name);

    if (heat.slider.isMouseOverOrDragging() || tone.slider.isMouseOverOrDragging() || tight.slider.isMouseOverOrDragging()
        || fuzz.slider.isMouseOverOrDragging() || output.slider.isMouseOverOrDragging())
        content.repaint (knobPanel);
}

void PepperAudioProcessorEditor::showPresetMenu()
{
    PopupMenu menu;
    menu.addSectionHeader ("FACTORY");
    const auto& factory = PresetManager::factoryPresets();
    for (int i = 0; i < (int) factory.size(); ++i)
        menu.addItem (1 + i, factory[(size_t) i].name, true, processor.presets.getCurrentFactoryIndex() == i);

    const auto user = processor.presets.getUserPresets();
    if (! user.isEmpty())
    {
        menu.addSectionHeader ("MY PRESETS");
        for (int i = 0; i < user.size(); ++i)
            menu.addItem (1000 + i, user[i].getFileNameWithoutExtension(), true,
                          processor.presets.getCurrentFactoryIndex() < 0
                              && processor.presets.getCurrentName() == user[i].getFileNameWithoutExtension());
    }

    menu.addSeparator();
    menu.addItem (5000, "Save preset...");
    menu.addItem (5001, "Show presets folder");

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&presetButton).withMinimumWidth (presetButton.getWidth()),
                        [this, user] (int result)
                        {
                            if (result >= 1 && result < 1000)       processor.presets.loadFactory (result - 1);
                            else if (result >= 1000 && result < 5000) processor.presets.loadUser (user[result - 1000]);
                            else if (result == 5000)                 savePreset();
                            else if (result == 5001)
                            {
                                auto dir = processor.presets.getUserPresetFolder();
                                dir.createDirectory();
                                dir.startAsProcess();
                            }
                        });
}

void PepperAudioProcessorEditor::savePreset()
{
    auto dir = processor.presets.getUserPresetFolder();
    dir.createDirectory();
    auto suggested = processor.presets.getCurrentName().upToLastOccurrenceOf (" *", false, false);

    chooser = std::make_unique<FileChooser> ("Save PEPPER preset",
                                             dir.getChildFile (File::createLegalFileName (suggested) + PresetManager::fileExtension),
                                             String ("*") + PresetManager::fileExtension);
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles
                              | FileBrowserComponent::warnAboutOverwriting,
                          [this] (const FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f != File())
                                  processor.presets.saveUser (f);
                          });
}
