#include "EQGraphLED.h"
#include "../Shared/UI/RetroFX.h"

EQGraphLED::EQGraphLED(TroakarSpectralAudioProcessor& p)
    : processor(p), gradientManager(p.gradientManager)
{
    startTimerHz(60);
    setMouseCursor(juce::MouseCursor::NormalCursor);

    globalThreshSlider.setSliderStyle(juce::Slider::LinearVertical);
    globalThreshSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    globalThreshSlider.setPopupDisplayEnabled(true, true, nullptr);
    
    globalThreshSlider.setRange(-maxDb, maxDb, 0.1);
    globalThreshSlider.setValue(0.0, juce::dontSendNotification);
    addAndMakeVisible(globalThreshSlider);

    threshAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "GLOBAL_THRESH", globalThreshSlider);

    paramListener = std::make_unique<ParameterListener>(*this);
    for (int i = 0; i < 8; ++i) {
        juce::String prefix = "BAND_" + juce::String(i);
        processor.apvts.addParameterListener(prefix + "_FREQ", paramListener.get());
        processor.apvts.addParameterListener(prefix + "_GAIN", paramListener.get());
        processor.apvts.addParameterListener(prefix + "_Q", paramListener.get());
        processor.apvts.addParameterListener(prefix + "_ENABLE", paramListener.get());
    }
    processor.apvts.addParameterListener("GLOBAL_THRESH", paramListener.get());
}

EQGraphLED::~EQGraphLED()
{
    if (paramListener) {
        for (int i = 0; i < 8; ++i) {
            juce::String prefix = "BAND_" + juce::String(i);
            processor.apvts.removeParameterListener(prefix + "_FREQ", paramListener.get());
            processor.apvts.removeParameterListener(prefix + "_GAIN", paramListener.get());
            processor.apvts.removeParameterListener(prefix + "_Q", paramListener.get());
            processor.apvts.removeParameterListener(prefix + "_ENABLE", paramListener.get());
        }
        processor.apvts.removeParameterListener("GLOBAL_THRESH", paramListener.get());
    }
}
float EQGraphLED::freqToX(float f) const  { return std::log10(juce::jlimit(20.0f, 20000.0f, f) / 20.0f) / 3.0f * (float)getWidth(); }
float EQGraphLED::xToFreq(float x) const  { return 20.0f * std::pow(10.0f, 3.0f * x / (float)getWidth()); }
float EQGraphLED::gainToY(float dB) const { return (float)getHeight() * 0.5f - (dB / maxDb) * (float)getHeight() * 0.45f; }
float EQGraphLED::yToGain(float y) const  { return ((float)getHeight() * 0.5f - y) / ((float)getHeight() * 0.45f) * maxDb; }

void EQGraphLED::resized()
{
    auto bounds = getLocalBounds();
    
    float yTop = gainToY(maxDb);
    float yBottom = gainToY(-maxDb);
    float trackHeight = yBottom - yTop;
    
    auto leftArea = bounds.removeFromLeft(28).reduced(4, 0);
    globalThreshSlider.setBounds(leftArea.getX(), (int)yTop, leftArea.getWidth(), (int)trackHeight);

    backgroundCache = juce::Image();
    rebuildBackgroundCache();

    const int w = getWidth();
    freqPerPixel.resize(w + 1);
    targetDbPerPixel.resize(w + 1, 0.0f);

    for (int x = 0; x <= w; ++x)
        freqPerPixel[x] = xToFreq((float)x);

    targetPathDirty = true;
}

