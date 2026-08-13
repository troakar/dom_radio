#pragma once
#include <JuceHeader.h>
#include <vector>
#include <atomic>
#include <cmath>
#include "UI/GradientBandModel.h"

constexpr int FFT_ORDER  = 9;                // 512 сэмплов (11 мс)
constexpr int FFT_SIZE   = 1 << FFT_ORDER;   // 512
constexpr int HOP_SIZE   = FFT_SIZE / 4;     // 75% overlap

inline float fast_tanh_sc(float x) noexcept
{
    if (x >  3.0f) return  1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2  = x * x;
    const float num = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float den = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return num / (den + 1.0e-9f);
}

// Легковесный стек-фильтр RBJ Biquad (0% алокаций памяти)
struct FastBiquadParams {
    float c0 = 1.0f, c1 = 0.0f, c2 = 0.0f;
    float d0 = 1.0f, d1 = 0.0f, d2 = 0.0f;

    void setPeak(float sr, float f0, float q, float gainDb) {
        if (std::abs(gainDb) < 0.01f) {
            c0 = 1.0f; c1 = 0.0f; c2 = 0.0f;
            d0 = 1.0f; d1 = 0.0f; d2 = 0.0f;
            return;
        }
        float A = std::pow(10.0f, gainDb / 40.0f);
        float w0 = 2.0f * juce::MathConstants<float>::pi * f0 / sr;
        float alpha = std::sin(w0) / (2.0f * std::max(0.05f, q));
        float cosw0 = std::cos(w0);

        float a0_inv = 1.0f / (1.0f + alpha / A);

        float b0 = (1.0f + alpha * A) * a0_inv;
        float b1 = (-2.0f * cosw0) * a0_inv;
        float b2 = (1.0f - alpha * A) * a0_inv;
        float a1 = (-2.0f * cosw0) * a0_inv;
        float a2 = (1.0f - alpha / A) * a0_inv;

        c0 = b0*b0 + b1*b1 + b2*b2;
        c1 = 2.0f * (b0*b1 + b1*b2);
        c2 = 2.0f * b0*b2;

        d0 = 1.0f + a1*a1 + a2*a2;
        d1 = 2.0f * (a1 + a1*a2);
        d2 = 2.0f * a2;
    }

    inline float getMagSq(float cw, float c2w) const {
        float num = c0 + c1 * cw + c2 * c2w;
        float den = d0 + d1 * cw + d2 * c2w;
        return num / std::max(1.0e-9f, den);
    }
};

class SpectralEngine
{
public:
    SpectralEngine()
        : forwardFFT(FFT_ORDER),
          inverseFFT(FFT_ORDER),
          window(FFT_SIZE, juce::dsp::WindowingFunction<float>::hann)
    {}

    void prepare(double sampleRate)
    {
        sr = sampleRate;

        inputFifo .assign(2, std::vector<float>(FFT_SIZE, 0.0f));
        outputFifo.assign(2, std::vector<float>(FFT_SIZE, 0.0f));
        fifoIndex = 0;
        hopCounter = 0;

        fftWorkBuf.assign(2, std::vector<float>(FFT_SIZE * 2, 0.0f));

        const int numBins = FFT_SIZE / 2;
        peakEnvBin.assign(numBins, 0.0f);
        rmsEnvBin.assign(numBins, 0.0f);
        bandGainsBin.assign(numBins, 1.0f);
        rawDeltaDb.assign(numBins, 0.0f);
        binTargetDb.assign(numBins, 0.0f);

        // Буферы градиентов
        binAmountNorm.assign(numBins, 0.0f);
        binUpMaxDb.assign(numBins, 0.0f);
        binDownMaxDb.assign(numBins, 0.0f);
        binWeightSum.assign(numBins, 0.0f);
        gradientBandsActive = false;

        // Таблицы предрасчета косинусов для устранения тригонометрии в цикле
        cosW.resize(numBins);
        cos2W.resize(numBins);
        for (int bin = 1; bin < numBins; ++bin) {
            float w = 2.0f * juce::MathConstants<float>::pi * static_cast<float>(bin) / static_cast<float>(FFT_SIZE);
            cosW[bin] = std::cos(w);
            cos2W[bin] = std::cos(2.0f * w);
        }

        hannWindow.resize(FFT_SIZE);
        window.fillWindowingTables(hannWindow.data(), FFT_SIZE,
                                   juce::dsp::WindowingFunction<float>::hann);

        olaGain = 1.0f / 1.5f;
        targetDirty = true;
    }

