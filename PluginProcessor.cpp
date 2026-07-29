#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
FantomFuzzAudioProcessor::FantomFuzzAudioProcessor()
     : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
       apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

FantomFuzzAudioProcessor::~FantomFuzzAudioProcessor() {}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout FantomFuzzAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        GAIN_ID, "Gain",
        juce::NormalisableRange<float> (1.0f, 40.0f, 0.01f, 0.4f), 8.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        TONE_ID, "Tone",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        BIAS_ID, "Bias",
        juce::NormalisableRange<float> (-0.5f, 0.5f, 0.001f), 0.08f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        MIX_ID, "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.8f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        OUTPUT_ID, "Output",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
void FantomFuzzAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    // 4x oversampling, 2 channels, FIR polyphase (good alias rejection for fuzz)
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true);
    oversampler->initProcessing (static_cast<size_t> (samplesPerBlock));

    juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (samplesPerBlock), 2 };

    preHighPassL.reset(); preHighPassR.reset();
    postLowShelfL.reset(); postLowShelfR.reset();
    postPresenceL.reset(); postPresenceR.reset();

    smoothedGain.reset (sampleRate, 0.02);
    smoothedBias.reset (sampleRate, 0.02);
    smoothedMix.reset (sampleRate, 0.02);
    smoothedOutput.reset (sampleRate, 0.02);

    updateFilters();
    juce::ignoreUnused (spec);
}

void FantomFuzzAudioProcessor::releaseResources()
{
    if (oversampler != nullptr)
        oversampler->reset();
}

void FantomFuzzAudioProcessor::updateFilters()
{
    // Pre-fuzz high-pass ~80 Hz: stops low end from swamping the clipper
    auto hp = juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, 80.0f, 0.707f);
    *preHighPassL.coefficients = *hp;
    *preHighPassR.coefficients = *hp;

    // Post low shelf: gentle cut around 300 Hz to remove "mud" the clipper adds
    auto lowShelf = juce::dsp::IIR::Coefficients<float>::makeLowShelf (currentSampleRate, 300.0f, 0.707f, 0.7f);
    *postLowShelfL.coefficients = *lowShelf;
    *postLowShelfR.coefficients = *lowShelf;

    // Post presence bump ~3 kHz: gives the Fantom-style "cut through" edge.
    // Tone parameter shifts the gain of this band +/- a few dB.
    float toneParam = apvts.getRawParameterValue (TONE_ID)->load();
    float presenceGainDb = 3.0f + toneParam * 4.0f; // range roughly -1..+7 dB
    auto presence = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
        currentSampleRate, 3000.0f, 1.0f, juce::Decibels::decibelsToGain (presenceGainDb));
    *postPresenceL.coefficients = *presence;
    *postPresenceR.coefficients = *presence;
}

bool FantomFuzzAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo();
}

//==============================================================================
void FantomFuzzAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    // Pull current parameter values
    smoothedGain.setTargetValue (apvts.getRawParameterValue (GAIN_ID)->load());
    smoothedBias.setTargetValue (apvts.getRawParameterValue (BIAS_ID)->load());
    smoothedMix.setTargetValue (apvts.getRawParameterValue (MIX_ID)->load());
    smoothedOutput.setTargetValue (
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue (OUTPUT_ID)->load()));

    updateFilters(); // cheap enough to call per block; tone can move live

    // Keep a dry copy for the mix stage
    juce::AudioBuffer<float> dryBuffer;
    dryBuffer.makeCopyOf (buffer);

    // --- Pre EQ (high-pass) ---
    {
        auto* left  = buffer.getWritePointer (0);
        auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            left[i] = preHighPassL.processSample (left[i]);
            if (right != nullptr)
                right[i] = preHighPassR.processSample (right[i]);
        }
    }

    // --- Oversample + waveshape + downsample ---
    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::AudioBlock<float> osBlock = oversampler->processSamplesUp (block);

    for (size_t ch = 0; ch < osBlock.getNumChannels(); ++ch)
    {
        auto* data = osBlock.getChannelPointer (ch);
        for (size_t i = 0; i < osBlock.getNumSamples(); ++i)
        {
            float g = smoothedGain.getNextValue();
            float b = smoothedBias.getNextValue();
            data[i] = shape (data[i], g, b);
        }
    }

    oversampler->processSamplesDown (block);

    // --- Post EQ (low shelf + presence) ---
    {
        auto* left  = buffer.getWritePointer (0);
        auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            float l = postLowShelfL.processSample (left[i]);
            l = postPresenceL.processSample (l);
            left[i] = l;

            if (right != nullptr)
            {
                float r = postLowShelfR.processSample (right[i]);
                r = postPresenceR.processSample (r);
                right[i] = r;
            }
        }
    }

    // --- Dry/Wet mix + output trim ---
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* wet = buffer.getWritePointer (ch);
        auto* dry = dryBuffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float mix = smoothedMix.getNextValue();
            float outGain = smoothedOutput.getNextValue();
            wet[i] = (wet[i] * mix + dry[i] * (1.0f - mix)) * outGain;
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* FantomFuzzAudioProcessor::createEditor()
{
    return new FantomFuzzAudioProcessorEditor (*this);
}

//==============================================================================
void FantomFuzzAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void FantomFuzzAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FantomFuzzAudioProcessor();
}
