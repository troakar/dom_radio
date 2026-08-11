#include "EQGraphLED.h"
#include "RetroFX.h"

EQGraphLED::EQGraphLED(DomRadioMasterAudioProcessor& p) : processor(p)
{
    startTimerHz(30);
}

float EQGraphLED::freqToX(float f) const  { return std::log10(f / 20.0f) / 3.0f * (float)getWidth(); }
float EQGraphLED::xToFreq(float x) const  { return 20.0f * std::pow(10.0f, 3.0f * x / (float)getWidth()); }
float EQGraphLED::gainToY(float dB) const { return (float)getHeight() * 0.5f - (dB / maxDb) * (float)getHeight() * 0.42f; }
float EQGraphLED::yToGain(float y) const  { return ((float)getHeight() * 0.5f - y) / ((float)getHeight() * 0.42f) * maxDb; }

void EQGraphLED::resized()
{
    persistenceBuffer = juce::Image(juce::Image::ARGB, juce::jmax(1, getWidth()),
                                    juce::jmax(1, getHeight()), true);
    persistenceBuffer.clear(persistenceBuffer.getBounds());
    rebuildBackgroundCache();
}

void EQGraphLED::rebuildBackgroundCache()
{
    backgroundCache = juce::Image(juce::Image::ARGB, getWidth(), getHeight(), true);
    juce::Graphics g(backgroundCache);
    auto b = getLocalBounds().toFloat();

    juce::ColourGradient bg(juce::Colour::fromRGB(16, 13, 8), b.getCentreX(), b.getY(),
                            juce::Colour::fromRGB(34, 24, 10), b.getCentreX(), b.getBottom(), false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(b, 5.0f);

    g.setColour(phosphor.withAlpha(0.10f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = freqToX(f);
        for (float y = 6.0f; y < b.getBottom() - 4.0f; y += 7.0f)
            g.fillEllipse(x - 0.8f, y, 1.6f, 1.6f);
    }
    for (int db = -12; db <= 12; db += 6)
    {
        const float y = gainToY((float)db);
        g.setColour(phosphor.withAlpha(db == 0 ? 0.22f : 0.10f));
        for (float x = 6.0f; x < b.getRight() - 4.0f; x += 7.0f)
            g.fillEllipse(x, y - 0.8f, 1.6f, 1.6f);
    }

    g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    g.setColour(phosphor.withAlpha(0.45f));
    for (auto& p : std::initializer_list<std::pair<float, const char*>>
         { {100.0f, "100"}, {1000.0f, "1K"}, {10000.0f, "10K"} })
    {
        const float f = p.first;
        const char* label = p.second;
        g.drawText(label, juce::Rectangle<int>((int)freqToX(f) - 14, getHeight() - 13, 28, 10),
                   juce::Justification::centred, false);
    }

    auto grunge = RetroFX::createGrungeTexture(getWidth(), getHeight(), 1984, 0.006f);
    g.drawImageAt(grunge, 0, 0);
}

juce::Path EQGraphLED::buildResponsePath(const DomRadioMasterAudioProcessor::TapeDisplayState& state) const
{
    juce::Path path;
    const int w = getWidth();
    
    const float age = *processor.apvts.getRawParameterValue("AGE") / 50.0f;
    const float scrape = *processor.apvts.getRawParameterValue("SCRAPE_FLUTTER");
    
    for (int i = 0; i < w; i += 2)
    {
        const double freq = 20.0 * std::pow(1000.0, (double)i / (double)(w - 1));
        double mag = processor.getCompositeMagnitude(freq);
        
        mag *= state.dropoutLeft;
        
        float hfRipple = 0.0f;
        if (freq > 2000.0) {
            float jitter = juce::Random::getSystemRandom().nextFloat() - 0.5f;
            float intensity = static_cast<float>(freq / 20000.0) * (scrape * 0.15f + age * 0.08f);
            hfRipple = jitter * intensity;
        }
        mag *= (1.0 + hfRipple);
        
        float dB = juce::Decibels::gainToDecibels((float)juce::jmax(1.0e-6, mag));
        
        float wobble = std::sin((float)i * 0.03f + state.wow * 15.0f) * (state.wow * 8.0f);
        wobble += std::cos((float)i * 0.15f + state.flutter * 60.0f) * (state.flutter * 4.0f);
        
        const float y = juce::jlimit(3.0f, (float)getHeight() - 3.0f, gainToY(dB) + wobble);
        
        if (i == 0) path.startNewSubPath(0.0f, y);
        else        path.lineTo((float)i, y);
    }
    return path;
}

juce::Path EQGraphLED::buildNoiseFloorPath(const DomRadioMasterAudioProcessor::TapeDisplayState& state, float noise, float hum, float age) const
{
    juce::Path path;
    const int w = getWidth();
    const float h = (float)getHeight();
    
    float noiseHeight = 3.0f + (noise * 30.0f) + (hum * 15.0f) + (age * 12.0f) + (state.effectiveTapeActivity * 18.0f);
    
    path.startNewSubPath(0.0f, h);
    
    for (int i = 0; i <= w; i += 4)
    {
        float jitter = (juce::Random::getSystemRandom().nextFloat() - 0.5f) * (noiseHeight * 0.9f);
        float humWave = std::sin((float)i * 0.12f + state.wow * 10.0f) * (hum * 14.0f);
        
        float y = h - (noiseHeight * 0.4f) + jitter + humWave;
        path.lineTo((float)i, y);
    }
    path.lineTo((float)w, h);
    path.closeSubPath();
    
    return path;
}
void EQGraphLED::paint(juce::Graphics& g)
{
    if (backgroundCache.isValid())
        g.drawImageAt(backgroundCache, 0, 0);

    auto state = processor.getTapeDisplayState();
    auto response = buildResponsePath(state);

    float satIntensity = juce::jlimit(0.0f, 1.0f, state.effectivePreampActivity * 1.3f + state.effectiveTapeActivity * 1.5f);
    float baseAlpha = 0.18f + satIntensity * 0.25f;

    if (persistenceBuffer.isValid())
    {
        // ИСПРАВЛЕНО: Вызываем затухание ДО создания juce::Graphics! 
        // Иначе Graphics лочит картинку, и multiplyAllAlphas игнорируется.
        persistenceBuffer.multiplyAllAlphas(0.92f); 

        juce::Graphics pg(persistenceBuffer);

        pg.setColour(phosphor.withAlpha(baseAlpha));
        pg.strokePath(response, juce::PathStrokeType(2.8f, juce::PathStrokeType::curved));

        pg.setColour(phosphor.withAlpha(baseAlpha * 0.6f));
        pg.strokePath(response, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved));
    }
    g.setOpacity(0.85f);
    g.drawImageAt(persistenceBuffer, 0, 0);
    g.setOpacity(1.0f);

    float glowThickness = 1.6f + satIntensity * 2.2f;
    RetroFX::drawGlowPath(g, response, phosphor, glowThickness, 4);

    {
        juce::Path fill(response);
        fill.lineTo((float)getWidth(), (float)getHeight());
        fill.lineTo(0.0f, (float)getHeight());
        fill.closeSubPath();
        
        juce::ColourGradient fg(phosphor.withAlpha(0.10f + satIntensity * 0.05f), 0.0f, gainToY(maxDb),
                                phosphor.withAlpha(0.0f),  0.0f, (float)getHeight(), false);
        g.setGradientFill(fg);
        g.fillPath(fill);
    }

    float noiseLvl = *processor.apvts.getRawParameterValue("TAPE_NOISE");
    float humLvl = *processor.apvts.getRawParameterValue("HUM");
    float ageLvl = *processor.apvts.getRawParameterValue("AGE") / 50.0f;
    
    if (noiseLvl > 0.01f || humLvl > 0.01f || ageLvl > 0.05f || satIntensity > 0.1f)
    {
        auto noisePath = buildNoiseFloorPath(state, noiseLvl, humLvl, ageLvl);
        juce::ColourGradient noiseGrad(phosphor.withAlpha(0.12f + noiseLvl * 0.15f + satIntensity * 0.1f), 
                                       0.0f, (float)getHeight() - 45.0f,
                                       phosphor.withAlpha(0.0f),  
                                       0.0f, (float)getHeight(), false);
        g.setGradientFill(noiseGrad);
        g.fillPath(noisePath);
    }

    auto& apvts = processor.apvts;
    auto drawNode = [&](const char* fID, const char* gID, const char* label, int idx)
    {
        juce::Point<float> pos { freqToX(*apvts.getRawParameterValue(fID)),
                                 gainToY(*apvts.getRawParameterValue(gID)) };

        const float intensity = (draggingNode == idx) ? 1.0f : 0.75f;
        RetroFX::drawGlowDot(g, pos, phosphor, 4.5f, intensity);

        g.setColour(phosphor.withAlpha(0.85f));
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText(label, juce::Rectangle<int>((int)pos.x - 15, (int)pos.y + 10, 30, 10),
                   juce::Justification::centred, false);
    };

    drawNode("BASS_FREQ",   "BASS",   "LF", 2);
    drawNode("TREBLE_FREQ", "TREBLE", "HF", 3);

    auto b = getLocalBounds().toFloat();
    RetroFX::drawScanlines(g, b, 0.05f, 3.0f);
    RetroFX::drawVignette(g, b, 0.38f);

    juce::ColourGradient glass(juce::Colours::white.withAlpha(0.08f),
                               b.getCentreX(), b.getY(),
                               juce::Colours::white.withAlpha(0.0f),
                               b.getCentreX(), b.getY() + b.getHeight() * 0.35f,
                               false);
    g.setGradientFill(glass);
    g.fillRoundedRectangle(b.getX() + 2.0f, b.getY() + 2.0f, b.getWidth() - 4.0f,
                           b.getHeight() * 0.35f, 4.0f);

    g.setColour(juce::Colour::fromRGB(55, 50, 44));
    g.drawRoundedRectangle(b.reduced(0.5f), 5.0f, 1.5f);
}

void EQGraphLED::mouseDown(const juce::MouseEvent& e)
{
    auto& apvts = processor.apvts;
    auto hit = [&](const char* fID, const char* gID) {
        return e.position.getDistanceFrom({ freqToX(*apvts.getRawParameterValue(fID)),
                                            gainToY(*apvts.getRawParameterValue(gID)) }) < 15.0f;
    };
    draggingNode = hit("BASS_FREQ", "BASS") ? 2 : hit("TREBLE_FREQ", "TREBLE") ? 3 : -1;
}

void EQGraphLED::mouseDrag(const juce::MouseEvent& e)
{
    if (draggingNode < 0) return;
    const float newFreq = xToFreq(juce::jlimit(0.0f, (float)getWidth(), e.position.x));
    const float newGain = juce::jlimit(-18.0f, 18.0f, yToGain(e.position.y));

    auto apply = [&](const char* fID, const char* gID, float minF, float maxF) {
        auto* fp = processor.apvts.getParameter(fID);
        auto* gp = processor.apvts.getParameter(gID);
        fp->setValueNotifyingHost(fp->convertTo0to1(juce::jlimit(minF, maxF, newFreq)));
        gp->setValueNotifyingHost(gp->convertTo0to1(newGain));
    };
    if (draggingNode == 2) apply("BASS_FREQ", "BASS", 30.0f, 300.0f);
    else                   apply("TREBLE_FREQ", "TREBLE", 1000.0f, 15000.0f);
}
