#include "PluginProcessor.h"
#include "PluginEditor.h"

DomRadioMasterAudioProcessorEditor::DomRadioMasterAudioProcessorEditor(DomRadioMasterAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p),
      satIndicator(p), vuMeter(p), eqGraph(p)
{
    setLookAndFeel(&customLookAndFeel);

    setSize(960, 640);

    addAndMakeVisible(tmtModeCombo);
    tmtModeCombo.addItemList({"Calibrated (OFF)", "Typical", "Loose", "Vintage"}, 1);
    tmtModeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "TMT_MODE", tmtModeCombo);

    addAndMakeVisible(oversamplingCombo);
    oversamplingCombo.addItemList({"1x (Off)", "2x", "4x", "8x"}, 1);
    oversamplingAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "ONLINE_OS", oversamplingCombo);

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
    tapeModelCombo.addItemList({"SVEMA A4409", "ORWO TYP 106", "SCOTCH 2500 HAEG", "BASF SPR 50 LHL"}, 1);
    tapeModelAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "TAPE_MODEL", tapeModelCombo);

    addAndMakeVisible(airKnob);
    addAndMakeVisible(biasKnob);
    addAndMakeVisible(decayKnob);

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

    addAndMakeVisible(archiveToggleButton);
    archiveToggleButton.setClickingTogglesState(true);
    archiveToggleButton.onClick = [this] { archiveExpanded = archiveToggleButton.getToggleState(); audioProcessor.archiveExpanded = archiveExpanded; updateArchiveVisibility(); updateWindowSize(); };

    addAndMakeVisible(temperatureSlider);
    temperatureSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    temperatureSlider.setPopupDisplayEnabled(true, true, nullptr);
    temperatureAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "TEMPERATURE", temperatureSlider);

    addAndMakeVisible(ageKnob);
    addAndMakeVisible(oxideKnob);
    addAndMakeVisible(azimuthKnob);
    addAndMakeVisible(biasSagKnob);
    addAndMakeVisible(scrapeKnob);
    addAndMakeVisible(crosstalkKnob);
    addAndMakeVisible(wowKnob);
    addAndMakeVisible(flutterKnob);
    addAndMakeVisible(tapeNoiseKnob);
    addAndMakeVisible(humKnob);

    addAndMakeVisible(noiseModeCombo);
    noiseModeCombo.addItemList({"Off", "Static", "Dynamic"}, 1);
    noiseModeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "NOISE_MODE", noiseModeCombo);

    addAndMakeVisible(eqMonitorToggleButton);
    eqMonitorToggleButton.setClickingTogglesState(true);
    eqMonitorToggleButton.onClick = [this] { eqMonitorExpanded = eqMonitorToggleButton.getToggleState(); audioProcessor.eqMonitorExpanded = eqMonitorExpanded; updateEqVisibility(); updateWindowSize(); };

    addAndMakeVisible(bassKnob);
    addAndMakeVisible(trebleKnob);
    addAndMakeVisible(bassFreqKnob);
    addAndMakeVisible(trebleFreqKnob);
    addAndMakeVisible(eqGraph);

    eqMonitorExpanded = audioProcessor.eqMonitorExpanded;
    archiveExpanded = audioProcessor.archiveExpanded;
    eqMonitorToggleButton.setToggleState(eqMonitorExpanded, juce::dontSendNotification);
    archiveToggleButton.setToggleState(archiveExpanded, juce::dontSendNotification);

    updateArchiveVisibility();
    updateEqVisibility();
    updateWindowSize();
}

DomRadioMasterAudioProcessorEditor::~DomRadioMasterAudioProcessorEditor() { setLookAndFeel(nullptr); }

void DomRadioMasterAudioProcessorEditor::handleLinkButton() {
    inOutLinked = !inOutLinked;
    inLinkButton.setToggleState(inOutLinked, juce::dontSendNotification);
    outLinkButton.setToggleState(inOutLinked, juce::dontSendNotification);
    if (inOutLinked)
        inOutSum = inGainKnob.slider.getValue() + outLvlKnob.slider.getValue();
}

void DomRadioMasterAudioProcessorEditor::sliderValueChanged(juce::Slider* slider)
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

