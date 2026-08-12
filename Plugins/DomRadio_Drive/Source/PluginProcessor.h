#pragma once
#include <JuceHeader.h>
#include "MakhachkalaDSP.h"
#include "ToleranceModel.h"

class DomRadioDriveAudioProcessor : public juce::AudioProcessor
{
public:
    DomRadioDriveAudioProcessor();
    ~DomRadioDriveAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Dom Radio Drive"; }

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

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::AudioBuffer<float> osWorkBuffer;

    TroakarDSP::InputTransformer transL, transR;
    TroakarDSP::Spiral2Core spiralL, spiralR;
    TroakarDSP::DCBlocker dcBlockL, dcBlockR;

    TroakarDSP::ToleranceModel toleranceModel;

    std::atomic<float>* inGainParam = nullptr;
    std::atomic<float>* driveTypeParam = nullptr;
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* ironCoreParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* outLvlParam = nullptr;

    juce::SmoothedValue<float> inGainSmoothed;
    juce::SmoothedValue<float> driveSmoothed;
    juce::SmoothedValue<float> ironCoreSmoothed;
    juce::SmoothedValue<float> mixSmoothed;
    juce::SmoothedValue<float> outLvlSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DomRadioDriveAudioProcessor)
};