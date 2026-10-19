#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

namespace eeq
{

// "A4", "F#2 +12c" etc. Empty string when out of the note range.
inline juce::String freqToNoteName (double freq)
{
    if (freq <= 0.0)
        return {};

    const double midi = 69.0 + 12.0 * std::log2 (freq / 440.0);
    const int n = (int) std::lround (midi);
    if (n < 0 || n > 135)
        return {};

    static const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                   "F#", "G", "G#", "A", "A#", "B" };
    juce::String s = juce::String (names[n % 12]) + juce::String (n / 12 - 1);

    const int cents = (int) std::lround ((midi - n) * 100.0);
    if (cents != 0)
        s << (cents > 0 ? " +" : " ") << juce::String (std::abs (cents)) << "c";
    return s;
}

// Frequency of the note nearest to freq.
inline double snapFreqToNote (double freq)
{
    const double midi = std::lround (69.0 + 12.0 * std::log2 (freq / 440.0));
    return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
}

// Parses "A4", "c#2", "Bb3". Returns -1 if the text is not a note name.
inline double noteNameToFreq (const juce::String& textIn)
{
    const auto text = textIn.trim().toUpperCase();
    if (text.length() < 2)
        return -1.0;

    static const int semis[] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    const auto letter = text[0];
    if (letter < 'A' || letter > 'G')
        return -1.0;

    int semi = semis[letter - 'A'];
    int pos = 1;

    if (text[pos] == '#')      { ++semi; ++pos; }
    else if (text[pos] == 'B' && text.length() > pos + 1) { --semi; ++pos; }

    const auto octaveText = text.substring (pos);
    if (octaveText.isEmpty() || ! octaveText.containsOnly ("-0123456789"))
        return -1.0;

    const int midi = (octaveText.getIntValue() + 1) * 12 + semi;
    return 440.0 * std::pow (2.0, (midi - 69) / 12.0);
}

// Parses user-entered frequency text: "440", "1.2k", "A4".
inline double parseFreqText (const juce::String& textIn)
{
    const auto asNote = noteNameToFreq (textIn);
    if (asNote > 0.0)
        return asNote;

    auto text = textIn.trim().toLowerCase().removeCharacters (" ");
    text = text.upToLastOccurrenceOf ("hz", false, false);

    double mult = 1.0;
    if (text.endsWithChar ('k'))
    {
        mult = 1000.0;
        text = text.dropLastCharacters (1);
    }

    const double v = text.getDoubleValue();
    return v > 0.0 ? v * mult : -1.0;
}

} // namespace eeq
