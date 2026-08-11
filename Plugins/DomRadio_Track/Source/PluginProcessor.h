#pragma once
#include <JuceHeader.h>
#include "MakhachkalaDSP.h"
#include "TapesDSP.h"
#include "../Shared/DetailExtractorDSP.h"

class DomRadioTrackAudioProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    struct TapeDisplayState
    {
        float wow = 0.0f;
        float flutter = 0.0f;
        float dropoutLeft = 1.0f;
        float dropoutRight = 1.0f;
        float effectiveTapeActivity = 0.0f;
        float effectivePreampActivity = 0.0f;
    };

    DomRadioTrackAudioProcessor();
    ~DomRadioTrackAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    float getMeterLevelLeft() const noexcept { return meterLevelLeft.load(); }
    float getMeterLevelRight() const noexcept { return meterLevelRight.load(); }
    float getInputSaturationLevel() const noexcept { return inputSaturationLevel.load(); }
    float getTapeSaturationLevel() const noexcept { return tapeSaturationLevel.load(); }
    TapeDisplayState getTapeDisplayState() const noexcept
    {
        TapeDisplayState state;
        state.wow = displayWow.load(std::memory_order_relaxed);
        state.flutter = displayFlutter.load(std::memory_order_relaxed);
        state.dropoutLeft = 1.0f;
        state.dropoutRight = 1.0f;
        state.effectiveTapeActivity = displayEffectiveTapeActivity.load(std::memory_order_relaxed);
        state.effectivePreampActivity = displayEffectivePreampActivity.load(std::memory_order_relaxed);
        return state;
    }
    float getDetailActivity() const noexcept { return displayDetailActivity.load(); }
    
    double getCompositeMagnitude(double frequency) const noexcept;
    double getEmphasisMagnitude(double frequency) const noexcept;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "DOM RADIO TRACK"; }
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
    int getRequestedOversamplingIndex() const noexcept;
    void resetProcessingState();
    void timerCallback() override;

    struct CompactWetChain
    {
        TroakarDSP::EmphasisTone emphasis;
        TroakarDSP::InputTransformer trans;
        TroakarDSP::PlatinumSlew slew;
        TroakarDSP::Spiral2Core spiral;
        TroakarDSP::DynamicBiasSag biasSag;
        TroakarDSP::MagneticTapeCore tapeCore;
        TroakarDSP::TapeProfileProcessor tapeProfile;
        TroakarDSP::DCBlocker dcBlock;
        TroakarDSP::TapeEqualizer eq;
        TroakarDSP::PultecTone pultec;
        TroakarDSP::DetailExtractor detailExtractor;
        
        void prepare(double sampleRate, int maxBlockSize) {
            trans.prepare(sampleRate, maxBlockSize);
            emphasis.prepare(sampleRate);
            slew.prepare(sampleRate);
            spiral.prepare();
            biasSag.prepare(sampleRate);
            tapeCore.prepare(sampleRate);
            tapeProfile.prepare(sampleRate, maxBlockSize);
            dcBlock.prepare(sampleRate);
            eq.prepare(sampleRate, maxBlockSize);
            pultec.prepare(sampleRate);
            detailExtractor.prepare(sampleRate);
        }

        void reset() {
            trans.reset();
            biasSag.reset();
            tapeCore.reset();
            tapeProfile.reset();
            dcBlock.reset();
            eq.reset();
            detailExtractor.reset();
        }
    };

    CompactWetChain wetChainL, wetChainR;
    
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int activeOversamplingIndex = -1;

    TroakarDSP::TransformerClipper clipL, clipR;
    TroakarDSP::DCBlocker finalDcBlockL, finalDcBlockR;
    TroakarDSP::FastBiquad noiseDecayFilterL, noiseDecayFilterR;
    
    float vuEnvL = 0.0f, vuEnvR = 0.0f;
    float meterAttackCoeff = 0.0f, meterReleaseCoeff = 0.0f;

    juce::AudioBuffer<float> osWorkBuffer;
    
    std::atomic<float> meterLevelLeft { 0.0f }, meterLevelRight { 0.0f };
    std::atomic<float> inputSaturationLevel { 0.0f }, tapeSaturationLevel { 0.0f };
    std::atomic<float> displayDetailActivity { 0.0f };
    std::atomic<float> displayWow { 0.0f }, displayFlutter { 0.0f };
    std::atomic<float> displayEffectiveTapeActivity { 0.0f };
    std::atomic<float> displayEffectivePreampActivity { 0.0f };
    std::atomic<bool> isEqUpdating { false };
    std::atomic<bool> requiresOversamplingChange { false };

    TroakarDSP::TapeMechanics mechL, mechR;
    TroakarDSP::WowFlutterGenerator wowGenL, wowGenR;
    TroakarDSP::ArchivalGrainPlayer velvetGrainL, velvetGrainR;
    TroakarDSP::ArchivalGrainPlayer vinylGrainL, vinylGrainR;

    float lastTapeSpeed = -999.0f;
    int lastEqStd = -1, lastTapeModel = -1;
    float lastAirGain = -999.0f;
    float lastMixAmount = -1.0f;
    float lastBass = -999.0f, lastTreble = -999.0f;
    float lastBassFrequency = -999.0f, lastTrebleFrequency = -999.0f;
    float lastPreBass = -999.0f, lastPreTreble = -999.0f;
    float lastPreBassFreq = -999.0f, lastPreTrebleFreq = -999.0f;

    juce::SmoothedValue<float> inputGainSmoothed, driveSmoothed, tapeDriveSmoothed, transientSmoothed;
    juce::SmoothedValue<float> tapeSpeedSmoothed, biasSmoothed, airSmoothed;
    juce::SmoothedValue<float> bassSmoothed, trebleSmoothed, mixSmoothed;
    juce::SmoothedValue<float> bassFrequencySmoothed, trebleFrequencySmoothed;
    juce::SmoothedValue<float> outputGainSmoothed, biasSagSmoothed, ironCoreSmoothed;
    juce::SmoothedValue<float> preBassSmoothed, preTrebleSmoothed, preBassFreqSmoothed, preTrebleFreqSmoothed;
    juce::SmoothedValue<float> wowSmoothed, flutterSmoothed, noiseSmoothed;
    
    double currentSampleRate = 44100.0;
    int preparedBlockSize = 0;
    float inputSatEnvelope = 0.0f, tapeSatEnvelope = 0.0f;

    std::atomic<float>* inGainParam = nullptr;
    std::atomic<float>* driveTypeParam = nullptr;
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* tapeDriveParam = nullptr;
    std::atomic<float>* transientParam = nullptr;
    std::atomic<float>* tapeSpeedParam = nullptr;
    std::atomic<float>* tapeModelParam = nullptr;
    std::atomic<float>* eqStdParam = nullptr;
    std::atomic<float>* airParam = nullptr;
    std::atomic<float>* biasParam = nullptr;
    std::atomic<float>* biasSagParam = nullptr;
    std::atomic<float>* ironCoreParam = nullptr;
    std::atomic<float>* bassParam = nullptr;
    std::atomic<float>* trebleParam = nullptr;
    std::atomic<float>* bassFreqParam = nullptr;
    std::atomic<float>* trebleFreqParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* outLvlParam = nullptr;
    std::atomic<float>* oversamplingParam = nullptr;
    std::atomic<float>* preBassParam = nullptr;
    std::atomic<float>* preTrebleParam = nullptr;
    std::atomic<float>* preBassFreqParam = nullptr;
    std::atomic<float>* preTrebleFreqParam = nullptr;
    std::atomic<float>* detailAmountParam = nullptr;
    std::atomic<float>* detailTiltParam = nullptr;
    std::atomic<float>* detailAlgoParam = nullptr;
    std::atomic<float>* wowParam = nullptr;
    std::atomic<float>* flutterParam = nullptr;
    std::atomic<float>* noiseParam = nullptr;

public:
    bool eqMonitorExpanded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DomRadioTrackAudioProcessor)
};
