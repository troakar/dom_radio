// TROAKAR SPECTRAL - Gradient Band Model
// Direct port of GradientBandModel.h
(function (root, factory) {
    if (typeof module !== 'undefined' && module.exports) {
        module.exports = factory();
    } else {
        root.GradientManager = factory();
    }
})(typeof window !== 'undefined' ? window : this, function () {

    class GradientPoint {
        constructor(id, freqHz, gainDb, color) {
            this.id             = id;
            this.name           = 'G' + (id + 1);
            this.color          = color;
            this.active         = true;   // включён ли градиент в DSP
            this.isSelected     = false;  // выделен ли в интерфейсе

            this.centerFreqHz   = freqHz !== undefined ? freqHz : 1000.0;
            this.centerGainDb   = gainDb !== undefined ? gainDb : 0.0;

            this.radiusOctaves  = 1.5;
            this.radiusDb       = 12.0;

            this.amountPct      = 100.0;
            this.upMaxDb        = 4.0;
            this.downMaxDb      = -12.0;

            this.useAutoSpeed   = true;
            this.speedPct       = 50.0;
            this.attackMs       = 5.0;
            this.releaseMs      = 150.0;
            this.kneeWidthDb    = 3.0;

            this.upSmoothPct    = 50.0;
            this.downSmoothPct  = 15.0;
            this.upSelectivity  = 0.0;
            this.downSelectivity= 0.0;
        }
    }

    return class GradientManager {
        constructor() {
            this.availableColors = [
                '#e63232', // G1 Red      — fromRGB(230, 50, 50)
                '#f08c1e', // G2 Orange   — fromRGB(240, 140, 30)
                '#28c864', // G3 Green    — fromRGB(40, 200, 100)
                '#28b4f0', // G4 Cyan     — fromRGB(40, 180, 240)
                '#c83cdc', // Purple      — fromRGB(200, 60, 220)
                '#dcc828', // Yellow      — fromRGB(220, 200, 40)
                '#3cdc78', // Mint        — fromRGB(60, 220, 120)
                '#ff6496'  // Pink        — fromRGB(255, 100, 150)
            ];
            this.points     = [];
            this.nextId     = 0;
        }

        addPoint(freqHz, gainDb) {
            if (this.points.length >= 4) return -1;

            var newId   = 0;
            for (var i = 0; i < 4; ++i) {
                var taken = false;
                for (var j = 0; j < this.points.length; ++j) {
                    if (this.points[j].id === i) { taken = true; break; }
                }
                if (!taken) { newId = i; break; }
            }

            var color   = this.availableColors[newId % this.availableColors.length];
            var pt      = new GradientPoint(newId, freqHz, gainDb, color);
            this.points.push(pt);
            this.nextId = newId + 1;
            return newId;
        }

        removePoint(id) {
            this.points = this.points.filter(function (p) { return p.id !== id; });
        }

        getPoint(id) {
            for (var i = 0; i < this.points.length; ++i) {
                if (this.points[i].id === id) return this.points[i];
            }
            return null;
        }

        getActivePoint() {
            for (var i = 0; i < this.points.length; ++i) {
                if (this.points[i].isSelected) return this.points[i];
            }
            return null;
        }

        setActivePoint(id) {
            for (var i = 0; i < this.points.length; ++i) {
                this.points[i].isSelected = (this.points[i].id === id);
            }
        }

        clearActive() {
            for (var i = 0; i < this.points.length; ++i) {
                this.points[i].isSelected = false;
            }
        }

        hasActivePoint() {
            for (var i = 0; i < this.points.length; ++i) {
                if (this.points[i].isSelected) return true;
            }
            return false;
        }

        getWeightAt(pointId, freqHz) {
            var p = this.getPoint(pointId);
            if (!p || !p.active) return 0.0;
            var logDist     = Math.abs(Math.log2(freqHz / p.centerFreqHz));
            var normDist    = logDist / Math.max(0.1, p.radiusOctaves);
            if (normDist < 1.0) {
                return 0.5 + 0.5 * Math.cos(normDist * Math.PI);
            }
            return 0.0;
        }

        syncFromJuce(juce) {
            if (!juce || !juce.isJuceAvailable()) return;
            this.points = [];

            for (var g = 0; g < 4; ++g) {
                var prefix    = 'GRADIENT_' + g;
                var enableVal = juce.getParameter(prefix + '_ENABLE');
                if ((enableVal || 0) < 0.5) continue;

                var id   = g;
                var freq = juce.getParameter(prefix + '_CENTER_FREQ');
                var gain = juce.getParameter(prefix + '_CENTER_GAIN');

                var color = this.availableColors[id % this.availableColors.length];
                var pt    = new GradientPoint(id, freq, gain, color);

                pt.active        = true;
                pt.isSelected    = false;
                pt.radiusOctaves = (juce.getParameter(prefix + '_BANDWIDTH') || 0.4) * 3.5 + 0.5;
                pt.amountPct     = (juce.getParameter(prefix + '_AMOUNT')    || 1) * 100;
                pt.upMaxDb       = (juce.getParameter(prefix + '_UP_MAX')    || 0) * 24;
                pt.downMaxDb     = -(juce.getParameter(prefix + '_DOWN_MAX') || 0) * 24;
                pt.upSmoothPct   = (juce.getParameter(prefix + '_UP_SMOOTH')   || 0.5) * 100;
                pt.downSmoothPct = (juce.getParameter(prefix + '_DOWN_SMOOTH') || 0.15) * 100 / 3;
                pt.upSelectivity = (juce.getParameter(prefix + '_UP_SEL')    || 0.5) * 200 - 100;
                pt.downSelectivity = (juce.getParameter(prefix + '_DOWN_SEL') || 0.5) * 200 - 100;
                pt.speedPct    = (juce.getParameter(prefix + '_SPEED')       || 0.5) * 100;
                pt.attackMs    = (juce.getParameter(prefix + '_ATTACK')       || 0.5) * 199.9 + 0.1;
                pt.releaseMs   = (juce.getParameter(prefix + '_RELEASE')      || 0.5) * 490 + 10;
                pt.kneeWidthDb = (juce.getParameter(prefix + '_KNEE')        || 0.5) * 12;

                this.points.push(pt);
            }
        }

        syncPointToJuce(juce, pointId) {
            if (!juce || !juce.isJuceAvailable()) return;
            var p = this.getPoint(pointId);
            if (!p) {
                juce.setParameter('GRADIENT_' + pointId + '_ENABLE', 0.0);
                return;
            }

            var prefix = 'GRADIENT_' + pointId;
            juce.setParameter(prefix + '_ENABLE',       p.active ? 1.0 : 0.0);
            juce.setParameter(prefix + '_CENTER_FREQ',  Math.log10(p.centerFreqHz / 20.0) / 3.0);
            juce.setParameter(prefix + '_CENTER_GAIN',  (p.centerGainDb + 60.0) / 120.0);
            juce.setParameter(prefix + '_BANDWIDTH',    (p.radiusOctaves - 0.5) / 3.5);
            juce.setParameter(prefix + '_AMOUNT',       p.amountPct / 100.0);
            juce.setParameter(prefix + '_UP_MAX',       p.upMaxDb / 24.0);
            juce.setParameter(prefix + '_DOWN_MAX',     p.downMaxDb / 24.0);
            juce.setParameter(prefix + '_SPEED',        p.speedPct / 100.0);
            juce.setParameter(prefix + '_UP_SMOOTH',    p.upSmoothPct / 100.0);
            juce.setParameter(prefix + '_DOWN_SMOOTH',  p.downSmoothPct / 100.0 * 3);
            juce.setParameter(prefix + '_UP_SEL',       (p.upSelectivity + 100) / 200);
            juce.setParameter(prefix + '_DOWN_SEL',     (p.downSelectivity + 100) / 200);
            juce.setParameter(prefix + '_ATTACK',       (p.attackMs - 0.1) / 199.9);
            juce.setParameter(prefix + '_RELEASE',      (p.releaseMs - 10) / 490);
            juce.setParameter(prefix + '_KNEE',         p.kneeWidthDb / 12.0);
        }
    };
});
