#include "BinauralConvolver.h"
#include "BinaryData.h"

// ===================== small helper thread wrapper =====================
namespace
{
    struct SimpleThread final : public juce::Thread
    {
        using Fn = std::function<void()>;
        Fn fn;

        SimpleThread(const juce::String& name, Fn f)
        : juce::Thread(name), fn(std::move(f)) {}

        void run() override
        {
            if (fn) fn();
        }
    };
}

BinauralConvolver::BinauralConvolver()
{
    // Create all 16 convolvers (4 grid points × 2 ears × 2 sets)

    // Set A
    convA_aL = std::make_unique<juce::dsp::Convolution>();
    convA_aR = std::make_unique<juce::dsp::Convolution>();
    convA_bL = std::make_unique<juce::dsp::Convolution>();
    convA_bR = std::make_unique<juce::dsp::Convolution>();
    convA_cL = std::make_unique<juce::dsp::Convolution>();
    convA_cR = std::make_unique<juce::dsp::Convolution>();
    convA_dL = std::make_unique<juce::dsp::Convolution>();
    convA_dR = std::make_unique<juce::dsp::Convolution>();

    // Set B
    convB_aL = std::make_unique<juce::dsp::Convolution>();
    convB_aR = std::make_unique<juce::dsp::Convolution>();
    convB_bL = std::make_unique<juce::dsp::Convolution>();
    convB_bR = std::make_unique<juce::dsp::Convolution>();
    convB_cL = std::make_unique<juce::dsp::Convolution>();
    convB_cR = std::make_unique<juce::dsp::Convolution>();
    convB_dL = std::make_unique<juce::dsp::Convolution>();
    convB_dR = std::make_unique<juce::dsp::Convolution>();
}

BinauralConvolver::~BinauralConvolver()
{
    stopLoaderThread();
}

void BinauralConvolver::prepare (double sampleRate, int maxBlockSize)
{
    fs = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) maxBlockSize;
    spec.numChannels = 1;

    // Prepare all convolvers
    convA_aL->prepare (spec); convA_aR->prepare (spec);
    convA_bL->prepare (spec); convA_bR->prepare (spec);
    convA_cL->prepare (spec); convA_cR->prepare (spec);
    convA_dL->prepare (spec); convA_dR->prepare (spec);

    convB_aL->prepare (spec); convB_aR->prepare (spec);
    convB_bL->prepare (spec); convB_bR->prepare (spec);
    convB_cL->prepare (spec); convB_cR->prepare (spec);
    convB_dL->prepare (spec); convB_dR->prepare (spec);

    // Crossfade duration: 30ms
    xfadeTotal = (int) juce::jlimit (64.0, 48000.0, sampleRate * 0.03);

    // Preallocate temps to max block (avoid realloc during playback)
    ensureTempsCapacity (maxBlockSize);

    reset();

    // Build maps (original filename -> symbol) once
    originalToSymbol.clear();
    originalToSymbol.reserve((size_t) BinaryData::namedResourceListSize);
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        originalToSymbol.emplace(BinaryData::originalFilenames[i], BinaryData::namedResourceList[i]);

    // Decode + resample all HRIR wavs into cache (3MB → totally fine)
    hrirCache.clear();
    hrirCache.reserve((size_t) BinaryData::namedResourceListSize);

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const juce::String original = BinaryData::originalFilenames[i];

        if (! original.endsWithIgnoreCase(".wav"))
            continue;

        const juce::String symbol = BinaryData::namedResourceList[i];

        int dataSize = 0;
        const char* data = BinaryData::getNamedResource(symbol.toRawUTF8(), dataSize);
        if (data == nullptr || dataSize <= 0)
            continue;

        juce::AudioBuffer<float> ir;
        double irSR = 0.0;

        if (! loadIrFromBinaryData(data, dataSize, ir, irSR))
            continue;

        auto irResampled = resampleMono(ir, irSR, fs);

        // Light peak limiting for safety
        const float peak = irResampled.getMagnitude(0, 0, irResampled.getNumSamples());
        if (peak > 1.0f)
            irResampled.applyGain(0.9f / peak);

        hrirCache.emplace(original, std::move(irResampled));
    }

    cacheBuilt = true;

    DBG("BinauralConvolver: HRIR cache built. Count=" + juce::String((int) hrirCache.size()));

    startLoaderThread();
}