void DomRadioMasterAudioProcessorEditor::updateWindowSize() {
    // 15 (верх) + 50 (шапка) + 10 (зазор) + 295 (верхние панели) + 10 (зазор) + 24 (полоса кнопок) + 15 (низ) = 419px
    int height = 419;
    if (eqMonitorExpanded) height += 155 + 12;
    if (archiveExpanded)   height += 145 + 12;
    setSize(960, height);
}

void DomRadioMasterAudioProcessorEditor::updateArchiveVisibility() {
    const bool v = archiveExpanded;
    for (auto* comp : {&ageKnob, &oxideKnob, &azimuthKnob, &biasSagKnob, &scrapeKnob,
                       &crosstalkKnob, &wowKnob, &flutterKnob, &tapeNoiseKnob, &humKnob})
        comp->setVisible(v);
    noiseModeCombo.setVisible(v);
    temperatureSlider.setVisible(v);
}

void DomRadioMasterAudioProcessorEditor::updateEqVisibility() {
    const bool v = eqMonitorExpanded;
    bassKnob.setVisible(v);
    trebleKnob.setVisible(v);
    bassFreqKnob.setVisible(v);
    trebleFreqKnob.setVisible(v);
    eqGraph.setVisible(v);
}

void DomRadioMasterAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(15, 15);
    
    headerRect = bounds.removeFromTop(50);
    bounds.removeFromTop(10);
    
    // 1. Верхние 3 панели увеличены до 295px (как в Track)
    auto mainPanels = bounds.removeFromTop(295);
    int panelW = (mainPanels.getWidth() - 30) / 3;
    
    p1Rect = mainPanels.removeFromLeft(panelW);
    mainPanels.removeFromLeft(15);
    p2Rect = mainPanels.removeFromLeft(panelW);
    mainPanels.removeFromLeft(15);
    p3Rect = mainPanels;
    
    // --- БЛОК 1: INPUT & DRIVE ---
    inGainKnob.setBounds(p1Rect.getX() + 10, p1Rect.getY() + 35, 100, 100);
    driveTypeCombo.setBounds(p1Rect.getX() + 125, p1Rect.getY() + 45, 140, 24);
    inLinkButton.setBounds(p1Rect.getX() + 145, p1Rect.getY() + 80, 100, 22);
    preDriveKnob.setBounds(p1Rect.getX() + 19,  p1Rect.getY() + 145, 70, 52);
    tapeDriveKnob.setBounds(p1Rect.getX() + 108, p1Rect.getY() + 145, 70, 52);
    slewKnob.setBounds(p1Rect.getX() + 197,      p1Rect.getY() + 145, 70, 52);
    satIndicator.setBounds(p1Rect.getX() + 10,  p1Rect.getY() + 208, 178, 36);

    // --- БЛОК 2: TAPE & CALIBRATION ---
    tapeSpeedKnob.setBounds(p2Rect.getX() + 10, p2Rect.getY() + 35, 100, 100);
    eqStdCombo.setBounds(p2Rect.getX() + 125, p2Rect.getY() + 45, 140, 24);
    airKnob.setBounds(p2Rect.getX() + 19,   p2Rect.getY() + 145, 70, 52);
    biasKnob.setBounds(p2Rect.getX() + 108,  p2Rect.getY() + 145, 70, 52);
    decayKnob.setBounds(p2Rect.getX() + 197, p2Rect.getY() + 145, 70, 52);
    tapeModelCombo.setBounds(p2Rect.getX() + 19, p2Rect.getY() + 206, 248, 24);

    // --- БЛОК 3: OUTPUT & METERING ---
    mixKnob.setBounds(p3Rect.getX() + 10,  p3Rect.getY() + 32, 62, 60);
    outLvlKnob.setBounds(p3Rect.getX() + 77, p3Rect.getY() + 32, 62, 60);
    detailAmountKnob.setBounds(p3Rect.getX() + 144, p3Rect.getY() + 32, 62, 60);
    detailTiltKnob.setBounds(p3Rect.getX() + 211, p3Rect.getY() + 32, 62, 60);
    detailAlgoCombo.setBounds(p3Rect.getX() + 10, p3Rect.getY() + 98, 120, 22);
    outLinkButton.setBounds(p3Rect.getX() + 135, p3Rect.getY() + 98, 50, 22);
    ironCoreKnob.setBounds(p3Rect.getX() + 192, p3Rect.getY() + 90, 82, 45); 
    vuMeter.setBounds(p3Rect.getX() + 15, p3Rect.getY() + 140, p3Rect.getWidth() - 30, 92); 

    // Элементы хедера
    oversamplingCombo.setBounds(headerRect.getRight() - 220, headerRect.getY() + 13, 95, 24); 
    tmtModeCombo.setBounds(headerRect.getRight() - 115, headerRect.getY() + 13, 105, 24); 

    // 2. Выделенный зазор под кнопками переключения
    bounds.removeFromTop(10);
    auto buttonRow = bounds.removeFromTop(24);
    archiveToggleButton.setBounds(p1Rect.getX(), buttonRow.getY(), 145, 24);
    eqMonitorToggleButton.setBounds(p3Rect.getRight() - 145, buttonRow.getY(), 145, 24);

    // 3. Выпадающие панели со сдвигом вниз
    if (eqMonitorExpanded) {
        bounds.removeFromTop(12);
        eqRect = bounds.removeFromTop(155);
        
        bassKnob.setBounds(eqRect.getX() + 15, eqRect.getY() + 15, 75, 62);
        trebleKnob.setBounds(eqRect.getX() + 95, eqRect.getY() + 15, 75, 62);
        bassFreqKnob.setBounds(eqRect.getX() + 15, eqRect.getY() + 82, 75, 62);
        trebleFreqKnob.setBounds(eqRect.getX() + 95, eqRect.getY() + 82, 75, 62);
        eqGraph.setBounds(eqRect.getX() + 180, eqRect.getY() + 15, eqRect.getWidth() - 195, 125);
    }
    
    if (archiveExpanded) {
        bounds.removeFromTop(12);
        archiveRect = bounds.removeFromTop(145);
        
        const int kY = archiveRect.getY() + 15;
        const int kW = 62; const int kH = 65; const int kStep = 75;

        ageKnob.setBounds(archiveRect.getX() + 10 + 0 * kStep, kY, kW, kH);
        oxideKnob.setBounds(archiveRect.getX() + 10 + 1 * kStep, kY, kW, kH);
        azimuthKnob.setBounds(archiveRect.getX() + 10 + 2 * kStep, kY, kW, kH);
        biasSagKnob.setBounds(archiveRect.getX() + 10 + 3 * kStep, kY, kW, kH);
        scrapeKnob.setBounds(archiveRect.getX() + 10 + 4 * kStep, kY, kW, kH);
        crosstalkKnob.setBounds(archiveRect.getX() + 10 + 5 * kStep, kY, kW, kH);
        wowKnob.setBounds(archiveRect.getX() + 10 + 6 * kStep, kY, kW, kH);
        flutterKnob.setBounds(archiveRect.getX() + 10 + 7 * kStep, kY, kW, kH);
        tapeNoiseKnob.setBounds(archiveRect.getX() + 10 + 8 * kStep, kY, kW, kH);
        humKnob.setBounds(archiveRect.getX() + 10 + 9 * kStep, kY, kW, kH);
        noiseModeCombo.setBounds(archiveRect.getX() + 10 + 10 * kStep, kY + 20, 105, 24);
        temperatureSlider.setBounds(archiveRect.getX() + 15, archiveRect.getY() + 100, archiveRect.getWidth() - 30, 24);
    }
}

void DomRadioMasterAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Фоновая текстура бежевого пластика СССР
    g.fillAll(juce::Colour::fromRGB(225, 220, 208));

    const juce::Colour textCol = juce::Colour::fromRGB(25, 25, 25);
    const juce::Colour darkShadow = juce::Colour::fromRGB(125, 120, 110);
    const juce::Colour lightHighlight = juce::Colour::fromRGB(255, 255, 248);

    auto draw3DPanel = [&](juce::Rectangle<float> bounds) {
        g.setColour(juce::Colour::fromRGB(216, 211, 198));
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

    // =========================================================================
    // ВЕРХНЯЯ ШИРОКАЯ ПАНЕЛЬ-ШАССИ (HEADER PLATE)
    // =========================================================================
    g.setColour(juce::Colour::fromRGB(38, 36, 32));
    g.fillRoundedRectangle(headerRect.toFloat(), 4.0f);
    g.setColour(darkShadow);
    g.drawRoundedRectangle(headerRect.toFloat(), 4.0f, 1.0f);

    auto badgeBounds = juce::Rectangle<float>(headerRect.getX() + 10.0f, headerRect.getY() + 12.0f, 78.0f, 26.0f);
    g.setColour(juce::Colour::fromRGB(165, 30, 30));
    g.fillRoundedRectangle(badgeBounds, 3.0f);
    g.setColour(juce::Colour::fromRGB(220, 180, 60));
    g.drawRoundedRectangle(badgeBounds, 3.0f, 1.2f);
    
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.setColour(juce::Colour::fromRGB(255, 240, 200));
    g.drawText(juce::CharPointer_UTF8("\xe2\x98\x85 \xd0\xa1\xd0\xa1\xd0\xa1\xd0\xa0"), badgeBounds.toNearestInt(), juce::Justification::centred, false);

    g.setFont(customLookAndFeel.getHeaderFont(24.0f));
    g.setColour(juce::Colour::fromRGB(245, 240, 225));
    g.drawText("D O M   R A D I O", juce::Rectangle<int>(headerRect.getX() + 100, headerRect.getY() + 8, 300, 24), juce::Justification::left, false);

    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.setColour(juce::Colour::fromRGB(190, 185, 170));
    g.drawText(juce::CharPointer_UTF8("\xd0\x9c\xd0\xb0\xd0\xb5\xd1\x81\xd1\x82\xd1\x80\xd0\xbe-\xd0\xbc\xd0\xb0\xd0\xb3\xd0\xbd\xd0\xb8\xd1\x82\xd0\xbe\xd1\x84\xd0\xbe\xd0\xbd \xe2\x80\xa2 \xd0\x9c\xd0\xb0\xd1\x85\xd0\xb0\xd1\x87\xd0\xba\xd0\xb0\xd0\xbb\xd0\xb0 1978 \xe2\x80\xa2 \xd0\x93\xd0\x9e\xd0\xa1\xd0\xa2 20838-75"), juce::Rectangle<int>(headerRect.getX() + 101, headerRect.getY() + 30, 450, 14), juce::Justification::left, false);

    drawScrew(headerRect.getX() + 6.0f, headerRect.getCentreY());
    drawScrew(headerRect.getRight() - 6.0f, headerRect.getCentreY());

    draw3DPanelWithScrews(p1Rect.toFloat());
    draw3DPanelWithScrews(p2Rect.toFloat());
    draw3DPanelWithScrews(p3Rect.toFloat());

    g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    g.setColour(textCol);
    g.drawText("1. INPUT & DRIVE", juce::Rectangle<int>(p1Rect.getX() + 5, p1Rect.getY() + 16, 280, 16), juce::Justification::left, false);
    g.drawText("2. TAPE & CALIBRATION", juce::Rectangle<int>(p2Rect.getX() + 5, p2Rect.getY() + 16, 300, 16), juce::Justification::left, false);
    g.drawText("3. OUTPUT & METERING", juce::Rectangle<int>(p3Rect.getX() + 5, p3Rect.getY() + 16, 320, 16), juce::Justification::left, false);

    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    auto drawLabelAbove = [&](juce::Component& comp, const juce::String& text, int widthOffset = 20) {
        if (comp.isVisible()) {
            int x = comp.getX() - widthOffset / 2;
            int y = comp.getY() - 16;
            int w = comp.getWidth() + widthOffset;
            g.drawText(text, juce::Rectangle<int>(x, y, w, 14), juce::Justification::centred, false);
        }
    };
    drawLabelAbove(driveTypeCombo, "DRIVE TYPE");
    drawLabelAbove(eqStdCombo, "EQ STD");
    drawLabelAbove(tapeModelCombo, "TAPE MODEL");
    drawLabelAbove(noiseModeCombo, "NOISE MODE");
    drawLabelAbove(temperatureSlider, "HEAD TEMPERATURE (\u00B0C)");

    if (eqMonitorExpanded) draw3DPanel(eqRect.toFloat());
    if (archiveExpanded) draw3DPanel(archiveRect.toFloat());
}
