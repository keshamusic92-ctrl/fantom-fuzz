#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
FantomFuzzAudioProcessorEditor::FantomFuzzAudioProcessorEditor (FantomFuzzAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setupKnob (gainSlider,   gainLabel,   "Gain");
    setupKnob (toneSlider,   toneLabel,   "Tone");
    setupKnob (biasSlider,   biasLabel,   "Bias");
    setupKnob (mixSlider,    mixLabel,    "Mix");
    setupKnob (outputSlider, outputLabel, "Output");

    auto& apvts = audioProcessor.apvts;
    gainAttachment   = std::make_unique<SliderAttachment> (apvts, FantomFuzzAudioProcessor::GAIN_ID,   gainSlider);
    toneAttachment   = std::make_unique<SliderAttachment> (apvts, FantomFuzzAudioProcessor::TONE_ID,   toneSlider);
    biasAttachment   = std::make_unique<SliderAttachment> (apvts, FantomFuzzAudioProcessor::BIAS_ID,   biasSlider);
    mixAttachment    = std::make_unique<SliderAttachment> (apvts, FantomFuzzAudioProcessor::MIX_ID,    mixSlider);
    outputAttachment = std::make_unique<SliderAttachment> (apvts, FantomFuzzAudioProcessor::OUTPUT_ID, outputSlider);

    setSize (520, 220);
}

FantomFuzzAudioProcessorEditor::~FantomFuzzAudioProcessorEditor() {}

void FantomFuzzAudioProcessorEditor::setupKnob (juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.attachToComponent (&slider, false);
    addAndMakeVisible (label);
}

//==============================================================================
void FantomFuzzAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (
        juce::Colour (0xff2a1010), 0, 0,
        juce::Colour (0xff100808), 0, (float) getHeight(), false));
    g.fillAll();

    g.setColour (juce::Colours::orange.withAlpha (0.9f));
    g.setFont (juce::Font (22.0f, juce::Font::bold));
    g.drawText ("FANTOM FUZZ", getLocalBounds().removeFromTop (36),
                juce::Justification::centred);
}

void FantomFuzzAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (30); // title space

    const int knobCount = 5;
    const int knobWidth = area.getWidth() / knobCount;

    juce::Slider* sliders[] = { &gainSlider, &toneSlider, &biasSlider, &mixSlider, &outputSlider };

    for (int i = 0; i < knobCount; ++i)
    {
        auto knobArea = area.removeFromLeft (knobWidth).reduced (8, 20);
        sliders[i]->setBounds (knobArea);
    }
}