void BinauralConvolver::reset()
{
    auto resetConv = [](std::unique_ptr<juce::dsp::Convolution>& c) {
        if (c) c->reset();
    };

    resetConv (convA_aL); resetConv (convA_aR);
    resetConv (convA_bL); resetConv (convA_bR);
    resetConv (convA_cL); resetConv (convA_cR);
    resetConv (convA_dL); resetConv (convA_dR);

    resetConv (convB_aL); resetConv (convB_aR);
    resetConv (convB_bL); resetConv (convB_bR);
    resetConv (convB_cL); resetConv (convB_cR);
    resetConv (convB_dL); resetConv (convB_dR);

    hasA = false;
    hasBReady.store(false);
    switching = false;
    xfadeLeft = 0;

    aAzLower = aAzUpper = aElLower = aElUpper = 0;
    bAzLower = bAzUpper = bElLower = bElUpper = 0;
    aAzFraction = aElFraction = 0.0f;
    bAzFraction = bElFraction = 0.0f;

    // clear pending request
    {
        const juce::ScopedLock sl(requestLock);
        pending = PendingRequest{};
    }
}

void BinauralConvolver::startLoaderThread()
{
    if (loaderThread)
        return;

    threadShouldExit.store(false);
    loaderThread = std::make_unique<SimpleThread>("HRIR_SetB_Loader", [this] { loaderThreadMain(); });
    loaderThread->startThread(juce::Thread::Priority::normal);
}

void BinauralConvolver::stopLoaderThread()
{
    if (! loaderThread)
        return;

    threadShouldExit.store(true);
    requestEvent.signal();
    loaderThread->stopThread(2000);
    loaderThread.reset();
}

void BinauralConvolver::loaderThreadMain()
{
    while (! threadShouldExit.load())
    {
        requestEvent.wait(200); // wake periodically; also wakes on signal

        if (threadShouldExit.load())
            break;

        PendingRequest req;
        {
            const juce::ScopedLock sl(requestLock);
            if (! pending.valid)
                continue;
            req = pending;
            pending.valid = false;
        }

        // Load Set B OFF the audio thread
        hasBReady.store(false);

        const bool ok = loadSetBFromCache(req.azLower, req.azUpper, req.elLower, req.elUpper);
        if (! ok)
            continue;

        // Set B state (safe: only written here; audio thread reads only after hasBReady)
        bAzLower = req.azLower;
        bAzUpper = req.azUpper;
        bElLower = req.elLower;
        bElUpper = req.elUpper;
        bAzFraction = req.azFrac;
        bElFraction = req.elFrac;

        hasBReady.store(true);
    }
}

void BinauralConvolver::requestLoadSetB (int azLower, int azUpper, int elLower, int elUpper,
                                        float azFrac, float elFrac)
{
    // Do not enqueue if not built
    if (! cacheBuilt)
        return;

    {
        const juce::ScopedLock sl(requestLock);
        pending.azLower = azLower;
        pending.azUpper = azUpper;
        pending.elLower = elLower;
        pending.elUpper = elUpper;
        pending.azFrac = azFrac;
        pending.elFrac = elFrac;
        pending.valid = true;
    }

    requestEvent.signal();
}

//==============================================================================
// BinaryData resource name generation (KEEP THIS because +/- collision)
//==============================================================================

juce::String BinauralConvolver::getBinaryResourceName (int azDeg, int elDeg, bool leftEar) const
{
    // Note: L/R swapped based on earlier testing
    const juce::String side = leftEar ? "R" : "L";  // swapped

    const juce::String targetFilename =
        "azi_" + juce::String(azDeg) +
        "_ele_" + juce::String(elDeg) +
        "_" + side + ".wav";

    // Use the map built in prepare() for O(1) lookup
    auto it = originalToSymbol.find(targetFilename);
    if (it != originalToSymbol.end())
        return it->second;

    DBG ("Could not find BinaryData symbol for: " + targetFilename);
    return {};
}

