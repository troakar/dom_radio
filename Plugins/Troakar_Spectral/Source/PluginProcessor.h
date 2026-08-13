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
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

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

    int getCurrentFFTSize() const { return currentFFTSize; }

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ИЗМЕНЕНИЕ: Теперь функция возвращает bool (true, если градиенты изменились)
    bool syncGradientPointsFromAPVTS(); 
    void syncGradientPointsToAPVTS();

    juce::AudioProcessorValueTreeState apvts;

    std::vector<GradientPoint> audioThreadGradients;
    juce::LinearSmoothedValue<float> smoothedMix { 1.0f };

    static constexpr int MAX_FFT_BINS = 1024;
    std::atomic<float> spectrumDataLeft[MAX_FFT_BINS] { 0.0f };
    std::atomic<float> compressionDeltaData[MAX_FFT_BINS] { 0.0f };

    float prevInGain = 1.0f;
    float prevOutGain = 1.0f;

    // ДОБАВЛЕНО: Кэш глобальных параметров для ленивых вычислений
    float prevUpMax = -999.0f;
    float prevDownMax = -999.0f;
    float prevAmount = -999.0f;
    float prevSpeed = -999.0f;
    float prevSmooth = -999.0f;

    GradientPointManager gradientManager;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    SpectralEngine spectralEngine;

    // ДОБАВЛЕНО: Буфер для выравнивания фазы чистого сигнала (Dry/Wet)
    juce::AudioBuffer<float> dryDelayBuffer;
    int dryDelayIndex = 0;

    int currentFFTSize = 512;
    int prevFFTMode = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TroakarSpectralAudioProcessor)
};