void EQGraphLED::rebuildBackgroundCache()
{
    backgroundCache = juce::Image(juce::Image::ARGB, juce::jmax(1, getWidth()), juce::jmax(1, getHeight()), true);
    juce::Graphics g(backgroundCache);
    auto b = getLocalBounds().toFloat();

    juce::ColourGradient bg(juce::Colour::fromRGB(16, 13, 8), b.getCentreX(), b.getY(),
                            juce::Colour::fromRGB(25, 20, 15), b.getCentreX(), b.getBottom(), false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(b, 5.0f);

    g.setColour(phosphor.withAlpha(0.08f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        float x = freqToX(f);
        g.drawVerticalLine((int)x, 0.0f, b.getBottom());
    }
    
    for (int db = -24; db <= 24; db += 6)
    {
        float y = gainToY((float)db);
        g.setColour(phosphor.withAlpha(db == 0 ? 0.20f : 0.05f));
        g.drawHorizontalLine((int)y, 0.0f, b.getRight());
    }

    auto grunge = RetroFX::createGrungeTexture(getWidth(), getHeight(), 2026, 0.003f);
    g.drawImageAt(grunge, 0, 0);
}

void EQGraphLED::updateTargetCurveCache()
{
    const int w = getWidth();
    if (w <= 0) return;

    if (freqPerPixel.size() != static_cast<size_t>(w + 1))
    {
        freqPerPixel.resize(w + 1);
        targetDbPerPixel.resize(w + 1, 0.0f);
        for (int x = 0; x <= w; ++x)
            freqPerPixel[x] = xToFreq((float)x);
    }

    const double sr = juce::jmax(44100.0, processor.getSampleRate());

    struct TempBiquad {
        double c0 = 1.0, c1 = 0.0, c2 = 0.0;
        double d0 = 1.0, d1 = 0.0, d2 = 0.0;
        void setPeak(double sampleRate, double f0, double q, double gainDb) {
            if (std::abs(gainDb) < 0.01) {
                c0 = 1.0; c1 = 0.0; c2 = 0.0;
                d0 = 1.0; d1 = 0.0; d2 = 0.0;
                return;
            }
            double A = std::pow(10.0, gainDb / 40.0);
            double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / sampleRate;
            double alpha = std::sin(w0) / (2.0 * std::max(0.05, q));
            double cosw0 = std::cos(w0);
            double a0_inv = 1.0 / (1.0 + alpha / A);

            double b0 = (1.0 + alpha * A) * a0_inv;
            double b1 = (-2.0 * cosw0) * a0_inv;
            double b2 = (1.0 - alpha * A) * a0_inv;
            double a1 = (-2.0 * cosw0) * a0_inv;
            double a2 = (1.0 - alpha / A) * a0_inv;

            c0 = b0*b0 + b1*b1 + b2*b2;
            c1 = 2.0 * (b0*b1 + b1*b2);
            c2 = 2.0 * b0*b2;
            d0 = 1.0 + a1*a1 + a2*a2;
            d1 = 2.0 * (a1 + a1*a2);
            d2 = 2.0 * a2;
        }
        inline double getMagSq(double cw, double c2w) const {
            double num = c0 + c1 * cw + c2 * c2w;
            double den = d0 + d1 * cw + d2 * c2w;
            return num / std::max(1.0e-15, den);
        }
    };

    TempBiquad filters[8];
    int activeCount = 0;

    for (int i = 0; i < 8; ++i)
    {
        juce::String prefix = "BAND_" + juce::String(i);
        if (*processor.apvts.getRawParameterValue(prefix + "_ENABLE") < 0.5f)
            continue;

        float f0 = *processor.apvts.getRawParameterValue(prefix + "_FREQ");
        float gainDb = *processor.apvts.getRawParameterValue(prefix + "_GAIN");
        float q = *processor.apvts.getRawParameterValue(prefix + "_Q");

        if (std::abs(gainDb) < 0.05f)
            continue;

        filters[activeCount++].setPeak(sr, f0, q, gainDb);
    }

    cachedTargetPath.clear();
    cachedTargetPath.preallocateSpace(w / 4 + 10);

    for (int x = 0; x <= w; ++x)
    {
        double freq = freqPerPixel[x];
        double w0 = 2.0 * juce::MathConstants<double>::pi * freq / sr;
        double cw = std::cos(w0);
        double c2w = std::cos(2.0 * w0);

        double totalMagSq = 1.0;
        for (int f = 0; f < activeCount; ++f)
            totalMagSq *= filters[f].getMagSq(cw, c2w);

        float dB = static_cast<float>(10.0 * std::log10(std::max(1.0e-15, totalMagSq)));
        targetDbPerPixel[x] = dB;

        if (x % 4 == 0 || x == w)
        {
            float y = juce::jlimit(3.0f, static_cast<float>(getHeight()) - 3.0f, gainToY(dB));
            if (x == 0) cachedTargetPath.startNewSubPath(0.0f, y);
            else        cachedTargetPath.lineTo(static_cast<float>(x), y);
        }
    }

    targetPathDirty = false;
}

void EQGraphLED::buildDeltaPaths()
{
    cachedUpFill.clear();
    cachedDownFill.clear();
    cachedUpLine.clear();
    cachedDownLine.clear();

    const int w = getWidth();
    if (w <= 0) return;
    const double sr = juce::jmax(44100.0, processor.getSampleRate());

    cachedUpFill.preallocateSpace(w + 10);
    cachedDownFill.preallocateSpace(w + 10);
    cachedUpLine.preallocateSpace(w / 3 + 10);
    cachedDownLine.preallocateSpace(w / 3 + 10);

    float spatialSmoothedDelta = 0.0f;

    for (int x = 0; x <= w; x += 3)
    {
        double freq = freqPerPixel[x];
        float targetDb = targetDbPerPixel[x];
        float rawDelta = getInterpolatedArray(processor.compressionDeltaData, freq, sr);

        if (x == 0) spatialSmoothedDelta = rawDelta;
        else spatialSmoothedDelta = spatialSmoothedDelta * 0.7f + rawDelta * 0.3f;

        float upDeltaDb = std::max(0.0f, spatialSmoothedDelta);
        float downDeltaDb = std::min(0.0f, spatialSmoothedDelta);

        float yTarget = juce::jlimit(3.0f, (float)getHeight() - 3.0f, gainToY(targetDb));
        float yUp = juce::jlimit(3.0f, (float)getHeight() - 3.0f, gainToY(targetDb + upDeltaDb));
        float yDown = juce::jlimit(3.0f, (float)getHeight() - 3.0f, gainToY(targetDb + downDeltaDb));

        if (x == 0) {
            cachedUpFill.startNewSubPath(x, yTarget);
            cachedDownFill.startNewSubPath(x, yTarget);
            cachedUpLine.startNewSubPath(x, yUp);
            cachedDownLine.startNewSubPath(x, yDown);
        } else {
            cachedUpFill.lineTo(x, yUp);
            cachedDownFill.lineTo(x, yDown);
            cachedUpLine.lineTo(x, yUp);
            cachedDownLine.lineTo(x, yDown);
        }
    }

    for (int x = w; x >= 0; x -= 3) {
        float yTarget = juce::jlimit(3.0f, (float)getHeight() - 3.0f, gainToY(targetDbPerPixel[x]));
        cachedUpFill.lineTo(x, yTarget);
        cachedDownFill.lineTo(x, yTarget);
    }

    cachedUpFill.closeSubPath();
    cachedDownFill.closeSubPath();
}

float EQGraphLED::getTargetCurveDb(double freq) const
{
    const double sr = juce::jmax(44100.0, processor.getSampleRate());
    double totalMagnitude = 1.0;

    for (int i = 0; i < 8; ++i)
    {
        const juce::String prefix = "BAND_" + juce::String(i);

        if (*processor.apvts.getRawParameterValue(prefix + "_ENABLE") < 0.5f)
            continue;

        const float f0 =
            *processor.apvts.getRawParameterValue(prefix + "_FREQ");

        const float gainDb =
            *processor.apvts.getRawParameterValue(prefix + "_GAIN");

        const float q =
            *processor.apvts.getRawParameterValue(prefix + "_Q");

        if (std::abs(gainDb) < 0.05f)
            continue;

        const auto coeffs =
            juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                sr,
                juce::jlimit(20.0, sr * 0.49, static_cast<double>(f0)),
                juce::jmax(0.05f, q),
                juce::Decibels::decibelsToGain(gainDb));

        totalMagnitude *= coeffs->getMagnitudeForFrequency(freq, sr);
    }

    return static_cast<float>(
        juce::Decibels::gainToDecibels(
            juce::jmax(1.0e-6, totalMagnitude)));
}

float EQGraphLED::getInterpolatedArray(const std::atomic<float>* arr, double freq, double sr) const
{
    const int fftSize = processor.getCurrentFFTSize();
    const int maxBin = fftSize / 2 - 2;

    float fBin = (float)(freq * (double)fftSize / sr);
    if (fBin < 1.0f) return arr[1].load(std::memory_order_relaxed);
    if (fBin > (float)maxBin) return arr[maxBin].load(std::memory_order_relaxed);

    int idx = (int)fBin;
    float frac = fBin - (float)idx;

    float v1 = arr[idx].load(std::memory_order_relaxed);
    float v2 = arr[idx + 1].load(std::memory_order_relaxed);

    return v1 + frac * (v2 - v1);
}

juce::Path& EQGraphLED::buildTargetCurvePath() const
{
    cachedTargetPath.clear();
    const int w = getWidth();
    if (w <= 0) return cachedTargetPath;

    cachedTargetPath.preallocateSpace(w / 4 + 10);

    for (int x = 0; x <= w; x += 4)
    {
        float dB = targetDbPerPixel[x];
        float y = juce::jlimit(3.0f, (float)getHeight() - 3.0f, gainToY(dB));

        if (x == 0) cachedTargetPath.startNewSubPath(0.0f, y);
        else        cachedTargetPath.lineTo((float)x, y);
    }
    return cachedTargetPath;
}

void EQGraphLED::drawSpectrumFog(juce::Graphics& g, const juce::Rectangle<float>& bounds)
{
    const int w = getWidth();
    const double sr = juce::jmax(44100.0, processor.getSampleRate());

    cachedSpecPath.clear();
    cachedSpecPath.preallocateSpace(w / 6 + 10);
    bool first = true;
    float spatialSmoothedSpec = 0.0f;
    
    for (int x = 0; x <= w; x += 6) {
        double freq = freqPerPixel[x];
        float specDb = getInterpolatedArray(processor.spectrumDataLeft, freq, sr);
        
        if (x == 0) spatialSmoothedSpec = specDb;
        else spatialSmoothedSpec = spatialSmoothedSpec * 0.4f + specDb * 0.6f;
        
        float visualDb = spatialSmoothedSpec;
        float ySpec = juce::jlimit(0.0f, bounds.getBottom(), gainToY(visualDb));
        
        if (first) { cachedSpecPath.startNewSubPath((float)x, ySpec); first = false; }
        else { cachedSpecPath.lineTo((float)x, ySpec); }
    }
    
    g.setColour(phosphor.withAlpha(0.12f));
    g.strokePath(cachedSpecPath, juce::PathStrokeType(1.2f));
}

void EQGraphLED::paint(juce::Graphics& g)
{
    if (!backgroundCache.isValid())
        rebuildBackgroundCache();

    g.drawImageAt(backgroundCache, 0, 0);

    auto bounds = getLocalBounds().toFloat();

    drawSpectrumFog(g, bounds);

    const float globalThresh = 
        *processor.apvts.getRawParameterValue("GLOBAL_THRESH");

    const float threshY = gainToY(globalThresh);

    g.setColour(juce::Colour::fromRGB(100, 200, 255).withAlpha(0.35f));
    g.drawHorizontalLine(static_cast<int>(threshY), 0.0f, bounds.getRight());

    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.setColour(juce::Colour::fromRGB(100, 200, 255).withAlpha(0.8f));
    g.drawText("THRESHOLD " + juce::String(globalThresh, 1) + " dB",
               10, static_cast<int>(threshY) - 15, 150, 12,
               juce::Justification::centredLeft);

    float avgGain = 0.0f;
    int validBins = 0;

    for (int i = 10; i < 200; ++i) {
        float deltaDb = processor.compressionDeltaData[i].load(std::memory_order_relaxed);
        if (std::abs(deltaDb) < 20.0f) {
            avgGain += deltaDb;
            ++validBins;
        }
    }

    if (validBins > 0) {
        avgGain /= (float)validBins;

        float avgY = gainToY(avgGain);
        g.setColour(juce::Colour::fromRGB(100, 200, 255).withAlpha(0.5f));

        juce::Path avgLine;
        for (float x = 0; x < getWidth(); x += 8.0f) {
            avgLine.startNewSubPath(x, avgY);
            avgLine.lineTo(x + 4.0f, avgY);
        }
        g.strokePath(avgLine, juce::PathStrokeType(1.5f));

        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.drawText("AVG: " + juce::String(avgGain, 1) + " dB",
                   10, (int)avgY - 15, 100, 12,
                   juce::Justification::centredLeft);
    }

    if (targetPathDirty) {
        buildTargetCurvePath();
        targetPathDirty = false;
    }
    
    // 1. Отрисовка заливок Downward (Красный цвет)
    if (!cachedDownFill.isEmpty()) {
        g.setColour(juce::Colour::fromRGB(240, 70, 70).withAlpha(0.25f));
        g.fillPath(cachedDownFill);
        
        // Линия контура Downward
        g.setColour(juce::Colour::fromRGB(255, 90, 90).withAlpha(0.85f));
        g.strokePath(cachedDownLine, juce::PathStrokeType(1.5f));
    }

    // 2. Отрисовка заливок Upward (Желтый цвет)
    if (!cachedUpFill.isEmpty()) {
        g.setColour(phosphor.withAlpha(0.2f));
        g.fillPath(cachedUpFill);
        
        // Линия контура Upward
        g.setColour(phosphor.brighter().withAlpha(0.85f));
        g.strokePath(cachedUpLine, juce::PathStrokeType(1.5f));
    }

    // 3. Отрисовка главной целевой кривой (Она всегда четкая и поверх компрессии)
    RetroFX::drawGlowPath(g, cachedTargetPath, phosphor, 2.2f, 3);
    g.setColour(phosphor);
    g.strokePath(cachedTargetPath, juce::PathStrokeType(2.0f));

    // Легкая заливка под главной кривой
    juce::Path fill(cachedTargetPath);
    fill.lineTo(bounds.getRight(), bounds.getBottom());
    fill.lineTo(0.0f, bounds.getBottom());
    fill.closeSubPath();
    
    juce::ColourGradient fg(phosphor.withAlpha(0.05f), 0.0f, gainToY(maxDb),
                            phosphor.withAlpha(0.0f),  0.0f, bounds.getBottom(), false);
    g.setGradientFill(fg);
    g.fillPath(fill);

    // 1. Отрисовка Гауссовых заливок градиентов (СЛОЙ ПОД ТОЧКАМИ)
    for (const auto& gBand : processor.gradientManager.points)
    {
        if (!gBand.active) continue;

        float centerX = freqToX(gBand.centerFreqHz);
        float octaveWidthPx = getWidth() * 0.100343f;
        float radiusPx = gBand.radiusOctaves * octaveWidthPx;
        float spanPx = radiusPx * 3.0f; 

        juce::ColourGradient grad;
        grad.point1 = {centerX - spanPx, 0};
        grad.point2 = {centerX + spanPx, 0};
        grad.isRadial = false;

        float alphaMult = gBand.isSelected ? 0.40f : 0.12f;

        for (float i = 0.0f; i <= 1.0f; i += 0.05f) {
            float dist = (i - 0.5f) * 6.0f; 
            float weight = std::exp(-0.5f * dist * dist);
            grad.addColour(i, gBand.color.withAlpha(weight * alphaMult));
        }

        g.setGradientFill(grad);
        g.fillRect(centerX - spanPx, bounds.getY(), spanPx * 2.0f, bounds.getHeight());

        if (gBand.isSelected || hoveredGradientId == gBand.id) {
            g.setColour(gBand.color.withAlpha(0.6f));
            g.drawVerticalLine((int)(centerX - radiusPx), bounds.getY(), bounds.getBottom());
            g.drawVerticalLine((int)(centerX + radiusPx), bounds.getY(), bounds.getBottom());
            g.setColour(gBand.color.withAlpha(0.9f));
            g.drawVerticalLine((int)centerX, bounds.getY(), bounds.getBottom());
        }
    }

    // 2. ВОССТАНОВЛЕНИЕ: Отрисовка узлов обычного эквалайзера (1-8)
    for (int i = 0; i < 8; ++i)
    {
        juce::String prefix = "BAND_" + juce::String(i);
        bool enabled = *processor.apvts.getRawParameterValue(prefix + "_ENABLE") > 0.5f;
        
        if (!enabled && draggingNode != i) continue;

        float freq = *processor.apvts.getRawParameterValue(prefix + "_FREQ");
        float gain = *processor.apvts.getRawParameterValue(prefix + "_GAIN");
        float q = *processor.apvts.getRawParameterValue(prefix + "_Q");

        juce::Point<float> pos { freqToX(freq), gainToY(gain) };

        float intensity = (draggingNode == i) ? 1.0f : (hoveredNode == i ? 0.85f : 0.5f);
        juce::Colour nodeColor = enabled ? phosphor : juce::Colours::grey;

        float qRadius = 35.0f / q; 
        if (draggingNode == i || hoveredNode == i) {
            g.setColour(nodeColor.withAlpha(0.20f * intensity));
            g.drawEllipse(pos.x - qRadius, pos.y - qRadius, qRadius * 2.0f, qRadius * 2.0f, 1.2f);
        } else {
            g.setColour(nodeColor.withAlpha(0.08f));
            g.fillEllipse(pos.x - qRadius, pos.y - qRadius, qRadius * 2.0f, qRadius * 2.0f);
        }

        RetroFX::drawGlowDot(g, pos, nodeColor, 5.0f, intensity);

        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
        g.drawText(juce::String(i + 1), (int)pos.x - 5, (int)pos.y - 5, 10, 10, juce::Justification::centred);
    }

    // 3. Отрисовка маркеров градиентов (G0, G1...) поверх всего остального
    for (const auto& gp : processor.gradientManager.points)
    {
        if (!gp.active) continue;
        
        juce::Point<float> center { freqToX(gp.centerFreqHz), gainToY(gp.centerGainDb) };
        float intensity = gp.isSelected ? 1.0f : (hoveredGradientId == gp.id ? 0.8f : 0.4f);

        g.setColour(gp.color.withAlpha(gp.isSelected ? 0.8f : 0.3f));
        g.drawEllipse(center.x - 10.0f, center.y - 10.0f, 20.0f, 20.0f, gp.isSelected ? 2.5f : 1.5f);
        RetroFX::drawGlowDot(g, center, gp.color, gp.isSelected ? 8.0f : 5.0f, intensity);

        if (gp.isSelected || hoveredGradientId == gp.id) {
            g.setColour(gp.color.withAlpha(0.9f));
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            g.drawText(juce::String(gp.radiusOctaves, 1) + " oct", 
                       (int)center.x - 30, (int)center.y + 15, 60, 12, juce::Justification::centred);
        }
    }

    RetroFX::drawVignette(g, bounds, 0.4f);
    RetroFX::drawGlassHighlight(g, bounds);
}

void EQGraphLED::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Add Gradient Here");

        menu.showMenuAsync(juce::PopupMenu::Options(),
            [this, e](int result)
            {
                if (result == 1)
                {
                    const float newFreq = 
                        juce::jlimit(20.0f, 20000.0f, xToFreq(e.position.x));
                    const float newGain = 
                        juce::jlimit(-maxDb, maxDb, yToGain(e.position.y));

                    const int newId = 
                        gradientManager.addPoint(newFreq, newGain);
                    gradientManager.setActivePoint(newId);

                    if (onGradientSelectionChanged) onGradientSelectionChanged();
                    if (onGradientParamsChanged) onGradientParamsChanged();
                    repaint();
                }
            });
        return;
    }

    bool clickedGradient = false;
    for (const auto& gp : gradientManager.points)
    {
        juce::Point<float> gpPos { 
            freqToX(gp.centerFreqHz), 
            gainToY(gp.centerGainDb) 
        };

        if (e.position.getDistanceFrom(gpPos) < 20.0f)
        {
            draggingGradientId = gp.id;
            gradientManager.setActivePoint(gp.id);
            clickedGradient = true;
            break;
        }
    }

    if (clickedGradient)
    {
        if (onGradientSelectionChanged) onGradientSelectionChanged();
        repaint();
        return;
    }

    bool clickedNode = false;
    for (int i = 0; i < 8; ++i)
    {
        const juce::String prefix = "BAND_" + juce::String(i);
        const bool enabled = 
            *processor.apvts.getRawParameterValue(prefix + "_ENABLE") > 0.5f;

        if (!enabled) 
            continue;

        const float freq = 
            *processor.apvts.getRawParameterValue(prefix + "_FREQ");
        const float gain = 
            *processor.apvts.getRawParameterValue(prefix + "_GAIN");

        const juce::Point<float> pos { freqToX(freq), gainToY(gain) };

        if (e.position.getDistanceFrom(pos) < 15.0f)
        {
            draggingNode = i;
            dragStartFreq = freq;
            dragStartGain = gain;
            clickedNode = true;
            break;
        }
    }

    if (!clickedNode && gradientManager.hasActivePoint()) {
        gradientManager.clearActive();
        if (onGradientSelectionChanged) onGradientSelectionChanged();
        repaint();
        return;
    }

    if (!clickedNode) {
        const float newFreq = 
            juce::jlimit(20.0f, 20000.0f, xToFreq(e.position.x));
        const float newGain = 
            juce::jlimit(-maxDb, maxDb, yToGain(e.position.y));

        createEQBandAt(newFreq, newGain);
        repaint();
    }
}