    // ВЫНЕСЕНО В PUBLIC СЕКЦИЮ ДЛЯ ДОСТУПА ИЗ PluginProcessor
    void refreshGradientBands(const std::vector<GradientBand>& bands, double sampleRate)
    {
        const int numBins = FFT_SIZE / 2;
        binAmountNorm.assign(numBins, 0.0f);
        binUpMaxDb.assign(numBins, 0.0f);
        binDownMaxDb.assign(numBins, 0.0f);
        binWeightSum.assign(numBins, 0.0f);
        gradientBandsActive = false;

        for (const auto& band : bands)
        {
            if (!band.enabled) continue;

            float sigma = band.bandwidthOctaves * 0.6f;
            float amountNorm = juce::jlimit(0.0f, 2.0f, band.amountPct / 100.0f);

            for (int bin = 1; bin < numBins; ++bin)
            {
                double freq = (double)bin * sampleRate / (double)FFT_SIZE;
                if (freq < 20.0 || freq > 20000.0) continue;

                double logDist = std::log2(freq / band.centerFreqHz);
                float weight = std::exp(-0.5f * (float)((logDist / sigma) * (logDist / sigma)));
                weight = juce::jlimit(0.0f, 1.0f, weight);

                binAmountNorm[bin] += amountNorm * weight;
                binUpMaxDb[bin] += band.upMaxDb * weight;
                binDownMaxDb[bin] += band.downMaxDb * weight;
                binWeightSum[bin] += weight;
            }
        }

        for (int bin = 1; bin < numBins; ++bin)
        {
            if (binWeightSum[bin] > 0.001f)
            {
                gradientBandsActive = true;
                float invW = 1.0f / binWeightSum[bin];
                binAmountNorm[bin] *= invW;
                binUpMaxDb[bin] *= invW;
                binDownMaxDb[bin] *= invW;
            }
        }
    }

    void process(juce::AudioBuffer<float>& buffer,
                 juce::AudioProcessorValueTreeState& apvts,
                 float upMaxDb, float downMaxDb, float amountPct,
                 float speedPct, float smoothPct,
                 std::atomic<float>* vizSpectrumL,
                 std::atomic<float>* vizDeltaL)
    {
        const int numSamples  = buffer.getNumSamples();
        const int numChannels = juce::jmin(buffer.getNumChannels(), 2);
        if (numSamples == 0 || numChannels == 0) return;

        const float hopsPerSec = static_cast<float>(sr) / HOP_SIZE;
        const float sp = juce::jlimit(0.0f, 1.0f, speedPct / 100.0f);
        const float sm = juce::jlimit(0.0f, 1.0f, smoothPct / 100.0f);
        const float amountNorm = juce::jlimit(0.0f, 2.0f, amountPct / 100.0f);

        // Сглаживание параметров для устранения Zipper Noise при кручении ручек
        const float rawThresh = *apvts.getRawParameterValue("GLOBAL_THRESH");
        smoothGlobalThresh += 0.15f * (rawThresh - smoothGlobalThresh);
        smoothAmount       += 0.15f * (amountNorm - smoothAmount);
        smoothUpMax        += 0.15f * (upMaxDb    - smoothUpMax);
        smoothDownMax      += 0.15f * (downMaxDb  - smoothDownMax);

        const float peakAtkT = 1.0f - std::exp(-1.0f / ((0.001f + sp * 0.010f) * hopsPerSec));
        const float peakRelT = 1.0f - std::exp(-1.0f / ((0.050f + sp * 0.150f) * hopsPerSec));
        const float rmsAtkT  = 1.0f - std::exp(-1.0f / ((0.100f + sp * 0.400f) * hopsPerSec));
        const float rmsRelT  = 1.0f - std::exp(-1.0f / ((0.300f + sp * 0.800f) * hopsPerSec));

        refreshTargetIfNeeded(apvts);

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                inputFifo[ch][fifoIndex] = buffer.getSample(ch, i);
                const float out = outputFifo[ch][fifoIndex] * olaGain;
                buffer.setSample(ch, i, out);
                outputFifo[ch][fifoIndex] = 0.0f;
            }

            fifoIndex = (fifoIndex + 1) % FFT_SIZE;
            ++hopCounter;

            if (hopCounter >= HOP_SIZE)
            {
                hopCounter = 0;
                processFFTFrame(numChannels, smoothUpMax, smoothDownMax, smoothAmount, smoothGlobalThresh,
                               peakAtkT, peakRelT, rmsAtkT, rmsRelT, sp, sm,
                               vizSpectrumL, vizDeltaL);
            }
        }
    }

