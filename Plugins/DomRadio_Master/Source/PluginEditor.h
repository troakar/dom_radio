#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/DomRadioLookAndFeel.h"
#include "UI/LabeledKnob.h"
#include "UI/VUMeterPro.h"
#include "UI/EQGraphLED.h"
#include "UI/SaturationIndicator.h"

class DomRadioMasterAudioProcessorEditor : public juce::AudioProcessorEditor,
                                           public juce::Slider::Listener
{
public:
    DomRadioMasterAudioProcessorEditor (DomRadioMasterAudioProcessor&);
    ~DomRadioMasterAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void sliderValueChanged (juce::Slider* slider) override;
    void updateWindowSize();
    void updateArchiveVisibility();
    void updateEqVisibility();
    void handleLinkButton();

    DomRadioMasterAudioProcessor& audioProcessor;
    DomRadioLookAndFeel customLookAndFeel;

    juce::ComboBox tmtModeCombo, oversamplingCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tmtModeAtt, oversamplingAtt;

    LabeledKnob inGainKnob { audioProcessor.apvts, "IN_GAIN", "IN GAIN" };
    juce::ComboBox driveTypeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> driveTypeAtt;
    juce::ToggleButton inLinkButton { "LINK" };

    LabeledKnob preDriveKnob { audioProcessor.apvts, "DRIVE", "PRE DRIVE" };
    LabeledKnob tapeDriveKnob { audioProcessor.apvts, "TAPE_DRIVE", "TAPE DRIVE" };
    LabeledKnob slewKnob { audioProcessor.apvts, "TRANSIENT", "SLEW" };
    SaturationIndicator satIndicator;

    LabeledKnob tapeSpeedKnob { audioProcessor.apvts, "TAPE_SPEED", "TAPE SPEED" };
    juce::ComboBox eqStdCombo, tapeModelCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> eqStdAtt, tapeModelAtt;

    LabeledKnob airKnob { audioProcessor.apvts, "AIR", "AIR" };
    LabeledKnob biasKnob { audioProcessor.apvts, "BIAS", "BIAS" };
    LabeledKnob decayKnob { audioProcessor.apvts, "DECAY", "DECAY" };

    LabeledKnob mixKnob { audioProcessor.apvts, "MIX", "SMART MIX" };
    LabeledKnob outLvlKnob { audioProcessor.apvts, "OUT_LVL", "OUTPUT" };
    LabeledKnob ironCoreKnob { audioProcessor.apvts, "IRON_CORE", "IRON" };
    LabeledKnob detailAmountKnob { audioProcessor.apvts, "DETAIL_AMOUNT", "DETAILS" };
    LabeledKnob detailTiltKnob { audioProcessor.apvts, "DETAIL_TILT", "TILT" };
    juce::ComboBox detailAlgoCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> detailAlgoAtt;
    juce::ToggleButton outLinkButton { "LINK" };
    VUMeterPro vuMeter;

    juce::ToggleButton archiveToggleButton { "ARCHIVE '84 UNIT" };
    bool archiveExpanded = false;

    LabeledKnob ageKnob { audioProcessor.apvts, "AGE", "AGE" };
    LabeledKnob oxideKnob { audioProcessor.apvts, "OXIDE", "OXIDE" };
    LabeledKnob azimuthKnob { audioProcessor.apvts, "AZIMUTH", "AZIMUTH" };
    LabeledKnob biasSagKnob { audioProcessor.apvts, "BIAS_SAG", "BIAS SAG" };
    LabeledKnob scrapeKnob { audioProcessor.apvts, "SCRAPE_FLUTTER", "SCRAPE" };
    LabeledKnob crosstalkKnob { audioProcessor.apvts, "CROSSTALK", "X-TALK" };
    LabeledKnob wowKnob { audioProcessor.apvts, "WOW_AMOUNT", "WOW" };
    LabeledKnob flutterKnob { audioProcessor.apvts, "FLUTTER_AMOUNT", "FLUTTER" };
    LabeledKnob tapeNoiseKnob { audioProcessor.apvts, "TAPE_NOISE", "NOISE" };
    LabeledKnob humKnob { audioProcessor.apvts, "HUM", "HUM" };

    juce::Slider temperatureSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> temperatureAtt;
    juce::ComboBox noiseModeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> noiseModeAtt;

    juce::ToggleButton eqMonitorToggleButton { "EQ MONITOR" };
    bool eqMonitorExpanded = false;

    LabeledKnob bassKnob { audioProcessor.apvts, "BASS", "BASS GAIN" };
    LabeledKnob trebleKnob { audioProcessor.apvts, "TREBLE", "TREB GAIN" };
    LabeledKnob bassFreqKnob { audioProcessor.apvts, "BASS_FREQ", "BASS FREQ" };
    LabeledKnob trebleFreqKnob { audioProcessor.apvts, "TREBLE_FREQ", "TREB FREQ" };
    EQGraphLED eqGraph;

    bool inOutLinked = false;
    bool isUpdatingLink = false;
    double inOutSum = 0.0;

    juce::Rectangle<int> headerRect, p1Rect, p2Rect, p3Rect, eqRect, archiveRect;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DomRadioMasterAudioProcessorEditor)
};