void EQGraphLED::mouseDoubleClick(const juce::MouseEvent& e)
{
    auto& apvts = processor.apvts;

    for (int i = 0; i < 8; ++i)
    {
        const juce::String prefix = "BAND_" + juce::String(i);
        const bool enabled = 
            *apvts.getRawParameterValue(prefix + "_ENABLE") > 0.5f;

        if (!enabled) 
            continue;

        const float freq = 
            *apvts.getRawParameterValue(prefix + "_FREQ");
        const float gain = 
            *apvts.getRawParameterValue(prefix + "_GAIN");

        const juce::Point<float> pos { freqToX(freq), gainToY(gain) };

        if (e.position.getDistanceFrom(pos) < 15.0f)
        {
            auto* enableParam = apvts.getParameter(prefix + "_ENABLE");
            enableParam->setValueNotifyingHost(0.0f);
            targetPathDirty = true;
            repaint();
            return;
        }
    }

    for (const auto& gp : gradientManager.points)
    {
        const juce::Point<float> gpPos { 
            freqToX(gp.centerFreqHz), 
            gainToY(gp.centerGainDb) 
        };

        if (e.position.getDistanceFrom(gpPos) < 15.0f)
        {
            gradientManager.removePoint(gp.id);
            
            if (onGradientParamsChanged) onGradientParamsChanged();
            if (onGradientSelectionChanged) onGradientSelectionChanged();
            
            repaint();
            return;
        }
    }

    const float newFreq = 
        juce::jlimit(20.0f, 20000.0f, xToFreq(e.position.x));
    const float newGain = 
        juce::jlimit(-maxDb, maxDb, yToGain(e.position.y));

    createEQBandAt(newFreq, newGain);
    repaint();
}

