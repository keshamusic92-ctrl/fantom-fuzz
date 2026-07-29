#pragma once

#include <JuceHeader.h>

//==============================================================================
// A trivial "sound" that applies to every MIDI note/channel - required by
// juce::Synthesiser even though we only have one voice type.
class FuzzSynthSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

//==============================================================================
// A single polyphonic voice: saw-tooth oscillator + ADSR envelope.
// The fuzz waveshaping itself happens later, on the full mixed output,
// inside the processor (so all voices get fuzzed together, like a real
// analog fuzz pedal sitting after the synth engine).
class FuzzSynthVoice : public juce::SynthesiserVoice
{
public:
    bool canPlaySound (juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<FuzzSynthSound*> (sound) != nullptr;
    }

    void startNote (int midiNoteNumber, float velocity,
                     juce::SynthesiserSound*, int /*currentPitchWheelPosition*/) override
    {
        currentFrequency = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);
        phase = 0.0;
        level = velocity;

        adsr.setSampleRate (getSampleRate());
        adsr.setParameters (adsrParams);
        adsr.noteOn();
    }

    void stopNote (float /*velocity*/, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            adsr.noteOff();
        }
        else
        {
            clearCurrentNote();
            adsr.reset();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override
    {
        if (! adsr.isActive() && currentFrequency <= 0.0)
            return;

        const double sampleRate = getSampleRate();
        const double phaseIncrement = currentFrequency / sampleRate;

        for (int i = 0; i < numSamples; ++i)
        {
            // Naive band-unlimited saw-tooth: fine here because the processor
            // oversamples 4x before the fuzz stage, which tames most aliasing.
            float sample = (float) (2.0 * phase - 1.0) * level;
            sample *= adsr.getNextSample();

            for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
                outputBuffer.addSample (ch, startSample + i, sample);

            phase += phaseIncrement;
            if (phase >= 1.0)
                phase -= 1.0;
        }

        if (! adsr.isActive())
            clearCurrentNote();
    }

private:
    double currentFrequency = 0.0;
    double phase = 0.0;
    float level = 0.0f;

    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParams { 0.01f, 0.15f, 0.8f, 0.25f }; // attack, decay, sustain, release
};

//==============================================================================
// Roland Fantom-style Fuzz Synth
// MIDI note -> saw-tooth voices (polyphonic) -> mixed down -> fuzz chain:
// HPF (pre) -> 4x oversample -> asymmetric waveshaper (bias) -> downsample
// -> post EQ (low cut mud, presence bump) -> Dry/Wet mix -> Output
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

    bool acceptsMidi() const override  { return true; }
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
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static constexpr auto GAIN_ID   = "gain";
    static constexpr auto TONE_ID   = "tone";
    static constexpr auto BIAS_ID   = "bias";
    static constexpr auto MIX_ID    = "mix";
    static constexpr auto OUTPUT_ID = "output";

private:
    //==============================================================================
    juce::Synthesiser synth;
    static constexpr int numVoices = 8;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    juce::dsp::IIR::Filter<float> preHighPassL, preHighPassR;
    juce::dsp::IIR::Filter<float> postLowShelfL, postLowShelfR;
    juce::dsp::IIR::Filter<float> postPresenceL, postPresenceR;

    double currentSampleRate = 44100.0;

    juce::LinearSmoothedValue<float> smoothedGain, smoothedBias, smoothedMix, smoothedOutput;

    void updateFilters();

    static forcedinline float shape (float x, float gain, float bias)
    {
        const float biased = x + bias;

        float y;
        if (biased >= 0.0f)
            y = std::tanh (gain * biased);
        else
            y = std::tanh (gain * 0.8f * biased);

        return y;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FantomFuzzAudioProcessor)
};
