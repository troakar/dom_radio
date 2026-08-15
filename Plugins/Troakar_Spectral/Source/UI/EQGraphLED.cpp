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
    for (int g = 0; g < 4; ++g) {
        juce::String prefix = "GRADIENT_" + juce::String(g);
        processor.apvts.addParameterListener(prefix + "_ENABLE", paramListener.get());
        processor.apvts.addParameterListener(prefix + "_CENTER_FREQ", paramListener.get());
        processor.apvts.addParameterListener(prefix + "_CENTER_GAIN", paramListener.get());
        processor.apvts.addParameterListener(prefix + "_BANDWIDTH", paramListener.get());
    }
    lastFFTMode = processor.getFFTModeIndex();
    updateViewRange(processor.getViewRangeIndex());
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
        for (int g = 0; g < 4; ++g) {
            juce::String prefix = "GRADIENT_" + juce::String(g);
            processor.apvts.removeParameterListener(prefix + "_ENABLE", paramListener.get());
            processor.apvts.removeParameterListener(prefix + "_CENTER_FREQ", paramListener.get());
            processor.apvts.removeParameterListener(prefix + "_CENTER_GAIN", paramListener.get());
            processor.apvts.removeParameterListener(prefix + "_BANDWIDTH", paramListener.get());
        }
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

    rebuildDisplayFrequencyGrid();
    targetPathDirty = true;
}

