#include "VUMeterPro.h"
#include "RetroFX.h"

VUMeterPro::VUMeterPro(DomRadioMasterAudioProcessor& p) : processor(p)
{
    startTimerHz(30);
}

float VUMeterPro::dbToAngle(float dB) const
{
    const float amp = std::pow(10.0f, dB / 20.0f);
    const float minAmp = std::pow(10.0f, minDb / 20.0f);
    const float maxAmp = std::pow(10.0f, maxDb / 20.0f);

    const float prop = juce::jlimit(0.0f, 1.0f, (amp - minAmp) / (maxAmp - minAmp));
    return -angleRange + prop * 2.0f * angleRange;
}

void VUMeterPro::timerCallback()
{
    const float lin = juce::jmax(processor.getMeterLevelLeft(), processor.getMeterLevelRight());
    const float targetDb = juce::jlimit(minDb - 3.0f, maxDb + 2.0f,
                                        juce::Decibels::gainToDecibels(lin, minDb - 3.0f));

    const float coeff = (targetDb > needleDb) ? 0.28f : 0.12f;
    needleDb += (targetDb - needleDb) * coeff;

    if (targetDb >= peakDb) { peakDb = targetDb; peakHoldFrames = 45; }
    else if (--peakHoldFrames <= 0) peakDb = juce::jmax(targetDb, peakDb - 0.4f);

    peakLampGlow = (targetDb > 0.0f) ? 1.0f : peakLampGlow * 0.88f;

    repaint();
}

void VUMeterPro::rebuildDialCache()
{
    dialCache = juce::Image(juce::Image::ARGB, getWidth(), getHeight(), true);
    juce::Graphics g(dialCache);
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);

    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(bounds.translated(2.0f, 3.0f), 6.0f);

    juce::ColourGradient bezel(juce::Colour::fromRGB(85, 82, 75), bounds.getX(), bounds.getY(),
                               juce::Colour::fromRGB(20, 20, 20), bounds.getRight(), bounds.getBottom(), false);
    g.setGradientFill(bezel);
    g.fillRoundedRectangle(bounds, 6.0f);

    auto inner = bounds.reduced(4.0f);

    juce::ColourGradient dial(juce::Colour::fromRGB(252, 205, 80), inner.getCentreX(), inner.getY() + 8.0f,
                              juce::Colour::fromRGB(168, 78, 12), inner.getCentreX(), inner.getBottom(), false);
    dial.addColour(0.6, juce::Colour::fromRGB(215, 132, 26));
    g.setGradientFill(dial);
    g.fillRoundedRectangle(inner, 4.0f);

    const float cx = inner.getCentreX();
    const float h = inner.getHeight();

    const float pivotY = inner.getBottom() + h * 0.40f;
    const float radius = h * 1.25f;

    struct Mark { float dB; const char* txt; bool major; };
    const Mark marks[] = {
        {-20,"20", true}, {-10,"10", true}, {-7,"7", false}, {-5,"5", true},
        {-3,"3", false}, {-1,"1", false}, {0,"0", true}, {1,"", false},
        {2,"", false}, {3,"+3", true}
    };

    for (auto& m : marks)
    {
        const float a = dbToAngle(m.dB);
        const bool red = m.dB >= 0.0f;
        const float r1 = radius * 0.94f;
        const float r2 = r1 - (m.major ? 9.0f : 5.0f);

        g.setColour(red ? juce::Colour::fromRGB(200, 30, 30) : juce::Colour::fromRGB(20, 20, 20));
        g.drawLine(cx + std::sin(a) * r1, pivotY - std::cos(a) * r1,
                   cx + std::sin(a) * r2, pivotY - std::cos(a) * r2, m.major ? 2.5f : 1.5f);

        if (m.major)
        {
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            const float labelR = r1 + 10.0f;
            g.drawText(m.txt,
                      (int)(cx + std::sin(a) * labelR) - 15,
                      (int)(pivotY - std::cos(a) * labelR) - 8,
                      30, 16,
                      juce::Justification::centred);
        }
    }

    juce::Path redArc;
    redArc.addCentredArc(cx, pivotY, radius * 0.94f, radius * 0.94f, 0.0f,
                         dbToAngle(0.0f), dbToAngle(maxDb), true);
    g.setColour(juce::Colour::fromRGB(200, 30, 30));
    g.strokePath(redArc, juce::PathStrokeType(3.5f));

    g.setColour(juce::Colour::fromRGB(35, 32, 28));

    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("VU", cx - 40.0f, inner.getY() + h * 0.45f, 80.0f, 24.0f, juce::Justification::centred);

    g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    g.drawText("DOM RADIO", cx - 50.0f, inner.getY() + h * 0.68f, 100.0f, 12.0f, juce::Justification::centred);

    g.setFont(juce::FontOptions(6.5f, juce::Font::plain));
    g.drawText("MAKHACHKALA", cx - 50.0f, inner.getBottom() - 12.0f, 100.0f, 8.0f, juce::Justification::centred);

    g.drawImageAt(RetroFX::createGrungeTexture((int)inner.getWidth(), (int)inner.getHeight(),
                                                1978, 0.007f),
                  (int)inner.getX(), (int)inner.getY());
    RetroFX::drawVignette(g, inner, 0.28f);
}

