#pragma once

#include <jdm/ui/LookAndFeel.h>

// Equinox's face of the shared family design language (jdm_shared): the
// common near-black blue-grey surfaces with Equinox's cyan control accent.
// The reserved hue is the summed EQ curve (same hue as the accent, by
// design - the curve IS the product); level metering is green.

namespace eeq
{

inline const jdm::colors::Palette equinoxPalette {
    juce::Colour (0xff15181e), juce::Colour (0xff1d2129),
    juce::Colour (0xff242a34), juce::Colour (0xff313845),
    juce::Colour (0xffd8dce4), juce::Colour (0xff828a9a),
    juce::Colour (0xff46b5dc), juce::Colour (0xff2a6e86),
    juce::Colour (0xff46b5dc), juce::Colour (0xff56b58c),
    juce::Colour (0xffd96a5a), juce::Colour (0xffb287d8)
};

inline const jdm::colors::Palette equinoxPaletteHighContrast {
    juce::Colour (0xff090b0e), juce::Colour (0xff11141a),
    juce::Colour (0xff1b2029), juce::Colour (0xff5a6478),
    juce::Colour (0xfff4f7fc), juce::Colour (0xffbcc5d6),
    juce::Colour (0xff62cdf4), juce::Colour (0xff35849f),
    juce::Colour (0xff62cdf4), juce::Colour (0xff6bd9a6),
    juce::Colour (0xffff8071), juce::Colour (0xffcfa5f4)
};

namespace colors
{
    using namespace jdm::colors;

    // Equinox's reserved hue is the summed EQ curve.
    inline juce::Colour& curve = jdm::colors::reserved;
}

struct LookAndFeel : jdm::LookAndFeel
{
    LookAndFeel() : jdm::LookAndFeel (equinoxPalette, equinoxPaletteHighContrast) {}
};

} // namespace eeq