void EQGraphLED::rebuildDisplayFrequencyGrid()
{
    const int numPoints = juce::jmin(juce::jmax(64, getWidth() / 2), 400);
    cachedNumDisplayPoints = numPoints;
    displayFrequencies.resize(numPoints + 1);

    const double sr = juce::jmax(44100.0, processor.getSampleRate());
    const double nyquist = sr * 0.5;
    const double maxFreq = nyquist * 0.98;
    const double logMin = std::log10(20.0);
    const double logMax = std::log10(juce::jmax(20.0, maxFreq));

    for (int i = 0; i <= numPoints; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(numPoints);
        const double logFreq = logMin + t * (logMax - logMin);
        displayFrequencies[i] = static_cast<float>(std::pow(10.0, logFreq));
    }
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

        float gradThreshOffset = 0.0f;
        for (const auto& gp : gradientManager.points)
        {
            if (!gp.active) continue;

            float logDist = std::abs(std::log2(static_cast<float>(freq) / gp.centerFreqHz));
            float normalizedDist = logDist / gp.radiusOctaves;

            if (normalizedDist < 1.0f)
            {
                float weight = 0.5f + 0.5f * std::cos(normalizedDist * juce::MathConstants<float>::pi);
                gradThreshOffset += gp.centerGainDb * weight;
            }
        }

        float totalCurveDb = eqDb + globalThresh + gradThreshOffset; 
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
    const int numBins = fftSize / 2 + 1;
    const int maxBin = numBins - 2;

    float fBin = static_cast<float>(freq * static_cast<double>(fftSize) / sr);
    if (fBin < 1.0f) return arr[1].load(std::memory_order_relaxed);
    if (fBin > (float)maxBin) return arr[maxBin].load(std::memory_order_relaxed);

    int idx = static_cast<int>(fBin);
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
    if (!processor.isEngineSettled())
        return;

    const int w = getWidth();
    if (w <= 0) return;

    const int fftSize = processor.getCurrentFFTSize();
    const double sr = juce::jmax(44100.0, processor.getSampleRate());
    const int numBins = fftSize / 2 + 1;
    const double binWidthHz = sr / static_cast<double>(fftSize);
    const double nyquistHz = sr * 0.5;

    if (numBins < 2) return;

    // Проверка наличия сайдчейна
    bool hasSidechainSignal = false;
    for (int i = 1; i < numBins; ++i) {
        if (processor.sidechainData[i].load(std::memory_order_relaxed) > 1.0e-7f) {
            hasSidechainSignal = true;
            break;
        }
    }

    // =========================================================================
    // ЛОГАРИФМИЧЕСКАЯ СЕТКА ЧАСТОТ (кешируется в resized())
    // =========================================================================

    if (cachedNumDisplayPoints == 0)
        rebuildDisplayFrequencyGrid();

    const int numDisplayPoints = cachedNumDisplayPoints;
    const float* freqs = displayFrequencies.data();

    std::vector<float> mainDisplayDb(numDisplayPoints + 1, -100.0f);
    std::vector<float> scDisplayDb(numDisplayPoints + 1, -100.0f);

    // =========================================================================
    // ЧАСТОТНО-АДАПТИВНОЕ ОБЪЕДИНЕНИЕ FFT-БИНОВ С ВЗВЕШЕННЫМ МАКСИМУМОМ
    // =========================================================================

    for (int i = 0; i <= numDisplayPoints; ++i) {
        const double centerFreq = freqs[i];
        if (centerFreq <= 20.0 || centerFreq >= nyquistHz)
            continue;

        // Адаптивная ширина полосы в октавах:
        //   низ  → узко (~1/96 окт)  → почти каждый бин
        //   верх → широко (~1/12 окт) → широкое объединение
        const double freqNormOct = std::log2(centerFreq / 1000.0);
        const double bandwidthOctaves = juce::jlimit(
            1.0 / 96.0,
            1.0 / 12.0,
            (1.0 / 96.0) + (1.0 / 24.0) * std::abs(freqNormOct));

        const double radiusOctaves = bandwidthOctaves;
        const double freqLeft  = centerFreq * std::pow(2.0, -radiusOctaves);
        const double freqRight = centerFreq * std::pow(2.0,  radiusOctaves);

        const int binLeft = juce::jlimit(1, numBins - 1,
            static_cast<int>(std::floor(juce::jmax(20.0, freqLeft)  / binWidthHz)));
        const int binRight = juce::jlimit(1, numBins - 1,
            static_cast<int>(std::ceil(juce::jmax(20.0, freqRight)  / binWidthHz)));

        // Перцептуальная компенсация (та же, что в детекторе)
        const float perceptualTiltDb =
            3.0f * std::log2(std::max(20.0f, static_cast<float>(centerFreq)) / 1000.0f);

        // Гауссов вес в логарифмическом пространстве
        const double logCenterFreq = std::log10(centerFreq);
        const double logFreqLeft   = std::log10(juce::jmax(freqLeft,  20.0));
        const double logFreqRight  = std::log10(juce::jmax(freqRight, 20.0));
        const double logBandwidth  = logFreqRight - logFreqLeft;
        const double weightSigma   = logBandwidth * 0.35;

        float maxMainMag = 0.0f;
        float maxScMag   = 0.0f;
        float weightSum  = 0.0f;

        for (int bin = binLeft; bin <= binRight; ++bin) {
            const double binFreq = bin * binWidthHz;
            const double logBinFreq = std::log10(juce::jmax(binFreq, 20.0));
            const double distFromCenter = std::abs(logBinFreq - logCenterFreq);

            const double weight = (weightSigma > 1.0e-9)
                ? std::exp(-0.5 * distFromCenter * distFromCenter / (weightSigma * weightSigma))
                : 1.0;

            const float w = static_cast<float>(weight);

            const float mainMag =
                processor.spectrumDataLeft[bin].load(std::memory_order_relaxed);

            if (mainMag > 0.0f) {
                maxMainMag = std::max(maxMainMag, mainMag * w);
                weightSum  += w;
            }

            if (hasSidechainSignal) {
                const float scMag =
                    processor.sidechainData[bin].load(std::memory_order_relaxed);
                if (scMag > 0.0f)
                    maxScMag = std::max(maxScMag, scMag * w);
            }
        }

        // Нормализация взвешенного максимума (коррекция на сжатие диапазона весов)
        if (weightSum > 0.1f) {
            const float normFactor = std::min(1.5f, 1.0f / weightSum);
            maxMainMag *= normFactor;
            maxScMag   *= normFactor;
        }

        mainDisplayDb[i] = (maxMainMag > 1.0e-7f)
            ? juce::Decibels::gainToDecibels(maxMainMag) + perceptualTiltDb
            : -100.0f;

        if (hasSidechainSignal)
            scDisplayDb[i] = (maxScMag > 1.0e-7f)
                ? juce::Decibels::gainToDecibels(maxScMag) + perceptualTiltDb
                : -100.0f;
    }

    // =========================================================================
    // ДОПОЛНИТЕЛЬНОЕ СГЛАЖИВАНИЕ КРИВОЙ (скользящее среднее)
    // =========================================================================

    const int smoothRadius = 2;
    const float smoothStrength = 0.5f; // 1.0 = полное сглаживание, 0.5 = вдвое слабее

    std::vector<float> smoothedMain(numDisplayPoints + 1);
    std::vector<float> smoothedSc(numDisplayPoints + 1);

    for (int i = 0; i <= numDisplayPoints; ++i) {
        float sumMain = 0.0f, sumSc = 0.0f, count = 0.0f;

        const int lo = juce::jmax(0, i - smoothRadius);
        const int hi = juce::jmin(numDisplayPoints, i + smoothRadius);

        for (int k = lo; k <= hi; ++k) {
            if (mainDisplayDb[k] > -99.0f) {
                sumMain += mainDisplayDb[k];
                if (hasSidechainSignal && scDisplayDb[k] > -99.0f)
                    sumSc += scDisplayDb[k];
                count += 1.0f;
            }
        }

        if (count > 0.0f) {
            const float averagedMain = sumMain / count;
            smoothedMain[i] = mainDisplayDb[i] + (averagedMain - mainDisplayDb[i]) * smoothStrength;

            if (hasSidechainSignal) {
                const float averagedSc = sumSc / count;
                smoothedSc[i] = scDisplayDb[i] + (averagedSc - scDisplayDb[i]) * smoothStrength;
            }
        } else {
            smoothedMain[i] = mainDisplayDb[i];
            smoothedSc[i]   = scDisplayDb[i];
        }
    }

    // =========================================================================
    // ПОСТРОЕНИЕ ПЛАВНОЙ КРИВОЙ ЧЕРЕЗ ЛОГАРИФМИЧЕСКИЕ ТОЧКИ
    // =========================================================================

    cachedSpecPath.clear();
    cachedScPath.clear();

    bool firstPoint = true;
    for (int i = 0; i <= numDisplayPoints; ++i) {
        const float x = freqToX(freqs[i]);
        const float yMain = juce::jlimit(0.0f, bounds.getBottom(),
                                         gainToY(smoothedMain[i]));
        if (firstPoint) {
            cachedSpecPath.startNewSubPath(x, yMain);
            firstPoint = false;
        } else {
            cachedSpecPath.lineTo(x, yMain);
        }
    }

    // Отрисовка Main Spectrum
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
        bool firstScPoint = true;
        for (int i = 0; i <= numDisplayPoints; ++i) {
            const float x = freqToX(freqs[i]);
            const float ySc = juce::jlimit(0.0f, bounds.getBottom(),
                                           gainToY(smoothedSc[i]));
            if (firstScPoint) {
                cachedScPath.startNewSubPath(x, ySc);
                firstScPoint = false;
            } else {
                cachedScPath.lineTo(x, ySc);
            }
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

    // 2. ВОССТАНОВЛЕНИЕ: Отрисовка узлов обычного эквалайзера (1-8) - ПРЯМО НА КРИВОЙ
    for (int i = 0; i < 8; ++i)
    {
        juce::String prefix = "BAND_" + juce::String(i);
        bool enabled = *processor.apvts.getRawParameterValue(prefix + "_ENABLE") > 0.5f;
        
        if (!enabled && draggingNode != i) continue;

        float freq = *processor.apvts.getRawParameterValue(prefix + "_FREQ");
        float q    = *processor.apvts.getRawParameterValue(prefix + "_Q");

        float nodeTotalDb = getTotalTargetDbAtFreq(freq);
        juce::Point<float> pos { freqToX(freq), gainToY(nodeTotalDb) };

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

    // 3. Отрисовка маркеров градиентов (G0, G1...) поверх всего остального - ПРЯМО НА КРИВОЙ
    for (const auto& gp : processor.gradientManager.points)
    {
        if (!gp.active) continue;
        
        float gradTotalDb = getTotalTargetDbAtFreq(gp.centerFreqHz);
        juce::Point<float> center { freqToX(gp.centerFreqHz), gainToY(gradTotalDb) };
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
            tooltipGain = getTotalTargetDbAtFreq(gp->centerFreqHz);
            tooltipPos = { freqToX(tooltipFreq), gainToY(tooltipGain) };
            tooltipColor = gp->color;
            showTooltip = true;
        }
    } 
    else if (draggingNode >= 0 || hoveredNode >= 0) {
        int id = draggingNode >= 0 ? draggingNode : hoveredNode;
        juce::String prefix = "BAND_" + juce::String(id);
        tooltipFreq = *processor.apvts.getRawParameterValue(prefix + "_FREQ");
        tooltipGain = getTotalTargetDbAtFreq(tooltipFreq);
        tooltipPos = { freqToX(tooltipFreq), gainToY(tooltipGain) };
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
        float gradTotalDb = getTotalTargetDbAtFreq(gp.centerFreqHz);
        juce::Point<float> gpPos { 
            freqToX(gp.centerFreqHz), 
            gainToY(gradTotalDb) 
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

        float nodeTotalDb = getTotalTargetDbAtFreq(freq);
        const juce::Point<float> pos { freqToX(freq), gainToY(nodeTotalDb) };

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

        float nodeTotalDb = getTotalTargetDbAtFreq(freq);
        const juce::Point<float> pos { freqToX(freq), gainToY(nodeTotalDb) };

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
        float gradTotalDb = getTotalTargetDbAtFreq(gp.centerFreqHz);
        const juce::Point<float> gpPos { 
            freqToX(gp.centerFreqHz), 
            gainToY(gradTotalDb) 
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
    int lastHovered = hoveredNode;
    hoveredNode = -1;
    hoveredGradientId = -1;

    bool anyClose = false;

    for (const auto& gp : gradientManager.points)
    {
        float gradTotalDb = getTotalTargetDbAtFreq(gp.centerFreqHz);
        juce::Point<float> gpPos { freqToX(gp.centerFreqHz), gainToY(gradTotalDb) };
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
            float nodeTotalDb = getTotalTargetDbAtFreq(freq);
            juce::Point<float> pos { freqToX(freq), gainToY(nodeTotalDb) };
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

    for (auto& gp : gradientManager.points)
    {
        float gradTotalDb = getTotalTargetDbAtFreq(gp.centerFreqHz);
        juce::Point<float> gpPos { freqToX(gp.centerFreqHz), gainToY(gradTotalDb) };
        
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