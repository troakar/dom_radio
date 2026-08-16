#pragma once
#include <JuceHeader.h>
#include <vector>
#include <array>
#include <atomic>
#include <cmath>
#include "UI/GradientBandModel.h"

constexpr int MAX_FFT_BINS = 2048;

inline float fast_tanh_sc(float x) noexcept
{
    if (x >  3.0f) return  1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2  = x * x;
    const float num = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float den = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return num / (den + 1.0e-9f);
}

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

    int getLookaheadSamples() const noexcept { return lookaheadSamples; }
    bool isEnvelopeSettled() const noexcept { return envelopeSettled; }

    void prepare(double sampleRate)
    {
        sr = sampleRate;

        for (int i = 0; i < 3; ++i) {
            const int size = 512 << i;
            const int order = static_cast<int>(std::log2(size));
            const int numBins = size / 2 + 1;

            fwdFFT[i] = std::make_unique<juce::dsp::FFT>(order);
            invFFT[i] = std::make_unique<juce::dsp::FFT>(order);
            windows[i].resize(size);
            juce::dsp::WindowingFunction<float>::fillWindowingTables(
                windows[i].data(), size, juce::dsp::WindowingFunction<float>::hann, false);
            
            cosW[i].assign(numBins, 0.0);
            cos2W[i].assign(numBins, 0.0);
            lnFreqs[i].assign(numBins, 0.0f);
            for (int bin = 1; bin < numBins; ++bin) {
                const double w = 2.0 * juce::MathConstants<double>::pi * static_cast<double>(bin) / static_cast<double>(size);
                cosW[i][bin] = std::cos(w);
                cos2W[i][bin] = std::cos(2.0 * w);

                const float freq = (static_cast<float>(bin) / static_cast<float>(size)) * static_cast<float>(sr);
                lnFreqs[i][bin] = std::log(std::max(1.0f, freq));
            }
        }

        inputFifo.assign(2, std::vector<float>(2048, 0.0f));
        outputFifo.assign(2, std::vector<float>(2048, 0.0f));
        sidechainFifo.assign(2, std::vector<float>(2048, 0.0f));
        fftWorkBuf.assign(2, std::vector<float>(4096, 0.0f));

        const int maxBins = 2048;
        peakEnvBin.assign(maxBins, 0.0f);
        rmsEnvBin.assign(maxBins, 0.0f);
        bandGainsBin.assign(maxBins, 1.0f);
        rawDeltaDb.assign(maxBins, 0.0f);
        rawUpDeltaDb.assign(maxBins, 0.0f);
        rawDownDeltaDb.assign(maxBins, 0.0f);
        binUpSmoothCoef.assign(maxBins, 0.5f);
        binDownSmoothCoef.assign(maxBins, 0.5f);
        binUpSmoothNorm.assign(maxBins, 0.5f);
        binDownSmoothNorm.assign(maxBins, 0.5f);
        upSmoothingPrefixDb.assign(maxBins + 1, 0.0f);
        downSmoothingPrefixDb.assign(maxBins + 1, 0.0f);
        binTargetDb.assign(maxBins, 0.0f);
        binPAtk.assign(maxBins, 1.0f);
        binPRel.assign(maxBins, 1.0f);
        binRAtk.assign(maxBins, 1.0f);
        binRRel.assign(maxBins, 1.0f);
        binEnvSpeed.assign(maxBins, 0.5f);
        binUpSmoothCoef.assign(maxBins, 0.5f);
        binDownSmoothCoef.assign(maxBins, 0.5f);
        binEffectiveAmount.assign(maxBins, 0.0f);
        binEffectiveUp.assign(maxBins, 0.0f);
        binEffectiveDown.assign(maxBins, 0.0f);
        binEffectiveUpSel.assign(maxBins, 0.0f);
        binEffectiveDownSel.assign(maxBins, 0.0f);
        binThreshOffsetDb.assign(maxBins, 0.0f);
        spectralFloorDb.assign(maxBins, 0.0f);
        binKneeWidth.assign(maxBins, 3.0f);
        binAttackCoef.assign(maxBins, 0.0f);
        binReleaseCoef.assign(maxBins, 0.0f);
        
        envDbFast.assign(maxBins, 0.0f);
        smoothedDelta.assign(maxBins, 0.0f);
        binWeightSum.assign(maxBins, 0.0f);
        binSpeedNorm.assign(maxBins, 0.0f);
        binUpSmoothNorm.assign(maxBins, 0.0f);
        binDownSmoothNorm.assign(maxBins, 0.0f);
        envPrefixDb.assign(MAX_FFT_BINS + 1, 0.0f);

        upSmoothingPrefixDb.assign(maxBins + 1, 0.0f);

        mainMagMax.assign(MAX_FFT_BINS + 1, 0.0f);
        sidechainMagMax.assign(MAX_FFT_BINS + 1, 0.0f);
        finalDownDeltaBuf.assign(MAX_FFT_BINS, 0.0f);
        finalUpDeltaBuf.assign(MAX_FFT_BINS, 0.0f);
        binAttackMsAccum.assign(MAX_FFT_BINS, 0.0f);
        binReleaseMsAccum.assign(MAX_FFT_BINS, 0.0f);
        binKneeAccum.assign(MAX_FFT_BINS, 0.0f);
        binAutoSpeedAccum.assign(MAX_FFT_BINS, 0.0f);

        lookaheadEnvBuffer.assign(2048, 0.0f);
        lookaheadWritePos = 0;
        lookaheadSamples = 0;

        switchFFTSize(512);
    }

    void switchFFTSize(int newSize)
    {
        currentFFTSize = newSize;
        currentFFTOrder = static_cast<int>(std::log2(currentFFTSize));
        currentHopSize = currentFFTSize / 4;

        olaGain = 1.0f / 1.5f;

        if (newSize == 512) activeFFTIndex = 0;
        else if (newSize == 1024) activeFFTIndex = 1;
        else if (newSize == 2048) activeFFTIndex = 2;
        else activeFFTIndex = 0;

        fifoIndex = 0;
        hopCounter = 0;

        for (int ch = 0; ch < 2; ++ch) {
            std::fill(inputFifo[ch].begin(), inputFifo[ch].end(), 0.0f);
            std::fill(outputFifo[ch].begin(), outputFifo[ch].end(), 0.0f);
            std::fill(sidechainFifo[ch].begin(), sidechainFifo[ch].end(), 0.0f);
            std::fill(fftWorkBuf[ch].begin(), fftWorkBuf[ch].end(), 0.0f);
        }

        std::fill(peakEnvBin.begin(), peakEnvBin.end(), 0.0f);
        std::fill(rmsEnvBin.begin(), rmsEnvBin.end(), 0.0f);
        std::fill(bandGainsBin.begin(), bandGainsBin.end(), 1.0f);
        std::fill(rawDeltaDb.begin(), rawDeltaDb.end(), 0.0f);
        std::fill(smoothedDelta.begin(), smoothedDelta.end(), 0.0f);
        std::fill(envDbFast.begin(), envDbFast.end(), -100.0f);
        std::fill(spectralFloorDb.begin(), spectralFloorDb.end(), -100.0f);
        std::fill(binTargetDb.begin(), binTargetDb.end(), 0.0f);

        targetDirty.store(true, std::memory_order_release);
        envelopeSettled = false;
        settleCounter = 0;
        smoothedParamsInitialised = false;
    }

    void invalidateTarget() noexcept
    { 
        targetDirty.store(true, std::memory_order_release); 
    }

    void linkParameters(std::atomic<float>** bEn, std::atomic<float>** bFr, std::atomic<float>** bGn, std::atomic<float>** bQ)
    {
        for (int i = 0; i < 8; ++i) {
            pBandEnable[i] = bEn[i];
            pBandFreq[i]   = bFr[i];
            pBandGain[i]   = bGn[i];
            pBandQ[i]      = bQ[i];
        }
    }

    void updateParameters(float globalUpMax, float globalDownMax, float globalAmountPct, 
                          float globalSpeedPct, 
                          float globalUpSmoothPct, float globalDownSmoothPct,
                          float globalUpSelPct, float globalDownSelPct,
                          bool globalAutoSpeed,
                          float globalAttackMs,
                          float globalReleaseMs,
                          float globalKneeWidthDb,
                          float lookaheadMs,
                          const std::array<GradientPoint, 4>& points,
                          double sampleRate)
    {
        const int numBins = currentFFTSize / 2 + 1;
        const float hopsPerSec = static_cast<float>(sampleRate) / currentHopSize;
        
        float globalAmountNorm     = juce::jlimit(0.0f, 3.0f, globalAmountPct / 100.0f);
        float globalSpeedNorm      = juce::jlimit(0.0f, 1.0f, globalSpeedPct / 100.0f);
        float globalUpSmoothNorm   = juce::jlimit(0.0f, 1.0f, globalUpSmoothPct / 100.0f);
        float globalDownSmoothNorm = juce::jlimit(0.0f, 1.0f, globalDownSmoothPct / 100.0f);
        float globalUpSelNorm      = globalUpSelPct / 100.0f;
        float globalDownSelNorm    = globalDownSelPct / 100.0f;

        lookaheadSamples = juce::jlimit(0, currentHopSize, static_cast<int>(lookaheadMs * sampleRate / 1000.0f));

        std::fill(binWeightSum.begin(), binWeightSum.begin() + numBins, 0.0f);
        std::fill(binSpeedNorm.begin(), binSpeedNorm.begin() + numBins, 0.0f);
        std::fill(binUpSmoothNorm.begin(), binUpSmoothNorm.begin() + numBins, 0.0f);
        std::fill(binDownSmoothNorm.begin(), binDownSmoothNorm.begin() + numBins, 0.0f);
        
        std::fill(binEffectiveAmount.begin(), binEffectiveAmount.begin() + numBins, 0.0f);
        std::fill(binEffectiveUp.begin(), binEffectiveUp.begin() + numBins, 0.0f);
        std::fill(binEffectiveDown.begin(), binEffectiveDown.begin() + numBins, 0.0f);
        std::fill(binEffectiveUpSel.begin(), binEffectiveUpSel.begin() + numBins, 0.0f);
        std::fill(binEffectiveDownSel.begin(), binEffectiveDownSel.begin() + numBins, 0.0f);
        std::fill(binThreshOffsetDb.begin(), binThreshOffsetDb.begin() + numBins, 0.0f);
        
        std::fill(binAttackMsAccum.begin(), binAttackMsAccum.begin() + numBins, 0.0f);
        std::fill(binReleaseMsAccum.begin(), binReleaseMsAccum.begin() + numBins, 0.0f);
        std::fill(binKneeAccum.begin(), binKneeAccum.begin() + numBins, 0.0f);
        std::fill(binAutoSpeedAccum.begin(), binAutoSpeedAccum.begin() + numBins, 0.0f);
        
        bool hasActiveGradients = false;

        for (const auto& point : points) {
            if (!point.active) continue;
            hasActiveGradients = true;
            
            float ptAmt    = juce::jlimit(0.0f, 3.0f, point.amountPct / 100.0f);
            float ptUpSm   = point.upSmoothPct / 100.0f;
            float ptDownSm = point.downSmoothPct / 100.0f;

            for (int bin = 1; bin < numBins; ++bin) {
                double freq = (double)bin * sampleRate / (double)currentFFTSize;
                if (freq < 20.0 || freq > 20000.0) continue;

                float logDist = (float)std::abs(std::log2(freq / point.centerFreqHz));
                 float normalizedDist = logDist / std::max(0.1f, point.radiusOctaves);

                float weight = 0.0f;
                if (normalizedDist < 1.0f) {
                    weight = 0.5f + 0.5f * std::cos(normalizedDist * juce::MathConstants<float>::pi);
                }

                binEffectiveAmount[bin] += ptAmt * weight;
                binEffectiveUp[bin]     += point.upMaxDb * weight;
                binEffectiveDown[bin]   += point.downMaxDb * weight;
                binThreshOffsetDb[bin]  += point.centerGainDb * weight;
                binUpSmoothNorm[bin]    += ptUpSm * weight;
                binDownSmoothNorm[bin]  += ptDownSm * weight;
                binEffectiveUpSel[bin]  += (point.upSelectivity / 100.0f) * weight;
                binEffectiveDownSel[bin]+= (point.downSelectivity / 100.0f) * weight;
                
                if (point.useAutoSpeed) {
                    float ptSpd = point.speedPct / 100.0f;
                    binSpeedNorm[bin] += ptSpd * weight;
                    binAutoSpeedAccum[bin] += weight;
                } else {
                    binAttackMsAccum[bin] += point.attackMs * weight;
                    binReleaseMsAccum[bin] += point.releaseMs * weight;
                }
                
                binKneeAccum[bin] += point.kneeWidthDb * weight;
                binWeightSum[bin] += weight;
            }
        }

        for (int bin = 1; bin < numBins; ++bin) {
            float totalW = binWeightSum[bin];
            float W = juce::jlimit(0.0f, 1.0f, totalW);

            if (hasActiveGradients && totalW > 0.0001f) {
                float invW = 1.0f / totalW;
                
                float localAmt     = binEffectiveAmount[bin] * invW;
                float localUp      = binEffectiveUp[bin] * invW;
                float localDown    = binEffectiveDown[bin] * invW;
                float localUpSm    = binUpSmoothNorm[bin] * invW;
                float localDownSm  = binDownSmoothNorm[bin] * invW;
                float localThresh  = binThreshOffsetDb[bin] * invW;
                float localUpSel   = binEffectiveUpSel[bin] * invW;
                float localDnSel   = binEffectiveDownSel[bin] * invW;
                float localKnee    = binKneeAccum[bin] * invW;

                binEffectiveAmount[bin] = globalAmountNorm * (1.0f - W) + localAmt * W;
                binEffectiveUp[bin]     = globalUpMax * (1.0f - W) + localUp * W;
                binEffectiveDown[bin]   = globalDownMax * (1.0f - W) + localDown * W;
                binUpSmoothNorm[bin]    = globalUpSmoothNorm * (1.0f - W) + localUpSm * W;
                binDownSmoothNorm[bin]  = globalDownSmoothNorm * (1.0f - W) + localDownSm * W;
                binThreshOffsetDb[bin]  = localThresh * W;
                binEffectiveUpSel[bin]  = globalUpSelNorm * (1.0f - W) + localUpSel * W;
                binEffectiveDownSel[bin]= globalDownSelNorm * (1.0f - W) + localDnSel * W;
                binKneeWidth[bin]       = globalKneeWidthDb * (1.0f - W) + localKnee * W;

                float autoWeight = binAutoSpeedAccum[bin] * invW;
                float manualWeight = 1.0f - autoWeight;

                if (globalAutoSpeed) {
                    float localSpd = (autoWeight > 0.001f) ? (binSpeedNorm[bin] * invW / autoWeight) : globalSpeedNorm;
                    float finalSpd = globalSpeedNorm * (1.0f - W) + localSpd * W;
                    binEnvSpeed[bin] = finalSpd;
                    
                    binPAtk[bin] = 1.0f - std::exp(-1.0f / ((0.001f + finalSpd * 0.010f) * hopsPerSec));
                    binPRel[bin] = 1.0f - std::exp(-1.0f / ((0.050f + finalSpd * 0.150f) * hopsPerSec));
                    binRAtk[bin] = 1.0f - std::exp(-1.0f / ((0.100f + finalSpd * 0.400f) * hopsPerSec));
                    binRRel[bin] = 1.0f - std::exp(-1.0f / ((0.300f + finalSpd * 0.800f) * hopsPerSec));
                } else {
                    float localAttack = (manualWeight > 0.001f) ? (binAttackMsAccum[bin] * invW) : globalAttackMs;
                    float localRelease = (manualWeight > 0.001f) ? (binReleaseMsAccum[bin] * invW) : globalReleaseMs;
                    
                    float finalAttack = globalAttackMs * (1.0f - W) + localAttack * W;
                    float finalRelease = globalReleaseMs * (1.0f - W) + localRelease * W;
                    
                    float attackSec = std::max(0.0001f, finalAttack / 1000.0f);
                    float releaseSec = std::max(0.001f, finalRelease / 1000.0f);
                    
                    binEnvSpeed[bin] = 0.0f;
                    binPAtk[bin] = 1.0f - std::exp(-1.0f / (attackSec * hopsPerSec));
                    binPRel[bin] = 1.0f - std::exp(-1.0f / (releaseSec * hopsPerSec));
                    binRAtk[bin] = binPAtk[bin];
                    binRRel[bin] = binPRel[bin];
                }
            } else {
                binEffectiveAmount[bin] = globalAmountNorm;
                binEffectiveUp[bin]     = globalUpMax;
                binEffectiveDown[bin]   = globalDownMax;
                binEffectiveUpSel[bin]  = globalUpSelNorm;
                binEffectiveDownSel[bin]= globalDownSelNorm;
                binThreshOffsetDb[bin]  = 0.0f;
                binUpSmoothNorm[bin]    = globalUpSmoothNorm;
                binDownSmoothNorm[bin]  = globalDownSmoothNorm;
                binKneeWidth[bin]       = globalKneeWidthDb;

                if (globalAutoSpeed) {
                    binEnvSpeed[bin] = globalSpeedNorm;
                    binPAtk[bin] = 1.0f - std::exp(-1.0f / ((0.001f + globalSpeedNorm * 0.010f) * hopsPerSec));
                    binPRel[bin] = 1.0f - std::exp(-1.0f / ((0.050f + globalSpeedNorm * 0.150f) * hopsPerSec));
                    binRAtk[bin] = 1.0f - std::exp(-1.0f / ((0.100f + globalSpeedNorm * 0.400f) * hopsPerSec));
                    binRRel[bin] = 1.0f - std::exp(-1.0f / ((0.300f + globalSpeedNorm * 0.800f) * hopsPerSec));
                } else {
                    float attackSec = std::max(0.0001f, globalAttackMs / 1000.0f);
                    float releaseSec = std::max(0.001f, globalReleaseMs / 1000.0f);
                    
                    binEnvSpeed[bin] = 0.0f;
                    binPAtk[bin] = 1.0f - std::exp(-1.0f / (attackSec * hopsPerSec));
                    binPRel[bin] = 1.0f - std::exp(-1.0f / (releaseSec * hopsPerSec));
                    binRAtk[bin] = binPAtk[bin];
                    binRRel[bin] = binPRel[bin];
                }
            }
            
            binUpSmoothCoef[bin]   = juce::jlimit(0.0f, 1.0f, binUpSmoothNorm[bin]);
            binDownSmoothCoef[bin] = juce::jlimit(0.0f, 1.0f, binDownSmoothNorm[bin]);
        }
    }

    void process(juce::AudioBuffer<float>& buffer,
                 const juce::AudioBuffer<float>* sidechainBuffer,
                 float rawThresh,
                 std::atomic<float>* vizSpectrumL,
                 std::atomic<float>* vizDeltaL,
                 std::atomic<float>* vizSidechain,
                 std::atomic<float>* vizDetectorDb,
                 std::atomic<float>* vizEffectiveTargetDb)
    {
        const int numSamples  = buffer.getNumSamples();
        const int numChannels = juce::jmin(buffer.getNumChannels(), 2);
        if (numSamples == 0 || numChannels == 0) return;

        const bool hasSidechain = (sidechainBuffer != nullptr && sidechainBuffer->getNumChannels() > 0);
        const int scChannels = hasSidechain ? juce::jmin(sidechainBuffer->getNumChannels(), 2) : 0;

        refreshTargetIfNeeded();

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
                                rawThresh, vizSpectrumL, vizDeltaL, vizSidechain,
                                vizDetectorDb, vizEffectiveTargetDb);
            }
        }
    }

