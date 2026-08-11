#include "PluginProcessor.h"
#include "PluginEditor.h"

#if JUCE_DEBUG
inline void LOG_DEBUG(const juce::String& text)
{
    auto logDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    auto logFile = logDir.getChildFile("makhachkala_debug.txt");
    logFile.appendText(text + "\n");
}
#else
inline void LOG_DEBUG(const juce::String&) {}
#endif

DomRadioMasterAudioProcessor::DomRadioMasterAudioProcessor()
     : AudioProcessor (BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    LOG_DEBUG("=== СТАРТ КОНСТРУКТОРА (CLEAN ARCHITECTURE) ===");
    inGainParam = apvts.getRawParameterValue("IN_GAIN");
    driveTypeParam = apvts.getRawParameterValue("DRIVE_TYPE");
    driveParam = apvts.getRawParameterValue("DRIVE");
    tapeDriveParam = apvts.getRawParameterValue("TAPE_DRIVE");
    transientParam = apvts.getRawParameterValue("TRANSIENT");
    wowAmountParam = apvts.getRawParameterValue("WOW_AMOUNT");
    flutterAmountParam = apvts.getRawParameterValue("FLUTTER_AMOUNT");
    tapeSpeedParam = apvts.getRawParameterValue("TAPE_SPEED");
    tapeModelParam = apvts.getRawParameterValue("TAPE_MODEL");
    eqStdParam = apvts.getRawParameterValue("EQ_STD");
    airParam = apvts.getRawParameterValue("AIR");
    biasParam = apvts.getRawParameterValue("BIAS");
    biasSagParam = apvts.getRawParameterValue("BIAS_SAG");
    decayParam = apvts.getRawParameterValue("DECAY");
    ironCoreParam = apvts.getRawParameterValue("IRON_CORE");
    scrapeFlutterParam = apvts.getRawParameterValue("SCRAPE_FLUTTER");
    crosstalkParam = apvts.getRawParameterValue("CROSSTALK");
    bassParam = apvts.getRawParameterValue("BASS");
    trebleParam = apvts.getRawParameterValue("TREBLE");
    bassFreqParam = apvts.getRawParameterValue("BASS_FREQ");
    trebleFreqParam = apvts.getRawParameterValue("TREBLE_FREQ");
    mixParam = apvts.getRawParameterValue("MIX");
    ageParam = apvts.getRawParameterValue("AGE");
    noiseModeParam = apvts.getRawParameterValue("NOISE_MODE");
    humParam = apvts.getRawParameterValue("HUM");
    tapeNoiseParam = apvts.getRawParameterValue("TAPE_NOISE");
    oxideParam = apvts.getRawParameterValue("OXIDE");
    azimuthParam = apvts.getRawParameterValue("AZIMUTH");
    outLvlParam = apvts.getRawParameterValue("OUT_LVL");
    onlineOsParam = apvts.getRawParameterValue("ONLINE_OS");
    offlineOsParam = apvts.getRawParameterValue("OFFLINE_OS");
    tmtModeParam = apvts.getRawParameterValue("TMT_MODE");
    temperatureParam = apvts.getRawParameterValue("TEMPERATURE");
    preBassParam = apvts.getRawParameterValue("PRE_BASS");
    preTrebleParam = apvts.getRawParameterValue("PRE_TREBLE");
    preBassFreqParam = apvts.getRawParameterValue("PRE_BASS_FREQ");
    preTrebleFreqParam = apvts.getRawParameterValue("PRE_TREBLE_FREQ");
    detailAmountParam = apvts.getRawParameterValue("DETAIL_AMOUNT");
    detailTiltParam = apvts.getRawParameterValue("DETAIL_TILT");
    detailAlgoParam = apvts.getRawParameterValue("DETAIL_ALGO");

    toleranceModel.setMode(TroakarDSP::ToleranceModel::UnitMode::Calibrated);

    startTimerHz(10); 
}

DomRadioMasterAudioProcessor::~DomRadioMasterAudioProcessor() {}

