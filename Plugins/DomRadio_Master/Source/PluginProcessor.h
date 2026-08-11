#pragma once
#include <JuceHeader.h>
#include "MakhachkalaDSP.h"
#include "TapesDSP.h"
#include "ToleranceModel.h"
#include "../Shared/DetailExtractorDSP.h"

class DomRadioMasterAudioProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    struct TapeDisplayState
    {
        float wow = 0.0f;
        float flutter = 0.0f;
        float dropoutLeft = 1.0f;
        float dropoutRight = 1.0f;
        float highLossLeftDb = 0.0f;
        float highLossRightDb = 0.0f;
        float channelDifference = 0.0f;
        float archiveMotion = 0.0f;
        float effectiveTapeActivity = 0.0f;
        float effectivePreampActivity = 0.0f;
    };

    DomRadioMasterAudioProcessor();
    ~DomRadioMasterAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    float getMeterLevelLeft() const noexcept { return meterLevelLeft.load(); }
    float getMeterLevelRight() const noexcept { return meterLevelRight.load(); }
    float getInputSaturationLevel() const noexcept { return inputSaturationLevel.load(); }
    float getTapeSaturationLevel() const noexcept { return tapeSaturationLevel.load(); }
    float getSlewSaturationLevel() const noexcept { return slewSaturationLevel.load(); }
    float getDetailActivity() const noexcept { return displayDetailActivity.load(); }
    double getTotalProcessedTime() const noexcept { return totalProcessedSeconds; }
    
    TapeDisplayState getTapeDisplayState() const noexcept
    {
        TapeDisplayState state;
        state.wow = displayWow.load(std::memory_order_relaxed);
        state.flutter = displayFlutter.load(std::memory_order_relaxed);
        state.dropoutLeft = displayDropoutLeft.load(std::memory_order_relaxed);
        state.dropoutRight = displayDropoutRight.load(std::memory_order_relaxed);
        state.highLossLeftDb = displayHighLossLeftDb.load(std::memory_order_relaxed);
        state.highLossRightDb = displayHighLossRightDb.load(std::memory_order_relaxed);
        state.channelDifference = displayChannelDifference.load(std::memory_order_relaxed);
        state.archiveMotion = displayArchiveMotion.load(std::memory_order_relaxed);
        state.effectiveTapeActivity = displayEffectiveTapeActivity.load(std::memory_order_relaxed);
        state.effectivePreampActivity = displayEffectivePreampActivity.load(std::memory_order_relaxed);
        return state;
    }
    
    TroakarDSP::ToleranceModel& getToleranceModel() noexcept { return toleranceModel; }
    const TroakarDSP::ToleranceModel& getToleranceModel() const noexcept { return toleranceModel; }
    double getCompositeMagnitude(double frequency) const noexcept;
    double getEmphasisMagnitude(double frequency) const noexcept;
    double getProcessingLatencySamples() const noexcept;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "DOM RADIO MASTER"; }
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
    void resetCacheVariables();
    void resetProcessingState();
    void applyTmtToAllModules();

    void timerCallback() override;

    struct WetChainDSP
    {
        TroakarDSP::EmphasisTone emphasis;
        TroakarDSP::InputTransformer trans;
        TroakarDSP::PlatinumSlew slew;
        TroakarDSP::Spiral2Core spiral;
        TroakarDSP::DynamicBiasSag biasSag;
        TroakarDSP::MagneticTapeCore tapeCore;
        TroakarDSP::TapeProfileProcessor tapeProfile;
        TroakarDSP::ScrapeFlutter scrape;
        TroakarDSP::DCBlocker dcBlock;
        TroakarDSP::TapeEqualizer eq;
        TroakarDSP::PultecTone pultec;
        TroakarDSP::ArchiveWearHF archiveWear;
        TroakarDSP::DetailExtractor detailExtractor;
        bool prepared = false;

        void prepare(double sampleRate, int maxBlockSize) {
            trans.prepare(sampleRate, maxBlockSize);
            emphasis.prepare(sampleRate);
            slew.prepare(sampleRate);
            spiral.prepare();
            biasSag.prepare(sampleRate);
            tapeCore.prepare(sampleRate);
            tapeProfile.prepare(sampleRate, maxBlockSize);
            scrape.prepare(sampleRate, maxBlockSize);
            dcBlock.prepare(sampleRate);
            eq.prepare(sampleRate, maxBlockSize);
            pultec.prepare(sampleRate);
            archiveWear.prepare(sampleRate);
            detailExtractor.prepare(sampleRate);
            prepared = true;
        }

        void setTolerances(const TroakarDSP::ToleranceModel::ComponentTolerances* t) {
            trans.setTolerances(t);
            spiral.setTolerances(t);
            tapeCore.setTolerances(t);
            eq.setTolerances(t);
        }

        void reset() {
            trans.reset();
            biasSag.reset();
            tapeCore.reset();
            tapeProfile.reset();
            scrape.reset();
            dcBlock.reset();
            eq.reset();
            archiveWear.reset();
            detailExtractor.reset();
        }
    };

    WetChainDSP wetChainL;
    WetChainDSP wetChainR;
    
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int activeOversamplingIndex = -1;

    TroakarDSP::TransformerClipper clipL, clipR;
    TroakarDSP::TapeMechanics mechL, mechR;
    TroakarDSP::OxideDropouts dropouts;
    TroakarDSP::WowFlutterGenerator wowGenL, wowGenR;
    TroakarDSP::ToleranceModel toleranceModel;
    TroakarDSP::MainsHum humGen;
    TroakarDSP::StereoCrosstalk crosstalk;
    TroakarDSP::DCBlocker finalDcBlockL, finalDcBlockR;
    TroakarDSP::ArchivalGrainPlayer velvetGrainL, velvetGrainR;
    TroakarDSP::ArchivalGrainPlayer vinylGrainL, vinylGrainR;
    TroakarDSP::ContactNoise contactL, contactR;
    TroakarDSP::FastBiquad noiseDecayFilterL, noiseDecayFilterR;
    
    float vuEnvL = 0.0f;
    float vuEnvR = 0.0f;
    float meterAttackCoeff = 0.0f;
    float meterReleaseCoeff = 0.0f;

    juce::AudioBuffer<float> osWorkBuffer;
    
    std::atomic<float> meterLevelLeft { 0.0f }, meterLevelRight { 0.0f };
    std::atomic<float> inputSaturationLevel { 0.0f }, tapeSaturationLevel { 0.0f };
    std::atomic<float> slewSaturationLevel { 0.0f };
    std::atomic<float> displayWow { 0.0f }, displayFlutter { 0.0f };
    std::atomic<float> displayDropoutLeft { 1.0f }, displayDropoutRight { 1.0f };
    std::atomic<float> displayHighLossLeftDb { 0.0f }, displayHighLossRightDb { 0.0f };
    std::atomic<float> displayChannelDifference { 0.0f }, displayArchiveMotion { 0.0f };
    std::atomic<float> displayEffectiveTapeActivity { 0.0f };
    std::atomic<float> displayEffectivePreampActivity { 0.0f };
    std::atomic<float> displayDetailActivity { 0.0f };
    std::atomic<bool> isEqUpdating { false };
    std::atomic<bool> requiresOversamplingChange { false };

    float lastTapeSpeed = -999.0f; 
    float lastMixAmount = -1.0f;
    int lastEqStd = -1, lastTapeModel = -1;
    int lastTmtMode = -1;
    float lastAirGain = -999.0f, lastDecay = -999.0f, lastAge = -999.0f;
    float lastBass = -999.0f, lastTreble = -999.0f;
    float lastBassFrequency = -999.0f, lastTrebleFrequency = -999.0f;
    float lastPreBass = -999.0f, lastPreTreble = -999.0f;
    float lastPreBassFreq = -999.0f, lastPreTrebleFreq = -999.0f;

    juce::SmoothedValue<float> inputGainSmoothed, driveSmoothed, tapeDriveSmoothed, transientSmoothed;
    juce::SmoothedValue<float> tapeSpeedSmoothed, biasSmoothed, airSmoothed, decaySmoothed;
    juce::SmoothedValue<float> bassSmoothed, trebleSmoothed, mixSmoothed, ageSmoothed, humSmoothed, tapeNoiseSmoothed;
    juce::SmoothedValue<float> bassFrequencySmoothed, trebleFrequencySmoothed;
    juce::SmoothedValue<float> oxideSmoothed, azimuthSmoothed, outputGainSmoothed;
    juce::SmoothedValue<float> wowAmountSmoothed, flutterAmountSmoothed, biasSagSmoothed;
    juce::SmoothedValue<float> ironCoreSmoothed, scrapeFlutterSmoothed, crosstalkSmoothed;
    juce::SmoothedValue<float> temperatureSmoothed;
    juce::SmoothedValue<float> preBassSmoothed, preTrebleSmoothed, preBassFreqSmoothed, preTrebleFreqSmoothed;
    
    double currentSampleRate = 44100.0;
    int preparedBlockSize = 0;
    double totalProcessedSeconds = 0.0;
    float inputSatEnvelope = 0.0f;
    float tapeSatEnvelope = 0.0f;
    float slewSatEnvelope = 0.0f;

    std::atomic<float>* inGainParam = nullptr;
    std::atomic<float>* driveTypeParam = nullptr;
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* tapeDriveParam = nullptr;
    std::atomic<float>* transientParam = nullptr;
    std::atomic<float>* wowAmountParam = nullptr;
    std::atomic<float>* flutterAmountParam = nullptr;
    std::atomic<float>* tapeSpeedParam = nullptr;
    std::atomic<float>* tapeModelParam = nullptr;
    std::atomic<float>* eqStdParam = nullptr;
    std::atomic<float>* airParam = nullptr;
    std::atomic<float>* biasParam = nullptr;
    std::atomic<float>* biasSagParam = nullptr;
    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* ironCoreParam = nullptr;
    std::atomic<float>* scrapeFlutterParam = nullptr;
    std::atomic<float>* crosstalkParam = nullptr;
    std::atomic<float>* bassParam = nullptr;
    std::atomic<float>* trebleParam = nullptr;
    std::atomic<float>* bassFreqParam = nullptr;
    std::atomic<float>* trebleFreqParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* ageParam = nullptr;
    std::atomic<float>* noiseModeParam = nullptr;
    std::atomic<float>* humParam = nullptr;
    std::atomic<float>* tapeNoiseParam = nullptr;
    std::atomic<float>* oxideParam = nullptr;
    std::atomic<float>* azimuthParam = nullptr;
    std::atomic<float>* outLvlParam = nullptr;
    std::atomic<float>* onlineOsParam = nullptr;
    std::atomic<float>* offlineOsParam = nullptr;
    std::atomic<float>* tmtModeParam = nullptr;
    std::atomic<float>* temperatureParam = nullptr;
    std::atomic<float>* preBassParam = nullptr;
    std::atomic<float>* preTrebleParam = nullptr;
    std::atomic<float>* preBassFreqParam = nullptr;
    std::atomic<float>* preTrebleFreqParam = nullptr;
    std::atomic<float>* detailAmountParam = nullptr;
    std::atomic<float>* detailTiltParam = nullptr;
    std::atomic<float>* detailAlgoParam = nullptr;

public:
    bool eqMonitorExpanded = false;
    bool archiveExpanded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DomRadioMasterAudioProcessor)
};