void EQGraphLED::mouseMove(const juce::MouseEvent& e)
{
    int lastHovered = hoveredNode;
    hoveredNode = -1;
    hoveredGradientId = -1;

    bool anyClose = false;

    for (const auto& gp : gradientManager.points)
    {
        juce::Point<float> gpPos { freqToX(gp.centerFreqHz), gainToY(gp.centerGainDb) };
        if (e.position.getDistanceFrom(gpPos) < 15.0f)
        {
            hoveredGradientId = gp.id;
            anyClose = true;
            break;
        }
    }

    if (!anyClose)
    {
        for (int i = 0; i < 8; ++i)
        {
            juce::String prefix = "BAND_" + juce::String(i);
            bool enabled = *processor.apvts.getRawParameterValue(prefix + "_ENABLE") > 0.5f;
            if (!enabled) continue;

            float freq = *processor.apvts.getRawParameterValue(prefix + "_FREQ");
            float gain = *processor.apvts.getRawParameterValue(prefix + "_GAIN");

            juce::Point<float> pos { freqToX(freq), gainToY(gain) };
            float dist = e.position.getDistanceFrom(pos);

            if (dist < 15.0f)
            {
                hoveredNode = i;
                break;
            }
        }
    }

    if (hoveredNode != lastHovered || hoveredGradientId >= 0)
        repaint();
}