void DomRadioMasterAudioProcessor::timerCallback()
{
    // Безопасная, чистая смена оверсэмплинга
    if (requiresOversamplingChange.exchange(false))
    {
        suspendProcessing(true);
        prepareToPlay(currentSampleRate, preparedBlockSize);
        suspendProcessing(false);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout DomRadioMasterAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto dbFormat = [](float value, int) {
        const juce::String sign = value > 0.049f ? "+" : "";
        return sign + juce::String(value, 1) + " dB";
    };
    auto dbParse = [](const juce::String& text) {
        return text.retainCharacters("-0123456789.").getFloatValue();
    };
    auto pctFormat = [](float value, int) {
        return juce::String(static_cast<int>(std::round(value))) + " %";
    };
    auto pctParse = [](const juce::String& text) {
        return text.retainCharacters("0123456789.").getFloatValue();
    };
    auto hzFormat = [](float value, int) {
        if (value >= 1000.0f)
            return juce::String(value / 1000.0f, 2) + " kHz";
        return juce::String(value, 0) + " Hz";
    };
    auto hzParse = [](const juce::String& text) {
        const juce::String cleaned = text.retainCharacters("0123456789.kKMHz ");
        float value = cleaned.getFloatValue();
        if (cleaned.containsIgnoreCase("k"))
            value *= 1000.0f;
        return value;
    };
    auto modulationPercentFormat = [](float value, int) {
        return juce::String(static_cast<int>(std::round(value * 100.0f))) + " %";
    };
    auto modulationPercentParse = [](const juce::String& text) {
        return juce::jlimit(0.0f, 1.0f, text.retainCharacters("0123456789.").getFloatValue() / 100.0f);
    };
    using FloatAttr = juce::AudioParameterFloatAttributes;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "IN_GAIN", "Input Gain", juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f,
        FloatAttr().withStringFromValueFunction(dbFormat).withValueFromStringFunction(dbParse)));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "DRIVE_TYPE", "Drive Type", juce::StringArray { "Silicon (BC414)", "Germanium (BC182)" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DRIVE", "Drive", juce::NormalisableRange<float>(1.0f, 10.0f, 0.1f), 1.0f,
        FloatAttr().withStringFromValueFunction([](float value, int) {
            constexpr float driveMin = 1.0f;
            constexpr float driveMax = 10.0f;
            const float normalized = juce::jlimit(0.0f, 1.0f, (value - driveMin) / (driveMax - driveMin));
            const float thd = 0.3f + std::pow(normalized, 1.5f) * 4.7f;
            return juce::String(value, 1) + " (THD ~" + juce::String(thd, 1) + " %)";
        })));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "TAPE_DRIVE", "Tape Saturation", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f,
        FloatAttr().withStringFromValueFunction([](float value, int) {
            const float effectiveTapeDrive = 1.0f + 9.0f * juce::jlimit(0.0f, 1.0f, value);
            return juce::String(static_cast<int>(std::round(value * 100.0f))) + " % (x" + juce::String(effectiveTapeDrive, 2) + ")";
        }).withValueFromStringFunction(pctParse)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "WOW_AMOUNT", "Wow", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f,
        FloatAttr().withStringFromValueFunction(modulationPercentFormat).withValueFromStringFunction(modulationPercentParse)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "FLUTTER_AMOUNT", "Flutter", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f,
        FloatAttr().withStringFromValueFunction(modulationPercentFormat).withValueFromStringFunction(modulationPercentParse)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "TRANSIENT", "Transient (Slew)", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f,
        FloatAttr().withStringFromValueFunction([](float value, int) {
            if (value >= 0.99f) return juce::String("Bypass");
            const float slewRate = 0.05f + value * value * 12.0f;
            return juce::String(slewRate, 2) + " V/\u00B5s";
        })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "TAPE_SPEED", "Tape Speed",
        juce::NormalisableRange<float>(TroakarDSP::TapesDSP::minTapeSpeedIps, TroakarDSP::TapesDSP::maxTapeSpeedIps, 0.001f, 0.55f), 15.0f,
        FloatAttr().withStringFromValueFunction([](float value, int) {
            return juce::String(value, value < 10.0f ? 3 : 2) + " ips";
        }).withValueFromStringFunction([](const juce::String& text) {
            return text.retainCharacters("0123456789.").getFloatValue();
        })));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "TAPE_MODEL", "Tape Model", juce::StringArray { "SVEMA A4409", "ORWO TYP 106", "SCOTCH 2500 HAEG", "BASF SPR 50 LHL" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "EQ_STD", "EQ Standard", juce::StringArray { "CCIR (35/70 \u00B5s)", "NAB (90 \u00B5s)" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "AIR", "Air Resonance", juce::NormalisableRange<float>(0.0f, 15.0f, 0.1f), 0.0f,
        FloatAttr().withStringFromValueFunction([](float value, int) {
            return value < 0.05f ? juce::String("Off") : "+" + juce::String(value, 1) + " dB @ HF";
        }).withValueFromStringFunction(dbParse)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "BIAS", "Tape Bias", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f,
        FloatAttr().withStringFromValueFunction([](float value, int) {
            if (std::abs(value) < 0.01f) return juce::String("Nominal (240 kHz)");
            const int percent = static_cast<int>(std::round(std::abs(value) * 50.0f));
            return juce::String(value < 0.0f ? "Under -" : "Over +") + juce::String(percent) + " %";
        })));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "BIAS_SAG", "Bias Sag", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f,
        FloatAttr().withStringFromValueFunction(modulationPercentFormat).withValueFromStringFunction(modulationPercentParse)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "TEMPERATURE", "Head Temperature",
        juce::NormalisableRange<float>(15.0f, 50.0f, 0.5f), 25.0f,
        FloatAttr().withStringFromValueFunction([](float value, int) {
            const juce::String zone = value < 22.0f ? " (Cold)" : value < 33.0f ? " (Warm)" : " (Hot)";
            return juce::String(value, 1) + " \u00B0C" + zone;
        }).withValueFromStringFunction([](const juce::String& text) {
            return text.retainCharacters("-0123456789.").getFloatValue();
        })));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DECAY", "Decay (HF Loss)", juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f), 0.0f,
        FloatAttr().withStringFromValueFunction([](float value, int) {
            return value < 0.05f ? juce::String("None") : "-" + juce::String(value * 0.8f, 1) + " kHz HF";
        })));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "IRON_CORE", "Iron Core", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f,
        FloatAttr().withStringFromValueFunction(modulationPercentFormat).withValueFromStringFunction(modulationPercentParse)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "SCRAPE_FLUTTER", "Scrape Flutter", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f,
        FloatAttr().withStringFromValueFunction(modulationPercentFormat).withValueFromStringFunction(modulationPercentParse)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "CROSSTALK", "Crosstalk", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f,
        FloatAttr().withStringFromValueFunction(modulationPercentFormat).withValueFromStringFunction(modulationPercentParse)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "BASS", "Bass Shelf", juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f,
        FloatAttr().withStringFromValueFunction([dbFormat](float value, int index) {
            return dbFormat(value, index) + " @ 60 Hz";
        }).withValueFromStringFunction(dbParse)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "TREBLE", "Treble Shelf", juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f,
        FloatAttr().withStringFromValueFunction([dbFormat](float value, int index) {
            return dbFormat(value, index) + " @ 10 kHz";
        }).withValueFromStringFunction(dbParse)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "BASS_FREQ", "Bass Frequency", juce::NormalisableRange<float>(30.0f, 300.0f, 1.0f), 60.0f,
        FloatAttr().withStringFromValueFunction(hzFormat).withValueFromStringFunction(hzParse)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "TREBLE_FREQ", "Treble Frequency", juce::NormalisableRange<float>(1000.0f, 15000.0f, 10.0f), 10000.0f,
        FloatAttr().withStringFromValueFunction(hzFormat).withValueFromStringFunction(hzParse)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "PRE_BASS", "Pre-Bass (Emphasis)", juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "PRE_TREBLE", "Pre-Treble (Emphasis)", juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "PRE_BASS_FREQ", "Pre-Bass Freq", juce::NormalisableRange<float>(30.0f, 300.0f, 1.0f), 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "PRE_TREBLE_FREQ", "Pre-Treble Freq", juce::NormalisableRange<float>(1000.0f, 15000.0f, 10.0f), 5000.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DETAIL_AMOUNT", "Detail Amount", juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 0.0f,
        FloatAttr().withStringFromValueFunction(pctFormat).withValueFromStringFunction(pctParse)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DETAIL_TILT", "Detail Tilt", juce::NormalisableRange<float>(-100.0f, 100.0f, 1.0f), 0.0f,
        FloatAttr().withStringFromValueFunction([](float value, int) {
            if (std::abs(value) < 1.0f) return juce::String("Center");
            return value < 0.0f ? "Low " + juce::String(std::abs(value), 0) + "%" : "Air " + juce::String(value, 0) + "%";
        })));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "DETAIL_ALGO", "Detail Algorithm", juce::StringArray { "Wideband Tilt", "Multiband Spectral" }, 1));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MIX", "Smart Mix", juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f,
        FloatAttr().withStringFromValueFunction(pctFormat).withValueFromStringFunction(pctParse)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "AGE", "Age", juce::NormalisableRange<float>(0.0f, 50.0f, 1.0f), 0.0f,
        FloatAttr().withStringFromValueFunction([](float value, int) {
            const int years = static_cast<int>(std::round(value));
            if (years == 0) return juce::String("New Reel");
            return juce::String(years) + (years == 1 ? " year" : " years");
        })));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "NOISE_MODE", "Noise Mode", juce::StringArray { "Off", "Static", "Dynamic (Envelope)" }, 2));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "HUM", "Mains Hum", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f,
        FloatAttr().withStringFromValueFunction([](float value, int) {
            if (value < 0.01f) return juce::String("-INF dB");
            return juce::String(static_cast<int>(std::round(-90.0f + value * 27.0f))) + " dB floor";
        })));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "TAPE_NOISE", "Tape Noise", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f,
        FloatAttr().withStringFromValueFunction(modulationPercentFormat).withValueFromStringFunction(modulationPercentParse)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "OXIDE", "Oxide Dropouts", juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f), 0.0f,
        FloatAttr().withStringFromValueFunction([](float value, int) {
            if (value < 0.05f) return juce::String("Intact");
            return "~" + juce::String(static_cast<int>(std::round(value * 3.0f))) + " drops/min";
        })));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "AZIMUTH", "Azimuth Drift", juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f), 0.0f,
        FloatAttr().withStringFromValueFunction([](float value, int) {
            if (value < 0.05f) return juce::String("Aligned (0')");
            return "+/-" + juce::String(value * 1.5f, 1) + " arc min";
        })));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "OUT_LVL", "Output", juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f,
        FloatAttr().withStringFromValueFunction(dbFormat).withValueFromStringFunction(dbParse)));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "ONLINE_OS", "Online Oversampling", juce::StringArray { "1x (Off)", "2x", "4x", "8x", "16x" }, 1));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "OFFLINE_OS", "Offline Oversampling", juce::StringArray { "1x (Off)", "2x", "4x", "8x", "16x" }, 4));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "TMT_MODE", "TMT Mode", juce::StringArray { "Calibrated (OFF)", "Typical", "Loose", "Vintage", "Custom" }, 0));

    return { params.begin(), params.end() };
}

void DomRadioMasterAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    LOG_DEBUG("=== СТАРТ prepareToPlay ===");
    
    currentSampleRate = sampleRate;
    preparedBlockSize = samplesPerBlock;
    
    const int osIndex = getRequestedOversamplingIndex();
    activeOversamplingIndex = osIndex;
    
    // Единый, стабильный инстанс оверсэмплера
    oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
        2, osIndex, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false
    );
    
    const size_t maxExpectedBlock = (size_t) juce::jmax(65536, samplesPerBlock);
    oversampler->initProcessing(maxExpectedBlock);
    oversampler->reset();

    const double osRate = sampleRate * (1 << osIndex);
    const int osMaxBlock = (int) maxExpectedBlock * (1 << osIndex);
    
    osWorkBuffer.setSize(2, (int) maxExpectedBlock);

    // Подготовка одного единственного DSP тракта
    wetChainL.prepare(osRate, osMaxBlock);
    wetChainR.prepare(osRate, osMaxBlock);

    clipL.prepare(sampleRate); clipR.prepare(sampleRate);
    finalDcBlockL.prepare(sampleRate); finalDcBlockR.prepare(sampleRate);
    finalDcBlockL.reset(); finalDcBlockR.reset();
    
    meterAttackCoeff = 1.0f - std::exp(-1.0f / (0.300f * static_cast<float>(sampleRate)));
    meterReleaseCoeff = 1.0f - std::exp(-1.0f / (0.300f * static_cast<float>(sampleRate)));
    vuEnvL = 0.0f; vuEnvR = 0.0f;
    
    mechL.prepare(sampleRate, (int)maxExpectedBlock);  
    mechR.prepare(sampleRate, (int)maxExpectedBlock);
    dropL.prepare(sampleRate);  dropR.prepare(sampleRate);
    wowGenL.prepare(sampleRate); wowGenR.prepare(sampleRate);
    wowGenL.setSeed(1984); wowGenR.setSeed(4891);
    humGen.prepare(sampleRate);
    crosstalk.prepare(sampleRate, (int)maxExpectedBlock);
    
    LOG_DEBUG("Загрузка MP3 шума...");
    velvetGrainL.prepare(sampleRate, BinaryData::noise_mid_velvet_and_soft_mp3, BinaryData::noise_mid_velvet_and_soft_mp3Size);
    velvetGrainR.prepare(sampleRate, BinaryData::noise_mid_velvet_and_soft_mp3, BinaryData::noise_mid_velvet_and_soft_mp3Size);
    vinylGrainL.prepare(sampleRate, BinaryData::noise_high_vinyl_like_mp3, BinaryData::noise_high_vinyl_like_mp3Size);
    vinylGrainR.prepare(sampleRate, BinaryData::noise_high_vinyl_like_mp3, BinaryData::noise_high_vinyl_like_mp3Size);
    contactL.prepare(sampleRate); contactR.prepare(sampleRate);
    noiseDecayFilterL.prepare(sampleRate); noiseDecayFilterR.prepare(sampleRate);

    dropL.setSeed(2026); dropR.setSeed(6202);
    wetChainL.scrape.setSeed(42);
    wetChainR.scrape.setSeed(24);

    applyTmtToAllModules();

    totalProcessedSeconds = 0.0;

    // Компенсация задержки
    const int tapeMechanicsLatency = static_cast<int>(sampleRate * TroakarDSP::TapeMechanics::baseDelaySec);
    const int maxLatency = (int)oversampler->getLatencyInSamples() + tapeMechanicsLatency;
    setLatencySamples(maxLatency);

    inputGainSmoothed.reset(sampleRate, 0.010);
    driveSmoothed.reset(sampleRate, 0.020);
    tapeDriveSmoothed.reset(sampleRate, 0.020);
    tapeSpeedSmoothed.reset(sampleRate, 0.080);
    transientSmoothed.reset(sampleRate, 0.010);
    biasSmoothed.reset(sampleRate, 0.020);
    biasSagSmoothed.reset(sampleRate, 0.050);
    ironCoreSmoothed.reset(sampleRate, 0.050);
    scrapeFlutterSmoothed.reset(sampleRate, 0.050);
    crosstalkSmoothed.reset(sampleRate, 0.050);
    temperatureSmoothed.reset(sampleRate, 0.100);
    airSmoothed.reset(sampleRate, 0.020);
    decaySmoothed.reset(sampleRate, 0.050);
    bassSmoothed.reset(sampleRate, 0.020);
    trebleSmoothed.reset(sampleRate, 0.020);
    bassFrequencySmoothed.reset(sampleRate, 0.020);
    trebleFrequencySmoothed.reset(sampleRate, 0.020);
    mixSmoothed.reset(sampleRate, 0.010);
    ageSmoothed.reset(sampleRate, 0.050);
    humSmoothed.reset(sampleRate, 0.050);
    tapeNoiseSmoothed.reset(sampleRate, 0.050);
    oxideSmoothed.reset(sampleRate, 0.050);
    azimuthSmoothed.reset(sampleRate, 0.050);
    outputGainSmoothed.reset(sampleRate, 0.010);
    wowAmountSmoothed.reset(sampleRate, 0.050);
    flutterAmountSmoothed.reset(sampleRate, 0.050);
    preBassSmoothed.reset(sampleRate, 0.020);
    preTrebleSmoothed.reset(sampleRate, 0.020);
    preBassFreqSmoothed.reset(sampleRate, 0.020);
    preTrebleFreqSmoothed.reset(sampleRate, 0.020);

    inputGainSmoothed.setCurrentAndTargetValue(inGainParam ? juce::Decibels::decibelsToGain(inGainParam->load()) : 1.0f);
    driveSmoothed.setCurrentAndTargetValue(driveParam ? driveParam->load() : 1.0f);
    tapeDriveSmoothed.setCurrentAndTargetValue(tapeDriveParam ? tapeDriveParam->load() : 0.0f);
    tapeSpeedSmoothed.setCurrentAndTargetValue(tapeSpeedParam ? tapeSpeedParam->load() : 15.0f);
    transientSmoothed.setCurrentAndTargetValue(transientParam ? transientParam->load() : 1.0f);
    biasSmoothed.setCurrentAndTargetValue(biasParam ? biasParam->load() : 0.0f);
    biasSagSmoothed.setCurrentAndTargetValue(biasSagParam ? biasSagParam->load() : 0.0f);
    ironCoreSmoothed.setCurrentAndTargetValue(ironCoreParam ? ironCoreParam->load() : 0.0f);
    scrapeFlutterSmoothed.setCurrentAndTargetValue(scrapeFlutterParam ? scrapeFlutterParam->load() : 0.0f);
    crosstalkSmoothed.setCurrentAndTargetValue(crosstalkParam ? crosstalkParam->load() : 0.0f);
    airSmoothed.setCurrentAndTargetValue(airParam ? airParam->load() : 0.0f);
    decaySmoothed.setCurrentAndTargetValue(decayParam ? decayParam->load() : 0.0f);
    bassSmoothed.setCurrentAndTargetValue(bassParam ? bassParam->load() : 0.0f);
    trebleSmoothed.setCurrentAndTargetValue(trebleParam ? trebleParam->load() : 0.0f);
    bassFrequencySmoothed.setCurrentAndTargetValue(bassFreqParam ? bassFreqParam->load() : 60.0f);
    trebleFrequencySmoothed.setCurrentAndTargetValue(trebleFreqParam ? trebleFreqParam->load() : 10000.0f);
    mixSmoothed.setCurrentAndTargetValue(mixParam ? mixParam->load() / 100.0f : 1.0f);
    ageSmoothed.setCurrentAndTargetValue(ageParam ? ageParam->load() : 0.0f);
    humSmoothed.setCurrentAndTargetValue(humParam ? humParam->load() : 0.0f);
    tapeNoiseSmoothed.setCurrentAndTargetValue(tapeNoiseParam ? tapeNoiseParam->load() : 0.0f);
    oxideSmoothed.setCurrentAndTargetValue(oxideParam ? oxideParam->load() : 0.0f);
    azimuthSmoothed.setCurrentAndTargetValue(azimuthParam ? azimuthParam->load() : 0.0f);
    outputGainSmoothed.setCurrentAndTargetValue(outLvlParam ? juce::Decibels::decibelsToGain(outLvlParam->load()) : 1.0f);
    wowAmountSmoothed.setCurrentAndTargetValue(wowAmountParam ? wowAmountParam->load() : 0.25f);
    flutterAmountSmoothed.setCurrentAndTargetValue(flutterAmountParam ? flutterAmountParam->load() : 0.25f);
    temperatureSmoothed.setCurrentAndTargetValue(temperatureParam ? temperatureParam->load() : 25.0f);
    preBassSmoothed.setCurrentAndTargetValue(preBassParam ? preBassParam->load() : 0.0f);
    preTrebleSmoothed.setCurrentAndTargetValue(preTrebleParam ? preTrebleParam->load() : 0.0f);
    preBassFreqSmoothed.setCurrentAndTargetValue(preBassFreqParam ? preBassFreqParam->load() : 100.0f);
    preTrebleFreqSmoothed.setCurrentAndTargetValue(preTrebleFreqParam ? preTrebleFreqParam->load() : 5000.0f);
    
    resetProcessingState();
    LOG_DEBUG("=== prepareToPlay ЗАВЕРШЕН УСПЕШНО ===");
}

