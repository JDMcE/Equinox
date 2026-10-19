# Equinox

A 24-band parametric EQ plugin (VST3 + CLAP + Standalone), built with JUCE 8.
Part of the Equinox Audio family alongside [Meridian](https://github.com/JDMcE/Meridian)
(compressor), [Zenith](https://github.com/JDMcE/Zenith) (limiter) and
[Horizon](https://github.com/JDMcE/Horizon) (reverb).

Code targets and namespaces use the internal name `eeq`.

## Building

Requirements: CMake 3.22+ and a C++20 compiler (developed with Visual Studio 2022).

Equinox shares code with the rest of the family through
[jdm_shared](https://github.com/JDMcE/jdm_shared), which must be checked out next to
this repository:

```
some-folder/
  Equinox/
  jdm_shared/
```

```
git clone https://github.com/JDMcE/Equinox.git
git clone https://github.com/JDMcE/jdm_shared.git
cd Equinox
git clone --depth 1 --branch 8.0.14 https://github.com/juce-framework/JUCE.git libs/JUCE
git clone --recurse-submodules https://github.com/free-audio/clap-juce-extensions.git libs/clap-juce-extensions
cmake -S . -B build
cmake --build build --config Release --target eeQ_VST3 eeQ_Standalone eeQ_CLAP
```

Outputs:

- VST3: `build/eeQ_artefacts/Release/VST3/Equinox.vst3`
- CLAP: `build/eeQ_artefacts/Release/CLAP/Equinox.clap`
- Standalone app: `build/eeQ_artefacts/Release/Standalone/Equinox.exe`

On Windows, copy `Equinox.vst3` to `C:\Program Files\Common Files\VST3\` to use it in your DAW.

The `eeq_tests` target builds an offline DSP verification harness (run the built
executable; it exits non-zero on failure).

## Using the EQ display

- **Double-click** empty space: add a band
- **Drag** a handle: frequency / gain
- **Mouse wheel** over a handle: Q
- **Double-click** a handle: text entry for freq/gain/Q (accepts `1.2k`, note names like `A4`)
- **Alt+hold** a handle (or hold the band panel's **Solo** button): intelligent solo
- **Right-click** a handle: shape, slope (cuts), channel routing (Stereo/L/R/Mid/Side),
  copy/paste band, snap to note, reset gain, delete
- **Right-click** empty space: paste band, copy/paste preset (works across instances),
  analyzer options (pre/post, freeze, speed)
- **Grab dot on a spectrum peak**: drag to create a band right on the peak (Spectrum Grab)
- **Sketch** button: draw the curve you want; bands are fitted on release (EQ Sketch)
- **A/B** and **A>B** header buttons: state comparison and copy
- **±3/6/12/30 dB**: display range (mastering vs mixing)
- **Match** button: EQ Match — capture the input and a reference (sidechain or a
  spectrum pasted from another instance), then fit bands to the difference
- **MIDI** button: MIDI Learn — map hardware CCs to Output Gain, Gain Scale, or
  the freq/gain/Q of whichever band is currently selected
- **GPU** toggle: render the UI with OpenGL; **Hints** toggle: hover help hints
- **Ctrl+Z / Ctrl+Shift+Z**: undo / redo

## Project layout

```
src/
  Parameters.h          parameter IDs, ranges, APVTS layout (24 bands x 7 params + master)
  PluginProcessor.*     audio processing: band cascade, smoothing, metering, analyzer feeds
  PluginEditor.*        top-level UI: header bar, display, band panel, meter
  dsp/
    FilterCoeffs.h      RBJ biquads, Butterworth cut cascades, response evaluation
    EqBand.h            per-band biquad state + Stereo/L/R/M/S block processing
    AnalyzerFifo.h      lock-free audio->GUI FIFO for the spectrum analyzer
  ui/
    EqDisplay.*         analyzer + EQ curve + draggable band handles
    BandPanel.*         selected-band controls
    LevelMeter.h        stereo peak meter with peak-hold readout
test/
  DspTests.cpp          offline DSP verification harness
```

## License

Equinox is free software, released under the GNU Affero General Public License v3.0
— see [LICENSE](LICENSE). It is built on [JUCE](https://juce.com), used under JUCE's
AGPLv3 open-source licence.
