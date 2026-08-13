#pragma once
#include <JuceHeader.h>
#include "SpectralEngine.h"
#include "UI/GradientBandModel.h"

constexpr int NUM_TARGET_BANDS = 8;

class TroakarSpectralAudioProcessor : public juce::AudioProcessor
{
public:
    TroakarSpectralAudioProcessor();
    ~TroakarSpectralAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TROAKAR SPECTRAL"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float> spectrumDataLeft[FFT_SIZE / 2] { 0.0f };
    std::atomic<float> compressionDeltaData[FFT_SIZE / 2] { 0.0f };

    float prevInGain = 1.0f;
    float prevOutGain = 1.0f;

    GradientBandManager gradientManager;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    SpectralEngine spectralEngine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TroakarSpectralAudioProcessor)
};