void DomRadioMasterAudioProcessor::releaseResources() {}

int DomRadioMasterAudioProcessor::getRequestedOversamplingIndex() const noexcept
{
    const auto* parameter = isNonRealtime() ? offlineOsParam : onlineOsParam;
    return parameter != nullptr ? juce::jlimit(0, 4, static_cast<int>(parameter->load())) : 1;
}

void DomRadioMasterAudioProcessor::resetProcessingState()
{
    inputSatEnvelope = 0.0f; tapeSatEnvelope = 0.0f; slewSatEnvelope = 0.0f;
    inputSaturationLevel.store(0.0f); tapeSaturationLevel.store(0.0f); slewSaturationLevel.store(0.0f);
    displayWow.store(0.0f); displayFlutter.store(0.0f);
    displayDropoutLeft.store(1.0f); displayDropoutRight.store(1.0f);
    displayHighLossLeftDb.store(0.0f); displayHighLossRightDb.store(0.0f);
    displayChannelDifference.store(0.0f); displayArchiveMotion.store(0.0f);
    displayEffectiveTapeActivity.store(0.0f); displayEffectivePreampActivity.store(0.0f);
    displayDetailActivity.store(0.0f);
    
    finalDcBlockL.reset(); finalDcBlockR.reset();
    noiseDecayFilterL.reset(); noiseDecayFilterR.reset();
    vuEnvL = 0.0f; vuEnvR = 0.0f;
    
    if (oversampler) oversampler->reset();
    wetChainL.reset();
    wetChainR.reset();
    crosstalk.reset();
}

double DomRadioMasterAudioProcessor::getProcessingLatencySamples() const noexcept
{
    double osLat = oversampler != nullptr ? oversampler->getLatencyInSamples() : 0.0;
    return osLat + currentSampleRate * TroakarDSP::TapeMechanics::baseDelaySec;
}

void DomRadioMasterAudioProcessor::applyTmtToAllModules()
{
    const auto* tolL = &toleranceModel.getTolerancesL();
    const auto* tolR = &toleranceModel.getTolerancesR();

    wetChainL.setTolerances(tolL);
    wetChainR.setTolerances(tolR);

    clipL.setTolerances(tolL);
    clipR.setTolerances(tolR);
    mechL.setTolerances(tolL);
    mechR.setTolerances(tolR);
    crosstalk.setTolerances(tolL);
    wowGenL.setTolerances(tolL);
    wowGenR.setTolerances(tolR);

    lastTapeSpeed = -999.0f; // Форсировать пересчет EQ
}

namespace {
struct ThermalState {
    float coercivityFactor    = 1.0f;
    float biasOffset          = 0.0f;
    float hfeFactor           = 1.0f;
    float speedFactor         = 1.0f;
    float dropoutMult         = 1.0f;
    float scrapeMult          = 1.0f;
    float ironHarmonicsFactor = 1.0f;
};

ThermalState computeThermal(float tempC, TroakarDSP::DriveType driveType) noexcept
{
    ThermalState s;
    const float dHot = juce::jmax(0.0f, (tempC - 25.0f) / 25.0f);
    const float dCold = juce::jmax(0.0f, (25.0f - tempC) / 10.0f);
    const float smoothHot = std::pow(dHot, 1.3f);

    s.coercivityFactor = 1.0f - smoothHot * 0.05f + dCold * 0.03f;
    s.biasOffset = -smoothHot * 0.04f + dCold * 0.01f;
    s.speedFactor = 1.0f + (smoothHot - dCold * 0.5f) * 0.0003f;
    s.hfeFactor = driveType == TroakarDSP::DriveType::germanium ? 
                  1.0f + smoothHot * 0.35f - dCold * 0.08f : 1.0f + smoothHot * 0.15f - dCold * 0.04f;
    s.ironHarmonicsFactor = 1.0f + smoothHot * 0.25f;
    s.dropoutMult = 1.0f + dCold * 0.45f;
    s.scrapeMult  = 1.0f + smoothHot * 0.45f;

    return s;
}
} // namespace

void DomRadioMasterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (oversampler == nullptr)
    {
        buffer.clear();
        return;
    }

    const int requested = getRequestedOversamplingIndex();
    if (requested != activeOversamplingIndex && !requiresOversamplingChange.load())
    {
        requiresOversamplingChange.store(true);
    }

    const int numSamples = buffer.getNumSamples();
    if (numSamples > 65536) return; 

    const float inputGainTarget = juce::Decibels::decibelsToGain(inGainParam->load());
    inputGainSmoothed.setTargetValue(inputGainTarget);

    driveSmoothed.setTargetValue(driveParam->load());
    tapeDriveSmoothed.setTargetValue(tapeDriveParam->load());
    tapeSpeedSmoothed.setTargetValue(tapeSpeedParam->load());
    transientSmoothed.setTargetValue(transientParam->load());
    biasSmoothed.setTargetValue(biasParam->load());
    biasSagSmoothed.setTargetValue(biasSagParam->load());
    ironCoreSmoothed.setTargetValue(ironCoreParam->load());
    scrapeFlutterSmoothed.setTargetValue(scrapeFlutterParam->load());
    crosstalkSmoothed.setTargetValue(crosstalkParam->load());
    airSmoothed.setTargetValue(airParam->load());
    decaySmoothed.setTargetValue(decayParam->load());
    bassSmoothed.setTargetValue(bassParam->load());
    trebleSmoothed.setTargetValue(trebleParam->load());
    bassFrequencySmoothed.setTargetValue(bassFreqParam->load());
    trebleFrequencySmoothed.setTargetValue(trebleFreqParam->load());
    mixSmoothed.setTargetValue(mixParam->load() / 100.0f);
    ageSmoothed.setTargetValue(ageParam->load());
    humSmoothed.setTargetValue(humParam->load());
    tapeNoiseSmoothed.setTargetValue(tapeNoiseParam->load());
    oxideSmoothed.setTargetValue(oxideParam->load());
    azimuthSmoothed.setTargetValue(azimuthParam->load());
    outputGainSmoothed.setTargetValue(juce::Decibels::decibelsToGain(outLvlParam->load()));
    wowAmountSmoothed.setTargetValue(wowAmountParam->load());
    flutterAmountSmoothed.setTargetValue(flutterAmountParam->load());
    preBassSmoothed.setTargetValue(preBassParam->load());
    preTrebleSmoothed.setTargetValue(preTrebleParam->load());
    preBassFreqSmoothed.setTargetValue(preBassFreqParam->load());
    preTrebleFreqSmoothed.setTargetValue(preTrebleFreqParam->load());
    temperatureSmoothed.setTargetValue(temperatureParam->load());
    
    driveSmoothed.skip(numSamples); tapeDriveSmoothed.skip(numSamples); tapeSpeedSmoothed.skip(numSamples);
    transientSmoothed.skip(numSamples); biasSmoothed.skip(numSamples); biasSagSmoothed.skip(numSamples);
    ironCoreSmoothed.skip(numSamples); scrapeFlutterSmoothed.skip(numSamples); crosstalkSmoothed.skip(numSamples);
    airSmoothed.skip(numSamples); decaySmoothed.skip(numSamples); bassSmoothed.skip(numSamples);
    trebleSmoothed.skip(numSamples); bassFrequencySmoothed.skip(numSamples); trebleFrequencySmoothed.skip(numSamples);
    ageSmoothed.skip(numSamples); humSmoothed.skip(numSamples); tapeNoiseSmoothed.skip(numSamples);
    oxideSmoothed.skip(numSamples); azimuthSmoothed.skip(numSamples); wowAmountSmoothed.skip(numSamples);
    mixSmoothed.skip(numSamples); flutterAmountSmoothed.skip(numSamples);
    preBassSmoothed.skip(numSamples); preTrebleSmoothed.skip(numSamples);
    preBassFreqSmoothed.skip(numSamples); preTrebleFreqSmoothed.skip(numSamples); temperatureSmoothed.skip(numSamples);

    totalProcessedSeconds += (double)numSamples / currentSampleRate;

    const int currentTmtMode = static_cast<int>(tmtModeParam->load());
    if (currentTmtMode != lastTmtMode)
    {
        toleranceModel.setMode(static_cast<TroakarDSP::ToleranceModel::UnitMode>(currentTmtMode));
        applyTmtToAllModules();
        lastTmtMode = currentTmtMode;
    }

    int driveType = static_cast<int>(driveTypeParam->load());
    const float requestedDrive = driveSmoothed.getCurrentValue();
    const float drive = juce::jlimit(1.0f, 10.0f, requestedDrive);

    const float preampDriveNorm = juce::jlimit(0.0f, 1.0f, (drive - 1.0f) / 9.0f);
    const float preampDriveShape = std::pow(preampDriveNorm, 1.25f);
    const float effectivePreampPressure = juce::jlimit(0.70f, 1.60f, 0.70f + preampDriveShape * 0.90f);
    const float effectivePreampDrive = juce::jlimit(1.0f, 10.0f, 1.0f + preampDriveNorm * 9.0f * effectivePreampPressure);

    const float tapeDriveRaw = juce::jlimit(0.0f, 1.0f, tapeDriveSmoothed.getCurrentValue());
    constexpr float tapeDriveStart = 0.04f;
    constexpr float maxInternalTapeDrive = 0.55f; 

    const float tapeDriveAfterThreshold = juce::jlimit(0.0f, 1.0f, (tapeDriveRaw - tapeDriveStart) / (1.0f - tapeDriveStart));
    const float tapeActivityNorm = std::pow(tapeDriveAfterThreshold, 1.65f);
    const float tapeDriveControl = tapeActivityNorm * maxInternalTapeDrive;
    const float tapeDriveNorm = tapeDriveControl;

    const float effectiveTapePressure = juce::jlimit(1.00f, 1.45f, 1.00f + tapeActivityNorm * 0.45f);
    const float tapeDrive = 1.0f + 9.0f * tapeDriveControl;
    const float effectiveTapeDrive = tapeDrive;
    juce::ignoreUnused(effectiveTapeDrive);

    const float effMix = mixSmoothed.getCurrentValue(); // ГЛАВНЫЙ СКАЛЯР (20 мс отклик)
    
    const float tapeFeedGain = 1.0f; 
    const float wowAmount = wowAmountSmoothed.getCurrentValue() * effMix;
    const float flutterAmount = flutterAmountSmoothed.getCurrentValue() * 0.4f * effMix; 
    const auto driveModel = driveType == 0 ? TroakarDSP::DriveType::silicon : TroakarDSP::DriveType::germanium;
    // Slew bypassed = 1.0
    float transient = 1.0f - ((1.0f - transientSmoothed.getCurrentValue()) * effMix);
    
    const float tapeSpeedIps = juce::jlimit(TroakarDSP::TapesDSP::minTapeSpeedIps, TroakarDSP::TapesDSP::maxTapeSpeedIps, tapeSpeedSmoothed.getCurrentValue());
    const float tapeSpeedNorm = TroakarDSP::TapesDSP::speedIpsToNorm(tapeSpeedIps) * 0.75f;
    const auto tapeFormat = TroakarDSP::TapesDSP::getFormatForSpeedIps(tapeSpeedIps);
    
    const float headroomScale = juce::Decibels::decibelsToGain(12.0f - tapeFormat.headroomDb);
    const float headroomMakeup = 1.0f / headroomScale;

    int eqStd = static_cast<int>(eqStdParam->load());
    int tapeModel = static_cast<int>(tapeModelParam->load());
    const auto tapeProfile = TroakarDSP::TapesDSP::getBalancedProfile(tapeModel);
    float airGain = airSmoothed.getCurrentValue();
    const float biasRaw = juce::jlimit(-1.0f, 1.0f, biasSmoothed.getCurrentValue());

    constexpr float maxInternalBias = 1.15f; 
    float tapeBias = 0.0f;
    if (std::abs(biasRaw) > 0.001f)
    {
        const float sign = biasRaw >= 0.0f ? 1.0f : -1.0f;
        const float normalized = std::abs(biasRaw);
        float shaped = normalized < 0.5f ? normalized * 1.5f : 0.75f + 0.25f * ((normalized - 0.5f) * 2.0f) * ((normalized - 0.5f) * 2.0f) * (3.0f - 2.0f * ((normalized - 0.5f) * 2.0f));
        tapeBias = sign * shaped * maxInternalBias;
    }

    const float temperature = temperatureSmoothed.getCurrentValue();
    const auto thermal = computeThermal(temperature, driveModel);

    auto thermalProfile = tapeProfile;
    thermalProfile.coercivity *= thermal.coercivityFactor;

    const float thermalBias         = juce::jlimit(-1.0f, 1.0f, tapeBias + thermal.biasOffset);
    const float thermalPreampDrive  = juce::jlimit(1.0f, 12.0f, effectivePreampDrive * thermal.hfeFactor);
    const float thermalSpeedNorm    = juce::jlimit(0.0f, 1.0f, tapeSpeedNorm * thermal.speedFactor);

    wetChainL.eq.updateBiasResponse(thermalBias, thermalSpeedNorm, effMix);
    wetChainR.eq.updateBiasResponse(thermalBias, thermalSpeedNorm, effMix);

    // 1. Считываем чистые значения без предварительного умножения на effMix
    const float biasSag = biasSagSmoothed.getCurrentValue();       // Убрали * effMix
    const float ironCore = ironCoreSmoothed.getCurrentValue();      // Убрали * effMix
    const float scrapeFlutter = scrapeFlutterSmoothed.getCurrentValue(); // Убрали * effMix
    const float crosstalkAmount = crosstalkSmoothed.getCurrentValue();   // Убрали * effMix
    float decay = decaySmoothed.getCurrentValue();         // Убрали * effMix

    float bassDb = bassSmoothed.getCurrentValue() * effMix;   // EQ-шельфы оставляем с масштабом
    float trebleDb = trebleSmoothed.getCurrentValue() * effMix;
    const float bassFrequency = bassFrequencySmoothed.getCurrentValue();
    const float trebleFrequency = trebleFrequencySmoothed.getCurrentValue();

    const float preBassDb = preBassSmoothed.getCurrentValue() * effMix;
    const float preTrebleDb = preTrebleSmoothed.getCurrentValue() * effMix;
    const float preBassFreq = preBassFreqSmoothed.getCurrentValue();
    const float preTrebleFreq = preTrebleFreqSmoothed.getCurrentValue();

    const float currentAgeForScrape = ageSmoothed.getCurrentValue(); // Убрали * effMix
    const float effAzimuth = azimuthSmoothed.getCurrentValue() * effMix;
    int noiseMode = static_cast<int>(noiseModeParam->load());
    const float detAmount = detailAmountParam->load() / 100.0f * effMix;
    const float detTilt = detailTiltParam->load() / 100.0f;
    const auto detAlgo = static_cast<TroakarDSP::ExtractorAlgorithm>(detailAlgoParam->load());

    displayEffectivePreampActivity.store(juce::jlimit(0.0f, 1.0f, preampDriveNorm * effectivePreampPressure * 0.55f), std::memory_order_relaxed);
    displayEffectiveTapeActivity.store(juce::jlimit(0.0f, 1.0f, tapeActivityNorm * effectiveTapePressure * 0.55f), std::memory_order_relaxed);

    if (inputGainSmoothed.isSmoothing())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = inputGainSmoothed.getNextValue();
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample(ch, i, buffer.getSample(ch, i) * g);
        }
    }
    else buffer.applyGain(inputGainSmoothed.getCurrentValue());

    const float ageNorm = juce::jlimit(0.0f, 1.0f, currentAgeForScrape / 50.0f);
    float wearAmount = ageNorm * ageNorm * (3.0f - 2.0f * ageNorm);
    float macroDropouts = (ageNorm > 0.35f) ? std::pow((ageNorm - 0.35f) / 0.65f, 2.0f) * 10.0f : 0.0f;
    float macroAzimuth = ageNorm * ageNorm * 10.0f;
    float macroCrosstalk = ageNorm + std::pow(ageNorm, 4.0f) * 2.0f;
    juce::ignoreUnused(macroAzimuth, macroCrosstalk);

    if (osWorkBuffer.getNumSamples() < numSamples) {
        osWorkBuffer.setSize(2, juce::jmax(65536, numSamples), false, false, true);
    }

    osWorkBuffer.copyFrom(0, 0, buffer.getReadPointer(0), numSamples);
    if (buffer.getNumChannels() == 1) osWorkBuffer.copyFrom(1, 0, buffer.getReadPointer(0), numSamples);
    else osWorkBuffer.copyFrom(1, 0, buffer.getReadPointer(1), numSamples);

    {
        juce::dsp::AudioBlock<float> fullBlock (osWorkBuffer);
        auto block = fullBlock.getSubBlock (0, (size_t) numSamples);
        auto osBlock = oversampler->processSamplesUp (block);

        const int osNumSamples = (int) osBlock.getNumSamples();
        auto* osL = osBlock.getChannelPointer(0);
        auto* osR = osBlock.getChannelPointer(1);

        if (tapeSpeedNorm != lastTapeSpeed || eqStd != lastEqStd || tapeModel != lastTapeModel
            || std::abs(airGain - lastAirGain) > 0.005f || std::abs(decay - lastDecay) > 0.005f
            || std::abs(currentAgeForScrape - lastAge) > 0.05f || std::abs(effMix - lastMixAmount) > 0.005f)
        {
            isEqUpdating.store(true, std::memory_order_release);
            wetChainL.eq.updateParameters(tapeSpeedNorm, eqStd, airGain, decay, currentAgeForScrape, tapeProfile, effMix);
            wetChainR.eq.updateParameters(tapeSpeedNorm, eqStd, airGain, decay, currentAgeForScrape, tapeProfile, effMix);
            wetChainL.trans.updateParameters(effMix);
            wetChainR.trans.updateParameters(effMix);
            wetChainL.tapeProfile.updateProfile(tapeProfile);
            wetChainR.tapeProfile.updateProfile(tapeProfile);
            isEqUpdating.store(false, std::memory_order_release);

            lastTapeSpeed = tapeSpeedNorm; lastEqStd = eqStd; lastTapeModel = tapeModel;
            lastAirGain = airGain; lastDecay = decay; lastAge = currentAgeForScrape; lastMixAmount = effMix;
        }

        if (std::abs(bassDb - lastBass) > 0.01f || std::abs(trebleDb - lastTreble) > 0.01f
            || std::abs(bassFrequency - lastBassFrequency) > 0.5f || std::abs(trebleFrequency - lastTrebleFrequency) > 5.0f)
        {
            wetChainL.pultec.updateCoefficients(bassDb, trebleDb, bassFrequency, trebleFrequency);
            wetChainR.pultec.updateCoefficients(bassDb, trebleDb, bassFrequency, trebleFrequency);
            lastBass = bassDb; lastTreble = trebleDb; lastBassFrequency = bassFrequency; lastTrebleFrequency = trebleFrequency;
        }

        if (std::abs(preBassDb - lastPreBass) > 0.01f || std::abs(preTrebleDb - lastPreTreble) > 0.01f
            || std::abs(preBassFreq - lastPreBassFreq) > 0.5f || std::abs(preTrebleFreq - lastPreTrebleFreq) > 5.0f)
        {
            wetChainL.emphasis.updateCoefficients(preBassDb, preTrebleDb, preBassFreq, preTrebleFreq);
            wetChainR.emphasis.updateCoefficients(preBassDb, preTrebleDb, preBassFreq, preTrebleFreq);
            lastPreBass = preBassDb; lastPreTreble = preTrebleDb; lastPreBassFreq = preBassFreq; lastPreTrebleFreq = preTrebleFreq;
        }

        const float presence  = 0.1f + (effectivePreampDrive * 0.02f);
        const float preGain = 1.0f;

        // --- BOUTIQUE EQUAL-LOUDNESS GAIN STAGING ---
        
        // 1. Нелинейная компенсация преампа (учитывает колоссальный рост RMS от tanh)
        const float driveNorm = (drive - 1.0f) / 9.0f;
        // ИСПРАВЛЕНО: Усилена компенсация (было 1.35f, стало 2.15f). 
        // Германиевый драйв чуть жирнее, поэтому для него компенсация сильнее на 10%
        const float driveTypeComp = (driveType == 1) ? 1.10f : 1.0f; 
        const float preampCompensation = 1.0f / (1.0f + std::pow(driveNorm, 0.70f) * 2.15f * driveTypeComp);

        // 2. Компенсация плотности ленты 
        // ИСПРАВЛЕНО: Усилена компенсация (было 0.25f, стало 0.65f)
        const float tapeCompensation = 1.0f / (1.0f + tapeActivityNorm * 0.65f);

        // 3. Синергетическое сдерживание громкости при одновременной раскачке обоих драйвов
        // ИСПРАВЛЕНО: Усилено взаимодействие (было 0.30f, стало 0.45f)
        const float crossCoupling = 1.0f + (driveNorm * tapeActivityNorm * 0.45f);

        const float rawPostGain = (preampCompensation * tapeCompensation) / crossCoupling;
        const float finalPostGain = 1.0f + (rawPostGain - 1.0f) * effMix;

        auto processWetChannel = [&](float input, auto& chain, float& inActivity, float& tapeActivity, float hfEnergy) -> float
        {
            float wet = input * preGain;
            wet = chain.emphasis.processPre(wet);
            const float inputBefore = wet;

            wet = chain.trans.process(wet, ironCore * thermal.ironHarmonicsFactor, effMix);
            wet = chain.slew.process(wet, transient);
            wet = chain.spiral.process(wet, thermalPreampDrive, presence, effMix);
            inActivity = std::abs(wet - inputBefore);

            if (biasSag > 0.001f)
            {
                const float sag = chain.biasSag.process(wet, biasSag);
                const float sagEffect = sag * 5.0f;
                const float ducking = 1.0f - juce::jlimit(0.0f, 0.65f, sagEffect * 0.35f);
                wet *= ducking;
                const float sputtering = sagEffect * 1.5f + (sagEffect * sagEffect * 2.5f);
                wet += wet * std::abs(wet) * sputtering * TroakarDSP::TapesDSP::harmonicDistortionTrim;
            }

            wet *= tapeFeedGain;
            wet = chain.eq.processPre(wet);
            const float tapeCoreBefore = wet;
            wet *= headroomScale;
            wet = chain.tapeCore.process(wet, tapeDriveNorm, effectiveTapePressure, thermalBias, thermalProfile);
            wet *= headroomMakeup;
            const float tapeCoreActivity = std::abs(wet - tapeCoreBefore);
            const float profileBefore = wet;
            wet = chain.tapeProfile.process(wet, tapeDriveControl, effectiveTapePressure, thermalBias, thermalSpeedNorm, effMix);
            const float profileActivity = std::abs(wet - profileBefore);
            tapeActivity = juce::jlimit(0.0f, 1.0f, tapeCoreActivity + profileActivity);
            
            wet = chain.dcBlock.process(wet);
            wet = chain.eq.processPost(wet);
            wet = chain.archiveWear.process(wet, wearAmount, hfEnergy);

            if (wearAmount > 0.1f) {
                float ceiling = 1.0f / (1.0f + wearAmount * 0.2f);
                wet = std::tanh(wet * ceiling) / ceiling;
            }

            wet = chain.emphasis.processPost(wet);
            wet = chain.pultec.process(wet);
            wet = chain.scrape.process(wet, currentAgeForScrape, scrapeFlutter * thermal.scrapeMult);
            wet = chain.detailExtractor.process(wet, detAmount, detTilt, detAlgo);

            return wet * finalPostGain;
        };

        for (int i = 0; i < osNumSamples; ++i)
        {
            float hfEnergyL = std::abs(osL[i] - (i > 0 ? osL[i-1] : 0.0f));
            float actInL = 0.0f, actTpL = 0.0f;
            osL[i] = processWetChannel(osL[i], wetChainL, actInL, actTpL, hfEnergyL);

            float hfEnergyR = std::abs(osR[i] - (i > 0 ? osR[i-1] : 0.0f));
            float actInR = 0.0f, actTpR = 0.0f;
            osR[i] = processWetChannel(osR[i], wetChainR, actInR, actTpR, hfEnergyR);
        }

        oversampler->processSamplesDown(block);
        
        displayHighLossLeftDb.store(wetChainL.tapeProfile.getCurrentHighLossDb(), std::memory_order_relaxed);
        displayHighLossRightDb.store(wetChainR.tapeProfile.getCurrentHighLossDb(), std::memory_order_relaxed);
        displayDetailActivity.store(
            juce::jlimit(0.0f, 1.0f, 
                std::max(wetChainL.detailExtractor.getCurrentDetailGainDb(), 
                         wetChainR.detailExtractor.getCurrentDetailGainDb()) / 18.0f), 
            std::memory_order_relaxed);
    }

    buffer.copyFrom(0, 0, osWorkBuffer.getReadPointer(0), numSamples);
    if (buffer.getNumChannels() > 1) buffer.copyFrom(1, 0, osWorkBuffer.getReadPointer(1), numSamples);

    auto* wetL = buffer.getWritePointer(0);
    auto* wetR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    const float tmtGainL = juce::Decibels::decibelsToGain(toleranceModel.getTolerancesL().channelImbalance);
    const float tmtGainR = juce::Decibels::decibelsToGain(toleranceModel.getTolerancesR().channelImbalance);

    // Масштабируем "грязные" артефакты от effMix один раз на блок (безопасно для фазы)
    const float effTapeNoise = tapeNoiseSmoothed.getNextValue() * effMix;
    const float effHum       = humSmoothed.getNextValue() * effMix;
    const float effOxide     = oxideSmoothed.getNextValue() * effMix;
    const float effCrosstalk = crosstalkSmoothed.getNextValue() * effMix;
    
    // Модуляция детонации считывается чисто (effMix применяется внутри модулей)
    const float effWow     = wowAmountSmoothed.getNextValue();
    const float effFlutter = flutterAmountSmoothed.getNextValue() * 0.5f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float currentOutput = outputGainSmoothed.getNextValue();
        
        const auto modulationL = wowGenL.generate(tapeSpeedNorm, effWow, effFlutter);
        const auto modulationR = wowGenR.generate(tapeSpeedNorm, effWow, effFlutter);
        displayWow.store(juce::jlimit(-1.0f, 1.0f, (modulationL.wow + modulationR.wow) * 0.5f), std::memory_order_relaxed);
        displayFlutter.store(juce::jlimit(-1.0f, 1.0f, (modulationL.flutter + modulationR.flutter) * 0.5f), std::memory_order_relaxed);
        
        const float humSample = humGen.process(effHum);

        float blendedL = wetL[i];
        float blendedR = wetR != nullptr ? wetR[i] : 0.0f;

        // ВАЖНО: TapeMechanics выполняется ВСЕГДА! 
        // 25мс задержка должна присутствовать для PDC. Глубина LFO обнуляется за счет effMix внутри.
        blendedL = mechL.process(blendedL, tapeSpeedNorm, effMix, effAzimuth, currentAgeForScrape, true, modulationL);
        if (wetR != nullptr)
            blendedR = mechR.process(blendedR, tapeSpeedNorm, effMix, effAzimuth, currentAgeForScrape, false, modulationR);

        crosstalk.process(blendedL, blendedR, effCrosstalk, currentAgeForScrape);

        blendedL = dropL.process(blendedL, (effOxide + macroDropouts) * thermal.dropoutMult, currentAgeForScrape);
        displayDropoutLeft.store(dropL.getCurrentGain(), std::memory_order_relaxed);
        if (wetR != nullptr)
        {
            blendedR = dropR.process(blendedR, (effOxide + macroDropouts) * thermal.dropoutMult, currentAgeForScrape);
            displayDropoutRight.store(dropR.getCurrentGain(), std::memory_order_relaxed);
        }
        else displayDropoutRight.store(dropL.getCurrentGain(), std::memory_order_relaxed);

        const auto modeEnum = (noiseMode == 0 ? TroakarDSP::NoiseMode::off : (noiseMode == 1 ? TroakarDSP::NoiseMode::staticNoise : TroakarDSP::NoiseMode::dynamicNoise));

        float midGrainL  = velvetGrainL.process(blendedL, modeEnum, tapeSpeedNorm, ageNorm);
        float highGrainL = vinylGrainL.process(blendedL, modeEnum, tapeSpeedNorm, ageNorm);
        float combinedGrainL = midGrainL * (1.0f - ageNorm * 0.2f) + highGrainL * (0.30f + ageNorm * 0.70f);

        float intermodDepthL = (0.08f + ageNorm * 0.20f) * (0.3f + effTapeNoise * 0.7f);
        float intermodL = 1.0f + combinedGrainL * intermodDepthL;
        blendedL *= intermodL;

        const float currentDecay = decaySmoothed.getCurrentValue();
        float noiseCutoff = 20000.0f - (ageNorm * 4000.0f) - (currentDecay * 400.0f);
        noiseCutoff = juce::jlimit(4000.0f, 20000.0f, noiseCutoff);
        
        noiseDecayFilterL.setLowPass(noiseCutoff, 0.707f);

        float noiseCurve = effTapeNoise * (0.45f + 0.55f * effTapeNoise);
        float additiveNoiseL = combinedGrainL * noiseCurve * (0.45f + ageNorm * 0.15f);
        float contactNoiseL = contactL.process(blendedL, currentAgeForScrape, effHum, modeEnum) * effTapeNoise;
        float rawNoiseSumL = (additiveNoiseL + humSample + contactNoiseL) * 0.6f;
        blendedL += noiseDecayFilterL.processSample(rawNoiseSumL);

        if (wetR != nullptr)
        {
            noiseDecayFilterR.setLowPass(noiseCutoff, 0.707f);
            
            float midGrainR  = velvetGrainR.process(blendedR, modeEnum, tapeSpeedNorm, ageNorm);
            float highGrainR = vinylGrainR.process(blendedR, modeEnum, tapeSpeedNorm, ageNorm);
            float combinedGrainR = midGrainR * (1.0f - ageNorm * 0.2f) + highGrainR * (0.30f + ageNorm * 0.70f);

            float intermodDepthR = (0.08f + ageNorm * 0.20f) * (0.3f + effTapeNoise * 0.7f);
            float intermodR = 1.0f + combinedGrainR * intermodDepthR;
            blendedR *= intermodR;

            float additiveNoiseR = combinedGrainR * noiseCurve * (0.45f + ageNorm * 0.15f);
            float contactNoiseR = contactR.process(blendedR, currentAgeForScrape, effHum, modeEnum) * effTapeNoise;
            float rawNoiseSumR = (additiveNoiseR + humSample + contactNoiseR) * 0.6f;
            blendedR += noiseDecayFilterR.processSample(rawNoiseSumR);
        }

        wetL[i] = finalDcBlockL.process(clipL.process(blendedL * currentOutput * tmtGainL, effMix));
        const float absL = std::abs(wetL[i]);
        vuEnvL += (absL > vuEnvL ? meterAttackCoeff : meterReleaseCoeff) * (absL - vuEnvL);

        if (wetR != nullptr)
        {
            wetR[i] = finalDcBlockR.process(clipR.process(blendedR * currentOutput * tmtGainR, effMix));
            const float absR = std::abs(wetR[i]);
            vuEnvR += (absR > vuEnvR ? meterAttackCoeff : meterReleaseCoeff) * (absR - vuEnvR);
        }
    }

    auto linToVU = [](float lin) {
        if (lin <= 1.0e-4f) return 0.0f;
        const float db = juce::Decibels::gainToDecibels(lin);
        return juce::jlimit(0.0f, 1.0f, (db + 48.0f) / 48.0f);
    };

    meterLevelLeft.store(linToVU(vuEnvL));
    meterLevelRight.store(linToVU(vuEnvR));
    const float dropoutDifference = std::abs(displayDropoutLeft.load(std::memory_order_relaxed) - displayDropoutRight.load(std::memory_order_relaxed));
    const float hfDifference = std::abs(displayHighLossLeftDb.load(std::memory_order_relaxed) - displayHighLossRightDb.load(std::memory_order_relaxed));
    const float channelDifference = juce::jlimit(0.0f, 1.0f, dropoutDifference + hfDifference * 0.08f);
    displayChannelDifference.store(channelDifference, std::memory_order_relaxed);
    displayArchiveMotion.store(juce::jlimit(0.0f, 1.0f, std::abs(displayWow.load(std::memory_order_relaxed)) * 0.35f + std::abs(displayFlutter.load(std::memory_order_relaxed)) * 0.25f + channelDifference), std::memory_order_relaxed);
    
    const float signalPresence = juce::jlimit(0.0f, 1.0f, (vuEnvL + vuEnvR) * 0.707f * 3.0f); 
    const float driveSatFactor = juce::jlimit(0.0f, 1.0f, (drive - 1.0f) / 5.0f);
    const float ironSatFactor  = juce::jlimit(0.0f, 1.0f, ironCore * 1.5f);
    const float slewSatFactor  = juce::jlimit(0.0f, 1.0f, (1.0f - transient) * 1.5f);
    const float targetInputSat = juce::jlimit(0.0f, 1.0f, (driveSatFactor * 0.60f + ironSatFactor * 0.35f + slewSatFactor * 0.20f) * TroakarDSP::TapesDSP::harmonicDistortionTrim * signalPresence);
    const float tapeDriveFactor = juce::jlimit(0.0f, 1.0f, tapeDriveRaw * 1.25f);
    const float biasSatFactor   = juce::jlimit(0.0f, 0.5f, std::abs(tapeBias) * 1.2f);
    const float profileSatMult  = 1.0f + (tapeProfile.oddHarmonics + tapeProfile.evenHarmonics) * 4.0f;
    const float targetTapeSat = juce::jlimit(0.0f, 1.0f, (tapeDriveFactor * 0.75f + biasSatFactor * 0.25f) * profileSatMult * TroakarDSP::TapesDSP::harmonicDistortionTrim * signalPresence);

    // Быстрая "нервная" атака на транзиенты + плавное затухание
    const float inCoeff   = (targetInputSat > inputSatEnvelope) ? 0.40f : 0.12f;
    const float tapeCoeff = (targetTapeSat > tapeSatEnvelope)   ? 0.40f : 0.12f;
    
    const float targetSlewSat = juce::jlimit(0.0f, 1.0f, slewSatFactor * signalPresence * 2.2f);
    const float slewCoeff = (targetSlewSat > slewSatEnvelope)   ? 0.50f : 0.15f;

    inputSatEnvelope += inCoeff * (targetInputSat - inputSatEnvelope);
    tapeSatEnvelope  += tapeCoeff * (targetTapeSat - tapeSatEnvelope);
    slewSatEnvelope  += slewCoeff * (targetSlewSat - slewSatEnvelope);

    inputSaturationLevel.store(inputSatEnvelope);
    tapeSaturationLevel.store(tapeSatEnvelope);
    slewSaturationLevel.store(slewSatEnvelope);
}

