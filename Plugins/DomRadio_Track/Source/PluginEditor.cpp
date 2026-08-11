#include "PluginProcessor.h"
#include "PluginEditor.h"

DomRadioTrackAudioProcessorEditor::DomRadioTrackAudioProcessorEditor(DomRadioTrackAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p),
      satIndicator(p), vuMeter(p), eqGraph(p)
{
    setLookAndFeel(&customLookAndFeel);

    setSize(920, 385);

    addAndMakeVisible(oversamplingCombo);
    oversamplingCombo.addItemList({"1x", "2x", "4x"}, 1);
    oversamplingAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "OVERSAMPLING", oversamplingCombo);

    addAndMakeVisible(inGainKnob);
    addAndMakeVisible(driveTypeCombo);
    driveTypeCombo.addItemList({"Silicon", "Germanium"}, 1);
    driveTypeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "DRIVE_TYPE", driveTypeCombo);

    addAndMakeVisible(inLinkButton);
    inLinkButton.setClickingTogglesState(true);
    inLinkButton.onClick = [this] { handleLinkButton(); };

    addAndMakeVisible(preDriveKnob);
    addAndMakeVisible(tapeDriveKnob);
    addAndMakeVisible(slewKnob);
    addAndMakeVisible(satIndicator);
    addAndMakeVisible(vuMeter);

    addAndMakeVisible(tapeSpeedKnob);
    addAndMakeVisible(eqStdCombo);
    eqStdCombo.addItemList({"CCIR", "NAB"}, 1);
    eqStdAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "EQ_STD", eqStdCombo);

    addAndMakeVisible(tapeModelCombo);
    tapeModelCombo.addItemList({"SVEMA A4409", "ORWO TYP 106", "SCOTCH 2500", "BASF SPR 50"}, 1);
    tapeModelAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "TAPE_MODEL", tapeModelCombo);

    addAndMakeVisible(airKnob);
    addAndMakeVisible(biasKnob);
    addAndMakeVisible(biasSagKnob);

    addAndMakeVisible(wowKnob);
    addAndMakeVisible(flutterKnob);
    addAndMakeVisible(tapeNoiseKnob);
    addAndMakeVisible(ageKnob);

    addAndMakeVisible(mixKnob);
    addAndMakeVisible(outLvlKnob);
    addAndMakeVisible(ironCoreKnob);
    addAndMakeVisible(detailAmountKnob);
    addAndMakeVisible(detailTiltKnob);
    
    addAndMakeVisible(detailAlgoCombo);
    detailAlgoCombo.addItemList({"Wideband Tilt", "Multiband Spectral"}, 1);
    detailAlgoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "DETAIL_ALGO", detailAlgoCombo);
    
    addAndMakeVisible(outLinkButton);
    outLinkButton.setClickingTogglesState(true);
    outLinkButton.onClick = [this] { handleLinkButton(); };

    inGainKnob.slider.addListener(this);
    outLvlKnob.slider.addListener(this);

    addAndMakeVisible(eqMonitorToggleButton);
    eqMonitorToggleButton.setClickingTogglesState(true);
    eqMonitorToggleButton.onClick = [this] { eqMonitorExpanded = eqMonitorToggleButton.getToggleState(); audioProcessor.eqMonitorExpanded = eqMonitorExpanded; updateEqVisibility(); updateWindowSize(); };

    addAndMakeVisible(bassKnob);
    addAndMakeVisible(trebleKnob);
    addAndMakeVisible(bassFreqKnob);
    addAndMakeVisible(trebleFreqKnob);
    addAndMakeVisible(eqGraph);

    eqMonitorExpanded = audioProcessor.eqMonitorExpanded;
    eqMonitorToggleButton.setToggleState(eqMonitorExpanded, juce::dontSendNotification);

    updateEqVisibility();
    updateWindowSize();
}

DomRadioTrackAudioProcessorEditor::~DomRadioTrackAudioProcessorEditor() { setLookAndFeel(nullptr); }

void DomRadioTrackAudioProcessorEditor::handleLinkButton() {
    inOutLinked = !inOutLinked;
    inLinkButton.setToggleState(inOutLinked, juce::dontSendNotification);
    outLinkButton.setToggleState(inOutLinked, juce::dontSendNotification);
    if (inOutLinked)
        inOutSum = inGainKnob.slider.getValue() + outLvlKnob.slider.getValue();
}

void DomRadioTrackAudioProcessorEditor::sliderValueChanged(juce::Slider* slider)
{
    if (isUpdatingLink || ! inOutLinked) return;

    if (slider == &inGainKnob.slider)
    {
        isUpdatingLink = true;
        double targetOut = inOutSum - inGainKnob.slider.getValue();
        targetOut = juce::jlimit(outLvlKnob.slider.getMinimum(),
                                 outLvlKnob.slider.getMaximum(), targetOut);
        outLvlKnob.slider.setValue(targetOut, juce::sendNotificationSync);
        inOutSum = inGainKnob.slider.getValue() + outLvlKnob.slider.getValue();
        isUpdatingLink = false;
    }
    else if (slider == &outLvlKnob.slider)
    {
        isUpdatingLink = true;
        double targetIn = inOutSum - outLvlKnob.slider.getValue();
        targetIn = juce::jlimit(inGainKnob.slider.getMinimum(),
                                inGainKnob.slider.getMaximum(), targetIn);
        inGainKnob.slider.setValue(targetIn, juce::sendNotificationSync);
        inOutSum = inGainKnob.slider.getValue() + outLvlKnob.slider.getValue();
        isUpdatingLink = false;
    }
}

