// DOM RADIO MASTER - Canvas EQ Monitor
// Ported from EQGraphLED.cpp / EQGraphLED.h
(function (root, factory) {
    if (typeof module !== 'undefined' && module.exports) {
        module.exports = factory(root.JuceBridge);
    } else {
        root.EQMonitor = factory(root.JuceBridge);
    }
})(typeof window !== 'undefined' ? window : this, function (JuceBridge) {
    'use strict';

    return class EQMonitor {
        constructor(canvasId) {
            this.canvas = document.getElementById(canvasId);
            if (!this.canvas) return;
            this.ctx = this.canvas.getContext('2d');

            this.width = 0;
            this.height = 0;
            this.maxDb = 12.0;

            this.params = {
                BASS_FREQ: 100, BASS: 0,
                TREBLE_FREQ: 5000, TREBLE: 0,
                AGE: 0, SCRAPE_FLUTTER: 0,
                MIX: 100, WOW_AMOUNT: 0, FLUTTER_AMOUNT: 0,
                TAPE_NOISE: 0, HUM: 0
            };

            this.draggingNode = -1;
            this.dragStartFreq = 0;
            this.dragStartGain = 0;
            this.dragStartX = 0;
            this.dragStartY = 0;

            this.phosphor = 'rgba(255, 176, 40, 1.0)';

            this.init();
        }

        init() {
            // ФИКС: ResizeObserver реагирует на изменение размера при открытии шторки
            if (typeof ResizeObserver !== 'undefined') {
                this.resizeObserver = new ResizeObserver(entries => {
                    for (let entry of entries) {
                        const w = entry.contentRect.width;
                        const h = entry.contentRect.height;
                        if (w > 0 && h > 0) {
                            this.width = w;
                            this.height = h;
                            const dpr = window.devicePixelRatio || 1;
                            this.canvas.width = this.width * dpr;
                            this.canvas.height = this.height * dpr;
                            this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
                        }
                    }
                });
                this.resizeObserver.observe(this.canvas.parentElement);
            } else {
                window.addEventListener('resize', () => this.resize());
                this.resize();
            }

            this.canvas.addEventListener('mousedown', (e) => this.onMouseDown(e));
            window.addEventListener('mousemove', (e) => this.onMouseMove(e));
            window.addEventListener('mouseup', () => this.onMouseUp());

            const PARAM_MAP = {
                BASS_FREQ:      { min: 30, max: 300 },
                BASS:           { min: -18, max: 18 },
                TREBLE_FREQ:    { min: 1000, max: 15000 },
                TREBLE:         { min: -18, max: 18 },
                AGE:            { min: 0, max: 50 },
                SCRAPE_FLUTTER: { min: 0, max: 100 },
                MIX:            { min: 0, max: 100 },
                WOW_AMOUNT:     { min: 0, max: 100 },
                FLUTTER_AMOUNT: { min: 0, max: 100 },
                TAPE_NOISE:     { min: 0, max: 100 },
                HUM:            { min: 0, max: 100 }
            };

            const updateParam = (id, normVal) => {
                if (PARAM_MAP[id]) {
                    this.params[id] = PARAM_MAP[id].min + normVal * (PARAM_MAP[id].max - PARAM_MAP[id].min);
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
            const rect = this.canvas.parentElement.getBoundingClientRect();
            if (rect.width > 0 && rect.height > 0) {
                this.width = rect.width;
                this.height = rect.height;
                const dpr = window.devicePixelRatio || 1;
                this.canvas.width = this.width * dpr;
                this.canvas.height = this.height * dpr;
                this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
            }
        }

        freqToX(f) { return (Math.log10(f / 20.0) / 3.0) * this.width; }
        xToFreq(x) { return 20.0 * Math.pow(10.0, 3.0 * x / this.width); }
        gainToY(dB) { return this.height * 0.5 - (dB / this.maxDb) * this.height * 0.42; }

        getBiquadMagSq(freq, f0, q, gainDb) {
            if (Math.abs(gainDb) < 0.01) return 1.0;
            const A = Math.pow(10.0, gainDb / 40.0);
            const w0 = 2.0 * Math.PI * f0 / 44100.0;
            const alpha = Math.sin(w0) / (2.0 * q);
            const cosw0 = Math.cos(w0);

            const b0 = (1.0 + alpha * A);
            const b1 = (-2.0 * cosw0);
            const b2 = (1.0 - alpha * A);
            const a0 = (1.0 + alpha / A);
            const a1 = (-2.0 * cosw0);
            const a2 = (1.0 - alpha / A);

            const c0 = (b0*b0 + b1*b1 + b2*b2) / (a0*a0);
            const c1 = 2.0 * (b0*b1 + b1*b2) / (a0*a0);
            const c2 = 2.0 * b0*b2 / (a0*a0);
            const d0 = 1.0 + (a1*a1 + a2*a2) / (a0*a0);
            const d1 = 2.0 * (a1/a0 + (a1*a2)/(a0*a0));
            const d2 = 2.0 * a2 / a0;

            const w = 2.0 * Math.PI * freq / 44100.0;
            const cw = Math.cos(w);
            const c2w = Math.cos(2.0 * w);

            const num = c0 + c1*cw + c2*c2w;
            const den = d0 + d1*cw + d2*c2w;

            return Math.max(1e-12, num / Math.max(1e-12, den));
        }

        getCompositeMagnitude(freq) {
            const mag1 = this.getBiquadMagSq(freq, this.params.BASS_FREQ, 0.7, this.params.BASS);
            const mag2 = this.getBiquadMagSq(freq, this.params.TREBLE_FREQ, 0.7, this.params.TREBLE);
            return Math.sqrt(mag1 * mag2);
        }

        drawBackground() {
            const ctx = this.ctx;

            ctx.fillStyle = 'rgba(255, 176, 40, 0.1)';
            [50, 100, 200, 500, 1000, 2000, 5000, 10000].forEach(f => {
                const x = this.freqToX(f);
                for (let y = 6; y < this.height - 4; y += 7) {
                    ctx.beginPath(); ctx.arc(x, y, 0.8, 0, Math.PI*2); ctx.fill();
                }
            });

            for (let db = -12; db <= 12; db += 6) {
                const y = this.gainToY(db);
                ctx.fillStyle = db === 0 ? 'rgba(255, 176, 40, 0.22)' : 'rgba(255, 176, 40, 0.1)';
                for (let x = 6; x < this.width - 4; x += 7) {
                    ctx.beginPath(); ctx.arc(x, y, 0.8, 0, Math.PI*2); ctx.fill();
                }
            });

            ctx.fillStyle = 'rgba(255, 176, 40, 0.45)';
            ctx.font = 'bold 8.5px "GOST Type B", monospace';
            ctx.textAlign = 'center';
            ctx.fillText('100', this.freqToX(100), this.height - 6);
            ctx.fillText('1K', this.freqToX(1000), this.height - 6);
            ctx.fillText('10K', this.freqToX(10000), this.height - 6);
        }

        render() {
            if (this.width === 0 || this.height === 0) return;

            this.ctx.clearRect(0, 0, this.width, this.height);
            this.drawBackground();

            const ctx = this.ctx;
            const t = performance.now() * 0.001;

            const mixNorm = this.params.MIX / 100.0;
            const wowDepth = (this.params.WOW_AMOUNT / 100.0) * mixNorm;
            const flutterDepth = (this.params.FLUTTER_AMOUNT / 100.0) * mixNorm;
            const age = this.params.AGE / 50.0;
            const scrape = this.params.SCRAPE_FLUTTER / 100.0;
            const noiseLvl = this.params.TAPE_NOISE / 100.0;
            const humLvl = this.params.HUM / 100.0;

            if (noiseLvl > 0.01 || humLvl > 0.01 || age > 0.05) {
                let noiseHeight = 3.0 + (noiseLvl * 30.0) + (humLvl * 15.0) + (age * 12.0);
                ctx.beginPath();
                ctx.moveTo(0, this.height);
                for (let i = 0; i <= this.width; i += 4) {
                    let jitter = (Math.random() - 0.5) * (noiseHeight * 0.9);
                    let humWave = Math.sin(i * 0.12 + t * 10.0) * (humLvl * 14.0);
                    let y = this.height - (noiseHeight * 0.4) + jitter + humWave;
                    ctx.lineTo(i, y);
                }
                ctx.lineTo(this.width, this.height);
                ctx.closePath();

                const grad = ctx.createLinearGradient(0, this.height - 45, 0, this.height);
                grad.addColorStop(0, 'rgba(255, 176, 40, ' + (0.12 + noiseLvl * 0.15) + ')');
                grad.addColorStop(1, 'rgba(255, 176, 40, 0)');
                ctx.fillStyle = grad;
                ctx.fill();
            }

            ctx.beginPath();
            for (let i = 0; i < this.width; i += 2) {
                const freq = 20.0 * Math.pow(1000.0, i / (this.width - 1));
                let mag = this.getCompositeMagnitude(freq);

                let hfRipple = 0;
                if (freq > 2000.0) {
                    let jitter = Math.random() - 0.5;
                    let intensity = (freq / 20000.0) * (scrape * 0.15 + age * 0.08);
                    hfRipple = jitter * intensity;
                }
                mag *= (1.0 + hfRipple);

                let dB = 10.0 * Math.log10(Math.max(1e-12, mag));

                const nx = i / this.width;
                const wowRipple = wowDepth * 12.0 * Math.sin(nx * 8.0 - t * 5.0);
                const flutterRipple = flutterDepth * 4.0 * Math.sin(nx * 35.0 - t * 25.0);

                let y = this.gainToY(dB) + wowRipple + flutterRipple;
                y = Math.max(3.0, Math.min(this.height - 3.0, y));

                if (i === 0) ctx.moveTo(i, y);
                else ctx.lineTo(i, y);
            }

            ctx.strokeStyle = this.phosphor;
            ctx.lineWidth = 2.0;
            ctx.shadowColor = this.phosphor;
            ctx.shadowBlur = 8;
            ctx.stroke();

            ctx.lineTo(this.width, this.height);
            ctx.lineTo(0, this.height);
            ctx.closePath();

            const fillGrad = ctx.createLinearGradient(0, this.gainToY(12), 0, this.height);
            fillGrad.addColorStop(0, 'rgba(255, 176, 40, 0.15)');
            fillGrad.addColorStop(1, 'rgba(255, 176, 40, 0.0)');
            ctx.fillStyle = fillGrad;
            ctx.fill();
            ctx.shadowBlur = 0;

            this.drawNode(this.params.BASS_FREQ, this.params.BASS, "LF", 2);
            this.drawNode(this.params.TREBLE_FREQ, this.params.TREBLE, "HF", 3);
        }

        drawNode(freq, gain, label, id) {
            const x = this.freqToX(freq);
            const y = this.gainToY(gain);
            const isDragging = this.draggingNode === id;

            this.ctx.fillStyle = isDragging ? 'rgba(255, 176, 40, 1.0)' : 'rgba(255, 176, 40, 0.75)';
            this.ctx.shadowColor = this.phosphor;
            this.ctx.shadowBlur = isDragging ? 12 : 6;
            this.ctx.beginPath();
            this.ctx.arc(x, y, 4.5, 0, Math.PI*2);
            this.ctx.fill();
            this.ctx.shadowBlur = 0;

            this.ctx.fillStyle = 'rgba(255, 176, 40, 0.85)';
            this.ctx.font = 'bold 9px "GOST Type B", monospace';
            this.ctx.textAlign = 'center';
            this.ctx.fillText(label, x, y + 14);
        }

        onMouseDown(e) {
            const rect = this.canvas.getBoundingClientRect();
            const x = e.clientX - rect.left;
            const y = e.clientY - rect.top;

            const hit = (freq, gain) => {
                const nx = this.freqToX(freq);
                const ny = this.gainToY(gain);
                return Math.hypot(x - nx, y - ny) < 15.0;
            };

            if (hit(this.params.BASS_FREQ, this.params.BASS)) {
                this.draggingNode = 2;
                this.dragStartFreq = this.params.BASS_FREQ;
                this.dragStartGain = this.params.BASS;
            } else if (hit(this.params.TREBLE_FREQ, this.params.TREBLE)) {
                this.draggingNode = 3;
                this.dragStartFreq = this.params.TREBLE_FREQ;
                this.dragStartGain = this.params.TREBLE;
            }

            if (this.draggingNode !== -1) {
                this.dragStartX = e.clientX;
                this.dragStartY = e.clientY;

                if (JuceBridge && JuceBridge.isJuceAvailable()) {
                    const prefix = this.draggingNode === 2 ? 'BASS' : 'TREBLE';
                    JuceBridge.beginGesture(prefix + '_FREQ');
                    JuceBridge.beginGesture(prefix);
                }
            }
        }

        onMouseMove(e) {
            if (this.draggingNode === -1) return;

            const sensitivity = e.shiftKey ? 0.1 : 0.4;
            const deltaX = (e.clientX - this.dragStartX) * sensitivity;
            const deltaY = (e.clientY - this.dragStartY) * sensitivity;

            const freqRatio = Math.pow(10.0, (deltaX * 3.0) / this.width);
            const newFreq = this.dragStartFreq * freqRatio;

            const gainDelta = -(deltaY * this.maxDb) / (this.height * 0.42);
            const newGain = this.dragStartGain + gainDelta;

            if (this.draggingNode === 2) {
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

        onMouseUp() {
            if (this.draggingNode !== -1 && JuceBridge && JuceBridge.isJuceAvailable()) {
                const prefix = this.draggingNode === 2 ? 'BASS' : 'TREBLE';
                JuceBridge.endGesture(prefix + '_FREQ');
                JuceBridge.endGesture(prefix);
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
