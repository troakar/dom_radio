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
    
    globalThreshSlider.setRange(-48.0, 12.0, 0.1);
    globalThreshSlider.setValue(0.0, juce::dontSendNotification);
    addAndMakeVisible(&globalThreshSlider);

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
    processor.apvts.addParameterListener("FFT_MODE", paramListener.get());
    processor.apvts.addParameterListener("VIEW_RANGE", paramListener.get());
    lastFFTMode = static_cast<int>(*processor.apvts.getRawParameterValue("FFT_MODE"));
    updateViewRange(static_cast<int>(*processor.apvts.getRawParameterValue("VIEW_RANGE")));
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
        processor.apvts.removeParameterListener("FFT_MODE", paramListener.get());
        processor.apvts.removeParameterListener("VIEW_RANGE", paramListener.get());
    }
}
float EQGraphLED::freqToX(float f) const  { return std::log10(juce::jlimit(20.0f, 20000.0f, f) / 20.0f) / 3.0f * (float)getWidth(); }
float EQGraphLED::xToFreq(float x) const  { return 20.0f * std::pow(10.0f, 3.0f * x / (float)getWidth()); }
float EQGraphLED::gainToY(float dB) const 
{ 
    float norm = (dB - minDb) / (maxDb - minDb);
    norm = juce::jlimit(0.0f, 1.0f, norm);
    float topMargin = (float)getHeight() * 0.08f;
    float bottomMargin = (float)getHeight() * 0.92f;
    return bottomMargin - norm * (bottomMargin - topMargin);
}

float EQGraphLED::yToGain(float y) const  
{ 
    float topMargin = (float)getHeight() * 0.08f;
    float bottomMargin = (float)getHeight() * 0.92f;
    float norm = (bottomMargin - y) / (bottomMargin - topMargin);
    return minDb + norm * (maxDb - minDb);
}