void EQGraphLED::mouseDrag(const juce::MouseEvent& e)
{
    if (draggingGradientId >= 0)
    {
        auto* gp = gradientManager.getPoint(draggingGradientId);
        if (gp)
        {
            gp->centerFreqHz = juce::jlimit(20.0f, 20000.0f, xToFreq(e.position.x));
            gp->centerGainDb = juce::jlimit(-maxDb, maxDb, yToGain(e.position.y));
            
            if (onGradientParamsChanged) onGradientParamsChanged();
            repaint();
        }
        return;
    }

    if (draggingNode < 0) return;

    const float sensitivity = e.mods.isShiftDown() ? 0.2f : 1.0f;
    const float deltaX = (float)e.getOffsetFromDragStart().x * sensitivity;
    const float deltaY = (float)e.getOffsetFromDragStart().y * sensitivity;

    const float freqRatio = std::pow(10.0f, (deltaX * 3.0f) / (float)getWidth());
    const float newFreq = juce::jlimit(20.0f, 20000.0f, dragStartFreq * freqRatio);

    const float gainDelta = -(deltaY * maxDb) / ((float)getHeight() * 0.45f);
    const float newGain = juce::jlimit(-maxDb, maxDb, dragStartGain + gainDelta);

    juce::String prefix = "BAND_" + juce::String(draggingNode);
    auto* fParam = processor.apvts.getParameter(prefix + "_FREQ");
    auto* gParam = processor.apvts.getParameter(prefix + "_GAIN");

    fParam->setValueNotifyingHost(fParam->convertTo0to1(newFreq));
    gParam->setValueNotifyingHost(gParam->convertTo0to1(newGain));

    targetPathDirty = true;
}