void DomRadioTrackAudioProcessorEditor::updateWindowSize() {
    // Высота: 15 (отступ) + 50 (шапка) + 10 (зазор) + 295 (панели) + 15 (нижний отступ) = 385px
    int height = 385;
    if (eqMonitorExpanded) height += 165;
    setSize(920, height);
}

void DomRadioTrackAudioProcessorEditor::updateEqVisibility() {
    const bool v = eqMonitorExpanded;
    bassKnob.setVisible(v);
    trebleKnob.setVisible(v);
    bassFreqKnob.setVisible(v);
    trebleFreqKnob.setVisible(v);
    eqGraph.setVisible(v);
}

void DomRadioTrackAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(15, 15);
    
    headerRect = bounds.removeFromTop(50);
    bounds.removeFromTop(10);
    
    auto mainPanels = bounds.removeFromTop(295);
    int panelW = (mainPanels.getWidth() - 30) / 3;
    
    p1Rect = mainPanels.removeFromLeft(panelW);
    mainPanels.removeFromLeft(15);
    p2Rect = mainPanels.removeFromLeft(panelW);
    mainPanels.removeFromLeft(15);
    p3Rect = mainPanels;
    
    inGainKnob.setBounds(p1Rect.getX() + 10, p1Rect.getY() + 35, 100, 100);
    driveTypeCombo.setBounds(p1Rect.getX() + 115, p1Rect.getY() + 42, 125, 24);
    inLinkButton.setBounds(p1Rect.getX() + 130, p1Rect.getY() + 76, 85, 22);
    preDriveKnob.setBounds(p1Rect.getX() + 19,  p1Rect.getY() + 145, 70, 52);
    tapeDriveKnob.setBounds(p1Rect.getX() + 108, p1Rect.getY() + 145, 70, 52);
    slewKnob.setBounds(p1Rect.getX() + 197,      p1Rect.getY() + 145, 70, 52);
    satIndicator.setBounds(p1Rect.getX() + 10, p1Rect.getY() + 208, 178, 36);

    tapeSpeedKnob.setBounds(p2Rect.getX() + 10, p2Rect.getY() + 35, 100, 100);
    eqStdCombo.setBounds(p2Rect.getX() + 115, p2Rect.getY() + 42, 140, 24);
    airKnob.setBounds(p2Rect.getX() + 19,   p2Rect.getY() + 145, 70, 52);
    biasKnob.setBounds(p2Rect.getX() + 108,  p2Rect.getY() + 145, 70, 52);
    biasSagKnob.setBounds(p2Rect.getX() + 197, p2Rect.getY() + 145, 70, 52);
    tapeModelCombo.setBounds(p2Rect.getX() + 15, p2Rect.getY() + 208, p2Rect.getWidth() - 30, 24);
    wowKnob.setBounds(p2Rect.getX() + 15,   p2Rect.getY() + 238, 70, 50);
    flutterKnob.setBounds(p2Rect.getX() + 95,  p2Rect.getY() + 238, 70, 50);
    tapeNoiseKnob.setBounds(p2Rect.getX() + 175, p2Rect.getY() + 238, 70, 50);
    ageKnob.setBounds(p2Rect.getX() + 205,       p2Rect.getY() + 238, 60, 50);

    // Блок 3 (Идеальная компоновка 4 ручек шириной 65px внутри панели шириной 286px)
    mixKnob.setBounds(p3Rect.getX() + 5,  p3Rect.getY() + 32, 65, 60);
    outLvlKnob.setBounds(p3Rect.getX() + 75, p3Rect.getY() + 32, 65, 60);
    detailAmountKnob.setBounds(p3Rect.getX() + 145, p3Rect.getY() + 32, 65, 60);
    detailTiltKnob.setBounds(p3Rect.getX() + 215, p3Rect.getY() + 32, 65, 60);

    // Сдвигаем элементы нижнего ряда, чтобы вписать IRON в 286px
    detailAlgoCombo.setBounds(p3Rect.getX() + 8, p3Rect.getY() + 98, 125, 22);
    outLinkButton.setBounds(p3Rect.getX() + 138, p3Rect.getY() + 99, 45, 20);
    ironCoreKnob.setBounds(p3Rect.getX() + 190, p3Rect.getY() + 90, 85, 45);

    vuMeter.setBounds(p3Rect.getX() + 15, p3Rect.getY() + 140, p3Rect.getWidth() - 30, 90);

    oversamplingCombo.setBounds(headerRect.getRight() - 80, headerRect.getY() + 13, 80, 24);
    
    // Кнопка EQ MONITOR перенесена ВНУТРЬ Панели 3 под VU-метр
    eqMonitorToggleButton.setBounds(p3Rect.getX() + 15, p3Rect.getY() + 252, p3Rect.getWidth() - 30, 24);

    bounds.removeFromTop(15);
    
    if (eqMonitorExpanded) {
        eqRect = bounds.removeFromTop(150);
        
        bassKnob.setBounds(eqRect.getX() + 10,     eqRect.getY() + 10, 70, 55);
        trebleKnob.setBounds(eqRect.getX() + 85,    eqRect.getY() + 10, 70, 55);
        bassFreqKnob.setBounds(eqRect.getX() + 10,  eqRect.getY() + 72, 70, 55);
        trebleFreqKnob.setBounds(eqRect.getX() + 85, eqRect.getY() + 72, 70, 55);
        eqGraph.setBounds(eqRect.getX() + 165, eqRect.getY() + 10, eqRect.getWidth() - 175, 128);
    }
}

void DomRadioTrackAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(227, 222, 210));

    const juce::Colour textCol = juce::Colour::fromRGB(30, 30, 30);
    const juce::Colour darkShadow = juce::Colour::fromRGB(130, 125, 115);
    const juce::Colour lightHighlight = juce::Colour::fromRGB(255, 255, 248);

    auto draw3DPanel = [&](juce::Rectangle<float> bounds) {
        g.setColour(juce::Colour::fromRGB(218, 213, 200));
        g.fillRoundedRectangle(bounds, 5.0f);

        g.setColour(darkShadow);
        g.drawRoundedRectangle(bounds, 5.0f, 1.0f);

        g.setColour(lightHighlight);
        g.drawRoundedRectangle(bounds.translated(0.5f, 1.0f), 5.0f, 0.8f);
    };

    auto drawScrew = [&](float x, float y) {
        g.setColour(juce::Colour::fromRGB(45, 42, 38));
        g.fillEllipse(x - 3.5f, y - 3.5f, 7.0f, 7.0f);
        g.setColour(juce::Colour::fromRGB(130, 125, 115));
        g.drawEllipse(x - 3.5f, y - 3.5f, 7.0f, 7.0f, 0.8f);
        g.setColour(juce::Colour::fromRGB(25, 25, 25));
        g.drawLine(x - 2.5f, y - 1.0f, x + 2.5f, y + 1.0f, 1.2f);
    };

    auto draw3DPanelWithScrews = [&](juce::Rectangle<float> bounds) {
        draw3DPanel(bounds);
        drawScrew(bounds.getX() + 8.0f, bounds.getY() + 8.0f);
        drawScrew(bounds.getRight() - 8.0f, bounds.getY() + 8.0f);
        drawScrew(bounds.getX() + 8.0f, bounds.getBottom() - 8.0f);
        drawScrew(bounds.getRight() - 8.0f, bounds.getBottom() - 8.0f);
    };

    draw3DPanelWithScrews(p1Rect.toFloat());
    draw3DPanelWithScrews(p2Rect.toFloat());
    draw3DPanelWithScrews(p3Rect.toFloat());

    g.setFont(customLookAndFeel.getHeaderFont(22.0f));
    g.setColour(textCol);
    g.drawText("DOM RADIO", juce::Rectangle<int>(headerRect.getX() + 10, headerRect.getY() + 6, 250, 22), juce::Justification::left, false);

    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.setColour(juce::Colour::fromRGB(150, 40, 40));
    g.drawText("TRACK EDITION", juce::Rectangle<int>(headerRect.getX() + 10, headerRect.getY() + 28, 250, 14), juce::Justification::left, false);

    g.setColour(textCol);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText("1. INPUT & DRIVE", juce::Rectangle<int>(p1Rect.getX() + 5, p1Rect.getY() + 16, 280, 16), juce::Justification::left, false);
    g.drawText("2. TAPE & CALIBRATION", juce::Rectangle<int>(p2Rect.getX() + 5, p2Rect.getY() + 16, 300, 16), juce::Justification::left, false);
    g.drawText("3. OUTPUT & METERING", juce::Rectangle<int>(p3Rect.getX() + 5, p3Rect.getY() + 16, 320, 16), juce::Justification::left, false);

    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    auto drawLabelAbove = [&](juce::Component& comp, const juce::String& text, int widthOffset = 20) {
        if (comp.isVisible()) {
            int x = comp.getX() - widthOffset / 2;
            int y = comp.getY() - 17;
            int w = comp.getWidth() + widthOffset;
            g.drawText(text, juce::Rectangle<int>(x, y, w, 15), juce::Justification::centred, false);
        }
    };

    drawLabelAbove(driveTypeCombo, "DRIVE TYPE");
    drawLabelAbove(eqStdCombo, "EQ STD");
    drawLabelAbove(tapeModelCombo, "TAPE MODEL");

    if (eqMonitorExpanded) {
        draw3DPanel(eqRect.toFloat());
    }
}
