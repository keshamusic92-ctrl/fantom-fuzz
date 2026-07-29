#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class FantomFuzzAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit FantomFuzzAudioProcessorEditor (FantomFuzzAudioProcessor&);
    ~FantomFuzzAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    FantomFuzzAudioProcessor& audioProcessor;

    juce::Slider gainSlider, toneSlider, biasSlider, mixSlider, outputSlider;
    juce::Label  gainLabel, toneLabel, biasLabel, mixLabel, outputLabel;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> gainAttachment, toneAttachment, biasAttachment,
                                       mixAttachment, outputAttachment;

    void setupKnob (juce::Slider& slider, juce::Label& label, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FantomFuzzAudioProcessorEditor)
};
