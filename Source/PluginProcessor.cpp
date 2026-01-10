/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"


//==============================================================================
BinauralPannerAudioProcessor::BinauralPannerAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
                     #endif
                       apvts(*this, nullptr, "PARAMS", createParameterLayout()) // initialize APVTS
#endif
{
}

//BinauralPannerAudioProcessor::~BinauralPannerAudioProcessor()
//{
//}

//==============================================================================
const juce::String BinauralPannerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool BinauralPannerAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool BinauralPannerAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool BinauralPannerAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double BinauralPannerAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int BinauralPannerAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int BinauralPannerAudioProcessor::getCurrentProgram()
{
    return 0;
}

void BinauralPannerAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String BinauralPannerAudioProcessor::getProgramName (int index)
{
    return {};
}

void BinauralPannerAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void BinauralPannerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Prepare Smoothing
    juce::ignoreUnused(samplesPerBlock);
    
    const double smoothTimeSec = 0.02; // ramping in 20ms to a new target value (smoothing)
    
    azSmoothDeg.reset (sampleRate, smoothTimeSec);
    elSmoothDeg.reset (sampleRate, smoothTimeSec);
    widthSmooth.reset (sampleRate, smoothTimeSec);
    
    // set current to current parameter values to avoid a jump on play
    azSmoothDeg.setCurrentAndTargetValue (apvts.getRawParameterValue("azimuth")->load());
    elSmoothDeg.setCurrentAndTargetValue (apvts.getRawParameterValue("elevation")->load());
    widthSmooth.setCurrentAndTargetValue (apvts.getRawParameterValue("width")->load());
    
    // ==================== For Bianural Panner Only ========================
    hrirSrcL.prepare(sampleRate, samplesPerBlock);
    hrirSrcR.prepare(sampleRate, samplesPerBlock);
    
    // After hrirSrcL.prepare / hrirSrcR.prepare:
    const float initAz = apvts.getRawParameterValue("azimuth")->load();
    const float initEl = apvts.getRawParameterValue("elevation")->load();

    const float maxSepDeg = 45.0f;
    const float initWidth = apvts.getRawParameterValue("width")->load();
    const float azLf = juce::jlimit (-90.0f, 90.0f, initAz - initWidth * maxSepDeg);
    const float azRf = juce::jlimit (-90.0f, 90.0f, initAz + initWidth * maxSepDeg);

    hrirSrcL.initialiseAtPositionDegrees(azLf, initEl);
    hrirSrcR.initialiseAtPositionDegrees(azRf, initEl);

    // set temporary input and output buffers
    tmpSrcLMono.setSize(1, samplesPerBlock);
    tmpSrcRMono.setSize(1, samplesPerBlock);
    tmpSrcLOut.setSize (2, samplesPerBlock);
    tmpSrcROut.setSize (2, samplesPerBlock);
    
}

void BinauralPannerAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool BinauralPannerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

static inline void equalPowerGainsFromPan (float panMinus1To1, float& weightL, float& weightR)
{
    const float angle = (panMinus1To1 + 1.0f) * 0.25f * juce::MathConstants<float>::pi; // [-1, 1]->[0, pi/2]
    weightL = std::cos (angle); // project the angle to the left
    weightR = std::sin (angle); // project the angle to the right
}

void BinauralPannerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());
    
    
    const int numCh = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    if (numCh < 2) return;
    // =================================================================================
    // --- Set targets once per block ---
    
    // read the prams
    const float azTargetDeg = apvts.getRawParameterValue("azimuth")->load();
    const float elTargetDeg = apvts.getRawParameterValue("elevation")->load();
    const float widthTarget =apvts.getRawParameterValue("width")->load();
    
    // set the targets
    azSmoothDeg.setTargetValue(azTargetDeg);
    elSmoothDeg.setTargetValue(elTargetDeg);
    widthSmooth.setTargetValue(widthTarget);    // set the targets
    
    const float maxSepDeg = 45.0f;
    
    const int mode = (int) apvts.getRawParameterValue("mode")->load();  // 0=Stereo, 1=Binaural
    
    if (mode ==0)
    {
        // ====================== Stereo Panner ("per-sample smoothing") ================================
        auto* inL = buffer.getReadPointer (0);
        auto* inR = buffer.getReadPointer (1);
        auto* outL = buffer.getWritePointer (0);
        auto* outR = buffer.getWritePointer (1);
        
        // --- Write the output samples ---
        for (int i = 0; i < numSamples; ++i)
        {
            // update the pram's ramping value at the current sample
            const float centerAz = azSmoothDeg.getNextValue(); //-90, 90
            const float width    = widthSmooth.getNextValue(); //0.0, 1.0
            
            // find the azimuths for the extended virtual stereo positions
            float azL = juce::jlimit (-90.0f, 90.0f, centerAz - width * maxSepDeg);
            float azR = juce::jlimit (-90.0f, 90.0f, centerAz + width * maxSepDeg);
            
            // convert azimuth(-90, 90) to pan values (-1, 1)
            const float panL = juce::jlimit (-1.0f, 1.0f, azL / 90.0f);
            const float panR = juce::jlimit (-1.0f, 1.0f, azR / 90.0f);
            
            // compute the panning weights
            float gLL, gLR, gRL, gRR;
            equalPowerGainsFromPan(panL, gLL, gLR); // stereo panning weights for the left source
            equalPowerGainsFromPan(panR, gRL, gRR); // stereo panning weights for the right source
            
            const float xL = inL[i];
            const float xR = inR[i];
            
            outL[i] = xL * gLL + xR * gRL;  // yL = xLL + xRL
            outR[i] = xL * gLR + xR * gRR;  // yR = xLR + xRR
        }
        return;
    }
    
    // ========================== Binaural Panner ("per-block smoothing")
    
    // --- advance smoothing across the block and grab the last values ---
    float centerAz = 0.0f;
    float centerEl = 0.0f;
    float width = 0.0f;
    
    for (int i = 0; i < numSamples; ++i)
    {
        // update the pram's ramping value at the current sample
        centerAz = azSmoothDeg.getNextValue();
        centerEl = elSmoothDeg.getNextValue();
        width = widthSmooth.getNextValue();
    }
    
    // compute final azL/azR for this block
    const float azLf = juce::jlimit (-90.0f, 90.0f, centerAz - width * maxSepDeg);
    const float azRf = juce::jlimit (-90.0f, 90.0f, centerAz + width * maxSepDeg);
    
    // Set position with azimuth AND elevation
    hrirSrcL.setPositionDegrees (azLf, centerEl);
    hrirSrcR.setPositionDegrees (azRf, centerEl);
    
    // prepare mono buffers from input L and input R
    tmpSrcLMono.setSize (1, numSamples, false, false, true);
    tmpSrcRMono.setSize (1, numSamples, false, false, true);
    
    // copy in source L and source R
    tmpSrcLMono.copyFrom (0, 0, buffer, 0, 0, numSamples); // xL
    tmpSrcRMono.copyFrom (0, 0, buffer, 1, 0, numSamples); // xR
    
    // prepare stereo out buffers 
    tmpSrcLOut.setSize (2, numSamples, false, false, true);
    tmpSrcROut.setSize (2, numSamples, false, false, true);
    tmpSrcLOut.clear();
    tmpSrcROut.clear();
    
    // start hrir convolution (with interpolation)
    hrirSrcL.process (tmpSrcLMono, tmpSrcLOut);
    hrirSrcR.process (tmpSrcRMono, tmpSrcROut);
    
    // sum to output
    buffer.copyFrom (0, 0, tmpSrcLOut, 0, 0, numSamples);
    buffer.addFrom (0, 0, tmpSrcROut, 0, 0, numSamples);   // yL = xLL + xRL
    
    buffer.copyFrom (1, 0, tmpSrcLOut, 1, 0, numSamples);
    buffer.addFrom (1, 0, tmpSrcROut, 1, 0, numSamples);    // yR = xLR + xRR

}

//==============================================================================
bool BinauralPannerAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* BinauralPannerAudioProcessor::createEditor()
{
    return new BinauralPannerAudioProcessorEditor (*this);
}

//==============================================================================
void BinauralPannerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void BinauralPannerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================

juce::AudioProcessorValueTreeState::ParameterLayout BinauralPannerAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

        // Azimuth: -90 (left) to +90 (right)
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "azimuth",
            "Azimuth",
            juce::NormalisableRange<float> (-90.0f, 90.0f, 0.01f),
            0.0f));  // default center
    
        // Elevation: -90 (below) to +90 (above)
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "elevation",
            "Elevation",
            juce::NormalisableRange<float> (-90.0f, 90.0f, 0.01f),
            0.0f));  // default center
    
        // Mode: Stereo or Binaural
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            "mode",
            "Mode",
            juce::StringArray { "Stereo", "Binaural" },
            0));  // default Stereo

        // Width: 0.0 to 1.0
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "width",
            "Width",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
            1.0f));  // default full width

    return { params.begin(), params.end() };
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BinauralPannerAudioProcessor();
}