void EQGraphLED::resized()
{
    auto bounds = getLocalBounds();
    
    float yTop = gainToY(maxDb);
    float yBottom = gainToY(minDb);
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

void EQGraphLED::cycleViewRange(int direction)
{
    static const float depths[] = { 24.0f, 48.0f, 72.0f, 96.0f, 120.0f };
    static const int numDepths = 5;

    int currentIndex = 2;
    float minDiff = 1000.0f;
    for (int i = 0; i < numDepths; ++i)
    {
        float diff = std::abs(depths[i] - baseViewDepth);
        if (diff < minDiff) { minDiff = diff; currentIndex = i; }
    }

    int newIndex = juce::jlimit(0, numDepths - 1, currentIndex + direction);

    if (newIndex != currentIndex)
    {
        if (auto* rangeParam = processor.apvts.getParameter("VIEW_RANGE"))
        {
            float normalizedValue = static_cast<float>(newIndex) / static_cast<float>(numDepths - 1);
            rangeParam->setValueNotifyingHost(normalizedValue);
        }
        
        zoomIndicatorAlpha = 1.0f;
    }
}

void EQGraphLED::updateViewRange(int rangeIndex)
{
    switch (rangeIndex)
    {
        case 0: baseViewDepth = 24.0f;  break;
        case 1: baseViewDepth = 48.0f;  break;
        case 2: baseViewDepth = 72.0f;  break;
        case 3: baseViewDepth = 96.0f;  break;
        case 4: baseViewDepth = 120.0f; break;
        default: baseViewDepth = 96.0f; break;
    }
    
    viewportOffset = 0.0f;
    maxDb = 12.0f;
    minDb = maxDb - baseViewDepth;
    
    backgroundCache = juce::Image();
    repaint();
}

void EQGraphLED::updateViewportFollow(float /*currentThreshold*/)
{
    float targetMaxDb = 12.0f;
    
    if (std::abs(maxDb - targetMaxDb) > 0.01f || std::abs(minDb - (targetMaxDb - baseViewDepth)) > 0.01f)
    {
        maxDb = targetMaxDb;
        minDb = maxDb - baseViewDepth;
        backgroundCache = juce::Image();
    }
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

    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));

    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        float x = freqToX(f);
        
        g.setColour(phosphor.withAlpha(0.08f));
        g.drawVerticalLine((int)x, 0.0f, b.getBottom());

        g.setColour(phosphor.withAlpha(0.4f));
        juce::String fText = (f >= 1000.0f) ? juce::String(static_cast<int>(f / 1000.0f)) + "k" 
                                            : juce::String(static_cast<int>(f));
        g.drawText(fText, (int)x - 20, (int)b.getBottom() - 16, 40, 14, juce::Justification::centredBottom);
    }
    
    float dbRange = maxDb - minDb;
    float gridStep = (dbRange <= 24.0f) ? 6.0f : 12.0f;
    
    for (float db = std::ceil(minDb / gridStep) * gridStep; db <= maxDb; db += gridStep)
    {
        float y = gainToY(db);
        bool isZero = std::abs(db) < 0.1f;
        g.setColour(phosphor.withAlpha(isZero ? 0.25f : 0.06f));
        g.drawHorizontalLine((int)y, 0.0f, b.getRight());
        
        if (isZero || std::abs(db - std::round(db / gridStep) * gridStep) < 0.1f)
        {
            g.setColour(phosphor.withAlpha(isZero ? 0.7f : 0.5f));
            g.setFont(juce::FontOptions(isZero ? 10.0f : 9.0f, juce::Font::bold));
            
            juce::String label;
            if (isZero) label = "0";
            else if (db > 0.0f) label = "+" + juce::String((int)db);
            else label = juce::String((int)db);

            g.drawText(label, 5, (int)y - 6, 35, 12, juce::Justification::centredLeft);
        }
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

    float globalThresh = *processor.apvts.getRawParameterValue("GLOBAL_THRESH");

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

        float eqDb = static_cast<float>(10.0 * std::log10(std::max(1.0e-15, totalMagSq)));
        
        float totalCurveDb = eqDb + globalThresh; 
        targetDbPerPixel[x] = totalCurveDb;

        if (x % 4 == 0 || x == w)
        {
            float y = juce::jlimit(3.0f, static_cast<float>(getHeight()) - 3.0f, gainToY(totalCurveDb));
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
    cachedUpLine.preallocateSpace(w / 2 + 10);
    cachedDownLine.preallocateSpace(w / 2 + 10);

    for (int x = 0; x <= w; x += 2)
    {
        double freq = freqPerPixel[x];
        float targetDb = targetDbPerPixel[x];
        
        float rawDelta = getInterpolatedArray(processor.compressionDeltaData, freq, sr);

        float upDeltaDb = std::max(0.0f, rawDelta);
        float downDeltaDb = std::min(0.0f, rawDelta);

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

    for (int x = w; x >= 0; x -= 2) {
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
    const int maxBin = (fftSize / 2) - 2;

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
    if (w <= 0) return;

    const int fftSize = processor.getCurrentFFTSize();
    const double sr = juce::jmax(44100.0, processor.getSampleRate());
    const int numBins = fftSize / 2;
    const double binWidthHz = sr / static_cast<double>(fftSize);
    const double nyquistHz = sr * 0.5;

    // Проверяем наличие сайдчейна
    bool hasSidechainSignal = false;
    for (int i = 0; i < std::min(numBins, 60); ++i) {
        if (processor.sidechainData[i].load(std::memory_order_relaxed) > 1.0e-4f) {
            hasSidechainSignal = true;
            break;
        }
    }

    // =========================================================================
    // ТОЧНЫЙ ПИК-ПУЛИНГ ПО ПИКСЕЛЯМ ЭКРАНА
    // =========================================================================
    std::vector<float> mainCurveDb(w + 1, -100.0f);
    std::vector<float> scCurveDb(w + 1, -100.0f);

    for (int x = 0; x <= w; ++x) {
        const double centerFreq = xToFreq(static_cast<float>(x));
        if (centerFreq <= 20.0 || centerFreq >= nyquistHz) continue;

        // Определяем границы физического пикселя в герцах
        const double freqLeft  = xToFreq(std::max(0.0f, static_cast<float>(x) - 0.5f));
        const double freqRight = xToFreq(std::min(static_cast<float>(w), static_cast<float>(x) + 0.5f));

        // Перцептуальная компенсация — ТА ЖЕ, что и в детекторе
        const float perceptualTiltDb = 3.0f * std::log2(std::max(20.0f, static_cast<float>(centerFreq)) / 1000.0f);

        // Если в пиксель помещается несколько бинов — берём максимум
        if ((freqRight - freqLeft) > binWidthHz) {
            int firstBin = juce::jlimit(1, numBins - 1, static_cast<int>(std::floor(freqLeft / binWidthHz)));
            int lastBin  = juce::jlimit(1, numBins - 1, static_cast<int>(std::ceil(freqRight / binWidthHz)));

            float maxMain = -100.0f;
            float maxSc   = -100.0f;

            for (int b = firstBin; b <= lastBin; ++b) {
                float rawMag = processor.spectrumDataLeft[b].load(std::memory_order_relaxed);
                if (rawMag > 1.0e-7f) {
                    float db = juce::Decibels::gainToDecibels(rawMag) + perceptualTiltDb;
                    maxMain = std::max(maxMain, db);
                }
                if (hasSidechainSignal) {
                    float scMag = processor.sidechainData[b].load(std::memory_order_relaxed);
                    if (scMag > 1.0e-7f) {
                        float scDb = juce::Decibels::gainToDecibels(scMag) + perceptualTiltDb;
                        maxSc = std::max(maxSc, scDb);
                    }
                }
            }

            mainCurveDb[x] = maxMain;
            if (hasSidechainSignal) scCurveDb[x] = maxSc;
        } else {
            // Если бины шире пикселя (зона НЧ) — линейная интерполяция
            const double binPos = juce::jmax(1.0, centerFreq / binWidthHz);
            int idx = juce::jlimit(1, numBins - 2, static_cast<int>(std::floor(binPos)));
            float frac = static_cast<float>(binPos - idx);

            float aMag = processor.spectrumDataLeft[idx].load(std::memory_order_relaxed);
            float bMag = processor.spectrumDataLeft[idx + 1].load(std::memory_order_relaxed);

            float interpMag = aMag + frac * (bMag - aMag);
            mainCurveDb[x] = (interpMag > 1.0e-7f)
                ? juce::Decibels::gainToDecibels(interpMag) + perceptualTiltDb
                : -100.0f;

            if (hasSidechainSignal) {
                float aSc = processor.sidechainData[idx].load(std::memory_order_relaxed);
                float bSc = processor.sidechainData[idx + 1].load(std::memory_order_relaxed);
                float interpSc = aSc + frac * (bSc - aSc);
                scCurveDb[x] = (interpSc > 1.0e-7f)
                    ? juce::Decibels::gainToDecibels(interpSc) + perceptualTiltDb
                    : -100.0f;
            }
        }
    }

    // Заполняем края, чтобы график не уходил в бездну
    int firstValid = -1, lastValid = -1;
    for (int x = 0; x <= w; ++x) {
        if (mainCurveDb[x] > -99.0f) {
            if (firstValid == -1) firstValid = x;
            lastValid = x;
        }
    }
    if (firstValid > 0) {
        for (int x = 0; x < firstValid; ++x) {
            mainCurveDb[x] = mainCurveDb[firstValid];
            if (hasSidechainSignal) scCurveDb[x] = scCurveDb[firstValid];
        }
    }
    if (lastValid != -1 && lastValid < w) {
        for (int x = lastValid + 1; x <= w; ++x) {
            mainCurveDb[x] = mainCurveDb[lastValid];
            if (hasSidechainSignal) scCurveDb[x] = scCurveDb[lastValid];
        }
    }

    // Отрисовка Main Spectrum
    cachedSpecPath.clear();
    cachedSpecPath.startNewSubPath(0.0f, juce::jlimit(0.0f, bounds.getBottom(), gainToY(mainCurveDb[0])));
    for (int x = 1; x <= w; ++x) {
        cachedSpecPath.lineTo(static_cast<float>(x), juce::jlimit(0.0f, bounds.getBottom(), gainToY(mainCurveDb[x])));
    }
    
    if (!cachedSpecPath.isEmpty()) {
        juce::Path fillPath(cachedSpecPath);
        fillPath.lineTo(bounds.getRight(), bounds.getBottom());
        fillPath.lineTo(0.0f, bounds.getBottom());
        fillPath.closeSubPath();

        juce::ColourGradient fillGrad(phosphor.withAlpha(0.45f), 0.0f, bounds.getY() + bounds.getHeight() * 0.15f,
                                      phosphor.withAlpha(0.0f),  0.0f, bounds.getBottom(), false);
        g.setGradientFill(fillGrad);
        g.fillPath(fillPath);

        g.setColour(phosphor.withAlpha(0.85f));
        g.strokePath(cachedSpecPath, juce::PathStrokeType(1.5f));
    }

    // Отрисовка Sidechain Spectrum
    if (hasSidechainSignal) {
        cachedScPath.clear();
        cachedScPath.startNewSubPath(0.0f, juce::jlimit(0.0f, bounds.getBottom(), gainToY(scCurveDb[0])));
        for (int x = 1; x <= w; ++x) {
            cachedScPath.lineTo(static_cast<float>(x), juce::jlimit(0.0f, bounds.getBottom(), gainToY(scCurveDb[x])));
        }
        g.setColour(juce::Colour::fromRGB(40, 200, 255).withAlpha(0.6f));
        g.strokePath(cachedScPath, juce::PathStrokeType(1.2f));
    }
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

    if (zoomIndicatorAlpha > 0.01f)
    {
        juce::String zoomText = juce::String((int)baseViewDepth) + " dB VIEW";
        
        g.setColour(juce::Colour::fromRGB(255, 176, 40).withAlpha(zoomIndicatorAlpha * 0.9f));
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        
        auto textBounds = bounds.withSizeKeepingCentre(150.0f, 30.0f).withY(bounds.getY() + 10.0f);
        
        g.setColour(juce::Colour::fromRGB(20, 18, 15).withAlpha(zoomIndicatorAlpha * 0.8f));
        g.fillRoundedRectangle(textBounds, 4.0f);
        
        g.setColour(juce::Colour::fromRGB(255, 176, 40).withAlpha(zoomIndicatorAlpha));
        g.drawText(zoomText, textBounds, juce::Justification::centred);
        
        zoomIndicatorAlpha *= 0.92f;
    }

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
    float globalThreshPaint = *processor.apvts.getRawParameterValue("GLOBAL_THRESH");
    for (int i = 0; i < 8; ++i)
    {
        juce::String prefix = "BAND_" + juce::String(i);
        bool enabled = *processor.apvts.getRawParameterValue(prefix + "_ENABLE") > 0.5f;
        
        if (!enabled && draggingNode != i) continue;

        float freq = *processor.apvts.getRawParameterValue(prefix + "_FREQ");
        float gain = *processor.apvts.getRawParameterValue(prefix + "_GAIN");
        float q = *processor.apvts.getRawParameterValue(prefix + "_Q");

        juce::Point<float> pos { freqToX(freq), gainToY(gain + globalThreshPaint) };

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
        
        juce::Point<float> center { freqToX(gp.centerFreqHz), gainToY(gp.centerGainDb + globalThreshPaint) };
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

    float tooltipFreq = -1.0f;
    float tooltipGain = 0.0f;
    juce::Point<float> tooltipPos;
    bool showTooltip = false;
    juce::Colour tooltipColor = juce::Colours::white;

    if (draggingGradientId >= 0 || hoveredGradientId >= 0) {
        int id = draggingGradientId >= 0 ? draggingGradientId : hoveredGradientId;
        if (auto* gp = processor.gradientManager.getPoint(id)) {
            tooltipFreq = gp->centerFreqHz;
            tooltipGain = gp->centerGainDb;
            tooltipPos = { freqToX(tooltipFreq), gainToY(tooltipGain + globalThreshPaint) };
            tooltipColor = gp->color;
            showTooltip = true;
        }
    } 
    else if (draggingNode >= 0 || hoveredNode >= 0) {
        int id = draggingNode >= 0 ? draggingNode : hoveredNode;
        juce::String prefix = "BAND_" + juce::String(id);
        tooltipFreq = *processor.apvts.getRawParameterValue(prefix + "_FREQ");
        tooltipGain = *processor.apvts.getRawParameterValue(prefix + "_GAIN");
        tooltipPos = { freqToX(tooltipFreq), gainToY(tooltipGain + globalThreshPaint) };
        tooltipColor = phosphor;
        showTooltip = true;
    }

    if (showTooltip) {
        juce::String fStr = tooltipFreq >= 1000.0f ? juce::String(tooltipFreq / 1000.0f, 2) + " kHz" 
                                                   : juce::String(std::round(tooltipFreq)) + " Hz";
        juce::String gStr = juce::String(tooltipGain, 1) + " dB";
        juce::String text = fStr + " | " + gStr;

        int textW = 100;
        int textH = 22;
        juce::Rectangle<float> badge(tooltipPos.x - textW / 2.0f, tooltipPos.y - 40.0f, (float)textW, (float)textH);

        if (badge.getY() < 5.0f) badge.setY(tooltipPos.y + 15.0f);
        if (badge.getX() < 5.0f) badge.setX(5.0f);
        if (badge.getRight() > bounds.getRight() - 5.0f) badge.setX(bounds.getRight() - textW - 5.0f);

        g.setColour(juce::Colour::fromRGB(20, 18, 15).withAlpha(0.95f));
        g.fillRoundedRectangle(badge, 4.0f);
        
        g.setColour(tooltipColor.withAlpha(0.7f));
        g.drawRoundedRectangle(badge, 4.0f, 1.2f);

        g.setColour(juce::Colours::white.withAlpha(0.95f));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(text, badge, juce::Justification::centred, false);
    }

    RetroFX::drawVignette(g, bounds, 0.4f);
    RetroFX::drawGlassHighlight(g, bounds);
}

void EQGraphLED::mouseDown(const juce::MouseEvent& e)
{
    float globalThresh = *processor.apvts.getRawParameterValue("GLOBAL_THRESH");

    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Add Gradient Here", true, gradientManager.points.size() < 4);

        menu.showMenuAsync(juce::PopupMenu::Options(),
            [this, e, globalThresh](int result)
            {
                if (result == 1)
                {
                    const float newFreq = juce::jlimit(20.0f, 20000.0f, xToFreq(e.position.x));
                    const float newGain = juce::jlimit(-60.0f, 60.0f, yToGain(e.position.y) - globalThresh);

                    const int newId = gradientManager.addPoint(newFreq, newGain);
                    if (newId >= 0)
                    {
                        gradientManager.setActivePoint(newId);
                        if (onGradientSelectionChanged) onGradientSelectionChanged();
                        if (onGradientParamsChanged) onGradientParamsChanged();
                        repaint();
                    }
                }
            });
        return;
    }

    bool clickedGradient = false;
    for (const auto& gp : gradientManager.points)
    {
        juce::Point<float> gpPos { 
            freqToX(gp.centerFreqHz), 
            gainToY(gp.centerGainDb + globalThresh) 
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

        const juce::Point<float> pos { freqToX(freq), gainToY(gain + globalThresh) };

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
            juce::jlimit(-60.0f, 60.0f, yToGain(e.position.y) - globalThresh);

        createEQBandAt(newFreq, newGain);
        repaint();
    }
}

void EQGraphLED::mouseDoubleClick(const juce::MouseEvent& e)
{
    auto& apvts = processor.apvts;
    float globalThresh = *apvts.getRawParameterValue("GLOBAL_THRESH");

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

        const juce::Point<float> pos { freqToX(freq), gainToY(gain + globalThresh) };

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
            gainToY(gp.centerGainDb + globalThresh) 
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
        juce::jlimit(-60.0f, 60.0f, yToGain(e.position.y) - globalThresh);

    createEQBandAt(newFreq, newGain);
    repaint();
}

void EQGraphLED::mouseMove(const juce::MouseEvent& e)
{
    float globalThresh = *processor.apvts.getRawParameterValue("GLOBAL_THRESH");
    int lastHovered = hoveredNode;
    hoveredNode = -1;
    hoveredGradientId = -1;

    bool anyClose = false;

    for (const auto& gp : gradientManager.points)
    {
        juce::Point<float> gpPos { freqToX(gp.centerFreqHz), gainToY(gp.centerGainDb + globalThresh) };
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

            juce::Point<float> pos { freqToX(freq), gainToY(gain + globalThresh) };
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
    float globalThresh = *processor.apvts.getRawParameterValue("GLOBAL_THRESH");

    if (draggingGradientId >= 0)
    {
        auto* gp = gradientManager.getPoint(draggingGradientId);
        if (gp)
        {
            float newFreq = juce::jlimit(20.0f, 20000.0f, xToFreq(e.position.x));
            float newGain = juce::jlimit(-60.0f, 60.0f, yToGain(e.position.y) - globalThresh);

            gp->centerFreqHz = newFreq;
            gp->centerGainDb = newGain;
            
            juce::String prefix = "GRADIENT_" + juce::String(draggingGradientId);
            if (auto* fParam = processor.apvts.getParameter(prefix + "_CENTER_FREQ"))
                fParam->setValueNotifyingHost(fParam->convertTo0to1(newFreq));
            if (auto* gParam = processor.apvts.getParameter(prefix + "_CENTER_GAIN"))
                gParam->setValueNotifyingHost(gParam->convertTo0to1(newGain));

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

    const float gainDelta = -(deltaY * (maxDb - minDb)) / ((float)getHeight() * 0.84f);
    const float newGain = juce::jlimit(-60.0f, 60.0f, dragStartGain + gainDelta);

    juce::String prefix = "BAND_" + juce::String(draggingNode);
    auto* fParam = processor.apvts.getParameter(prefix + "_FREQ");
    auto* gParam = processor.apvts.getParameter(prefix + "_GAIN");

    fParam->setValueNotifyingHost(fParam->convertTo0to1(newFreq));
    gParam->setValueNotifyingHost(gParam->convertTo0to1(newGain));

    targetPathDirty = true;
}

void EQGraphLED::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown())
    {
        int direction = (wheel.deltaY > 0.0f) ? -1 : 1;
        cycleViewRange(direction);
        return;
    }

    float globalThresh = *processor.apvts.getRawParameterValue("GLOBAL_THRESH");

    for (auto& gp : gradientManager.points)
    {
        juce::Point<float> gpPos { freqToX(gp.centerFreqHz), gainToY(gp.centerGainDb + globalThresh) };
        
        bool hoverDot = e.position.getDistanceFrom(gpPos) < 25.0f;
        bool hoverSpan = gp.isSelected && std::abs(e.position.x - freqToX(gp.centerFreqHz)) < (getWidth() * 0.1f * gp.radiusOctaves);
        
        if (hoverDot || hoverSpan)
        {
            float factor = std::exp(wheel.deltaY * 0.4f);
            float newOctaves = juce::jlimit(0.3f, 4.0f, gp.radiusOctaves * factor);
            gp.radiusOctaves = newOctaves;
            
            juce::String prefix = "GRADIENT_" + juce::String(gp.id);
            if (auto* bwParam = processor.apvts.getParameter(prefix + "_BANDWIDTH"))
                bwParam->setValueNotifyingHost(bwParam->convertTo0to1(newOctaves));

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