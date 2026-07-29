#pragma once

#include <JuceHeader.h>

//==============================================================================
// Roland Fantom-style Fuzz/Distortion VST
// Signal chain: HPF (pre) -> Oversample -> Asymmetric waveshaper (with bias) ->
//               Downsample -> Post EQ (low cut mud, presence bump) -> Dry/Wet mix
//==============================================================================
class FantomFuzzAudioProcessor : public juce::AudioProcessor
{
public:
    FantomFuzzAudioProcessor();
    ~FantomFuzzAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==============================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Parameter layout, shared with the editor
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Parameter IDs
    static constexpr auto GAIN_ID   = "gain";
    static constexpr auto TONE_ID   = "tone";
    static constexpr auto BIAS_ID   = "bias";
    static constexpr auto MIX_ID    = "mix";
    static constexpr auto OUTPUT_ID = "output";

private:
    //==============================================================================
    // Oversampling: 4x, FIR-based (linear phase-ish, low aliasing)
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    // Pre-fuzz high-pass (removes mud before clipping)
    juce::dsp::IIR::Filter<float> preHighPassL, preHighPassR;

    // Post-fuzz EQ: low shelf cut + presence bump
    juce::dsp::IIR::Filter<float> postLowShelfL, postLowShelfR;
    juce::dsp::IIR::Filter<float> postPresenceL, postPresenceR;

    double currentSampleRate = 44100.0;

    // Cached smoothed parameter values
    juce::LinearSmoothedValue<float> smoothedGain, smoothedBias, smoothedMix, smoothedOutput;

    void updateFilters();

    // Asymmetric waveshaper core - the "fuzz" itself
    static forcedinline float shape (float x, float gain, float bias)
    {
        // Bias shifts the waveform before clipping -> asymmetric harmonic content
        const float biased = x + bias;

        float y;
        if (biased >= 0.0f)
            y = std::tanh (gain * biased);
        else
            y = std::tanh (gain * 0.8f * biased); // slightly less gain on negative half

        return y - std::tanh (gain * bias) * 0.0f; // (reserved for future DC-removal tweak)
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FantomFuzzAudioProcessor)
};