void EQGraphLED::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    for (auto& gp : gradientManager.points)
    {
        juce::Point<float> gpPos { freqToX(gp.centerFreqHz), gainToY(gp.centerGainDb) };
        
        bool hoverDot = e.position.getDistanceFrom(gpPos) < 25.0f;
        bool hoverSpan = gp.isSelected && std::abs(e.position.x - freqToX(gp.centerFreqHz)) < (getWidth() * 0.1f * gp.radiusOctaves);
        
        if (hoverDot || hoverSpan)
        {
            float factor = std::exp(wheel.deltaY * 0.4f);
            gp.radiusOctaves = juce::jlimit(0.3f, 4.0f, gp.radiusOctaves * factor);
            
            if (onGradientParamsChanged) onGradientParamsChanged();
            repaint();
            return;
        }
    }

    int targetNode = draggingNode >= 0 ? draggingNode : hoveredNode;
    if (targetNode >= 0)
    {
        juce::String prefix = "BAND_" + juce::String(targetNode);
        auto* qParam = processor.apvts.getParameter(prefix + "_Q");

        float currentQ = *processor.apvts.getRawParameterValue(prefix + "_Q");
        float factor = std::exp(wheel.deltaY * 0.4f);
        float newQ = juce::jlimit(0.1f, 10.0f, currentQ * factor);

        qParam->setValueNotifyingHost(qParam->convertTo0to1(newQ));
        repaint();
    }
}

int EQGraphLED::createEQBandAt(float freq, float gainDb)
{
    auto& apvts = processor.apvts;

    for (int i = 0; i < 8; ++i)
    {
        const juce::String prefix = "BAND_" + juce::String(i);
        const bool enabled = 
            *apvts.getRawParameterValue(prefix + "_ENABLE") > 0.5f;

        if (!enabled)
        {
            auto* enableParam = apvts.getParameter(prefix + "_ENABLE");
            auto* freqParam   = apvts.getParameter(prefix + "_FREQ");
            auto* gainParam   = apvts.getParameter(prefix + "_GAIN");
            auto* qParam      = apvts.getParameter(prefix + "_Q");

            enableParam->setValueNotifyingHost(1.0f);
            freqParam->setValueNotifyingHost(
                freqParam->convertTo0to1(freq));
            gainParam->setValueNotifyingHost(
                gainParam->convertTo0to1(gainDb));
            qParam->setValueNotifyingHost(
                qParam->convertTo0to1(1.0f));

            targetPathDirty = true;
            return i;
        }
    }

    return -1;
}