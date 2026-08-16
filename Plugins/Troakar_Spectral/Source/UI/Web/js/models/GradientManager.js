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
            this.active         = false;  // Disabled by default
            this.isSelected     = false;

            this.centerFreqHz   = freqHz !== undefined ? freqHz : 1000.0;
            this.centerGainDb   = gainDb !== undefined ? gainDb : 0.0;
            this.radiusOctaves  = 1.5;
            
            // Храним нормализованные параметры для отрисовки маркеров
            this.normAmount = 0;
            this.normUpMax = 0;
            this.normDownMax = 0;
            this.normSpeed = 0;
            this.normUpSmooth = 0;
            this.normDownSmooth = 0;
            this.normUpSel = 0;
            this.normDownSel = 0;
            this.normAttack = 0;
            this.normRelease = 0;
            this.normKnee = 0;
            this.useAutoSpeed = true;
        }
    }

    return class GradientManager {
        constructor() {
            this.availableColors = [
                '#e63232', '#f08c1e', '#28c864', '#28b4f0',
                '#c83cdc', '#dcc828', '#3cdc78', '#ff6496'
            ];
            this.points = [];
            for (let i = 0; i < 4; ++i) {
                this.points.push(new GradientPoint(i, 1000.0, 0.0, this.availableColors[i]));
            }
        }

        addPoint(freqHz, gainDb) {
            let pointId = -1;
            for (let i = 0; i < 4; ++i) {
                if (!this.points[i].active) {
                    pointId = i;
                    break;
                }
            }
            
            if (pointId < 0) return -1;

            const point = this.points[pointId];
            point.active = true;
            point.isSelected = true;
            point.centerFreqHz = Math.max(20.0, Math.min(20000.0, Number(freqHz) || 1000.0));
            point.centerGainDb = Math.max(-60.0, Math.min(60.0, Number(gainDb) || 0.0));
            point.radiusOctaves = 1.5;

            this.setActivePoint(pointId);
            return pointId;
        }

        removePoint(id) {
            let p = this.getPoint(id);
            if (p) {
                p.active = false;
                p.isSelected = false;
            }
        }

        getPoint(id) {
            return this.points[id] || null;
        }

        getActivePoint() {
            for (let i = 0; i < 4; ++i) {
                if (this.points[i].isSelected && this.points[i].active) return this.points[i];
            }
            return null;
        }

        setActivePoint(id) {
            for (let i = 0; i < 4; ++i) {
                this.points[i].isSelected = (this.points[i].id === id && this.points[i].active);
            }
        }

        clearActive() {
            for (let i = 0; i < 4; ++i) {
                this.points[i].isSelected = false;
            }
        }

        hasActivePoint() {
            return this.getActivePoint() !== null;
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

            for (let g = 0; g < 4; ++g) {
                let prefix = 'GRADIENT_' + g;
                let pt = this.points[g];
                
                let enableVal = juce.getParameter(prefix + '_ENABLE');
                pt.active = (enableVal !== null && typeof enableVal !== 'object') ? (enableVal >= 0.5) : false;

                let freqNorm = juce.getParameter(prefix + '_CENTER_FREQ');
                let gainNorm = juce.getParameter(prefix + '_CENTER_GAIN');
                let bwNorm   = juce.getParameter(prefix + '_BANDWIDTH');

                if (freqNorm !== null && typeof freqNorm !== 'object') {
                    pt.centerFreqHz = 20.0 * Math.pow(10.0, freqNorm * 3.0);
                }
                if (gainNorm !== null && typeof gainNorm !== 'object') {
                    pt.centerGainDb = gainNorm * 120.0 - 60.0;
                }
                if (bwNorm !== null && typeof bwNorm !== 'object') {
                    pt.radiusOctaves = bwNorm * 3.5 + 0.5;
                }

                pt.normAmount = juce.getParameter(prefix + '_AMOUNT') || 0;
                pt.normUpMax = juce.getParameter(prefix + '_UP_MAX') || 0;
                pt.normDownMax = juce.getParameter(prefix + '_DOWN_MAX') || 0;
                pt.normSpeed = juce.getParameter(prefix + '_SPEED') || 0;
                pt.normUpSmooth = juce.getParameter(prefix + '_UP_SMOOTH') || 0;
                pt.normDownSmooth = juce.getParameter(prefix + '_DOWN_SMOOTH') || 0;
                pt.normUpSel = juce.getParameter(prefix + '_UP_SEL') || 0;
                pt.normDownSel = juce.getParameter(prefix + '_DOWN_SEL') || 0;
                pt.normAttack = juce.getParameter(prefix + '_ATTACK') || 0;
                pt.normRelease = juce.getParameter(prefix + '_RELEASE') || 0;
                pt.normKnee = juce.getParameter(prefix + '_KNEE') || 0;
                
                pt.useAutoSpeed = (juce.getParameter(prefix + '_AUTO_SPEED') || 0) >= 0.5;
            }
        }

        syncPointToJuce(juce, pointId) {
            if (!juce || !juce.isJuceAvailable()) return;
            var p = this.getPoint(pointId);
            if (!p) return;

            var prefix = 'GRADIENT_' + pointId;
            juce.setParameter(prefix + '_ENABLE',       p.active ? 1.0 : 0.0);
            juce.setParameter(prefix + '_CENTER_FREQ',  Math.log10(p.centerFreqHz / 20.0) / 3.0);
            juce.setParameter(prefix + '_CENTER_GAIN',  (p.centerGainDb + 60.0) / 120.0);
            juce.setParameter(prefix + '_BANDWIDTH',    (p.radiusOctaves - 0.5) / 3.5);
        }
    };
});
