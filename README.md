# BinauralPanner

A binaural audio panner plugin for spatial audio production using HRTF convolution.

## Features

- **Azimuth & elevation control**: Full spherical positioning (-90° to +90° on both axes)
- **Bilinear interpolation**: Smooth spatial transitions by mixing 4 neighboring HRIR positions (a, b, c, d)
- **Crossfading**: Dual convolver sets (A/B) for glitch-free transitions when crossing grid boundaries
- **Thread-safe loading**: All WAV decoding and impulse response loading happens off the audio thread
- **CIPIC HRTF database**: 10° grid resolution with embedded HRIR data

## Demo

🎬 [Watch the demo on Bilibili](https://www.bilibili.com/video/BV1Ne6QBpEC4)

## Download

Pre-built plugins are available on the [Releases](https://github.com/zuyuchen/BinauralPanner/releases) page.

### Installation

**macOS AU**: Copy `BinauralPanner.component` to `~/Library/Audio/Plug-Ins/Components/`

**macOS VST3** *(coming soon)*: Copy `BinauralPanner.vst3` to `~/Library/Audio/Plug-Ins/VST3/`

## Building from Source

### Requirements

- [JUCE](https://juce.com/) v8.x
- Xcode (macOS)

### Build Steps

1. Clone this repository
2. Open `BinauralPanner.jucer` in Projucer
3. Set your JUCE module path in Projucer (File → Global Paths)
4. Click "Save and Open in IDE"
5. In Xcode, select Release configuration and build (Cmd+B)

## HRIR Data

This plugin uses HRTF data from the [CIPIC HRTF Database](https://www.ece.ucdavis.edu/cipic/spatial-sound/hrtf-data/). The HRIRs are pre-baked into `JuceLibraryCode/BinaryData.cpp` at 10° resolution for both azimuth and elevation (-90° to +90°).

### Regenerating HRIR Data

If you want to use different HRTF data:

1. Obtain a SOFA file from CIPIC or another database
2. Use `tools/sofa_to_wav/export_cipic_nc.py` to convert to WAV files
3. Add the WAVs to your Projucer project as binary resources
4. Rebuild

## License

MIT License

Copyright (c) 2026 Zuyu Chen

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Acknowledgments

- [CIPIC HRTF Database](https://www.ece.ucdavis.edu/cipic/spatial-sound/hrtf-data/), UC Davis
- [JUCE Framework](https://juce.com/)