//==============================================================================
// Grid calculations
//==============================================================================

void BinauralConvolver::calculateGridPoints (float azDeg, float elDeg,
                                             int& azLower, int& azUpper, float& azFraction,
                                             int& elLower, int& elUpper, float& elFraction) const
{
    azDeg = juce::jlimit ((float) azimuthMin, (float) azimuthMax, azDeg);
    elDeg = juce::jlimit ((float) elevationMin, (float) elevationMax, elDeg);

    // Azimuth grid
    azLower = ((int) std::floor (azDeg / (float) azimuthGridStep)) * azimuthGridStep;
    azUpper = azLower + azimuthGridStep;
    azLower = juce::jlimit (azimuthMin, azimuthMax, azLower);
    azUpper = juce::jlimit (azimuthMin, azimuthMax, azUpper);

    azFraction = (azUpper != azLower) ? (azDeg - (float) azLower) / (float) azimuthGridStep : 0.0f;
    azFraction = juce::jlimit (0.0f, 1.0f, azFraction);

    // Elevation grid
    elLower = ((int) std::floor (elDeg / (float) elevationGridStep)) * elevationGridStep;
    elUpper = elLower + elevationGridStep;
    elLower = juce::jlimit (elevationMin, elevationMax, elLower);
    elUpper = juce::jlimit (elevationMin, elevationMax, elUpper);

    elFraction = (elUpper != elLower) ? (elDeg - (float) elLower) / (float) elevationGridStep : 0.0f;
    elFraction = juce::jlimit (0.0f, 1.0f, elFraction);
}

//==============================================================================
// Loading (NON-audio thread only)
//==============================================================================

bool BinauralConvolver::loadIrFromBinaryData (const char* data, int dataSize,
                                              juce::AudioBuffer<float>& irBuffer,
                                              double& irSampleRate)
{
    if (data == nullptr || dataSize <= 0)
        return false;

    auto memStream = std::make_unique<juce::MemoryInputStream> (data, (size_t) dataSize, false);

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatReader> reader (wavFormat.createReaderFor (memStream.release(), true));

    if (reader == nullptr)
        return false;

    irSampleRate = reader->sampleRate;

    const int numSamples  = (int) reader->lengthInSamples;
    const int numChannels = (int) reader->numChannels;

    if (numSamples <= 0 || numChannels <= 0)
        return false;

    juce::AudioBuffer<float> temp (numChannels, numSamples);
    reader->read (&temp, 0, numSamples, 0, true, true);

    irBuffer.setSize (1, numSamples);
    irBuffer.clear();

    if (numChannels == 1)
        irBuffer.copyFrom (0, 0, temp, 0, 0, numSamples);
    else
        for (int ch = 0; ch < numChannels; ++ch)
            irBuffer.addFrom (0, 0, temp, ch, 0, numSamples, 1.0f / (float) numChannels);

    return true;
}

juce::AudioBuffer<float> BinauralConvolver::resampleMono (const juce::AudioBuffer<float>& in,
                                                          double inSR,
                                                          double outSR)
{
    if (std::abs (inSR - outSR) < 1.0)
        return in;

    const int inN = in.getNumSamples();
    const double ratio = outSR / inSR;
    const int outN = (int) std::ceil (inN * ratio);

    juce::AudioBuffer<float> out (1, outN);

    juce::LagrangeInterpolator interp;
    interp.reset();

    auto* inPtr  = const_cast<float*> (in.getReadPointer (0));
    auto* outPtr = out.getWritePointer (0);

    interp.process ((double) ratio, inPtr, outPtr, outN);

    return out;
}