double DomRadioMasterAudioProcessor::getCompositeMagnitude(double frequency) const noexcept
{
    if (isEqUpdating.load(std::memory_order_acquire)) return 1.0;
    const double hpf = frequency / std::sqrt(frequency * frequency + 30.0 * 30.0);
    return hpf * wetChainL.eq.getMagnitudeForFrequency(frequency) * wetChainL.pultec.getMagnitudeForFrequency(frequency);
}

double DomRadioMasterAudioProcessor::getEmphasisMagnitude(double frequency) const noexcept
{
    if (isEqUpdating.load(std::memory_order_acquire)) return 1.0;
    return wetChainL.emphasis.getMagnitudeForFrequency(frequency);
}

void DomRadioMasterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("eqMonitorExpanded", eqMonitorExpanded, nullptr);
    state.setProperty("archiveExpanded", archiveExpanded, nullptr);
    
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void DomRadioMasterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName (apvts.state.getType()))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
        eqMonitorExpanded = apvts.state.getProperty("eqMonitorExpanded", false);
        archiveExpanded = apvts.state.getProperty("archiveExpanded", false);
    }
}

juce::AudioProcessorEditor* DomRadioMasterAudioProcessor::createEditor()
{
    return new DomRadioMasterAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DomRadioMasterAudioProcessor();
}