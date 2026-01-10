/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "BinauralConvolver.h"

//==============================================================================
/**
*/
class BinauralPannerAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    BinauralPannerAudioProcessor();
    ~BinauralPannerAudioProcessor() override = default;
    
    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    //==============================================================================
    
    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BinauralPannerAudioProcessor)
    
    // Smoothed prams
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> azSmoothDeg;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> elSmoothDeg;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> widthSmooth;
    
    // Separate left and right binaural convolvers
    BinauralConvolver hrirSrcL;
    BinauralConvolver hrirSrcR;
    
    // temp buffers for source and output
    juce::AudioBuffer<float> tmpSrcLMono, tmpSrcRMono;
    juce::AudioBuffer<float> tmpSrcLOut,  tmpSrcROut;
    
};