bool BinauralConvolver::loadConvolverFromCache (juce::dsp::Convolution& conv,
                                                int azDeg, int elDeg, bool leftEar)
{
    if (! cacheBuilt)
        return false;

    // IMPORTANT: keep your L/R swap decision
    const juce::String side = leftEar ? "R" : "L";

    const juce::String originalFilename =
        "azi_" + juce::String(azDeg) +
        "_ele_" + juce::String(elDeg) +
        "_" + side + ".wav";

    auto it = hrirCache.find(originalFilename);
    if (it == hrirCache.end())
    {
        DBG("HRIR not in cache: " + originalFilename);
        return false;
    }

    // Copy buffer (still not audio thread here). We move into convolver.
    juce::AudioBuffer<float> ir = it->second;

    conv.loadImpulseResponse (std::move(ir),
                              fs,
                              juce::dsp::Convolution::Stereo::no,
                              juce::dsp::Convolution::Trim::no,
                              juce::dsp::Convolution::Normalise::no);

    return true;
}

bool BinauralConvolver::loadHrirPairFromCache (std::unique_ptr<juce::dsp::Convolution>& convL,
                                               std::unique_ptr<juce::dsp::Convolution>& convR,
                                               int azDeg, int elDeg)
{
    if (! loadConvolverFromCache (*convL, azDeg, elDeg, true))  return false;
    if (! loadConvolverFromCache (*convR, azDeg, elDeg, false)) return false;
    return true;
}

bool BinauralConvolver::loadSetAFromCache (int azLower, int azUpper, int elLower, int elUpper)
{
    bool ok = true;
    ok &= loadHrirPairFromCache (convA_aL, convA_aR, azLower, elLower);
    ok &= loadHrirPairFromCache (convA_bL, convA_bR, azUpper, elLower);
    ok &= loadHrirPairFromCache (convA_cL, convA_cR, azUpper, elUpper);
    ok &= loadHrirPairFromCache (convA_dL, convA_dR, azLower, elUpper);
    return ok;
}

bool BinauralConvolver::loadSetBFromCache (int azLower, int azUpper, int elLower, int elUpper)
{
    bool ok = true;
    ok &= loadHrirPairFromCache (convB_aL, convB_aR, azLower, elLower);
    ok &= loadHrirPairFromCache (convB_bL, convB_bR, azUpper, elLower);
    ok &= loadHrirPairFromCache (convB_cL, convB_cR, azUpper, elUpper);
    ok &= loadHrirPairFromCache (convB_dL, convB_dR, azLower, elUpper);
    return ok;
}

//==============================================================================
// Position control
//==============================================================================

void BinauralConvolver::initialiseAtPositionDegrees (float azDeg, float elDeg)
{
    int azL, azU, elL, elU;
    float azF, elF;

    calculateGridPoints(azDeg, elDeg, azL, azU, azF, elL, elU, elF);

    // Synchronously load Set A (safe: called in prepareToPlay, not audio thread)
    if (loadSetAFromCache(azL, azU, elL, elU))
    {
        aAzLower = azL; aAzUpper = azU;
        aElLower = elL; aElUpper = elU;
        aAzFraction = azF;
        aElFraction = elF;
        hasA = true;
    }
}

void BinauralConvolver::beginCrossfadeToReadyB()
{
    // B is ready, so we can begin audio-thread crossfade without loading anything.
    switching = true;
    xfadeLeft = xfadeTotal;
}

void BinauralConvolver::setPositionDegrees (float azDeg, float elDeg)
{
    int newAzLower, newAzUpper, newElLower, newElUpper;
    float newAzFraction, newElFraction;

    calculateGridPoints (azDeg, elDeg,
                         newAzLower, newAzUpper, newAzFraction,
                         newElLower, newElUpper, newElFraction);

    // If not initialised (should be initialised in prepareToPlay), just update fractions safely.
    if (!hasA)
    {
        aAzLower = newAzLower; aAzUpper = newAzUpper;
        aElLower = newElLower; aElUpper = newElUpper;
        aAzFraction = newAzFraction;
        aElFraction = newElFraction;
        return;
    }

    // Same grid region — update fractions only (cheap)
    if (newAzLower == aAzLower && newAzUpper == aAzUpper &&
        newElLower == aElLower && newElUpper == aElUpper)
    {
        aAzFraction = newAzFraction;
        aElFraction = newElFraction;
        return;
    }

    // If currently crossfading:
    // - If B is ready and matches our new region, update B fractions (cheap)
    // - Otherwise request new B load (background)
    if (switching)
    {
        if (hasBReady.load() &&
            newAzLower == bAzLower && newAzUpper == bAzUpper &&
            newElLower == bElLower && newElUpper == bElUpper)
        {
            bAzFraction = newAzFraction;
            bElFraction = newElFraction;
        }
        else
        {
            requestLoadSetB(newAzLower, newAzUpper, newElLower, newElUpper, newAzFraction, newElFraction);
        }
        return;
    }

    // Not switching: request B load for new cell (background). Crossfade will start only when ready.
    requestLoadSetB(newAzLower, newAzUpper, newElLower, newElUpper, newAzFraction, newElFraction);
}

