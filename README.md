# BinauralPanner

A binaural audio panner plugin for spatial audio production. Place mono or stereo sources in 3D space using HRTF-based convolution for realistic headphone listening.

## Features

- **Azimuth & Elevation Control**: Position audio sources from -90° to +90° on both axes
- **Bilinear Interpolation**: Smooth transitions between HRIR positions (10° grid)
- **Real-time Convolution**: Low-latency HRTF processing suitable for DAW use
- **Multiple Formats**: Builds as VST3 and AU (macOS)

## Download

Pre-built plugins are available on the [Releases](https://github.com/zuyuchen/BinauralPanner/releases) page.

### Installation

**macOS AU**: Copy `BinauralPanner.component` to `~/Library/Audio/Plug-Ins/Components/`

**macOS/Windows VST3**: Copy `BinauralPanner.vst3` to your VST3 folder:
- macOS: `~/Library/Audio/Plug-Ins/VST3/`
- Windows: `C:\Program Files\Common Files\VST3\`

## Building from Source

### Requirements

- [JUCE](https://juce.com/) (tested with JUCE 7.x)
- Xcode (macOS) or Visual Studio (Windows)

### Build Steps

1. Clone this repository
2. Open `BinauralPanner.jucer` in Projucer
3. Set your JUCE module path in Projucer
4. Export to your IDE (Xcode/VS)
5. Build the project

## HRIR Data

This plugin uses HRTF data from the [CIPIC HRTF Database](https://www.ece.ucdavis.edu/cipic/spatial-sound/hrtf-data/). The HRIRs are pre-baked into `JuceLibraryCode/BinaryData.cpp` at 10° resolution for both azimuth and elevation (-90° to +90°).

### Regenerating HRIR Data

If you want to use different HRTF data:

1. Obtain a SOFA file from CIPIC or another database
2. Use `tools/sofa_to_wav/export_cipic_nc.py` to convert to WAV files
3. Add the WAVs to your Projucer project as binary resources
4. Rebuild

## License

[Add your license here]

## Acknowledgments

- CIPIC HRTF Database, UC Davis
- JUCE Framework by ROLI
