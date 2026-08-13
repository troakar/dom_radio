#include "EQGraphLED.h"
#include "../Shared/UI/RetroFX.h"

EQGraphLED::EQGraphLED(TroakarSpectralAudioProcessor& p) : processor(p)
{
    startTimerHz(45);
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

float EQGraphLED::freqToX(float f) const  { return std::log10(juce::jlimit(20.0f, 20000.0f, f) / 20.0f) / 3.0f * (float)getWidth(); }
float EQGraphLED::xToFreq(float x) const  { return 20.0f * std::pow(10.0f, 3.0f * x / (float)getWidth()); }
float EQGraphLED::gainToY(float dB) const { return (float)getHeight() * 0.5f - (dB / maxDb) * (float)getHeight() * 0.45f; }
float EQGraphLED::yToGain(float y) const  { return ((float)getHeight() * 0.5f - y) / ((float)getHeight() * 0.45f) * maxDb; }

void EQGraphLED::resized()
{
    backgroundCache = juce::Image();
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

float EQGraphLED::getTargetCurveDb(double freq) const
{
    float globalThresh = *processor.apvts.getRawParameterValue("GLOBAL_THRESH");
    double sr = juce::jmax(44100.0, processor.getSampleRate());
    double totalMag = 1.0;
    for (int i = 0; i < 8; ++i) {
        juce::String prefix = "BAND_" + juce::String(i);
        if (*processor.apvts.getRawParameterValue(prefix + "_ENABLE") < 0.5f) continue;

        float f0 = *processor.apvts.getRawParameterValue(prefix + "_FREQ");
        float gainDb = *processor.apvts.getRawParameterValue(prefix + "_GAIN");
        float q = *processor.apvts.getRawParameterValue(prefix + "_Q");

        if (std::abs(gainDb) < 0.05f) continue;
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, f0, q, juce::Decibels::decibelsToGain(gainDb));
        totalMag *= coeffs->getMagnitudeForFrequency(freq, sr);
    }
    return globalThresh + (float)juce::Decibels::gainToDecibels(juce::jmax(1.0e-6, totalMag));
}

float EQGraphLED::getInterpolatedArray(const std::atomic<float>* arr, double freq, double sr) const
{
    float fBin = (float)(freq * 512.0 / sr);
    if (fBin < 1.0f) return arr[1].load(std::memory_order_relaxed);
    if (fBin > 254.0f) return arr[254].load(std::memory_order_relaxed);
    
    int idx = (int)fBin;
    float frac = fBin - (float)idx;
    float v1 = arr[idx].load(std::memory_order_relaxed);
    float v2 = arr[idx + 1].load(std::memory_order_relaxed);
    
    return v1 + frac * (v2 - v1);
}

juce::Path EQGraphLED::buildTargetCurvePath() const
{
    juce::Path path;
    const int w = getWidth();
    for (int x = 0; x <= w; x += 3)
    {
        double freq = xToFreq((float)x);
        float dB = getTargetCurveDb(freq);
        float y = juce::jlimit(3.0f, (float)getHeight() - 3.0f, gainToY(dB));
        
        if (x == 0) path.startNewSubPath(0.0f, y);
        else        path.lineTo((float)x, y);
    }
    return path;
}

juce::Path EQGraphLED::buildActiveCurvePath() const
{
    juce::Path path;
    const int w = getWidth();
    const double sr = juce::jmax(44100.0, processor.getSampleRate());
    
    float spatialSmoothedDelta = 0.0f;

    for (int x = 0; x <= w; x += 3)
    {
        double freq = xToFreq((float)x);
        float targetDb = getTargetCurveDb(freq);
        float deltaDb = getInterpolatedArray(processor.compressionDeltaData, freq, sr);
        
        if (x == 0) spatialSmoothedDelta = deltaDb;
        else spatialSmoothedDelta = spatialSmoothedDelta * 0.85f + deltaDb * 0.15f;
        
        float y = juce::jlimit(3.0f, (float)getHeight() - 3.0f, gainToY(targetDb + spatialSmoothedDelta));
        
        if (x == 0) path.startNewSubPath((float)x, y);
        else        path.lineTo((float)x, y);
    }
    return path;
}

juce::Path EQGraphLED::buildDeltaPath(bool isUpward) const
{
    juce::Path path;
    const int w = getWidth();
    const double sr = juce::jmax(44100.0, processor.getSampleRate());
    
    float spatialSmoothedDelta = 0.0f;
    bool first = true;
    
    for (int x = 0; x <= w; x += 3)
    {
        double freq = xToFreq((float)x);
        float targetDb = getTargetCurveDb(freq);
        float deltaDb = getInterpolatedArray(processor.compressionDeltaData, freq, sr);
        
        if (x == 0) spatialSmoothedDelta = deltaDb;
        else spatialSmoothedDelta = spatialSmoothedDelta * 0.85f + deltaDb * 0.15f;
        
        float clampedDelta = isUpward ? std::max(0.0f, spatialSmoothedDelta) 
                                      : std::min(0.0f, spatialSmoothedDelta);
        
        float y = juce::jlimit(3.0f, (float)getHeight() - 3.0f, gainToY(targetDb + clampedDelta));
        
        if (first) { path.startNewSubPath((float)x, y); first = false; }
        else         path.lineTo((float)x, y);
    }
    
    for (int x = w; x >= 0; x -= 3)
    {
        double freq = xToFreq((float)x);
        float targetDb = getTargetCurveDb(freq);
        float y = juce::jlimit(3.0f, (float)getHeight() - 3.0f, gainToY(targetDb));
        path.lineTo((float)x, y);
    }
    
    path.closeSubPath();
    return path;
}

void EQGraphLED::drawSpectrumFog(juce::Graphics& g, const juce::Rectangle<float>& bounds)
{
    const int w = getWidth();
    const double sr = juce::jmax(44100.0, processor.getSampleRate());

    juce::Path specPath;
    bool first = true;
    float spatialSmoothedSpec = 0.0f;
    
    for (int x = 0; x <= w; x += 3) {
        double freq = xToFreq((float)x);
        float specDb = getInterpolatedArray(processor.spectrumDataLeft, freq, sr);
        
        if (x == 0) spatialSmoothedSpec = specDb;
        else spatialSmoothedSpec = spatialSmoothedSpec * 0.7f + specDb * 0.3f;
        
        float visualDb = spatialSmoothedSpec + 12.0f;
        float ySpec = juce::jlimit(0.0f, bounds.getBottom(), gainToY(visualDb));
        
        if (first) { specPath.startNewSubPath((float)x, ySpec); first = false; }
        else { specPath.lineTo((float)x, ySpec); }
    }
    
    g.setColour(phosphor.withAlpha(0.12f));
    g.strokePath(specPath, juce::PathStrokeType(1.2f));
}

void EQGraphLED::paint(juce::Graphics& g)
{
    if (!backgroundCache.isValid())
        rebuildBackgroundCache();
    
    g.drawImageAt(backgroundCache, 0, 0);

    auto bounds = getLocalBounds().toFloat();
    
    drawSpectrumFog(g, bounds);

    juce::Path targetCurve = buildTargetCurvePath();
    juce::Path activeCurve = buildActiveCurvePath();

    juce::Path upDelta = buildDeltaPath(true);
    juce::Path downDelta = buildDeltaPath(false);

    if (!upDelta.isEmpty()) {
        juce::ColourGradient upGrad(phosphor.withAlpha(0.28f), 0.0f, bounds.getY(),
                                   phosphor.withAlpha(0.05f), 0.0f, bounds.getBottom(), false);
        g.setGradientFill(upGrad);
        g.fillPath(upDelta);
    }
    if (!downDelta.isEmpty()) {
        juce::ColourGradient downGrad(juce::Colour::fromRGB(240, 70, 70).withAlpha(0.35f), 0.0f, bounds.getY(),
                                     juce::Colour::fromRGB(240, 70, 70).withAlpha(0.08f), 0.0f, bounds.getBottom(), false);
        g.setGradientFill(downGrad);
        g.fillPath(downDelta);
    }

    g.setColour(phosphor.withAlpha(0.40f));
    g.strokePath(activeCurve, juce::PathStrokeType(1.0f));

    RetroFX::drawGlowPath(g, targetCurve, phosphor, 2.2f, 3);
    g.setColour(phosphor);
    g.strokePath(targetCurve, juce::PathStrokeType(1.8f));

    juce::Path fill(targetCurve);
    fill.lineTo(bounds.getRight(), bounds.getBottom());
    fill.lineTo(0.0f, bounds.getBottom());
    fill.closeSubPath();
    
    juce::ColourGradient fg(phosphor.withAlpha(0.08f), 0.0f, gainToY(maxDb),
                            phosphor.withAlpha(0.0f),  0.0f, bounds.getBottom(), false);
    g.setGradientFill(fg);
    g.fillPath(fill);

    float globalThresh = *processor.apvts.getRawParameterValue("GLOBAL_THRESH");

    for (int i = 0; i < 8; ++i)
    {
        juce::String prefix = "BAND_" + juce::String(i);
        bool enabled = *processor.apvts.getRawParameterValue(prefix + "_ENABLE") > 0.5f;
        
        if (!enabled && draggingNode != i) continue;

        float freq = *processor.apvts.getRawParameterValue(prefix + "_FREQ");
        float gain = *processor.apvts.getRawParameterValue(prefix + "_GAIN");
        float q = *processor.apvts.getRawParameterValue(prefix + "_Q");

        juce::Point<float> pos { freqToX(freq), gainToY(gain + globalThresh) };

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

    RetroFX::drawVignette(g, bounds, 0.4f);
    RetroFX::drawGlassHighlight(g, bounds);
}

void EQGraphLED::mouseDown(const juce::MouseEvent& e)
{
    auto& apvts = processor.apvts;
    float globalThresh = *apvts.getRawParameterValue("GLOBAL_THRESH");
    
    for (int i = 0; i < 8; ++i)
    {
        juce::String prefix = "BAND_" + juce::String(i);
        bool enabled = *apvts.getRawParameterValue(prefix + "_ENABLE") > 0.5f;
        if (!enabled) continue;

        float freq = *apvts.getRawParameterValue(prefix + "_FREQ");
        float gain = *apvts.getRawParameterValue(prefix + "_GAIN");

        juce::Point<float> pos { freqToX(freq), gainToY(gain + globalThresh) };
        if (e.position.getDistanceFrom(pos) < 15.0f)
        {
            draggingNode = i;
            dragStartFreq = freq;
            dragStartGain = gain;
            return;
        }
    }
    draggingNode = -1;
}

void EQGraphLED::mouseDoubleClick(const juce::MouseEvent& e)
{
    auto& apvts = processor.apvts;
    float globalThresh = *apvts.getRawParameterValue("GLOBAL_THRESH");
    
    for (int i = 0; i < 8; ++i)
    {
        juce::String prefix = "BAND_" + juce::String(i);
        bool enabled = *apvts.getRawParameterValue(prefix + "_ENABLE") > 0.5f;
        if (!enabled) continue;

        float freq = *apvts.getRawParameterValue(prefix + "_FREQ");
        float gain = *apvts.getRawParameterValue(prefix + "_GAIN");

        juce::Point<float> pos { freqToX(freq), gainToY(gain + globalThresh) };
        if (e.position.getDistanceFrom(pos) < 15.0f)
        {
            auto* enableParam = apvts.getParameter(prefix + "_ENABLE");
            enableParam->setValueNotifyingHost(0.0f);
            draggingNode = -1;
            hoveredNode = -1;
            repaint();
            return;
        }
    }

    for (int i = 0; i < 8; ++i)
    {
        juce::String prefix = "BAND_" + juce::String(i);
        bool enabled = *apvts.getRawParameterValue(prefix + "_ENABLE") > 0.5f;
        if (!enabled)
        {
            float newFreq = juce::jlimit(20.0f, 20000.0f, xToFreq(e.position.x));
            float newGain = juce::jlimit(-maxDb, maxDb, yToGain(e.position.y) - globalThresh);
            
            auto* eParam = apvts.getParameter(prefix + "_ENABLE");
            auto* fParam = apvts.getParameter(prefix + "_FREQ");
            auto* gParam = apvts.getParameter(prefix + "_GAIN");
            auto* qParam = apvts.getParameter(prefix + "_Q");
            
            fParam->setValueNotifyingHost(fParam->convertTo0to1(newFreq));
            gParam->setValueNotifyingHost(gParam->convertTo0to1(newGain));
            qParam->setValueNotifyingHost(qParam->convertTo0to1(1.0f)); 
            eParam->setValueNotifyingHost(1.0f);
            
            draggingNode = i;
            dragStartFreq = newFreq;
            dragStartGain = newGain;
            repaint();
            return;
        }
    }
}

void EQGraphLED::mouseMove(const juce::MouseEvent& e)
{
    int lastHovered = hoveredNode;
    hoveredNode = -1;
    
    float globalThresh = *processor.apvts.getRawParameterValue("GLOBAL_THRESH");
    for (int i = 0; i < 8; ++i)
    {
        juce::String prefix = "BAND_" + juce::String(i);
        bool enabled = *processor.apvts.getRawParameterValue(prefix + "_ENABLE") > 0.5f;
        if (!enabled) continue;

        float freq = *processor.apvts.getRawParameterValue(prefix + "_FREQ");
        float gain = *processor.apvts.getRawParameterValue(prefix + "_GAIN");

        juce::Point<float> pos { freqToX(freq), gainToY(gain + globalThresh) };
        if (e.position.getDistanceFrom(pos) < 15.0f)
        {
            hoveredNode = i;
            break;
        }
    }
    if (hoveredNode != lastHovered)
        repaint();
}

void EQGraphLED::mouseDrag(const juce::MouseEvent& e)
{
    if (draggingNode < 0) return;
    
    const float sensitivity = e.mods.isShiftDown() ? 0.1f : 0.4f;
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
}

void EQGraphLED::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
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