//==============================================================================
// Processing
//==============================================================================

void BinauralConvolver::ensureTempsCapacity (int numSamples)
{
    auto ensureStereo = [numSamples](juce::AudioBuffer<float>& buf)
    {
        if (buf.getNumChannels() != 2 || buf.getNumSamples() < numSamples)
            buf.setSize(2, numSamples, false, false, true);
    };

    ensureStereo(tempA_a); ensureStereo(tempA_b);
    ensureStereo(tempA_c); ensureStereo(tempA_d);
    ensureStereo(tempB_a); ensureStereo(tempB_b);
    ensureStereo(tempB_c); ensureStereo(tempB_d);
    ensureStereo(tempA);   ensureStereo(tempB);

    if (monoTempL.getNumChannels() != 1 || monoTempL.getNumSamples() < numSamples)
        monoTempL.setSize(1, numSamples, false, false, true);

    if (monoTempR.getNumChannels() != 1 || monoTempR.getNumSamples() < numSamples)
        monoTempR.setSize(1, numSamples, false, false, true);
}

void BinauralConvolver::processConvolverPair (const juce::AudioBuffer<float>& monoIn,
                                              juce::AudioBuffer<float>& stereoOut,
                                              juce::dsp::Convolution& convL,
                                              juce::dsp::Convolution& convR)
{
    const int N = monoIn.getNumSamples();

    // Ensure output capacity (no realloc if already large enough)
    if (stereoOut.getNumChannels() != 2 || stereoOut.getNumSamples() < N)
        stereoOut.setSize(2, N, false, false, true);

    // Left ear
    monoTempL.copyFrom(0, 0, monoIn, 0, 0, N);
    {
        juce::dsp::AudioBlock<float> block(monoTempL);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        convL.process(ctx);
    }
    stereoOut.copyFrom(0, 0, monoTempL, 0, 0, N);

    // Right ear
    monoTempR.copyFrom(0, 0, monoIn, 0, 0, N);
    {
        juce::dsp::AudioBlock<float> block(monoTempR);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        convR.process(ctx);
    }
    stereoOut.copyFrom(1, 0, monoTempR, 0, 0, N);
}

void BinauralConvolver::processBilinearSet (const juce::AudioBuffer<float>& monoIn,
                                            juce::AudioBuffer<float>& stereoOut,
                                            juce::dsp::Convolution& conv_aL, juce::dsp::Convolution& conv_aR,
                                            juce::dsp::Convolution& conv_bL, juce::dsp::Convolution& conv_bR,
                                            juce::dsp::Convolution& conv_cL, juce::dsp::Convolution& conv_cR,
                                            juce::dsp::Convolution& conv_dL, juce::dsp::Convolution& conv_dR,
                                            juce::AudioBuffer<float>& temp_a,
                                            juce::AudioBuffer<float>& temp_b,
                                            juce::AudioBuffer<float>& temp_c,
                                            juce::AudioBuffer<float>& temp_d,
                                            float azFrac, float elFrac)
{
    const int N = monoIn.getNumSamples();

    // Process all 4 grid points
    processConvolverPair (monoIn, temp_a, conv_aL, conv_aR);
    processConvolverPair (monoIn, temp_b, conv_bL, conv_bR);
    processConvolverPair (monoIn, temp_c, conv_cL, conv_cR);
    processConvolverPair (monoIn, temp_d, conv_dL, conv_dR);

    // Bilinear weights
    const float w_a = (1.0f - azFrac) * (1.0f - elFrac);
    const float w_b = azFrac * (1.0f - elFrac);
    const float w_c = azFrac * elFrac;
    const float w_d = (1.0f - azFrac) * elFrac;

    if (stereoOut.getNumChannels() != 2 || stereoOut.getNumSamples() < N)
        stereoOut.setSize(2, N, false, false, true);

    for (int ch = 0; ch < 2; ++ch)
    {
        const float* a = temp_a.getReadPointer(ch);
        const float* b = temp_b.getReadPointer(ch);
        const float* c = temp_c.getReadPointer(ch);
        const float* d = temp_d.getReadPointer(ch);
        float* out      = stereoOut.getWritePointer(ch);

        for (int n = 0; n < N; ++n)
            out[n] = w_a * a[n] + w_b * b[n] + w_c * c[n] + w_d * d[n];
    }
}

