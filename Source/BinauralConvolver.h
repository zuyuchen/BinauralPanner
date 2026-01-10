#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <unordered_map>

/**
    BinauralConvolver
    - Bilinear interpolation across azimuth/elevation by mixing outputs of 4 convolvers (a,b,c,d).
    - Uses two sets (A/B) for crossfading when grid cell changes.
    - HRIR WAVs are embedded via BinaryData.
    - IMPORTANT: All WAV decode + Convolution::loadImpulseResponse happens OFF the audio thread.
*/
class BinauralConvolver
{
public:
    BinauralConvolver();
    ~BinauralConvolver();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // Call ONCE in prepareToPlay (non-audio thread) to synchronously load SetA so playback starts glitch-free.
    void initialiseAtPositionDegrees (float azDeg, float elDeg);

    // Can be called from audio thread. This function NEVER decodes WAV and NEVER calls loadImpulseResponse.
    void setPositionDegrees (float azDeg, float elDeg);

    // Audio-thread processing
    void process (const juce::AudioBuffer<float>& monoIn,
                  juce::AudioBuffer<float>& stereoOut);

private:
    // ===================== Config =====================
    int azimuthMin = -90;
    int azimuthMax =  90;
    int azimuthGridStep = 10;

    int elevationMin = -90;
    int elevationMax =  90;
    int elevationGridStep = 10;

    // ===================== Convolution sets (A/B) =====================
    // Set A
    std::unique_ptr<juce::dsp::Convolution> convA_aL, convA_aR;
    std::unique_ptr<juce::dsp::Convolution> convA_bL, convA_bR;
    std::unique_ptr<juce::dsp::Convolution> convA_cL, convA_cR;
    std::unique_ptr<juce::dsp::Convolution> convA_dL, convA_dR;

    // Set B
    std::unique_ptr<juce::dsp::Convolution> convB_aL, convB_aR;
    std::unique_ptr<juce::dsp::Convolution> convB_bL, convB_bR;
    std::unique_ptr<juce::dsp::Convolution> convB_cL, convB_cR;
    std::unique_ptr<juce::dsp::Convolution> convB_dL, convB_dR;

    bool hasA = false;
    std::atomic<bool> hasBReady { false };
    bool switching = false;

    // Set A state
    int aAzLower = 0, aAzUpper = 0, aElLower = 0, aElUpper = 0;
    float aAzFraction = 0.0f, aElFraction = 0.0f;

    // Set B state (prepared by background thread)
    int bAzLower = 0, bAzUpper = 0, bElLower = 0, bElUpper = 0;
    float bAzFraction = 0.0f, bElFraction = 0.0f;

    // Crossfade
    int xfadeTotal = 0;
    int xfadeLeft  = 0;

    // ===================== Temp buffers (preallocated) =====================
    // For 4 corners per set: each is stereo
    juce::AudioBuffer<float> tempA_a, tempA_b, tempA_c, tempA_d;
    juce::AudioBuffer<float> tempB_a, tempB_b, tempB_c, tempB_d;
    juce::AudioBuffer<float> tempA, tempB;

    // Mono temp buffers to avoid allocating inside processConvolverPair
    juce::AudioBuffer<float> monoTempL, monoTempR;

    // ===================== HRIR cache (decoded & resampled) =====================
    struct JuceStringHash
    {
        size_t operator()(const juce::String& s) const noexcept { return (size_t) s.hashCode64(); }
    };

    // Key: original filename (e.g. "azi_-10_ele_-10_L.wav")
    std::unordered_map<juce::String, juce::AudioBuffer<float>, JuceStringHash> hrirCache;

    // Map original filename -> BinaryData symbol name (wav / wav2 / wav3...)
    std::unordered_map<juce::String, juce::String, JuceStringHash> originalToSymbol;

    bool cacheBuilt = false;
    double fs = 48000.0;

    // ===================== Background loader thread =====================
    struct PendingRequest
    {
        int azLower = 0, azUpper = 0, elLower = 0, elUpper = 0;
        float azFrac = 0.0f, elFrac = 0.0f;
        bool valid = false;
    };

    juce::CriticalSection requestLock;
    PendingRequest pending;
    std::atomic<bool> threadShouldExit { false };
    juce::WaitableEvent requestEvent;

    std::unique_ptr<juce::Thread> loaderThread;

    void startLoaderThread();
    void stopLoaderThread();
    void loaderThreadMain();

    void requestLoadSetB (int azLower, int azUpper, int elLower, int elUpper,
                          float azFrac, float elFrac);

    // ===================== Internal helpers =====================
    void ensureTempsCapacity (int numSamples);

    void calculateGridPoints (float azDeg, float elDeg,
                              int& azLower, int& azUpper, float& azFraction,
                              int& elLower, int& elUpper, float& elFraction) const;

    // Keep this (you need it because of - sign collisions → wav/wav2/wav3...)
    juce::String getBinaryResourceName (int azDeg, int elDeg, bool leftEar) const;

    // Decode WAV from BinaryData into mono buffer (NOT audio thread)
    bool loadIrFromBinaryData (const char* data, int dataSize,
                               juce::AudioBuffer<float>& irBuffer,
                               double& irSampleRate);

    juce::AudioBuffer<float> resampleMono (const juce::AudioBuffer<float>& in,
                                           double inSR,
                                           double outSR);

    // Load a single convolver from cache (NOT audio thread)
    bool loadConvolverFromCache (juce::dsp::Convolution& conv,
                                 int azDeg, int elDeg, bool leftEar);

    bool loadHrirPairFromCache (std::unique_ptr<juce::dsp::Convolution>& convL,
                                std::unique_ptr<juce::dsp::Convolution>& convR,
                                int azDeg, int elDeg);

    bool loadSetAFromCache (int azLower, int azUpper, int elLower, int elUpper);
    bool loadSetBFromCache (int azLower, int azUpper, int elLower, int elUpper);

    // Processing kernels
    void processConvolverPair (const juce::AudioBuffer<float>& monoIn,
                               juce::AudioBuffer<float>& stereoOut,
                               juce::dsp::Convolution& convL,
                               juce::dsp::Convolution& convR);

    void processBilinearSet (const juce::AudioBuffer<float>& monoIn,
                             juce::AudioBuffer<float>& stereoOut,
                             juce::dsp::Convolution& conv_aL, juce::dsp::Convolution& conv_aR,
                             juce::dsp::Convolution& conv_bL, juce::dsp::Convolution& conv_bR,
                             juce::dsp::Convolution& conv_cL, juce::dsp::Convolution& conv_cR,
                             juce::dsp::Convolution& conv_dL, juce::dsp::Convolution& conv_dR,
                             juce::AudioBuffer<float>& temp_a,
                             juce::AudioBuffer<float>& temp_b,
                             juce::AudioBuffer<float>& temp_c,
                             juce::AudioBuffer<float>& temp_d,
                             float azFrac, float elFrac);

    void beginCrossfadeToReadyB();
};
