/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
BinauralPannerAudioProcessorEditor::BinauralPannerAudioProcessorEditor (BinauralPannerAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    // Label
    azimuthLabel.setText ("Azimuth (deg)", juce::dontSendNotification);
    azimuthLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (azimuthLabel);

    // Slider
    azimuthSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    azimuthSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    azimuthSlider.setRange (-90.0, 90.0, 0.01);
    azimuthSlider.setSkewFactorFromMidPoint (0.0); // optional
    addAndMakeVisible (azimuthSlider);

    // Attach slider to APVTS parameter "azimuth"
    azimuthAttachment = std::make_unique<SliderAttachment> (audioProcessor.apvts,
                                                            "azimuth",
                                                            azimuthSlider);

    setSize (260, 180);
}

//BinauralPannerAudioProcessorEditor::~BinauralPannerAudioProcessorEditor()
//{
//}

//==============================================================================
void BinauralPannerAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (15.0f));
    g.drawFittedText ("Hello World!", getLocalBounds(), juce::Justification::centred, 1);
}

void BinauralPannerAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
    auto area = getLocalBounds().reduced (16);

    azimuthLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (8);
    azimuthSlider.setBounds (area.removeFromTop (120).withSizeKeepingCentre (140, 140));
    
}
