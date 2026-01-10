/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
*/
class BinauralPannerAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
   explicit BinauralPannerAudioProcessorEditor (BinauralPannerAudioProcessor&);
    ~BinauralPannerAudioProcessorEditor() override = default;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    BinauralPannerAudioProcessor& audioProcessor;
    
    juce::Label azimuthLabel;
    juce::Slider azimuthSlider;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> azimuthAttachment;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BinauralPannerAudioProcessorEditor)
};