private:
    void processFFTFrame(int numChannels,
                         float upMaxDb, float downMaxDb, float amountNorm, float globalThreshDb,
                         float pAtk, float pRel, float rAtk, float rRel,
                         float speedNorm, float smoothNorm,
                         std::atomic<float>* vizSpectrumL,
                         std::atomic<float>* vizDeltaL)
    {
        const int numBins = FFT_SIZE / 2;
        float binMagMax[FFT_SIZE / 2] = {};

        for (int ch = 0; ch < numChannels; ++ch)
        {
            for (int k = 0; k < FFT_SIZE; ++k)
            {
                const int readIdx = (fifoIndex + k) % FFT_SIZE;
                fftWorkBuf[ch][k]            = inputFifo[ch][readIdx] * hannWindow[k];
                fftWorkBuf[ch][k + FFT_SIZE] = 0.0f;
            }
            forwardFFT.performRealOnlyForwardTransform(fftWorkBuf[ch].data());

            for (int bin = 1; bin < numBins; ++bin)
            {
                const float re = fftWorkBuf[ch][bin * 2];
                const float im = fftWorkBuf[ch][bin * 2 + 1];
                const float mag = std::sqrt(re * re + im * im) / (FFT_SIZE * 0.5f);
                binMagMax[bin] = std::max(binMagMax[bin], mag);
            }
        }

        for (int bin = 1; bin < numBins; ++bin)
        {
            const float mag = binMagMax[bin];

            peakEnvBin[bin] += (mag > peakEnvBin[bin] ? pAtk : pRel) * (mag - peakEnvBin[bin]);
            rmsEnvBin[bin]  += (mag > rmsEnvBin[bin]  ? rAtk : rRel) * (mag - rmsEnvBin[bin]);

            const float env = peakEnvBin[bin] * (1.0f - speedNorm) + rmsEnvBin[bin] * speedNorm;
            const float envDb = juce::Decibels::gainToDecibels(std::max(env, 1.0e-6f));

            const float multibandComp = 15.0f; 
            const float rawDelta = (binTargetDb[bin] + globalThreshDb - multibandComp) - envDb;

            float noiseGate = 1.0f;
            if (envDb < -90.0f) {
                noiseGate = std::max(0.0f, (envDb + 110.0f) / 20.0f);
            }

            float effectiveAmount = amountNorm;
            float effectiveUpMax = upMaxDb;
            float effectiveDownMax = downMaxDb;

            if (gradientBandsActive && binWeightSum[bin] > 0.001f)
            {
                effectiveAmount   = binAmountNorm[bin];
                effectiveUpMax    = binUpMaxDb[bin];
                effectiveDownMax  = binDownMaxDb[bin];
            }

            const float ratioDepth = effectiveAmount; 
            
            float variMuDelta = 0.0f;
            const float kneeWidthDb = 12.0f; // Стабильное колено Vari-Mu

            if (rawDelta > 0.0f) {
                if (effectiveUpMax > 0.001f) {
                    float normalizedInput = rawDelta / kneeWidthDb;
                    variMuDelta = fast_tanh_sc(normalizedInput) * effectiveUpMax * ratioDepth * noiseGate;
                }
            } else {
                if (effectiveDownMax < -0.001f) {
                    float normalizedInput = -rawDelta / kneeWidthDb;
                    variMuDelta = fast_tanh_sc(normalizedInput) * effectiveDownMax * ratioDepth;
                }
            }

            rawDeltaDb[bin] = variMuDelta;
        }

        const int blurRadius = static_cast<int>(smoothNorm * 18.0f);

        for (int bin = 1; bin < numBins; ++bin)
        {
            float finalDeltaDb = 0.0f;

            if (blurRadius <= 0)
            {
                finalDeltaDb = rawDeltaDb[bin];
            }
            else
            {
                float weightSum = 0.0f;
                float valSum = 0.0f;

                for (int r = -blurRadius; r <= blurRadius; ++r) {
                    const int sampleBin = juce::jlimit(1, numBins - 1, bin + r);
                    const float weight = 1.0f - (std::abs(r) / static_cast<float>(blurRadius + 1));
                    valSum += rawDeltaDb[sampleBin] * weight;
                    weightSum += weight;
                }
                finalDeltaDb = valSum / weightSum;
            }

            const float targetGainLin = juce::Decibels::decibelsToGain(finalDeltaDb);
            bandGainsBin[bin] += 0.25f * (targetGainLin - bandGainsBin[bin]);
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            for (int bin = 1; bin < numBins; ++bin)
            {
                fftWorkBuf[ch][bin * 2]     *= bandGainsBin[bin];
                fftWorkBuf[ch][bin * 2 + 1] *= bandGainsBin[bin];
            }

            if (ch == 0 && vizSpectrumL != nullptr && vizDeltaL != nullptr)
            {
                for (int bin = 1; bin < numBins; ++bin)
                {
                    const float re = fftWorkBuf[0][bin * 2];
                    const float im = fftWorkBuf[0][bin * 2 + 1];
                    const float mag = std::sqrt(re * re + im * im) / (FFT_SIZE * 0.5f);
                    
                    const float tiltDb = 10.0f * std::log10(std::max(1.0f, (float)bin));
                    const float magDb = juce::Decibels::gainToDecibels(mag, -120.0f) + tiltDb + 15.0f;
                    const float gainDb = juce::Decibels::gainToDecibels(bandGainsBin[bin]);
                    
                    const float oldMag = vizSpectrumL[bin].load(std::memory_order_relaxed);
                    const float oldDelta = vizDeltaL[bin].load(std::memory_order_relaxed);
                    
                    vizSpectrumL[bin].store(oldMag * 0.5f + magDb * 0.5f, std::memory_order_relaxed);
                    vizDeltaL[bin].store(oldDelta * 0.5f + gainDb * 0.5f, std::memory_order_relaxed);
                }
            }

            inverseFFT.performRealOnlyInverseTransform(fftWorkBuf[ch].data());

            for (int k = 0; k < FFT_SIZE; ++k)
            {
                const int writeIdx = (fifoIndex + k) % FFT_SIZE;
                outputFifo[ch][writeIdx] += fftWorkBuf[ch][k] * hannWindow[k];
            }
        }
    }

    // БЕЗАЛОКАЦИОННЫЙ РАСЧЕТ ЦЕЛЕВОЙ КРИВОЙ ЭКВАЛАЙЗЕРА
    void refreshTargetIfNeeded(juce::AudioProcessorValueTreeState& apvts)
    {
        if (++targetRefreshCounter < 16 && !targetDirty) return;
        targetRefreshCounter = 0;
        targetDirty = false;

        const int numBins = FFT_SIZE / 2;

        FastBiquadParams filters[8];
        int activeCount = 0;

        for (int i = 0; i < 8; ++i) {
            juce::String prefix = "BAND_" + juce::String(i);
            if (*apvts.getRawParameterValue(prefix + "_ENABLE") < 0.5f) continue;

            float f0 = *apvts.getRawParameterValue(prefix + "_FREQ");
            float gainDb = *apvts.getRawParameterValue(prefix + "_GAIN");
            float q = *apvts.getRawParameterValue(prefix + "_Q");

            if (std::abs(gainDb) < 0.05f) continue;

            filters[activeCount++].setPeak(static_cast<float>(sr), f0, q, gainDb);
        }

        for (int bin = 1; bin < numBins; ++bin)
        {
            float totalMagSq = 1.0f;
            float cw = cosW[bin];
            float c2w = cos2W[bin];

            for (int f = 0; f < activeCount; ++f) {
                totalMagSq *= filters[f].getMagSq(cw, c2w);
            }

            // 10 * log10(magSq) = gainToDecibels из квадрата амплитуды
            binTargetDb[bin] = 10.0f * std::log10(std::max(1.0e-9f, totalMagSq));
        }
    }

    double sr = 44100.0;

    juce::dsp::FFT forwardFFT;
    juce::dsp::FFT inverseFFT;
    juce::dsp::WindowingFunction<float> window;
    std::vector<float> hannWindow;
    float olaGain = 1.0f / 1.5f;

    std::vector<std::vector<float>> inputFifo;
    std::vector<std::vector<float>> outputFifo;
    std::vector<std::vector<float>> fftWorkBuf;
    int fifoIndex  = 0;
    int hopCounter = 0;

    std::vector<float> cosW;
    std::vector<float> cos2W;

    std::vector<float> peakEnvBin;
    std::vector<float> rmsEnvBin;
    std::vector<float> bandGainsBin;
    std::vector<float> rawDeltaDb;
    std::vector<float> binTargetDb;

    std::vector<float> binAmountNorm;
    std::vector<float> binUpMaxDb;
    std::vector<float> binDownMaxDb;
    std::vector<float> binWeightSum;
    bool gradientBandsActive = false;

    // Сглаженные значения параметров для убирания zipper noise
    float smoothGlobalThresh = 0.0f;
    float smoothAmount       = 1.0f;
    float smoothUpMax        = 4.0f;
    float smoothDownMax      = -4.0f;

    bool targetDirty = true;
    int  targetRefreshCounter = 0;
};