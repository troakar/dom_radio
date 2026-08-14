#pragma once
#include <JuceHeader.h>
#include <array>
#include "SpectralEngine.h"
#include "UI/GradientBandModel.h"

constexpr int NUM_TARGET_BANDS = 8;

class TroakarSpectralAudioProcessor : public juce::AudioProcessor,
                                      public juce::AudioProcessorValueTreeState::Listener,
                                      public juce::AsyncUpdater
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

    int getCurrentFFTSize() const noexcept { return visualFFTSize.load(std::memory_order_acquire); }

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void parameterChanged (const juce::String& parameterID, float newValue) override;

    bool syncGradientPointsFromAPVTS(); 
    void syncGradientPointsToAPVTS();
    void handleAsyncUpdate() override;

    juce::AudioProcessorValueTreeState apvts;

    std::array<GradientPoint, 4> audioThreadGradients;
    juce::LinearSmoothedValue<float> smoothedMix { 1.0f };

    static constexpr int MAX_FFT_BINS = 2048;
    std::atomic<float> spectrumDataLeft[MAX_FFT_BINS];
    std::atomic<float> compressionDeltaData[MAX_FFT_BINS];
    std::atomic<float> sidechainData[MAX_FFT_BINS];

    float prevInGain = 1.0f;
    float prevOutGain = 1.0f;

    float prevUpMax = -999.0f;
    float prevDownMax = -999.0f;
    float prevAmount = -999.0f;
    float prevSpeed = -999.0f;
    float prevSmooth = -999.0f;
    float prevUpSel = -999.0f;
    float prevDownSel = -999.0f;

    float prevAttackMs = -999.0f;
    float prevReleaseMs = -999.0f;
    float prevSpeedAuto = -999.0f;
    float prevKneeWidth = -999.0f;
    float prevLookaheadMs = -999.0f;

    GradientPointManager gradientManager;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    SpectralEngine spectralEngine;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 8192 };
    juce::AudioBuffer<float> delayedDryBuffer; 

    int currentFFTSize = 512;
    int prevFFTMode = -1;

    std::atomic<int> visualFFTSize { 512 };
    std::atomic<bool> requiresLatencyUpdate { false };

    static constexpr int MAX_BLOCK_SIZE = 16384; 
    
    std::atomic<float>* pInGain = nullptr;
    std::atomic<float>* pOutLvl = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pAmount = nullptr;
    std::atomic<float>* pThresh = nullptr;
    std::atomic<float>* pUpRange = nullptr;
    std::atomic<float>* pDownRange = nullptr;
    std::atomic<float>* pSpeed = nullptr;
    std::atomic<float>* pSmooth = nullptr;
    std::atomic<float>* pUpSel = nullptr;
    std::atomic<float>* pDownSel = nullptr;
    std::atomic<float>* pFftMode = nullptr;
    std::atomic<float>* pDeltaMode = nullptr;
    std::atomic<float>* pViewRange = nullptr;

    std::atomic<float>* pGradEnable[4];
    std::atomic<float>* pGradFreq[4];
    std::atomic<float>* pGradGain[4];
    std::atomic<float>* pGradBw[4];
    std::atomic<float>* pGradAmt[4];
    std::atomic<float>* pGradUpMax[4];
    std::atomic<float>* pGradDownMax[4];
    std::atomic<float>* pGradSpeed[4];
    std::atomic<float>* pGradSmooth[4];
    std::atomic<float>* pGradUpSel[4];
    std::atomic<float>* pGradDownSel[4];
    std::atomic<float>* pGradAutoSpeed[4];
    std::atomic<float>* pGradAttack[4];
    std::atomic<float>* pGradRelease[4];
    std::atomic<float>* pGradKnee[4];

    std::atomic<float>* pBandEnable[8];
    std::atomic<float>* pBandFreq[8];
    std::atomic<float>* pBandGain[8];
    std::atomic<float>* pBandQ[8];

    std::atomic<float>* pSpeedAuto = nullptr;
    std::atomic<float>* pAttackMs = nullptr;
    std::atomic<float>* pReleaseMs = nullptr;
    std::atomic<float>* pKneeWidth = nullptr;
    std::atomic<float>* pLookaheadMs = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TroakarSpectralAudioProcessor)
};
