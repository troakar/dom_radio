#pragma once
#include <JuceHeader.h>
#include <vector>
#include <atomic>
#include <cmath>
#include "UI/GradientBandModel.h"

// Константа максимального размера буфера (устраняет ошибку порядка компиляции C++)
constexpr int MAX_FFT_BINS = 1024;

inline float fast_tanh_sc(float x) noexcept
{
    if (x >  3.0f) return  1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2  = x * x;
    const float num = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float den = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return num / (den + 1.0e-9f);
}

// Переведено на double для исключения искажений на НЧ
struct FastBiquadParams {
    double c0 = 1.0, c1 = 0.0, c2 = 0.0;
    double d0 = 1.0, d1 = 0.0, d2 = 0.0;

    void setPeak(double sr, double f0, double q, double gainDb) {
        if (std::abs(gainDb) < 0.01) {
            c0 = 1.0; c1 = 0.0; c2 = 0.0;
            d0 = 1.0; d1 = 0.0; d2 = 0.0;
            return;
        }
        double A = std::pow(10.0, gainDb / 40.0);
        double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / sr;
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

class SpectralEngine
{
public:
    SpectralEngine() {}

    void prepare(double sampleRate)
    {
        sr = sampleRate;
        switchFFTSize(512); // Инициализируем плагин размером по умолчанию
    }

    void switchFFTSize(int newSize)
    {
        currentFFTSize = newSize;
        currentFFTOrder = static_cast<int>(std::log2(currentFFTSize));
        currentHopSize = currentFFTSize / 4;

        forwardFFT = std::make_unique<juce::dsp::FFT>(currentFFTOrder);
        inverseFFT = std::make_unique<juce::dsp::FFT>(currentFFTOrder);

        inputFifo.assign(2, std::vector<float>(currentFFTSize, 0.0f));
        outputFifo.assign(2, std::vector<float>(currentFFTSize, 0.0f));
        sidechainFifo.assign(2, std::vector<float>(currentFFTSize, 0.0f));
        fftWorkBuf.assign(2, std::vector<float>(currentFFTSize * 2, 0.0f));
        fifoIndex = 0;
        hopCounter = 0;

        const int numBins = currentFFTSize / 2;
        peakEnvBin.assign(numBins, 0.0f);
        rmsEnvBin.assign(numBins, 0.0f);
        bandGainsBin.assign(numBins, 1.0f);
        rawDeltaDb.assign(numBins, 0.0f);
        binTargetDb.assign(numBins, 0.0f);

        binPAtk.assign(numBins, 1.0f);
        binPRel.assign(numBins, 1.0f);
        binRAtk.assign(numBins, 1.0f);
        binRRel.assign(numBins, 1.0f);
        binEnvSpeed.assign(numBins, 0.5f);
        binSmoothCoef.assign(numBins, 0.5f);
        binEffectiveAmount.assign(numBins, 0.0f);
        binEffectiveUp.assign(numBins, 0.0f);
        binEffectiveDown.assign(numBins, 0.0f);
        binThreshOffsetDb.assign(numBins, 0.0f);

        cosW.resize(numBins);
        cos2W.resize(numBins);
        for (int bin = 1; bin < numBins; ++bin) {
            double w = 2.0 * juce::MathConstants<double>::pi * static_cast<double>(bin) / static_cast<double>(currentFFTSize);
            cosW[bin] = std::cos(w);
            cos2W[bin] = std::cos(2.0 * w);
        }

        hannWindow.resize(currentFFTSize);
        juce::dsp::WindowingFunction<float>::fillWindowingTables(
            hannWindow.data(),
            currentFFTSize,
            juce::dsp::WindowingFunction<float>::hann,
            false);

        olaGain = 1.0f / 1.5f;
        targetDirty = true;
        envelopeSettled = false;
        settleCounter = 0;
        smoothedParamsInitialised = false;
    }

    void updateParameters(float globalUpMax, float globalDownMax, float globalAmountPct, 
                          float globalSpeedPct, float globalSmoothPct, 
                          const std::vector<GradientPoint>& points, double sampleRate)
    {
        const int numBins = currentFFTSize / 2;
        const float hopsPerSec = static_cast<float>(sampleRate) / currentHopSize;
        
        float globalAmountNorm = juce::jlimit(0.0f, 2.0f, globalAmountPct / 100.0f);
        float globalSpeedNorm  = juce::jlimit(0.0f, 1.0f, globalSpeedPct / 100.0f);
        float globalSmoothNorm = juce::jlimit(0.0f, 1.0f, globalSmoothPct / 100.0f);

        std::vector<float> binWeightSum(numBins, 0.0f);
        std::vector<float> binSpeedNorm(numBins, 0.0f);
        std::vector<float> binSmoothNorm(numBins, 0.0f);
        
        binEffectiveAmount.assign(numBins, 0.0f);
        binEffectiveUp.assign(numBins, 0.0f);
        binEffectiveDown.assign(numBins, 0.0f);
        binThreshOffsetDb.assign(numBins, 0.0f);
        
        bool hasActiveGradients = false;

        for (const auto& point : points) {
            if (!point.active) continue;
            hasActiveGradients = true;
            
            float ptAmt = juce::jlimit(0.0f, 2.0f, point.amountPct / 100.0f);
            float ptSpd = point.speedPct / 100.0f;
            float ptSm  = point.smoothPct / 100.0f;

            for (int bin = 1; bin < numBins; ++bin) {
                double freq = (double)bin * sampleRate / (double)currentFFTSize;
                if (freq < 20.0 || freq > 20000.0) continue;

                float logDist = (float)std::log2(freq / point.centerFreqHz);
                float weight = std::exp(-0.5f * (logDist / point.radiusOctaves) * (logDist / point.radiusOctaves));
                weight = juce::jlimit(0.0f, 1.0f, weight);

                binEffectiveAmount[bin] += ptAmt * weight;
                binEffectiveUp[bin]     += point.upMaxDb * weight;
                binEffectiveDown[bin]   += point.downMaxDb * weight;
                binThreshOffsetDb[bin]  += point.centerGainDb * weight;
                binSpeedNorm[bin]       += ptSpd * weight;
                binSmoothNorm[bin]      += ptSm * weight;
                binWeightSum[bin]       += weight;
            }
        }

        for (int bin = 1; bin < numBins; ++bin) {
            float finalSpd = globalSpeedNorm;
            float finalSm  = globalSmoothNorm;

            float W = juce::jlimit(0.0f, 1.0f, binWeightSum[bin]);

            if (hasActiveGradients && W > 0.001f) {
                float invW = 1.0f / binWeightSum[bin];
                
                float localAmt = binEffectiveAmount[bin] * invW;
                float localUp  = binEffectiveUp[bin] * invW;
                float localDown = binEffectiveDown[bin] * invW;
                float localSpd = binSpeedNorm[bin] * invW;
                float localSm  = binSmoothNorm[bin] * invW;
                float localThresh = binThreshOffsetDb[bin] * invW;

                binEffectiveAmount[bin] = globalAmountNorm * (1.0f - W) + localAmt * W;
                binEffectiveUp[bin]     = globalUpMax * (1.0f - W) + localUp * W;
                binEffectiveDown[bin]   = globalDownMax * (1.0f - W) + localDown * W;
                finalSpd                = globalSpeedNorm * (1.0f - W) + localSpd * W;
                finalSm                 = globalSmoothNorm * (1.0f - W) + localSm * W;
                binThreshOffsetDb[bin]  = localThresh * W; 
            } else {
                binEffectiveAmount[bin] = globalAmountNorm;
                binEffectiveUp[bin]     = globalUpMax;
                binEffectiveDown[bin]   = globalDownMax;
                binThreshOffsetDb[bin]  = 0.0f;
            }

            binEnvSpeed[bin] = finalSpd;
            binPAtk[bin] = 1.0f - std::exp(-1.0f / ((0.001f + finalSpd * 0.010f) * hopsPerSec));
            binPRel[bin] = 1.0f - std::exp(-1.0f / ((0.050f + finalSpd * 0.150f) * hopsPerSec));
            binRAtk[bin] = 1.0f - std::exp(-1.0f / ((0.100f + finalSpd * 0.400f) * hopsPerSec));
            binRRel[bin] = 1.0f - std::exp(-1.0f / ((0.300f + finalSpd * 0.800f) * hopsPerSec));
            
            binSmoothCoef[bin] = juce::jlimit(0.2f, 0.98f, 0.4f + finalSm * 0.58f);
        }
    }

    void process(juce::AudioBuffer<float>& buffer,
                 const juce::AudioBuffer<float>* sidechainBuffer,
                 juce::AudioProcessorValueTreeState& apvts,
                 std::atomic<float>* vizSpectrumL,
                 std::atomic<float>* vizDeltaL)
    {
        const int numSamples  = buffer.getNumSamples();
        const int numChannels = juce::jmin(buffer.getNumChannels(), 2);
        if (numSamples == 0 || numChannels == 0) return;

        const bool hasSidechain = (sidechainBuffer != nullptr && sidechainBuffer->getNumChannels() > 0);
        const int scChannels = hasSidechain ? juce::jmin(sidechainBuffer->getNumChannels(), 2) : 0;

        const float rawThresh = *apvts.getRawParameterValue("GLOBAL_THRESH");
        if (!smoothedParamsInitialised) {
            smoothGlobalThresh = rawThresh;
            smoothedParamsInitialised = true;
        } else {
            smoothGlobalThresh += 0.15f * (rawThresh  - smoothGlobalThresh);
        }

        refreshTargetIfNeeded(apvts);

        for (int i = 0; i < numSamples; ++i) {
            for (int ch = 0; ch < numChannels; ++ch) {
                inputFifo[ch][fifoIndex] = buffer.getSample(ch, i);
                buffer.setSample(ch, i, outputFifo[ch][fifoIndex] * olaGain);
                outputFifo[ch][fifoIndex] = 0.0f;
            }

            if (hasSidechain) {
                for (int ch = 0; ch < scChannels; ++ch) {
                    sidechainFifo[ch][fifoIndex] = sidechainBuffer->getSample(ch, i);
                }
            }

            fifoIndex = (fifoIndex + 1) % currentFFTSize;
            if (++hopCounter >= currentHopSize) {
                hopCounter = 0;
                processFFTFrame(numChannels, hasSidechain ? scChannels : 0, 
                                smoothGlobalThresh, vizSpectrumL, vizDeltaL);
            }
        }
    }

private:
    void processFFTFrame(int numChannels, int scChannels, float globalThreshDb,
                         std::atomic<float>* vizSpectrumL, std::atomic<float>* vizDeltaL)
    {
        const int numBins = currentFFTSize / 2;
        float binMagMax[MAX_FFT_BINS] = {};
        const bool useSidechain = (scChannels > 0);

        if (useSidechain) {
            for (int ch = 0; ch < scChannels; ++ch) {
                for (int k = 0; k < currentFFTSize; ++k) {
                    const int readIdx = (fifoIndex + k) % currentFFTSize;
                    fftWorkBuf[ch][k]            = sidechainFifo[ch][readIdx] * hannWindow[k];
                    fftWorkBuf[ch][k + currentFFTSize] = 0.0f;
                }
                forwardFFT->performRealOnlyForwardTransform(fftWorkBuf[ch].data());
                for (int bin = 1; bin < numBins; ++bin) {
                    const float re = fftWorkBuf[ch][bin * 2];
                    const float im = fftWorkBuf[ch][bin * 2 + 1];
                    const float mag = std::sqrt(re * re + im * im) / (currentFFTSize * 0.5f);
                    binMagMax[bin] = std::max(binMagMax[bin], mag);
                }
            }
        }

        for (int ch = 0; ch < numChannels; ++ch) {
            for (int k = 0; k < currentFFTSize; ++k) {
                const int readIdx = (fifoIndex + k) % currentFFTSize;
                fftWorkBuf[ch][k]            = inputFifo[ch][readIdx] * hannWindow[k];
                fftWorkBuf[ch][k + currentFFTSize] = 0.0f;
            }
            forwardFFT->performRealOnlyForwardTransform(fftWorkBuf[ch].data());

            if (!useSidechain) {
                for (int bin = 1; bin < numBins; ++bin) {
                    const float re = fftWorkBuf[ch][bin * 2];
                    const float im = fftWorkBuf[ch][bin * 2 + 1];
                    const float mag = std::sqrt(re * re + im * im) / (currentFFTSize * 0.5f);
                    binMagMax[bin] = std::max(binMagMax[bin], mag);
                }
            }
        }

        for (int bin = 1; bin < numBins; ++bin) {
            const float mag = binMagMax[bin];

            float pAtk = binPAtk[bin], pRel = binPRel[bin];
            float rAtk = binRAtk[bin], rRel = binRRel[bin];
            if (!envelopeSettled) { pAtk = 1.0f; rAtk = 0.2f; }

            peakEnvBin[bin] += (mag > peakEnvBin[bin] ? pAtk : pRel) * (mag - peakEnvBin[bin]);
            rmsEnvBin[bin]  += (mag > rmsEnvBin[bin]  ? rAtk : rRel) * (mag - rmsEnvBin[bin]);

            const float env = peakEnvBin[bin] * (1.0f - binEnvSpeed[bin]) + rmsEnvBin[bin] * binEnvSpeed[bin];
            const float envDb = juce::Decibels::gainToDecibels(std::max(env, 1.0e-7f));

            // ОРИГИНАЛЬНАЯ ФОРМУЛА MATCHING: Целевой уровень - Текущий спектр
            const float multibandComp = 15.0f;
            const float localThreshOffset = binThreshOffsetDb[bin];
            const float targetLevelDb = binTargetDb[bin] + globalThreshDb + localThreshOffset - multibandComp;
            
            const float rawDelta = targetLevelDb - envDb;

            float noiseGate = 1.0f;
            if (envDb < -90.0f) {
                noiseGate = std::max(0.0f, (envDb + 110.0f) / 20.0f);
            }

            float effectiveAmount  = binEffectiveAmount[bin];
            float effectiveUpMax   = binEffectiveUp[bin];
            float effectiveDownMax = binEffectiveDown[bin];

            const float ratioDepth = effectiveAmount;
            float variMuDelta = 0.0f;
            const float kneeWidthDb = 12.0f;

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

        // 1. Рассчитываем сырую дельту для всех бинов
        for (int bin = 1; bin < numBins; ++bin) {
            const float mag = binMagMax[bin];
            float pAtk = binPAtk[bin], pRel = binPRel[bin];
            float rAtk = binRAtk[bin], rRel = binRRel[bin];
            if (!envelopeSettled) { pAtk = 1.0f; rAtk = 0.2f; }

            peakEnvBin[bin] += (mag > peakEnvBin[bin] ? pAtk : pRel) * (mag - peakEnvBin[bin]);
            rmsEnvBin[bin]  += (mag > rmsEnvBin[bin]  ? rAtk : rRel) * (mag - rmsEnvBin[bin]);

            const float env = peakEnvBin[bin] * (1.0f - binEnvSpeed[bin]) + rmsEnvBin[bin] * binEnvSpeed[bin];
            const float envDb = juce::Decibels::gainToDecibels(std::max(env, 1.0e-7f));

            const float multibandComp = 15.0f;
            const float targetLevelDb = binTargetDb[bin] + globalThreshDb + binThreshOffsetDb[bin] - multibandComp;
            const float rawDelta = targetLevelDb - envDb;

            // Защита от усиления шума (Noise Gate)
            float noiseGate = 1.0f;
            if (envDb < -65.0f) {
                noiseGate = std::max(0.0f, (envDb + 85.0f) / 20.0f);
            }

            float ratioDepth = binEffectiveAmount[bin];
            float variMuDelta = 0.0f;
            const float kneeWidthDb = 12.0f;

            if (rawDelta > 0.0f) {
                if (binEffectiveUp[bin] > 0.001f) {
                    float normalizedInput = rawDelta / kneeWidthDb;
                    variMuDelta = fast_tanh_sc(normalizedInput) * binEffectiveUp[bin] * ratioDepth * noiseGate;
                }
            } else {
                if (binEffectiveDown[bin] < -0.001f) {
                    float normalizedInput = -rawDelta / kneeWidthDb;
                    variMuDelta = fast_tanh_sc(normalizedInput) * binEffectiveDown[bin] * ratioDepth;
                }
            }
            rawDeltaDb[bin] = variMuDelta;
        }

        // 2. ИДЕАЛЬНОЕ ПРОСТРАНСТВЕННОЕ СГЛАЖИВАНИЕ (Forward-Backward IIR)
        std::vector<float> smoothedDelta(numBins, 0.0f);
        smoothedDelta[1] = rawDeltaDb[1];
        
        for (int bin = 2; bin < numBins; ++bin) {
            float coef = binSmoothCoef[bin];
            smoothedDelta[bin] = smoothedDelta[bin - 1] * coef + rawDeltaDb[bin] * (1.0f - coef);
        }
        for (int bin = numBins - 2; bin >= 1; --bin) {
            float coef = binSmoothCoef[bin];
            smoothedDelta[bin] = smoothedDelta[bin + 1] * coef + smoothedDelta[bin] * (1.0f - coef);
        }

        // 3. Плавное применение
        for (int bin = 1; bin < numBins; ++bin) {
            const float targetGainLin = juce::Decibels::decibelsToGain(smoothedDelta[bin]);
            bandGainsBin[bin] += 0.3f * (targetGainLin - bandGainsBin[bin]);
        }

        for (int ch = 0; ch < numChannels; ++ch) {
            for (int bin = 1; bin < numBins; ++bin) {
                fftWorkBuf[ch][bin * 2]     *= bandGainsBin[bin];
                fftWorkBuf[ch][bin * 2 + 1] *= bandGainsBin[bin];
            }

            if (ch == 0 && vizSpectrumL != nullptr && vizDeltaL != nullptr) {
                for (int bin = 1; bin < numBins; ++bin) {
                    const float re = fftWorkBuf[0][bin * 2];
                    const float im = fftWorkBuf[0][bin * 2 + 1];
                    const float mag = std::sqrt(re * re + im * im) / (currentFFTSize * 0.5f);
                    
                    const float tiltDb = 10.0f * std::log10(std::max(1.0f, (float)bin));
                    const float magDb = juce::Decibels::gainToDecibels(mag, -120.0f) + tiltDb + 15.0f;
                    const float gainDb = juce::Decibels::gainToDecibels(bandGainsBin[bin]);
                    
                    const float oldMag = vizSpectrumL[bin].load(std::memory_order_relaxed);
                    const float oldDelta = vizDeltaL[bin].load(std::memory_order_relaxed);
                    
                    vizSpectrumL[bin].store(oldMag * 0.5f + magDb * 0.5f, std::memory_order_relaxed);
                    vizDeltaL[bin].store(oldDelta * 0.85f + gainDb * 0.15f, std::memory_order_relaxed);
                }
            }

            inverseFFT->performRealOnlyInverseTransform(fftWorkBuf[ch].data());

            for (int k = 0; k < currentFFTSize; ++k) {
                const int writeIdx = (fifoIndex + k) % currentFFTSize;
                outputFifo[ch][writeIdx] += fftWorkBuf[ch][k] * hannWindow[k];
            }
        }
    }

    void refreshTargetIfNeeded(juce::AudioProcessorValueTreeState& apvts)
    {
        if (++targetRefreshCounter < 16 && !targetDirty) return;
        targetRefreshCounter = 0;
        targetDirty = false;

        const int numBins = currentFFTSize / 2;

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
            double totalMagSq = 1.0;
            double cw = cosW[bin];
            double c2w = cos2W[bin];

            for (int f = 0; f < activeCount; ++f) {
                totalMagSq *= filters[f].getMagSq(cw, c2w);
            }

            binTargetDb[bin] = static_cast<float>(10.0 * std::log10(std::max(1.0e-15, totalMagSq)));
        }
    }

    double sr = 44100.0;

    // ДОБАВЬТЕ ЭТИ ТРИ СТРОКИ:
    int currentFFTSize = 512;
    int currentFFTOrder = 9;
    int currentHopSize = 128;

    std::unique_ptr<juce::dsp::FFT> forwardFFT;
    std::unique_ptr<juce::dsp::FFT> inverseFFT;
    std::vector<float> hannWindow;
    float olaGain = 1.0f / 1.5f;

    std::vector<std::vector<float>> inputFifo;
    std::vector<std::vector<float>> outputFifo;
    std::vector<std::vector<float>> sidechainFifo;
    std::vector<std::vector<float>> fftWorkBuf;
    int fifoIndex  = 0;
    int hopCounter = 0;

    std::vector<double> cosW;
    std::vector<double> cos2W;

    std::vector<float> peakEnvBin;
    std::vector<float> rmsEnvBin;
    std::vector<float> bandGainsBin;
    std::vector<float> rawDeltaDb;
    std::vector<float> binTargetDb;

    std::vector<float> binPAtk, binPRel, binRAtk, binRRel, binEnvSpeed;
    std::vector<float> binSmoothCoef;
    std::vector<float> binEffectiveAmount, binEffectiveUp, binEffectiveDown;
    std::vector<float> binThreshOffsetDb;

    float smoothGlobalThresh = 0.0f;
    bool smoothedParamsInitialised = false;

    bool targetDirty = true;
    int  targetRefreshCounter = 0;
    bool envelopeSettled = false;
    int  settleCounter = 0;
};