void BinauralConvolver::process (const juce::AudioBuffer<float>& monoIn,
                                 juce::AudioBuffer<float>& stereoOut)
{
    const int N = monoIn.getNumSamples();

    if (!hasA)
    {
        stereoOut.setSize(2, N, false, false, true);
        stereoOut.clear();
        return;
    }

    ensureTempsCapacity(N);

    // If B finished loading in background AND we are not currently switching,
    // begin the crossfade now (safe & cheap on audio thread).
    if (!switching && hasBReady.load())
    {
        beginCrossfadeToReadyB();
    }

    // Process set A
    processBilinearSet(monoIn, tempA,
                       *convA_aL, *convA_aR,
                       *convA_bL, *convA_bR,
                       *convA_cL, *convA_cR,
                       *convA_dL, *convA_dR,
                       tempA_a, tempA_b, tempA_c, tempA_d,
                       aAzFraction, aElFraction);

    // If not crossfading, output A
    if (!switching || !hasBReady.load())
    {
        stereoOut.setSize(2, N, false, false, true);
        stereoOut.copyFrom(0, 0, tempA, 0, 0, N);
        stereoOut.copyFrom(1, 0, tempA, 1, 0, N);
        return;
    }

    // Process set B (already loaded)
    processBilinearSet(monoIn, tempB,
                       *convB_aL, *convB_aR,
                       *convB_bL, *convB_bR,
                       *convB_cL, *convB_cR,
                       *convB_dL, *convB_dR,
                       tempB_a, tempB_b, tempB_c, tempB_d,
                       bAzFraction, bElFraction);

    // Crossfade A → B (pointer-based, faster)
    stereoOut.setSize(2, N, false, false, true);

    auto* outL = stereoOut.getWritePointer(0);
    auto* outR = stereoOut.getWritePointer(1);

    const auto* aL = tempA.getReadPointer(0);
    const auto* aR = tempA.getReadPointer(1);
    const auto* bL = tempB.getReadPointer(0);
    const auto* bR = tempB.getReadPointer(1);

    for (int n = 0; n < N; ++n)
    {
        const float t  = 1.0f - (float) xfadeLeft / (float) xfadeTotal;
        const float gA = 1.0f - t;
        const float gB = t;

        outL[n] = gA * aL[n] + gB * bL[n];
        outR[n] = gA * aR[n] + gB * bR[n];

        if (xfadeLeft > 0)
            --xfadeLeft;
    }

    // Crossfade complete — swap B → A
    if (xfadeLeft <= 0)
    {
        std::swap (convA_aL, convB_aL); std::swap (convA_aR, convB_aR);
        std::swap (convA_bL, convB_bL); std::swap (convA_bR, convB_bR);
        std::swap (convA_cL, convB_cL); std::swap (convA_cR, convB_cR);
        std::swap (convA_dL, convB_dL); std::swap (convA_dR, convB_dR);

        aAzLower = bAzLower; aAzUpper = bAzUpper;
        aElLower = bElLower; aElUpper = bElUpper;
        aAzFraction = bAzFraction;
        aElFraction = bElFraction;

        // Consume B
        hasBReady.store(false);
        switching = false;
        xfadeLeft = 0;
    }
}