private:
    void processFFTFrame(int numChannels, int scChannels, float rawGlobalThreshDb,
                         std::atomic<float>* vizSpectrumL, std::atomic<float>* vizDeltaL,
                         std::atomic<float>* vizSidechain,
                         std::atomic<float>* vizDetectorDb,
                         std::atomic<float>* vizEffectiveTargetDb)
    {
        if (!envelopeSettled) {
            if (++settleCounter >= 16) envelopeSettled = true;
        }

        const float hopDurationSec = static_cast<float>(currentHopSize) / static_cast<float>(sr);
        const float threshSmoothCoef = 1.0f - std::exp(-hopDurationSec / 0.05f);

        if (!smoothedParamsInitialised) {
            smoothGlobalThresh = rawGlobalThreshDb;
            smoothedParamsInitialised = true;
        }
        smoothGlobalThresh += threshSmoothCoef * (rawGlobalThreshDb - smoothGlobalThresh);

        const int numBins = currentFFTSize / 2 + 1;
        const int halfSize = currentFFTSize / 2;
        const int nyquistBin = halfSize;
        const float fftNorm = 2.0f / static_cast<float>(currentFFTSize);
        
        std::fill(mainMagMax.begin(), mainMagMax.begin() + numBins, 0.0f);
        std::fill(sidechainMagMax.begin(), sidechainMagMax.begin() + numBins, 0.0f);
        
        const bool useSidechain = (scChannels > 0);
        
        // 1. ОБРАБАТЫВАЕМ SIDECHAIN
        if (useSidechain) {
            for (int ch = 0; ch < scChannels; ++ch) {
                std::fill(fftWorkBuf[ch].begin(),
                          fftWorkBuf[ch].begin() + 2 * currentFFTSize, 0.0f);
                for (int k = 0; k < currentFFTSize; ++k) {
                    const int readIdx = (fifoIndex + k) % currentFFTSize;
                    fftWorkBuf[ch][k] = sidechainFifo[ch][readIdx] * windows[activeFFTIndex][k];
                }
                fwdFFT[activeFFTIndex]->performRealOnlyForwardTransform(fftWorkBuf[ch].data(), false);
                
                for (int bin = 0; bin < numBins; ++bin) {
                    const float re = fftWorkBuf[ch][2 * bin];
                    const float im = fftWorkBuf[ch][2 * bin + 1];
                    float scale = fftNorm;
                    if (bin == 0 || bin == halfSize)
                        scale *= 0.5f;
                    float mag = std::sqrt(re * re + im * im) * scale;
                    sidechainMagMax[bin] = std::max(sidechainMagMax[bin], mag);
                }
            }
        }

        // 2. ОБРАБАТЫВАЕМ MAIN INPUT
        for (int ch = 0; ch < numChannels; ++ch) {
            std::fill(fftWorkBuf[ch].begin(),
                      fftWorkBuf[ch].begin() + 2 * currentFFTSize, 0.0f);
            for (int k = 0; k < currentFFTSize; ++k) {
                const int readIdx = (fifoIndex + k) % currentFFTSize;
                fftWorkBuf[ch][k] = inputFifo[ch][readIdx] * windows[activeFFTIndex][k];
            }
            fwdFFT[activeFFTIndex]->performRealOnlyForwardTransform(fftWorkBuf[ch].data(), false);

            for (int bin = 0; bin < numBins; ++bin) {
                const float re = fftWorkBuf[ch][2 * bin];
                const float im = fftWorkBuf[ch][2 * bin + 1];
                float scale = fftNorm;
                if (bin == 0 || bin == halfSize)
                    scale *= 0.5f;
                float mag = std::sqrt(re * re + im * im) * scale;
                mainMagMax[bin] = std::max(mainMagMax[bin], mag);
            }
        }

        // 3. ПУБЛИКАЦИЯ В ИНТЕРФЕЙС
        if (vizSpectrumL != nullptr) {
            const float releaseCoef = std::exp(-hopDurationSec / 0.15f);
            
            for (int bin = 1; bin < numBins; ++bin) {
                const float inMag = mainMagMax[bin];
                const float oldMag = vizSpectrumL[bin].load(std::memory_order_relaxed);
                
                float newMag = (inMag >= oldMag) ? inMag : (oldMag * releaseCoef + inMag * (1.0f - releaseCoef));
                vizSpectrumL[bin].store(newMag, std::memory_order_relaxed);

                if (vizSidechain != nullptr) {
                    const float scMag = useSidechain ? sidechainMagMax[bin] : 0.0f;
                    const float oldSc = vizSidechain[bin].load(std::memory_order_relaxed);
                    const float newSc = (scMag >= oldSc) ? scMag : (oldSc * releaseCoef + scMag * (1.0f - releaseCoef));
                    vizSidechain[bin].store(newSc, std::memory_order_relaxed);
                }
            }
        }

        // 4. ДЕТЕКТОР ОГИБАЮЩИХ
        for (int bin = 1; bin < numBins; ++bin) {
            const float mag = useSidechain ? sidechainMagMax[bin] : mainMagMax[bin];
            
            float pAtk = binPAtk[bin], pRel = binPRel[bin];
            if (!envelopeSettled) { pAtk = 1.0f; }

            peakEnvBin[bin] += (mag > peakEnvBin[bin] ? pAtk : pRel) * (mag - peakEnvBin[bin]);
            const float power = mag * mag;
            rmsEnvBin[bin]  += (power > rmsEnvBin[bin] ? pAtk : pRel) * (power - rmsEnvBin[bin]);
            
            const float rms = std::sqrt(std::max(rmsEnvBin[bin], 1.0e-12f));
            const float crestRatio = peakEnvBin[bin] / std::max(rms, 1.0e-7f); 
            const float transientWeight = juce::jlimit(0.0f, 1.0f, (crestRatio - 1.5f) / 3.0f);
            
            const float dynamicSpeed = binEnvSpeed[bin] * (1.0f - transientWeight * 0.85f);
            const float env = peakEnvBin[bin] * (1.0f - dynamicSpeed) + rms * dynamicSpeed;
            
            float rawDbFS = juce::Decibels::gainToDecibels(std::max(env, 1.0e-7f));
            const float freqHz = static_cast<float>(bin) * (static_cast<float>(sr) / static_cast<float>(currentFFTSize));
            
            const float tiltFreq = std::min(15000.0f, std::max(freqHz, 20.0f));
            const float perceptualTiltDb = 3.0f * std::log2(tiltFreq / 1000.0f);
            
            envDbFast[bin] = rawDbFS + perceptualTiltDb;

            /*
                Publish the exact detector used by DSP.
                Telemetry only — does not affect processing.
            */
            if (vizDetectorDb != nullptr)
            {
                vizDetectorDb[bin].store (
                    envDbFast[bin],
                    std::memory_order_relaxed);
            }
        }

        // =====================================================================
        // АДАПТИВНЫЙ РАСЧЕТ БАЗОВОЙ ЛИНИИ (Частотно-зависимый охват)
        // =====================================================================
        envPrefixDb[0] = 0.0f;
        for (int bin = 0; bin < numBins; ++bin) {
            envPrefixDb[bin + 1] = envPrefixDb[bin] + envDbFast[bin];
        }

        const auto& lnF = lnFreqs[activeFFTIndex];
        const float baselineRadiusLn = 1.25f * std::log(2.0f); // 1.25 октавы

        const int minBinSpan = (currentFFTSize >= 2048) ? 16 : (currentFFTSize >= 1024 ? 8 : 4);

        for (int bin = 1; bin < numBins; ++bin) {
            const float centerLn = lnF[bin];

            int bLeft = std::max(1, bin - minBinSpan);
            int bRight = std::min(numBins - 1, bin + minBinSpan);

            while (bLeft > 1 && (centerLn - lnF[bLeft]) < baselineRadiusLn) bLeft--;
            while (bRight < numBins - 1 && (lnF[bRight] - centerLn) < baselineRadiusLn) bRight++;

            const float sum = envPrefixDb[bRight + 1] - envPrefixDb[bLeft];
            const float count = static_cast<float>(bRight - bLeft + 1);
            spectralFloorDb[bin] = sum / count;
        }

        // =====================================================================
        // 5. РАЗДЕЛЬНЫЙ РАСЧЕТ UPWARD И DOWNWARD ДЕЛЬТЫ
        // =====================================================================
        std::fill(rawUpDeltaDb.begin(), rawUpDeltaDb.begin() + numBins, 0.0f);
        std::fill(rawDownDeltaDb.begin(), rawDownDeltaDb.begin() + numBins, 0.0f);

        for (int bin = 1; bin < numBins; ++bin) {
            const float envDb = envDbFast[bin];
            const float prominenceDb = envDb - spectralFloorDb[bin];
            const float targetLevelDb = binTargetDb[bin] + smoothGlobalThresh + binThreshOffsetDb[bin];

            /*
                Publish the exact per-bin target that is
                compared with envDb by the DSP.

                Includes EQ target, smoothed global
                threshold, and gradient offset.
            */
            if (vizEffectiveTargetDb != nullptr)
            {
                vizEffectiveTargetDb[bin].store (
                    targetLevelDb,
                    std::memory_order_relaxed);
            }

            const float deltaDb = envDb - targetLevelDb;

            const float rawEnergyDb = envDb; 
            const float noiseGate = juce::jlimit(0.0f, 1.0f, (rawEnergyDb + 65.0f) / 20.0f);
            
            const float amt = binEffectiveAmount[bin]; 
            const float depthMult = (amt <= 1.0f) ? amt : (1.0f + (amt - 1.0f) * 0.5f);

            const float kneeHalf = 2.5f;

            if (deltaDb < 0.0f) { 
                // ==================== UPWARD PATH ====================
                if (binEffectiveUp[bin] > 0.001f) {
                    const float underThreshDb = -deltaDb;
                    const float upSel = binEffectiveUpSel[bin];
                    
                    float upWeight = 1.0f;
                    if (upSel > 0.0f) {
                        float valleyDepth = std::max(0.0f, -prominenceDb);
                        float valleyMask = 1.0f - std::exp(-valleyDepth * valleyDepth / 10.0f);
                        upWeight = std::pow(1.0f - upSel, 2.0f) + upSel * valleyMask * 2.5f;
                    }
                    
                    rawUpDeltaDb[bin] = fast_tanh_sc(underThreshDb / 12.0f) 
                                     * binEffectiveUp[bin] * depthMult * noiseGate * upWeight;
                }
            } 
            else { 
                // ==================== DOWNWARD PATH ====================
                if (binEffectiveDown[bin] < -0.001f) {
                    const float overThreshDb = deltaDb;
                    const float downSel = binEffectiveDownSel[bin];

                    float smoothExceedDb = 0.0f;
                    if (overThreshDb < kneeHalf * 2.0f) {
                        smoothExceedDb = (overThreshDb * overThreshDb) / (4.0f * kneeHalf);
                    } else {
                        smoothExceedDb = overThreshDb - kneeHalf;
                    }

                    if (downSel > 0.001f) {
                        const float posProm = std::max(0.0f, prominenceDb - 1.2f);
                        
                        const float localCrest = peakEnvBin[bin] / std::max(std::sqrt(rmsEnvBin[bin]), 1.0e-7f);
                        const float sustainWeight = 1.0f - juce::jlimit(0.0f, 1.0f, (localCrest - 1.5f) / 2.0f);
                        
                        const float resonanceMultiplier = 1.0f + std::min(4.0f, (posProm / 2.0f) * sustainWeight);
                        const float resonanceWeight = 1.0f - std::exp(-posProm * posProm / 10.0f);
                        
                        const float broadFactor = std::pow(1.0f - downSel, 3.0f);
                        
                        const float surgicalScale = broadFactor + downSel * resonanceWeight * resonanceMultiplier;
                        const float finalDriveDb = smoothExceedDb * surgicalScale;
                        
                        const float maxDownDb = -binEffectiveDown[bin] * depthMult;
                        const float cutDb = maxDownDb * (finalDriveDb / (finalDriveDb + 10.0f));
                        
                        rawDownDeltaDb[bin] = -cutDb * noiseGate;
                    } 
                    else {
                        const float broadWeight = (downSel < -0.001f) 
                                                ? (1.0f + downSel * std::min(1.0f, std::max(0.0f, prominenceDb) / 4.0f)) 
                                                : 1.0f;
                        const float effExceed = smoothExceedDb * std::max(0.05f, broadWeight);
                        rawDownDeltaDb[bin] = fast_tanh_sc(effExceed / 12.0f) 
                                           * binEffectiveDown[bin] * depthMult * noiseGate;
                    }
                }
            }
        }

        // =====================================================================
        // 6. ANTI-FLANGER ГАУССОВ КЕРНЕЛ (Применяется ТОЛЬКО к Downward Cut)
        // =====================================================================
        std::fill(smoothedDelta.begin(), smoothedDelta.begin() + numBins, 0.0f);

        const float minNotchOctaves = 0.055f; 
        const float targetRadiusLn = minNotchOctaves * 0.693147f;
        const int maxSpreadBins = currentFFTSize / 6; 

        for (int bin = 2; bin < numBins - 2; ++bin) {
            float cut = rawDownDeltaDb[bin];
            if (cut >= -0.05f) continue;

            const float centerLn = lnF[bin];

            int kLeft = 1;
            while ((bin - kLeft) > 1 && (centerLn - lnF[bin - kLeft]) < targetRadiusLn * 1.5f && kLeft < maxSpreadBins) kLeft++;
            int kRight = 1;
            while ((bin + kRight) < (numBins - 1) && (lnF[bin + kRight] - centerLn) < targetRadiusLn * 1.5f && kRight < maxSpreadBins) kRight++;

            for (int k = -kLeft; k <= kRight; ++k) {
                int targetBin = bin + k;
                float distOct = std::abs(centerLn - lnF[targetBin]) / 0.693147f;
                float normDist = distOct / minNotchOctaves;
                float gWeight = std::exp(-0.5f * normDist * normDist);

                float weightedCut = cut * gWeight;
                if (weightedCut < smoothedDelta[targetBin]) {
                    smoothedDelta[targetBin] = weightedCut;
                }
            }
        }

        for (int bin = 1; bin < numBins; ++bin) {
            if (smoothedDelta[bin] < -0.01f) {
                rawDownDeltaDb[bin] = smoothedDelta[bin];
            }
        }

        // =====================================================================
        // 7. РАЗДЕЛЬНОЕ ПРЕФИКСНОЕ СГЛАЖИВАНИЕ ДЕЛЬТ
        // =====================================================================
        downSmoothingPrefixDb[0] = 0.0f;
        upSmoothingPrefixDb[0]   = 0.0f;
        for (int bin = 0; bin < numBins; ++bin) {
            downSmoothingPrefixDb[bin + 1] = downSmoothingPrefixDb[bin] + rawDownDeltaDb[bin];
            upSmoothingPrefixDb[bin + 1]   = upSmoothingPrefixDb[bin]   + rawUpDeltaDb[bin];
        }

        const float maxRadiusLn = std::log(2.0f);

        std::fill(finalDownDeltaBuf.begin(), finalDownDeltaBuf.begin() + numBins, 0.0f);
        std::fill(finalUpDeltaBuf.begin(), finalUpDeltaBuf.begin() + numBins, 0.0f);

        // Проход Downward Smoothing
        int dLeft = 1, dRight = 1;
        for (int bin = 1; bin < numBins; ++bin) {
            const float smoothAmount = binDownSmoothCoef[bin];
            const float radiusLn = smoothAmount * maxRadiusLn;

            if (radiusLn <= 1.0e-5f) {
                finalDownDeltaBuf[bin] = rawDownDeltaDb[bin];
                continue;
            }

            const float centerLn = lnF[bin];
            while (dLeft < bin && (centerLn - lnF[dLeft]) > radiusLn) dLeft++;
            dRight = std::max(dRight, bin);
            while (dRight + 1 < numBins && (lnF[dRight + 1] - centerLn) <= radiusLn) dRight++;

            const float sum = downSmoothingPrefixDb[dRight + 1] - downSmoothingPrefixDb[dLeft];
            const float count = static_cast<float>(dRight - dLeft + 1);
            const float averaged = sum / count;
            const float raw = rawDownDeltaDb[bin];

            finalDownDeltaBuf[bin] = raw + (averaged - raw) * smoothAmount;
        }

        // Проход Upward Smoothing
        int uLeft = 1, uRight = 1;
        for (int bin = 1; bin < numBins; ++bin) {
            const float smoothAmount = binUpSmoothCoef[bin];
            const float radiusLn = smoothAmount * maxRadiusLn;

            if (radiusLn <= 1.0e-5f) {
                finalUpDeltaBuf[bin] = rawUpDeltaDb[bin];
                continue;
            }

            const float centerLn = lnF[bin];
            while (uLeft < bin && (centerLn - lnF[uLeft]) > radiusLn) uLeft++;
            uRight = std::max(uRight, bin);
            while (uRight + 1 < numBins && (lnF[uRight + 1] - centerLn) <= radiusLn) uRight++;

            const float sum = upSmoothingPrefixDb[uRight + 1] - upSmoothingPrefixDb[uLeft];
            const float count = static_cast<float>(uRight - uLeft + 1);
            const float averaged = sum / count;
            const float raw = rawUpDeltaDb[bin];

            finalUpDeltaBuf[bin] = raw + (averaged - raw) * smoothAmount;
        }

        // Объединение сглаженных дельт
        for (int bin = 1; bin < numBins; ++bin) {
            smoothedDelta[bin] = finalDownDeltaBuf[bin] + finalUpDeltaBuf[bin];
            rawDeltaDb[bin]    = smoothedDelta[bin];
        }

        // 8. ЧАСТОТНО-ЗАВИСИМОЕ ВРЕМЕННОЕ СГЛАЖИВАНИЕ (Анти-Флаттер / Phase Guard)
        for (int bin = 1; bin < numBins; ++bin) {
            float freqHz = static_cast<float>(bin) * (static_cast<float>(sr) / static_cast<float>(currentFFTSize));
            
            float freqScale = std::sqrt(1000.0f / std::max(freqHz, 30.0f)); 
            
            float dynamicAtkTime = std::max(0.002f, 0.004f * freqScale);
            float dynamicRelTime = std::max(0.015f, 0.025f * freqScale);

            float gainAtkCoef = 1.0f - std::exp(-hopDurationSec / dynamicAtkTime);
            float gainRelCoef = 1.0f - std::exp(-hopDurationSec / dynamicRelTime);

            float targetLin = juce::Decibels::decibelsToGain(juce::jlimit(-48.0f, 48.0f, smoothedDelta[bin]));
            
            float coef = (targetLin < bandGainsBin[bin]) ? gainAtkCoef : gainRelCoef;
            bandGainsBin[bin] += coef * (targetLin - bandGainsBin[bin]);
        }

        for (int ch = 0; ch < numChannels; ++ch) {
            for (int bin = 1; bin < halfSize; ++bin) {
                const float gain = bandGainsBin[bin];

                const int positiveIndex = 2 * bin;
                fftWorkBuf[ch][positiveIndex]     *= gain;
                fftWorkBuf[ch][positiveIndex + 1] *= gain;

                const int mirrorBin = currentFFTSize - bin;
                const int mirrorIndex = 2 * mirrorBin;
                fftWorkBuf[ch][mirrorIndex]     *= gain;
                fftWorkBuf[ch][mirrorIndex + 1] *= gain;
            }
            // Nyquist находится в комплексном бине N/2 (индексы N и N+1)
            const int nyquistIndex = 2 * halfSize;
            const float nyquistGain = bandGainsBin[nyquistBin];
            fftWorkBuf[ch][nyquistIndex]     *= nyquistGain;
            fftWorkBuf[ch][nyquistIndex + 1] *= nyquistGain;
            
            if (ch == 0 && vizDeltaL != nullptr) {
                for (int bin = 1; bin < numBins; ++bin) {
                    const float gainDb = juce::Decibels::gainToDecibels(juce::jmax(1.0e-8f, bandGainsBin[bin]));
                    const float oldDelta = vizDeltaL[bin].load(std::memory_order_relaxed);
                    vizDeltaL[bin].store(oldDelta * 0.85f + gainDb * 0.15f, std::memory_order_relaxed);
                }
            }

            invFFT[activeFFTIndex]->performRealOnlyInverseTransform(fftWorkBuf[ch].data());
            for (int k = 0; k < currentFFTSize; ++k) {
                const int writeIdx = (fifoIndex + k) % currentFFTSize;
                outputFifo[ch][writeIdx] += fftWorkBuf[ch][k] * windows[activeFFTIndex][k];
            }
        }
    }

    void refreshTargetIfNeeded()
    {
        if (!targetDirty.exchange(false, std::memory_order_acquire)) return;

        const int numBins = currentFFTSize / 2 + 1;
        FastBiquadParams filters[8];
        int activeCount = 0;

        for (int i = 0; i < 8; ++i) {
            if (*pBandEnable[i] < 0.5f) continue;

            float f0 = *pBandFreq[i];
            float gainDb = *pBandGain[i];
            float q = *pBandQ[i];

            if (std::abs(gainDb) < 0.05f) continue;

            filters[activeCount++].setPeak(static_cast<float>(sr), f0, q, gainDb);
        }

        for (int bin = 1; bin < numBins; ++bin)
        {
            double totalMagSq = 1.0;
            double cw = cosW[activeFFTIndex][bin];
            double c2w = cos2W[activeFFTIndex][bin];

            for (int f = 0; f < activeCount; ++f) {
                totalMagSq *= filters[f].getMagSq(cw, c2w);
            }

            binTargetDb[bin] = static_cast<float>(10.0 * std::log10(std::max(1.0e-15, totalMagSq)));
        }
    }

    double sr = 44100.0;

    int currentFFTSize = 512;
    int currentFFTOrder = 8;
    int currentHopSize = 128;

    std::unique_ptr<juce::dsp::FFT> fwdFFT[3];
    std::unique_ptr<juce::dsp::FFT> invFFT[3];
    std::vector<float> windows[3];
    int activeFFTIndex = 0;

    std::atomic<float>* pBandEnable[8] = {nullptr};
    std::atomic<float>* pBandFreq[8] = {nullptr};
    std::atomic<float>* pBandGain[8] = {nullptr};
    std::atomic<float>* pBandQ[8] = {nullptr};

    float olaGain = 1.0f / 1.5f;

    std::vector<std::vector<float>> inputFifo;
    std::vector<std::vector<float>> outputFifo;
    std::vector<std::vector<float>> sidechainFifo;
    std::vector<std::vector<float>> fftWorkBuf;
    int fifoIndex  = 0;
    int hopCounter = 0;

    std::vector<double> cosW[3];
    std::vector<double> cos2W[3];
    std::vector<float> lnFreqs[3];
    std::vector<float> peakEnvBin, rmsEnvBin, bandGainsBin, rawDeltaDb, binTargetDb;
    std::vector<float> binPAtk, binPRel, binRAtk, binRRel, binEnvSpeed, binUpSmoothCoef, binDownSmoothCoef;
    std::vector<float> binEffectiveAmount, binEffectiveUp, binEffectiveDown;
    std::vector<float> binEffectiveUpSel, binEffectiveDownSel, binThreshOffsetDb, spectralFloorDb;
    std::vector<float> binKneeWidth;
    std::vector<float> binAttackCoef, binReleaseCoef;

    std::vector<float> envDbFast, smoothedDelta, binWeightSum, binSpeedNorm;
    std::vector<float> binUpSmoothNorm, binDownSmoothNorm;
    std::vector<float> envPrefixDb;
    std::vector<float> rawUpDeltaDb, rawDownDeltaDb;
    std::vector<float> upSmoothingPrefixDb, downSmoothingPrefixDb;
    std::vector<float> mainMagMax, sidechainMagMax;
    std::vector<float> finalDownDeltaBuf, finalUpDeltaBuf;
    std::vector<float> binAttackMsAccum, binReleaseMsAccum, binKneeAccum, binAutoSpeedAccum;

    float smoothGlobalThresh = 0.0f;
    bool smoothedParamsInitialised = false;

    std::atomic<bool> targetDirty { true };
    bool envelopeSettled = false;
    int  settleCounter = 0;

    int lookaheadSamples = 0;
    std::vector<float> lookaheadEnvBuffer;
    int lookaheadWritePos = 0;
};