void VUMeterPro::paint(juce::Graphics& g)
{
    if (!dialCache.isValid())
        rebuildDialCache();
    g.drawImageAt(dialCache, 0, 0);

    auto inner = getLocalBounds().toFloat().reduced(6.0f);
    const float cx = inner.getCentreX();
    const float h = inner.getHeight();
    
    const float pivotY = inner.getBottom() + h * 0.40f;
    const float radius = h * 1.25f;

    g.saveState();
    g.reduceClipRegion(inner.toNearestInt());

    const float angle = dbToAngle(needleDb);
    juce::Path needle;
    const float needleLength = radius * 0.96f;
    
    needle.startNewSubPath(-0.7f, -needleLength);
    needle.lineTo( 0.7f, -needleLength);
    needle.lineTo( 2.5f, 10.0f);
    needle.lineTo(-2.5f, 10.0f);
    needle.closeSubPath();

    juce::Path shadow(needle);
    shadow.applyTransform(juce::AffineTransform::rotation(angle).translated(cx + 2.0f, pivotY + 2.0f));
    g.setColour(juce::Colours::black.withAlpha(0.25f));
    g.fillPath(shadow);

    needle.applyTransform(juce::AffineTransform::rotation(angle).translated(cx, pivotY));
    g.setColour(juce::Colour::fromRGB(25, 25, 25));
    g.fillPath(needle);

    g.restoreState();

    if (peakLampGlow > 0.03f)
    {
        RetroFX::drawGlowDot(g, { inner.getRight() - 16.0f, inner.getY() + 16.0f },
                             juce::Colour::fromRGB(230, 30, 25), 4.5f, peakLampGlow);
    }

    {
        const float lcdW = 68.0f;
        const float lcdH = 18.0f;
        juce::Rectangle<float> lcd(inner.getRight() - lcdW - 8.0f,
                                    inner.getBottom() - lcdH - 8.0f,
                                    lcdW, lcdH);

        g.setColour(juce::Colours::black.withAlpha(0.40f));
        g.fillRoundedRectangle(lcd.translated(0.5f, 1.0f), 2.5f);

        g.setColour(juce::Colour::fromRGB(12, 10, 6));
        g.fillRoundedRectangle(lcd, 2.5f);

        g.setColour(juce::Colour::fromRGB(65, 55, 38));
        g.drawRoundedRectangle(lcd, 2.5f, 0.8f);

        const juce::String txt = (peakDb <= minDb - 2.0f) ? "-inf"
                               : juce::String(peakDb, 1);

        g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                     12.0f, juce::Font::bold));

        g.setColour(juce::Colour::fromRGB(255, 176, 40).withAlpha(0.28f));
        g.drawText(txt, lcd.translated(0.0f, 0.8f).toNearestInt(), juce::Justification::centred, false);

        g.setColour(juce::Colour::fromRGB(255, 196, 70));
        g.drawText(txt, lcd.toNearestInt(), juce::Justification::centred, false);

        g.setFont(juce::FontOptions(7.0f, juce::Font::plain));
        g.setColour(juce::Colour::fromRGB(180, 140, 60));
        
        g.drawText("dB", 
                   static_cast<int>(lcd.getRight() - 18.0f), 
                   static_cast<int>(lcd.getBottom() - 10.0f), 
                   16, 
                   8,
                   juce::Justification::centredRight);
    }

    juce::ColourGradient glass(juce::Colours::white.withAlpha(0.12f),
                               cx, inner.getY(),
                               juce::Colours::white.withAlpha(0.0f),
                               cx, inner.getY() + inner.getHeight() * 0.45f,
                               false);
    g.setGradientFill(glass);
    g.fillRoundedRectangle(inner.getX() + 3.0f, inner.getY() + 3.0f,
                           inner.getWidth() - 6.0f, inner.getHeight() * 0.45f, 3.5f);
}
