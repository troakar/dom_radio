#pragma once
#include <JuceHeader.h>
#include "TapesDSP.h"
#include <cmath>
#include <algorithm>

namespace TroakarDSP
{

enum class ExtractorAlgorithm { WidebandTilt = 0, MultibandSpectral };

class OnePoleSplit
{
public:
    void setCutoff(float freq, double sr)
    {
        alpha = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * freq / static_cast<float>(sr));
    }

    forcedinline void process(float input, float& outLow, float& outHigh)
    {
        z += alpha * (input - z);
        outLow = z;
        outHigh = input - z;
    }

    void reset() { z = 0.0f; }

private:
    float alpha = 0.0f;
    float z = 0.0f;
};

class DetailExtractor
{
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        
        wbAttack = 1.0f - std::exp(-1.0f / (0.001f * static_cast<float>(sr)));  
        wbRelease = 1.0f - std::exp(-1.0f / (0.080f * static_cast<float>(sr))); 
        
        wbTiltLow.prepare(sr);
        wbTiltHigh.prepare(sr);

        split1.setCutoff(800.0f, sr);
        split2.setCutoff(150.0f, sr);
        split3.setCutoff(6000.0f, sr);

        mbAttack[0] = 1.0f - std::exp(-1.0f / (0.008f * static_cast<float>(sr)));
        mbRelease[0] = 1.0f - std::exp(-1.0f / (0.150f * static_cast<float>(sr)));
        
        mbAttack[1] = 1.0f - std::exp(-1.0f / (0.004f * static_cast<float>(sr)));
        mbRelease[1] = 1.0f - std::exp(-1.0f / (0.080f * static_cast<float>(sr)));
        
        mbAttack[2] = 1.0f - std::exp(-1.0f / (0.002f * static_cast<float>(sr)));
        mbRelease[2] = 1.0f - std::exp(-1.0f / (0.040f * static_cast<float>(sr)));
        
        mbAttack[3] = 1.0f - std::exp(-1.0f / (0.0005f * static_cast<float>(sr)));
        mbRelease[3] = 1.0f - std::exp(-1.0f / (0.015f * static_cast<float>(sr)));

        reset();
    }

    void reset()
    {
        wbEnvelope = 0.0f;
        currentDynGainDb = 0.0f;
        wbTiltLow.reset();
        wbTiltHigh.reset();

        split1.reset(); split2.reset(); split3.reset();
        for (int i = 0; i < 4; ++i) mbEnvelope[i] = 0.0f;
    }

    forcedinline float process(float input, float rawAmount, float tilt, ExtractorAlgorithm algo, float mixAmount = 1.0f)
    {
        if (rawAmount <= 0.001f || mixAmount <= 0.001f)
        {
            currentDynGainDb *= 0.9f;
            return input;
        }

        float detailSignal = 0.0f;
        constexpr float thresholdLin = 0.25f;
        constexpr float maxGainDb = 18.0f;

        if (algo == ExtractorAlgorithm::WidebandTilt)
        {
            const float absInput = std::abs(input);
            wbEnvelope += (absInput > wbEnvelope ? wbAttack : wbRelease) * (absInput - wbEnvelope);
            
            float detailGainDb = 0.0f;
            if (wbEnvelope < thresholdLin) {
                float diff = (thresholdLin - wbEnvelope) / thresholdLin; 
                detailGainDb = std::pow(diff, 1.5f) * maxGainDb; 
            }

            currentDynGainDb += (detailGainDb - currentDynGainDb) * 0.05f;
            
            const float tiltBoostDb = tilt * 6.0f; 
            wbTiltLow.setLowShelf(800.0, 0.4, juce::Decibels::decibelsToGain(-tiltBoostDb));
            wbTiltHigh.setHighShelf(1200.0, 0.4, juce::Decibels::decibelsToGain(tiltBoostDb));

            detailSignal = input * juce::Decibels::decibelsToGain(currentDynGainDb * rawAmount);
            detailSignal = wbTiltHigh.processSample(wbTiltLow.processSample(detailSignal));
        }
        else
        {
            float lowMidHalf, highAirHalf;
            split1.process(input, lowMidHalf, highAirHalf);

            float b[4];
            split2.process(lowMidHalf, b[0], b[1]);
            split3.process(highAirHalf, b[2], b[3]);

            const float weights[4] = {
                juce::jlimit(0.0f, 2.0f, 1.0f - tilt * 0.8f),
                juce::jlimit(0.0f, 2.0f, 1.0f - tilt * 0.3f),
                juce::jlimit(0.0f, 2.0f, 1.0f + tilt * 0.3f),
                juce::jlimit(0.0f, 2.0f, 1.0f + tilt * 0.8f)
            };

            float avgGainDb = 0.0f;

            for (int i = 0; i < 4; ++i)
            {
                const float absBand = std::abs(b[i]);
                mbEnvelope[i] += (absBand > mbEnvelope[i] ? mbAttack[i] : mbRelease[i]) * (absBand - mbEnvelope[i]);

                float bandGainDb = 0.0f;
                if (mbEnvelope[i] < thresholdLin) {
                    float diff = (thresholdLin - mbEnvelope[i]) / thresholdLin;
                    bandGainDb = (diff * diff * 0.6f + diff * 0.4f) * maxGainDb; 
                }

                avgGainDb += bandGainDb;
                
                const float linearGain = std::exp(bandGainDb * weights[i] * rawAmount * 0.1151292546f);
                detailSignal += b[i] * linearGain;
            }

            currentDynGainDb += ((avgGainDb * 0.25f) - currentDynGainDb) * 0.05f;
        }

        float detailsOnly = detailSignal - input;

        const float hdTrim = TapesDSP::harmonicDistortionTrim;
        float satDetails = fast_tanh(detailsOnly * 1.5f) + (detailsOnly * detailsOnly * 0.08f * hdTrim);
        
        satDetails = satDetails / (1.0f + std::abs(satDetails) * 0.2f);

        const float volumeCompensation = 1.0f / (1.0f + rawAmount * 1.25f); 

        float fullyProcessed = (input + satDetails) * volumeCompensation; 

        return input + (fullyProcessed - input) * mixAmount; 
    }

    float getCurrentDetailGainDb() const noexcept { return currentDynGainDb; }

private:
    double sr = 44100.0;
    
    float wbEnvelope = 0.0f;
    float wbAttack = 0.0f, wbRelease = 0.0f;
    FastBiquad wbTiltLow, wbTiltHigh;

    OnePoleSplit split1, split2, split3;
    float mbEnvelope[4] = {0.0f};
    float mbAttack[4] = {0.0f};
    float mbRelease[4] = {0.0f};

    float currentDynGainDb = 0.0f;
};

} // namespace TroakarDSP
