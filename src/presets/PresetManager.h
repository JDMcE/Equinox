#pragma once

#include <jdm/presets/PresetManager.h>
#include "../Parameters.h"

namespace eeq
{

using jdm::PresetManager;
using jdm::PresetInfo;

// Equinox's preset ecosystem configuration: file format identity plus the
// factory library, handed to the shared jdm::PresetManager. Presets without
// a "midimap" property inherit the session's mappings on load, and the
// per-user UI preferences that ride in the state tree (GPU, hints, display
// range) are preserved from the session on load and stripped on save - a
// sound must never toggle GPU rendering or resize the display. Band
// existence ("active" flags) is derived from the "on" overrides by the
// usual state migration in applyLoadedState, so the table only speaks in
// parameters.
inline jdm::PresetProductConfig makePresetConfig()
{
    jdm::PresetProductConfig c;
    c.productName = "Equinox";
    c.extension = "eqnx";
    c.presetTag = juce::Identifier ("EquinoxPreset");
    c.stateTag = juce::Identifier ("Params"); // the APVTS root
    c.stateVersion = 1;
    c.inheritProperties = { "midimap" };
    c.sessionProperties = { "ui_gpu", "ui_hints", "displayRange" };

    using FT = FilterType;
    auto ft = [] (FT t) { return (float) (int) t; };
    auto bp = [] (int band, const char* suffix, float v)
    {
        return std::pair<juce::String, float> (bandParamId (band, suffix), v);
    };

    c.factory = {
        { "Init", "Basics", "default;flat",
          "Flat starting point: double-click the display to add a band.",
          {} },

        { "Gentle High-Pass", "Basics", "highpass;cleanup;low end",
          "12 dB/oct high-pass at 80 Hz - the everyday rumble cleanup.",
          { bp (0, "on", 1.0f), bp (0, "type", ft (FT::lowCut)),
            bp (0, "freq", 80.0f) } },

        { "Vocal Clarity", "Vocals", "vocal;lead;presence;air",
          "High-pass the rumble, dip the mud, open the presence and air.",
          { bp (0, "on", 1.0f), bp (0, "type", ft (FT::lowCut)), bp (0, "freq", 90.0f),
            bp (1, "on", 1.0f), bp (1, "freq", 250.0f), bp (1, "gain", -2.0f),
            bp (2, "on", 1.0f), bp (2, "freq", 3200.0f), bp (2, "gain", 2.5f),
            bp (2, "q", 0.9f),
            bp (3, "on", 1.0f), bp (3, "type", ft (FT::highShelf)),
            bp (3, "freq", 12000.0f), bp (3, "gain", 1.5f) } },

        { "De-Harsh Vocal", "Vocals", "vocal;harshness;dynamic",
          "A dynamic bell tames 3-4 kHz harshness only when it flares up.",
          { bp (0, "on", 1.0f), bp (0, "freq", 3500.0f), bp (0, "q", 1.4f),
            bp (0, "dyn", 1.0f), bp (0, "dynrange", -6.0f), bp (0, "thresh", -32.0f),
            bp (0, "att", 5.0f), bp (0, "rel", 120.0f),
            bp (1, "on", 1.0f), bp (1, "type", ft (FT::highShelf)),
            bp (1, "freq", 9000.0f), bp (1, "gain", 1.0f) } },

        { "Sibilance Tamer", "Vocals", "vocal;sibilance;de-ess;dynamic",
          "A de-esser built from a fast dynamic high shelf at 6.8 kHz.",
          { bp (0, "on", 1.0f), bp (0, "type", ft (FT::highShelf)),
            bp (0, "freq", 6800.0f), bp (0, "dyn", 1.0f),
            bp (0, "dynrange", -8.0f), bp (0, "thresh", -30.0f),
            bp (0, "att", 1.0f), bp (0, "rel", 60.0f) } },

        { "Kick Punch", "Drums", "kick;drums;punch",
          "Thump at 55 Hz, boxiness carved at 350 Hz, beater click at 4 kHz.",
          { bp (0, "on", 1.0f), bp (0, "freq", 55.0f), bp (0, "gain", 3.0f),
            bp (0, "q", 1.1f),
            bp (1, "on", 1.0f), bp (1, "freq", 350.0f), bp (1, "gain", -3.5f),
            bp (1, "q", 1.2f),
            bp (2, "on", 1.0f), bp (2, "freq", 4000.0f), bp (2, "gain", 2.5f) } },

        { "Drum Bus Tight", "Drums", "drums;bus;dynamic",
          "Sub-rumble gone, boxiness ducked dynamically, a lift of air on top.",
          { bp (0, "on", 1.0f), bp (0, "type", ft (FT::lowCut)),
            bp (0, "freq", 35.0f), bp (0, "slope", 3.0f),
            bp (1, "on", 1.0f), bp (1, "freq", 500.0f), bp (1, "q", 1.1f),
            bp (1, "dyn", 1.0f), bp (1, "dynrange", -3.0f), bp (1, "thresh", -28.0f),
            bp (2, "on", 1.0f), bp (2, "type", ft (FT::highShelf)),
            bp (2, "freq", 10000.0f), bp (2, "gain", 1.5f) } },

        { "Bass Definition", "Instruments", "bass;low end;definition",
          "Weight below 90 Hz, less woolliness at 700 Hz, string detail at 1.6 kHz.",
          { bp (0, "on", 1.0f), bp (0, "type", ft (FT::lowShelf)),
            bp (0, "freq", 90.0f), bp (0, "gain", 2.0f),
            bp (1, "on", 1.0f), bp (1, "freq", 700.0f), bp (1, "gain", -2.0f),
            bp (2, "on", 1.0f), bp (2, "freq", 1600.0f), bp (2, "gain", 2.0f) } },

        { "Acoustic Sparkle", "Instruments", "acoustic;guitar;sparkle",
          "Clean low end and an airy shelf, with Gentle character saturation.",
          { bp (0, "on", 1.0f), bp (0, "type", ft (FT::lowCut)), bp (0, "freq", 70.0f),
            bp (1, "on", 1.0f), bp (1, "freq", 200.0f), bp (1, "gain", -1.5f),
            bp (2, "on", 1.0f), bp (2, "type", ft (FT::highShelf)),
            bp (2, "freq", 8500.0f), bp (2, "gain", 2.0f),
            { characterId, (float) (int) CharacterMode::gentle } } },

        { "Mix Bus Polish", "Bus & Master", "bus;mix;glue",
          "A gentle smile - half a shelf each end - plus Gentle character.",
          { bp (0, "on", 1.0f), bp (0, "type", ft (FT::lowShelf)),
            bp (0, "freq", 100.0f), bp (0, "gain", 1.0f), bp (0, "q", 0.8f),
            bp (1, "on", 1.0f), bp (1, "type", ft (FT::highShelf)),
            bp (1, "freq", 10000.0f), bp (1, "gain", 1.2f),
            { characterId, (float) (int) CharacterMode::gentle } } },

        { "Mastering Air (Linear)", "Bus & Master", "mastering;air;linear phase",
          "Linear-phase air shelf at 12.5 kHz - transparent top, adds latency.",
          { bp (0, "on", 1.0f), bp (0, "type", ft (FT::highShelf)),
            bp (0, "freq", 12500.0f), bp (0, "gain", 1.2f), bp (0, "q", 0.7f),
            { phaseModeId, (float) (int) PhaseMode::linearPhase },
            { lpQualityId, 3.0f } } },

        { "Warm Tilt", "Bus & Master", "tilt;warm;tone",
          "A slight tilt toward the lows with Warm character saturation.",
          { bp (0, "on", 1.0f), bp (0, "type", ft (FT::tiltShelf)),
            bp (0, "freq", 700.0f), bp (0, "gain", -1.5f),
            { characterId, (float) (int) CharacterMode::warm } } },

        { "Rumble Guard", "Utility", "highpass;rumble;protection",
          "24 dB/oct high-pass at 30 Hz: sub protection that leaves the low end alone.",
          { bp (0, "on", 1.0f), bp (0, "type", ft (FT::lowCut)),
            bp (0, "freq", 30.0f), bp (0, "slope", 3.0f) } },

        { "Telephone", "Effects", "lofi;telephone;bandlimit",
          "Steep band-limit to 500 Hz - 3.2 kHz with a mid honk: instant handset.",
          { bp (0, "on", 1.0f), bp (0, "type", ft (FT::lowCut)),
            bp (0, "freq", 500.0f), bp (0, "slope", 5.0f),
            bp (1, "on", 1.0f), bp (1, "type", ft (FT::highCut)),
            bp (1, "freq", 3200.0f), bp (1, "slope", 5.0f),
            bp (2, "on", 1.0f), bp (2, "freq", 1800.0f), bp (2, "gain", 3.0f),
            bp (2, "q", 1.5f) } },

        { "Spectral Smooth", "Utility", "spectral;resonance;smooth",
          "No bands at all: spectral dynamics gently even out bins that poke "
          "above their neighbourhood.",
          { { specOnId, 1.0f }, { specAmountId, 6.0f }, { specSensId, 5.0f } } },
    };
    return c;
}

} // namespace eeq
