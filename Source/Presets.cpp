#include "Presets.h"
#include "Parameters.h"

namespace pepper
{
const std::vector<FactoryPreset>& PresetManager::factoryPresets()
{
    //                       name                pepper  heat   tone   tight  fuzz   out dB
    static const std::vector<FactoryPreset> list {
        { "Hybrid Crunch",    3,     0.55f, 0.52f, 0.70f, 0.05f, 0.0f },  // Bird's Eye: tight, punchy, mid-forward
        { "Meteora Lead",     2,     0.80f, 0.62f, 0.45f, 0.15f, 0.0f },  // Cayenne: thick, singing, a touch of fuzz
        { "Modern Metal",     4,     0.70f, 0.55f, 0.78f, 0.00f, 0.0f },  // Habanero: dense, tight, biting
        { "Sempiternal Fuzz", 4,     0.62f, 0.48f, 0.60f, 0.55f, 0.0f },  // Habanero + fuzz: huge and saturated
        { "Carolina Chaos",   5,     0.85f, 0.50f, 0.35f, 0.70f, -1.0f }, // Reaper: blown-out, chaotic
    };
    return list;
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s) : state (s)
{
    for (auto* id : { &params::pepper, &params::heat, &params::tone, &params::tight, &params::fuzz, &params::output })
        state.addParameterListener (*id, this);
}

PresetManager::~PresetManager()
{
    for (auto* id : { &params::pepper, &params::heat, &params::tone, &params::tight, &params::fuzz, &params::output })
        state.removeParameterListener (*id, this);
}

void PresetManager::parameterChanged (const juce::String&, float)
{
    if (! applying)
        modified = true;
}

juce::File PresetManager::getUserPresetFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("PEPPER").getChildFile ("Presets");
}

juce::Array<juce::File> PresetManager::getUserPresets() const
{
    auto files = getUserPresetFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension);
    files.sort();
    return files;
}

void PresetManager::applyValues (const std::map<juce::String, float>& values)
{
    applying = true;
    for (const auto& [id, value] : values)
    {
        if (auto* p = state.getParameter (id))
        {
            const float norm = p->convertTo0to1 (value);
            p->beginChangeGesture();
            p->setValueNotifyingHost (norm);
            p->endChangeGesture();
        }
    }
    applying = false;
    modified = false;
}

void PresetManager::loadFactory (int index)
{
    const auto& list = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) list.size())) return;

    const auto& p = list[(size_t) index];
    applyValues ({ { params::pepper, (float) p.pepper }, { params::heat, p.heat }, { params::tone, p.tone },
                   { params::tight, p.tight }, { params::fuzz, p.fuzz }, { params::output, p.outputDb } });
    currentName = p.name;
    factoryIndex = index;
    currentFile = juce::File();
}

bool PresetManager::loadUser (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("PepperPreset")) return false;

    std::map<juce::String, float> values;
    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
        values[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("value");

    applyValues (values);
    currentName = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
    factoryIndex = -1;
    currentFile = file;
    return true;
}

bool PresetManager::saveUser (const juce::File& requested)
{
    auto file = requested.withFileExtension (fileExtension);
    file.getParentDirectory().createDirectory();

    juce::XmlElement xml ("PepperPreset");
    xml.setAttribute ("name", file.getFileNameWithoutExtension());
    xml.setAttribute ("version", 1);
    for (auto* id : { &params::pepper, &params::heat, &params::tone, &params::tight, &params::fuzz, &params::output })
    {
        if (auto* p = state.getParameter (*id))
        {
            auto* e = xml.createNewChildElement ("PARAM");
            e->setAttribute ("id", *id);
            e->setAttribute ("value", (double) p->convertFrom0to1 (p->getValue()));
        }
    }

    if (! xml.writeTo (file)) return false;
    currentName = file.getFileNameWithoutExtension();
    factoryIndex = -1;
    currentFile = file;
    modified = false;
    return true;
}

void PresetManager::loadNext (int direction)
{
    const int numFactory = (int) factoryPresets().size();
    const auto user = getUserPresets();
    const int total = numFactory + user.size();
    if (total == 0) return;

    int pos = factoryIndex;
    if (pos < 0 && currentFile.existsAsFile())
        pos = numFactory + user.indexOf (currentFile);
    pos = pos < 0 ? (direction > 0 ? 0 : total - 1) : (pos + direction + total) % total;

    if (pos < numFactory) loadFactory (pos);
    else                  loadUser (user[pos - numFactory]);
}
} // namespace pepper
