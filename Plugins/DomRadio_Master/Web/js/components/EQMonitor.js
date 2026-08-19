// DOM RADIO MASTER - Canvas Precision EQ & Live RTA Spectrum Monitor
(function (root, factory) {
    if (typeof module !== 'undefined' && module.exports) {
        module.exports = factory(root.JuceBridge, root.TapeProfiles);
    } else {
        root.EQMonitor = factory(root.JuceBridge, root.TapeProfiles);
    }
})(typeof window !== 'undefined' ? window : this, function (JuceBridge, TapeProfiles) {
    'use strict';

    return class EQMonitor {
        constructor(canvasId) {
            this.canvas = document.getElementById(canvasId);
            if (!this.canvas) return;
            this.ctx = this.canvas.getContext('2d');

            this.width = 0;
            this.height = 0;
            this.maxDb = 18.0;

            // Полный стек параметров аппарата
            this.params = {
                BASS_FREQ: 60, BASS: 0,
                TREBLE_FREQ: 10000, TREBLE: 0,
                AIR: 0, DECAY: 0, AGE: 0,
                TAPE_SPEED: 15.0, TAPE_MODEL: 0, EQ_STD: 0,
                MIX: 100, WOW_AMOUNT: 0, FLUTTER_AMOUNT: 0,
                TAPE_NOISE: 0, HUM: 0, SCRAPE_FLUTTER: 0
            };

            this.spectrum = new Array(48).fill(0);
            this.smoothSpectrum = new Array(48).fill(0);

            this.draggingNode = -1;
            this.dragStartX = 0;
            this.dragStartY = 0;

            this.amber = 'rgba(255, 176, 40, 1.0)';
            this.amberGlow = 'rgba(255, 150, 20, 0.45)';

            this.init();
        }

        init() {
            if (window.ResizeObserver && this.canvas.parentElement) {
                const ro = new ResizeObserver(() => this.resize());
                ro.observe(this.canvas.parentElement);
            }

            this.canvas.addEventListener('pointerdown', (e) => this.onPointerDown(e));
            window.addEventListener('pointermove', (e) => this.onPointerMove(e));
            window.addEventListener('pointerup', (e) => this.onPointerUp(e));
            window.addEventListener('pointercancel', (e) => this.onPointerUp(e));

            // Прием FFT данных от C++
            const attachTelemetry = () => {
                if (window.__JUCE__ && window.__JUCE__.backend) {
                    window.__JUCE__.backend.addEventListener('telemetry', (data) => {
                        if (data && Array.isArray(data.spectrum)) {
                            for (let i = 0; i < data.spectrum.length; ++i) {
                                this.spectrum[i] = Number(data.spectrum[i]) || 0;
                            }
                        }
                    });
                }
            };
            attachTelemetry();
            setTimeout(attachTelemetry, 500);

            const PARAM_MAP = {
                BASS_FREQ:      { min: 30, max: 300 },
                BASS:           { min: -18, max: 18 },
                TREBLE_FREQ:    { min: 1000, max: 15000 },
                TREBLE:         { min: -18, max: 18 },
                AIR:            { min: 0, max: 15 },
                DECAY:          { min: 0, max: 10 },
                AGE:            { min: 0, max: 50 },
                TAPE_SPEED:     { min: 3.75, max: 30, skew: 0.55 },
                TAPE_MODEL:     { min: 0, max: 3, isChoice: true },
                EQ_STD:         { min: 0, max: 1, isChoice: true },
                MIX:            { min: 0, max: 100 },
                WOW_AMOUNT:     { min: 0, max: 100 },
                FLUTTER_AMOUNT: { min: 0, max: 100 },
                TAPE_NOISE:     { min: 0, max: 100 },
                HUM:            { min: 0, max: 100 },
                SCRAPE_FLUTTER: { min: 0, max: 100 }
            };

            const updateParam = (id, normVal) => {
                const cfg = PARAM_MAP[id];
                if (!cfg) return;
                const n = Math.max(0, Math.min(1, Number(normVal) || 0));

                if (cfg.isChoice) {
                    this.params[id] = Math.round(n * (cfg.max - cfg.min));
                } else if (cfg.skew) {
                    const linear = Math.pow(n, 1.0 / cfg.skew);
                    this.params[id] = cfg.min + linear * (cfg.max - cfg.min);
                } else {
                    this.params[id] = cfg.min + n * (cfg.max - cfg.min);
                }
            };

            if (JuceBridge) {
                Object.keys(PARAM_MAP).forEach(id => {
                    JuceBridge.onParamUpdate(id, (val) => updateParam(id, val));
                    Promise.resolve(JuceBridge.getParameter(id)).then(val => {
                        if (val !== null && val !== undefined) updateParam(id, val);
                    });
                });
            }

            this.startLoop();
        }

        resize() {
            if (!this.canvas || !this.canvas.parentElement) return;
            const cw = this.canvas.parentElement.clientWidth;
            const ch = this.canvas.parentElement.clientHeight;

            if (cw > 0 && ch > 0 && (cw !== this.width || ch !== this.height)) {
                this.width = cw;
                this.height = ch;
                const dpr = window.devicePixelRatio || 1;
                this.canvas.width = Math.round(this.width * dpr);
                this.canvas.height = Math.round(this.height * dpr);
                this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
            }
        }

        freqToX(f) {
            const minF = 20.0, maxF = 20000.0;
            const norm = Math.log10(Math.max(minF, Math.min(maxF, f)) / minF) / Math.log10(maxF / minF);
            return norm * this.width;
        }

        xToFreq(x) {
            const minF = 20.0, maxF = 20000.0;
            const norm = Math.max(0, Math.min(1, x / this.width));
            return minF * Math.pow(maxF / minF, norm);
        }

        gainToY(dB) {
            return this.height * 0.5 - (dB / this.maxDb) * (this.height * 0.42);
        }

        yToGain(y) {
            return -((y - this.height * 0.5) / (this.height * 0.42)) * this.maxDb;
        }

        // =========================================================================
        // ТОЧНЫЙ РАСЧЕТ АЧХ: СВЯЗКА TAPE GAP LOSS, DECAY, PULTEC И HEAD BUMP
        // =========================================================================
        getCompositeMagnitudeDb(freq) {
            const effMix = this.params.MIX / 100.0;
            if (effMix <= 0.001) return 0.0;

            const profile = TapeProfiles ? TapeProfiles.getProfile(this.params.TAPE_MODEL) : {
                headBumpFreq: 60.0, headBumpGainDb: 2.2, preEmphasisFreq: 15000.0,
                preEmphasisGainDb: 13.5, gapLossBaseFreq: 11500.0, midContourFreq: 320.0,
                midContourGainDb: -1.6, midContourQ: 0.85
            };

            const speedIps = this.params.TAPE_SPEED;
            const speedScale = Math.max(0.125, Math.min(1.0, speedIps / 15.0));
            const speedNorm = TapeProfiles ? TapeProfiles.speedIpsToNorm(speedIps) : 0.75;
            const eqMode = this.params.EQ_STD; // 0 = CCIR, 1 = NAB
            const ageNorm = this.params.AGE / 50.0;

            let totalDb = 0.0;

            // 1. HEAD BUMP (Основной резонанс + Дип + Вторичный горб)
            const bumpSpeedScale = Math.pow(speedScale, 0.70);
            const eqStdFreqScale = (eqMode === 0) ? 1.15 : 0.85;
            const f0 = profile.headBumpFreq * bumpSpeedScale * eqStdFreqScale * (1.0 - ageNorm * 0.08);

            const baseBumpGain = profile.headBumpGainDb * (1.0 + (1.0 - speedScale) * 0.55)
                               * (eqMode === 0 ? 1.35 : 0.75) * (1.0 - ageNorm * 0.15) * effMix;

            // Primary peak
            const w1 = Math.log2(freq / f0) * 1.5;
            totalDb += baseBumpGain * Math.exp(-w1 * w1 * 2.0);

            // Dip
            const fDip = f0 * 1.85;
            const wDip = Math.log2(freq / fDip) * 1.8;
            totalDb -= (baseBumpGain * 0.75) * Math.exp(-wDip * wDip * 2.5);

            // Secondary peak
            const fSec = f0 * 2.7;
            const wSec = Math.log2(freq / fSec) * 2.0;
            totalDb += (baseBumpGain * 0.35) * Math.exp(-wSec * wSec * 3.0);

            // 2. MID CONTOUR (Индивидуальный характер середины ленты)
            if (Math.abs(profile.midContourGainDb) > 0.05) {
                const wMid = Math.log2(freq / profile.midContourFreq) * profile.midContourQ;
                totalDb += (profile.midContourGainDb * effMix) * Math.exp(-wMid * wMid * 2.0);
            }

            // 3. GAP LOSS & DECAY (Честный расчет фильтра Баттерворта 2-го порядка -12 дБ/октава)
            const eqStdGapScale = (eqMode === 0) ? 1.10 : 0.90;
            const speedLossFreq = profile.gapLossBaseFreq * Math.pow(speedScale, 0.82) * eqStdGapScale;
            const wearFactor = 1.0 - ageNorm * 0.22;

            // Физический сдвиг частоты среза от ручки DECAY (до -8 кГц)
            const decayCurve = Math.pow(this.params.DECAY / 10.0, 1.15) * 10.0;
            const magneticDecay = decayCurve * 800.0;
            const targetGapFreq = speedLossFreq * wearFactor - magneticDecay;

            // Частота среза фильтра с учетом регулятора MIX
            const effectiveGapFreq = Math.max(1200.0, targetGapFreq + (22050.0 - targetGapFreq) * (1.0 - effMix));

            // Точная передаточная функция Баттерворта: 1 / sqrt(1 + (f / fc)^4)
            const ratioGap = freq / effectiveGapFreq;
            const gapMagSq = 1.0 / (1.0 + Math.pow(ratioGap, 4.0));
            const gapLossDb = 10.0 * Math.log10(Math.max(1e-6, gapMagSq));
            totalDb += gapLossDb;

            // 4. AIR RESONANCE (Ручка AIR)
            if (this.params.AIR > 0.05) {
                const airFreq = 7500.0 + speedNorm * 1500.0;
                const ratioAir = freq / airFreq;
                const airGain = this.params.AIR * effMix;
                totalDb += airGain / (1.0 + Math.pow(1.0 / Math.max(0.01, ratioAir), 2.0));
            }

            // 5. PULTEC BASS SHELF
            if (Math.abs(this.params.BASS) > 0.05) {
                const ratio = freq / this.params.BASS_FREQ;
                totalDb += (this.params.BASS * effMix) / (1.0 + Math.pow(ratio, 1.8));
            }

            // 6. PULTEC TREBLE SHELF
            if (Math.abs(this.params.TREBLE) > 0.05) {
                const ratio = this.params.TREBLE_FREQ / Math.max(1.0, freq);
                totalDb += (this.params.TREBLE * effMix) / (1.0 + Math.pow(ratio, 1.8));
            }

            return Math.max(-24.0, Math.min(18.0, totalDb));
        }

        drawBackground() {
            const ctx = this.ctx;

            ctx.fillStyle = 'rgba(255, 176, 40, 0.12)';
            [50, 100, 250, 500, 1000, 2500, 5000, 10000, 20000].forEach(f => {
                const x = this.freqToX(f);
                for (let y = 6; y < this.height - 4; y += 8) {
                    ctx.beginPath(); ctx.arc(x, y, 0.8, 0, Math.PI * 2); ctx.fill();
                }
            });

            [-18, -12, -6, 0, 6, 12, 18].forEach(db => {
                const y = this.gainToY(db);
                ctx.fillStyle = db === 0 ? 'rgba(255, 176, 40, 0.28)' : 'rgba(255, 176, 40, 0.1)';
                for (let x = 6; x < this.width - 4; x += 8) {
                    ctx.beginPath(); ctx.arc(x, y, 0.8, 0, Math.PI * 2); ctx.fill();
                }
            });

            ctx.fillStyle = 'rgba(255, 176, 40, 0.4)';
            ctx.font = 'bold 8px "GOST Type B", monospace';
            ctx.textAlign = 'center';
            ctx.fillText('100', this.freqToX(100), this.height - 4);
            ctx.fillText('1K', this.freqToX(1000), this.height - 4);
            ctx.fillText('10K', this.freqToX(10000), this.height - 4);
        }

        drawSpectrum() {
            const ctx = this.ctx;
            const numBins = this.spectrum.length;
            const barWidth = this.width / numBins;

            for (let i = 0; i < numBins; ++i) {
                this.smoothSpectrum[i] += (this.spectrum[i] - this.smoothSpectrum[i]) * 0.35;
                const val = this.smoothSpectrum[i];
                if (val < 0.015) continue;

                const h = val * (this.height * 0.72);
                const x = i * barWidth;
                const y = this.height - h;

                const grad = ctx.createLinearGradient(0, y, 0, this.height);
                grad.addColorStop(0, 'rgba(255, 176, 40, 0.35)');
                grad.addColorStop(1, 'rgba(255, 120, 20, 0.03)');

                ctx.fillStyle = grad;
                ctx.fillRect(x + 1, y, barWidth - 2, h);
            }
        }

        render() {
            this.resize();
            if (this.width <= 0 || this.height <= 0) return;

            this.ctx.clearRect(0, 0, this.width, this.height);
            this.drawBackground();
            this.drawSpectrum();

            const ctx = this.ctx;
            const t = performance.now() * 0.001;

            // Динамические флуктуации ленты (детонация и шероховатость)
            const mixNorm = this.params.MIX / 100.0;
            const wowDepth = (this.params.WOW_AMOUNT / 100.0) * mixNorm;
            const flutterDepth = (this.params.FLUTTER_AMOUNT / 100.0) * mixNorm;
            const scrape = (this.params.SCRAPE_FLUTTER / 100.0) * mixNorm;
            const noiseLvl = (this.params.TAPE_NOISE / 100.0) * mixNorm;
            const humLvl = (this.params.HUM / 100.0) * mixNorm;

            // Отрисовка шумового пола
            if (noiseLvl > 0.01 || humLvl > 0.01) {
                const noiseHeight = (noiseLvl * 24.0) + (humLvl * 12.0);
                ctx.beginPath();
                ctx.moveTo(0, this.height);
                for (let x = 0; x <= this.width; x += 4) {
                    const jitter = (Math.random() - 0.5) * (noiseHeight * 0.8);
                    const humWave = Math.sin(x * 0.15 + t * 12.0) * (humLvl * 8.0);
                    ctx.lineTo(x, this.height - (noiseHeight * 0.35) + jitter + humWave);
                }
                ctx.lineTo(this.width, this.height);
                ctx.closePath();
                ctx.fillStyle = 'rgba(255, 176, 40, ' + (0.08 + noiseLvl * 0.12) + ')';
                ctx.fill();
            }

            // 1. Градиентная заливка под АЧХ
            ctx.beginPath();
            for (let x = 0; x <= this.width; x += 2) {
                const freq = this.xToFreq(x);
                let db = this.getCompositeMagnitudeDb(freq);

                // Высокочастотный шелест ленты
                if (freq > 3000.0 && scrape > 0.01) {
                    db += (Math.random() - 0.5) * (scrape * 0.6);
                }

                // Микро-модуляция Wow/Flutter
                const nx = x / this.width;
                const wowRipple = wowDepth * 0.8 * Math.sin(nx * 6.0 - t * 4.0);
                const flutterRipple = flutterDepth * 0.4 * Math.sin(nx * 28.0 - t * 20.0);

                const y = Math.max(3, Math.min(this.height - 3, this.gainToY(db) + wowRipple + flutterRipple));

                if (x === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            }
            ctx.lineTo(this.width, this.height);
            ctx.lineTo(0, this.height);
            ctx.closePath();

            const fillGrad = ctx.createLinearGradient(0, this.gainToY(12), 0, this.height);
            fillGrad.addColorStop(0, 'rgba(255, 176, 40, 0.18)');
            fillGrad.addColorStop(1, 'rgba(255, 176, 40, 0.0)');
            ctx.fillStyle = fillGrad;
            ctx.fill();

            // 2. Линия АЧХ ленты
            ctx.beginPath();
            for (let x = 0; x <= this.width; x += 2) {
                const freq = this.xToFreq(x);
                let db = this.getCompositeMagnitudeDb(freq);

                if (freq > 3000.0 && scrape > 0.01) {
                    db += (Math.random() - 0.5) * (scrape * 0.6);
                }

                const nx = x / this.width;
                const wowRipple = wowDepth * 0.8 * Math.sin(nx * 6.0 - t * 4.0);
                const flutterRipple = flutterDepth * 0.4 * Math.sin(nx * 28.0 - t * 20.0);

                const y = Math.max(3, Math.min(this.height - 3, this.gainToY(db) + wowRipple + flutterRipple));

                if (x === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            }
            ctx.strokeStyle = this.amber;
            ctx.lineWidth = 2.0;
            ctx.shadowColor = this.amberGlow;
            ctx.shadowBlur = 8;
            ctx.stroke();
            ctx.shadowBlur = 0;

            // 3. Интерактивные точки
            this.drawNode(this.params.BASS_FREQ, this.params.BASS, "LF", 1);
            this.drawNode(this.params.TREBLE_FREQ, this.params.TREBLE, "HF", 2);
        }

        drawNode(freq, gain, label, id) {
            const x = this.freqToX(freq);
            const y = this.gainToY(gain);
            const isDragging = this.draggingNode === id;

            this.ctx.fillStyle = isDragging ? '#ffffff' : this.amber;
            this.ctx.shadowColor = this.amber;
            this.ctx.shadowBlur = isDragging ? 12 : 6;
            this.ctx.beginPath();
            this.ctx.arc(x, y, isDragging ? 5.5 : 4.5, 0, Math.PI * 2);
            this.ctx.fill();
            this.ctx.shadowBlur = 0;

            this.ctx.fillStyle = 'rgba(255, 176, 40, 0.85)';
            this.ctx.font = 'bold 9px "GOST Type B", monospace';
            this.ctx.textAlign = 'center';
            this.ctx.fillText(label, x, y + 14);
        }

        getLogicalPosition(e) {
            const rect = this.canvas.getBoundingClientRect();
            if (rect.width === 0 || rect.height === 0) return { x: 0, y: 0, hit: false };
            return {
                x: (e.clientX - rect.left) * (this.width / rect.width),
                y: (e.clientY - rect.top) * (this.height / rect.height),
                hit: true
            };
        }

        onPointerDown(e) {
            const pos = this.getLogicalPosition(e);
            if (!pos.hit) return;

            const hitTest = (f, g) => Math.hypot(pos.x - this.freqToX(f), pos.y - this.gainToY(g)) < 24.0;

            if (hitTest(this.params.BASS_FREQ, this.params.BASS)) {
                this.draggingNode = 1;
            } else if (hitTest(this.params.TREBLE_FREQ, this.params.TREBLE)) {
                this.draggingNode = 2;
            }

            if (this.draggingNode !== -1) {
                e.preventDefault();
                try { this.canvas.setPointerCapture(e.pointerId); } catch (_) {}
                this.dragStartX = pos.x;
                this.dragStartY = pos.y;

                if (JuceBridge && JuceBridge.isJuceAvailable()) {
                    const prefix = this.draggingNode === 1 ? 'BASS' : 'TREBLE';
                    JuceBridge.beginGesture(prefix + '_FREQ');
                    JuceBridge.beginGesture(prefix);
                }
            }
        }

        onPointerMove(e) {
            if (this.draggingNode === -1) return;
            e.preventDefault();

            const pos = this.getLogicalPosition(e);
            if (!pos.hit) return;

            const newFreq = this.xToFreq(pos.x);
            const newGain = this.yToGain(pos.y);

            if (this.draggingNode === 1) {
                this.params.BASS_FREQ = Math.max(30, Math.min(300, newFreq));
                this.params.BASS = Math.max(-18, Math.min(18, newGain));
                this.sendParam('BASS_FREQ', (this.params.BASS_FREQ - 30) / (300 - 30));
                this.sendParam('BASS', (this.params.BASS + 18) / 36);
            } else {
                this.params.TREBLE_FREQ = Math.max(1000, Math.min(15000, newFreq));
                this.params.TREBLE = Math.max(-18, Math.min(18, newGain));
                this.sendParam('TREBLE_FREQ', (this.params.TREBLE_FREQ - 1000) / (15000 - 1000));
                this.sendParam('TREBLE', (this.params.TREBLE + 18) / 36);
            }
        }

        sendParam(id, normVal) {
            if (JuceBridge && JuceBridge.queueParameterChange) {
                JuceBridge.queueParameterChange(id, normVal);
            } else if (JuceBridge) {
                JuceBridge.setParameter(id, normVal);
            }
        }

        onPointerUp(e) {
            if (this.draggingNode !== -1) {
                try { this.canvas.releasePointerCapture(e.pointerId); } catch (_) {}
                if (JuceBridge && JuceBridge.isJuceAvailable()) {
                    const prefix = this.draggingNode === 1 ? 'BASS' : 'TREBLE';
                    JuceBridge.endGesture(prefix + '_FREQ');
                    JuceBridge.endGesture(prefix);
                }
            }
            this.draggingNode = -1;
        }

        startLoop() {
            const loop = () => {
                this.render();
                requestAnimationFrame(loop);
            };
            requestAnimationFrame(loop);
        }
    };
});
