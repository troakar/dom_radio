// DOM RADIO MASTER - Physical Tape Profiles Port (1:1 from TapesDSP.h)
(function (root, factory) {
    if (typeof module !== 'undefined' && module.exports) {
        module.exports = factory();
    } else {
        root.TapeProfiles = factory();
    }
})(typeof window !== 'undefined' ? window : this, function () {
    'use strict';

    // Точные спецификации лент из TapesDSP.h
    const PROFILES = [
        {
            name: "SVEMA A4409",
            headBumpFreq: 60.0,
            headBumpGainDb: 2.2,
            preEmphasisFreq: 15000.0,
            preEmphasisGainDb: 13.5,
            gapLossBaseFreq: 11500.0,
            midContourFreq: 320.0,
            midContourGainDb: -1.6,
            midContourQ: 0.85,
            highCutAtFullLevel: 9500.0
        },
        {
            name: "ORWO TYP 106",
            headBumpFreq: 75.0,
            headBumpGainDb: 2.6,
            preEmphasisFreq: 12000.0,
            preEmphasisGainDb: 10.0,
            gapLossBaseFreq: 11000.0,
            midContourFreq: 180.0,
            midContourGainDb: 1.8,
            midContourQ: 0.75,
            highCutAtFullLevel: 8500.0
        },
        {
            name: "SCOTCH 2500 HAEG",
            headBumpFreq: 42.0,
            headBumpGainDb: 0.8,
            preEmphasisFreq: 18000.0,
            preEmphasisGainDb: 6.0,
            gapLossBaseFreq: 19500.0,
            midContourFreq: 450.0,
            midContourGainDb: -0.6,
            midContourQ: 0.90,
            highCutAtFullLevel: 17500.0
        },
        {
            name: "BASF SPR 50 LHL",
            headBumpFreq: 32.0,
            headBumpGainDb: 0.3,
            preEmphasisFreq: 19000.0,
            preEmphasisGainDb: 3.5,
            gapLossBaseFreq: 22000.0,
            midContourFreq: 1000.0,
            midContourGainDb: 0.0,
            midContourQ: 0.70,
            highCutAtFullLevel: 21000.0
        }
    ];

    return {
        getProfile: function (index) {
            const idx = Math.max(0, Math.min(PROFILES.length - 1, Math.round(Number(index) || 0)));
            return PROFILES[idx];
        },
        speedIpsToNorm: function (ips) {
            const minIps = 1.875, maxIps = 15.0;
            const clamped = Math.max(minIps, Math.min(maxIps, ips));
            return Math.log2(clamped / minIps) / Math.log2(maxIps / minIps);
        }
    };
});
