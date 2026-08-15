// TROAKAR SPECTRAL - Full Interactive EQGraphLED Canvas Renderer
// Ported from EQGraphLED.cpp — includes gradient points, EQ nodes, tooltips,
// context menu, sidechain spectrum, and compression delta rendering.
(function (root, factory) {
    if (typeof module !== 'undefined' && module.exports) {
        module.exports = factory(root.JuceBridge);
    } else {
        root.SpectrumScreen = factory(root.JuceBridge);
    }
})(typeof window !== 'undefined' ? window : this, function (JuceBridge) {

    return class SpectrumScreen {
        constructor(canvasId, gradientManager) {
            this.canvas          = document.getElementById(canvasId);
            this.ctx             = this.canvas.getContext('2d');
            this.gradientManager = gradientManager;

        this.baseViewDepth = 72.0;
        this.maxDb         = 12.0;
        this.minDb         = this.maxDb - this.baseViewDepth;
        this.sampleRate    = 44100.0;

        this.fftSize = 512;
        this.numBins = this.fftSize / 2 + 1;

            this.colors = {
                phosphor:     '#f4c472',
                phosphorGlow: 'rgba(244, 196, 114, 0.4)',
                grid:         'rgba(212, 164, 70, 0.15)',
                gridText:     'rgba(212, 164, 70, 0.45)',
                threshold:    'rgba(100, 200, 255, 0.85)',
                thresholdLine:'rgba(100, 200, 255, 0.35)',
                downwardFill: 'rgba(240, 70, 70, 0.25)',
                downwardLine: 'rgba(255, 90, 90, 0.85)',
                upwardFill:   'rgba(244, 196, 114, 0.2)',
                upwardLine:   'rgba(244, 196, 114, 0.85)'
            };

            // 8 EQ bands — normalized values 0..1
            this.eqBands = Array.from({ length: 8 }, function (_, i) {
                return {
                    id:      i,
                    enabled: false,
                    freq:    20.0 * Math.pow(1000, i / 7),
                    gain:    0.0,
                    q:       1.0
                };
            });

            this.globalThresh = 0.0;

            // Interaction states
            this.draggingNode       = -1;
            this.hoveredNode        = -1;
            this.dragStartFreq      = 0.0;
            this.dragStartGain      = 0.0;

            this.draggingGradientId = -1;
            this.hoveredGradientId  = -1;

            this.isDraggingThresh    = false;
            this.zoomIndicatorAlpha  = 0.0;

            // Spectrum data
            this.spectrumData   = new Float32Array(this.numBins).fill(-100.0);
            this.sidechainData  = new Float32Array(this.numBins).fill(-100.0);
            this.deltaData      = new Float32Array(this.numBins).fill(0.0);

            // Cached target curve per pixel
            this.targetDbPerPixel = [];
            this.targetCurveDirty = true;
            this.animationFrameId = 0;
            this.hasNativeAnalysis = false;

            this.analysisPackets = 0;
            this.lastAnalysisPacketTime = 0;
            this.analysisFormat = 'linearMagnitude';

            this.parameterSyncSerial = 0;

            // Cached scanline overlay (rebuilt only on resize)
            this.scanlineCanvas = null;
            this.scanlineCtx = null;

            // Callbacks
            this.onGradientSelectionChanged = null;
            this.onGradientParamsChanged    = null;

            this.resize();
            this.initEvents();
            this.fetchAllParameters();
            this.startLoop();
        }

        // =================================================================
        // Parameter Sync (port of EQGraphLED::timerCallback parameter sync)
        // =================================================================

        updateAnalysisData(data) {
            if (!data ||
                !Array.isArray(data.spectrum) ||
                data.spectrum.length === 0) {
                return;
            }

            const incomingSpectrum =
                data.spectrum;

            const incomingSidechain =
                Array.isArray(data.sidechain)
                    ? data.sidechain
                    : [];

            const incomingDelta =
                Array.isArray(data.delta)
                    ? data.delta
                    : [];

            this.analysisFormat =
                data.spectrumFormat
                    || 'linearMagnitude';

            this.fftSize =
                Math.max(
                    2,
                    Number(data.fftSize) || 512
                );

            this.sampleRate =
                Math.max(
                    8000,
                    Number(data.sampleRate) || 44100
                );

            this.numBins =
                incomingSpectrum.length;

            if (this.spectrumData.length !== this.numBins) {
                this.spectrumData =
                    new Float32Array(this.numBins);

                this.sidechainData =
                    new Float32Array(this.numBins);

                this.deltaData =
                    new Float32Array(this.numBins);
            }

            const sidechainFormat =
                data.sidechainFormat
                    || this.analysisFormat;

            for (let i = 0;
                 i < this.numBins;
                 ++i) {
                const value =
                    Number(incomingSpectrum[i]);

                this.spectrumData[i] =
                    this.convertIncomingSpectrumValue(
                        value,
                        this.analysisFormat,
                        this.spectrumData[i]
                    );

                if (i < incomingSidechain.length) {
                    const sidechainValue =
                        Number(incomingSidechain[i]);

                    this.sidechainData[i] =
                        this.convertIncomingSpectrumValue(
                            sidechainValue,
                            sidechainFormat,
                            this.sidechainData[i]
                        );
                } else {
                    this.sidechainData[i] = -120.0;
                }

                if (i < incomingDelta.length) {
                    const deltaValue =
                        Number(incomingDelta[i]);

                    this.deltaData[i] =
                        Number.isFinite(deltaValue)
                            ? deltaValue
                            : 0.0;
                } else {
                    this.deltaData[i] = 0.0;
                }
            }

            this.hasNativeAnalysis = true;

            this.analysisPackets++;
            this.lastAnalysisPacketTime =
                performance.now();
        }

        convertIncomingSpectrumValue(
            value,
            format,
            previousValue
        ) {
            if (!Number.isFinite(value))
                return previousValue || -120.0;

            if (format === 'decibels') {
                return Math.max(
                    -120.0,
                    Math.min(24.0, value)
                );
            }

            /*
                linearMagnitude
            */
            if (value <= 1.0e-9)
                return -120.0;

            const db =
                20.0 * Math.log10(value);

            return Math.max(
                -120.0,
                Math.min(24.0, db)
            );
        }

    frequencyToBin(freq, arrayLength) {
        const nyquist = this.sampleRate * 0.5;

        const clampedFrequency = Math.max(
            0.0,
            Math.min(nyquist, freq)
        );

        const bins = Math.max(
            1,
            Number(arrayLength) || this.numBins
        );

        return clampedFrequency *
            (bins - 1) /
            nyquist;
    }

    getEventPosition(event) {
        var rect =
            this.canvas.getBoundingClientRect();

        if (!rect ||
            rect.width <= 0 ||
            rect.height <= 0) {
            return {
                x: 0,
                y: 0,
                inside: false
            };
        }

        /*
            event.clientX/Y are screen (viewport) coordinates.
            this.width/height are logical canvas coordinates
            in the 1300x780 artboard space. The chassis is
            scaled via CSS transform, so we must compensate.
        */
        var scaleX =
            this.width / rect.width;

        var scaleY =
            this.height / rect.height;

        var logicalX =
            (event.clientX - rect.left) * scaleX;

        var logicalY =
            (event.clientY - rect.top) * scaleY;

        var inside =
            logicalX >= 0 &&
            logicalX <= this.width &&
            logicalY >= 0 &&
            logicalY <= this.height;

        return {
            x: Math.max(
                0,
                Math.min(this.width, logicalX)
            ),

            y: Math.max(
                0,
                Math.min(this.height, logicalY)
            ),

            inside: inside
        };
    }

    getArrayValueAtFrequency(freq, sourceArray, fallback) {
        if (!sourceArray || sourceArray.length === 0)
            return fallback;

        let bin = this.frequencyToBin(
            freq,
            sourceArray.length
        );

        bin = Math.max(
            0,
            Math.min(sourceArray.length - 1, bin)
        );

        const index = Math.floor(bin);
        const fraction = bin - index;
        const nextIndex = Math.min(
            sourceArray.length - 1,
            index + 1
        );

        return sourceArray[index]
            + (sourceArray[nextIndex] - sourceArray[index])
            * fraction;
    }

    getSpectrumDbAtFrequency(freq, sourceArray) {
        if (!sourceArray || sourceArray.length === 0)
            return -120.0;

        let bin = this.frequencyToBin(
            freq,
            sourceArray.length
        );

        bin = Math.max(
            0,
            Math.min(sourceArray.length - 1, bin)
        );

        const index = Math.floor(bin);
        const fraction = bin - index;
        const nextIndex = Math.min(
            sourceArray.length - 1,
            index + 1
        );

        return sourceArray[index]
            + (sourceArray[nextIndex] - sourceArray[index])
            * fraction;
    }

    // =================================================================
    // Parameter Sync (port of EQGraphLED::timerCallback parameter sync)
    // =================================================================

    async fetchAllParameters() {
        if (!JuceBridge || !JuceBridge.isJuceAvailable()) return;

        var syncSerial =
            ++this.parameterSyncSerial;

        const get = async function (id, fallback) {
            try {
                const value = await JuceBridge.getParameter(id);
                return value === null || value === undefined ? fallback : Number(value);
            } catch (e) {
                return fallback;
            }
        };

        if (syncSerial !== this.parameterSyncSerial)
            return;

        this.globalThresh = (await get('GLOBAL_THRESH', 0.8)) * 60.0 - 48.0;

        if (syncSerial !== this.parameterSyncSerial)
            return;

        for (var i = 0; i < 8; ++i) {
            if (syncSerial !== this.parameterSyncSerial)
                return;

            var prefix  = 'BAND_' + i;
            var en      = await get(prefix + '_ENABLE', 0.0);
            var fr      = await get(prefix + '_FREQ', 0.5);
            var gn      = await get(prefix + '_GAIN', 0.5);
            var q       = await get(prefix + '_Q', 0.5);

            this.eqBands[i].enabled = en >= 0.5;
            this.eqBands[i].freq    = 20.0 * Math.pow(1000.0, fr);
            this.eqBands[i].gain    = gn * 120.0 - 60.0;
            this.eqBands[i].q       = Math.pow(10.0, q * 2.0 - 1.0);
        }

        this.targetCurveDirty = true;

        if (this.gradientManager) {
            this.gradientManager.syncFromJuce(JuceBridge);
        }
    }

        // =================================================================
        // Geometry helpers (exact port of EQGraphLED.cpp)
        // =================================================================

        resize() {
            const dpr = Math.min(
                window.devicePixelRatio || 1,
                2
            );

            const nextWidth = Math.max(
                1,
                Math.round(this.canvas.clientWidth)
            );

            const nextHeight = Math.max(
                1,
                Math.round(this.canvas.clientHeight)
            );

            /*
                Skip if nothing changed — avoids
                clearing backing store canvas during
                resize drags.
            */
            if (this.width === nextWidth &&
                this.height === nextHeight &&
                this.canvas.width === Math.round(nextWidth * dpr) &&
                this.canvas.height === Math.round(nextHeight * dpr)) {
                return;
            }

            this.width = nextWidth;
            this.height = nextHeight;

            this.canvas.width =
                Math.round(this.width * dpr);

            this.canvas.height =
                Math.round(this.height * dpr);

            this.ctx.setTransform(
                dpr, 0, 0, dpr, 0, 0
            );

            this.targetCurveDirty = true;
            this.rebuildFrequencyGrid();
            this.buildScanlineLayer();
        }

        buildScanlineLayer() {
            this.scanlineCanvas =
                document.createElement('canvas');

            this.scanlineCanvas.width =
                Math.max(1, Math.round(this.width));

            this.scanlineCanvas.height =
                Math.max(1, Math.round(this.height));

            this.scanlineCtx =
                this.scanlineCanvas.getContext('2d');

            this.scanlineCtx.fillStyle =
                'rgba(0, 0, 0, 0.12)';

            for (var y = 0;
                 y < this.height;
                 y += 3) {
                this.scanlineCtx.fillRect(
                    0, y, this.width, 1
                );
            }
        }

        rebuildFrequencyGrid() {
            var numPoints    = Math.min(Math.max(64, Math.floor(this.width / 2)), 400);
            this.freqGrid     = new Array(numPoints + 1);

            var sr           = Math.max(44100.0, this.sampleRate);
            var nyquist      = sr * 0.5;
            var maxFreq      = nyquist * 0.98;
            var logMin       = Math.log10(20.0);
            var logMax       = Math.log10(Math.max(20.0, maxFreq));

            for (var i = 0; i <= numPoints; ++i) {
                var t     = i / numPoints;
                var logF  = logMin + t * (logMax - logMin);
                this.freqGrid[i] = Math.pow(10.0, logF);
            }
        }

        setViewRange(depthDb) {
            this.baseViewDepth = depthDb;
            this.maxDb = 12.0;
            this.minDb = this.maxDb - this.baseViewDepth;
        }

        freqToX(freq) {
            var f = Math.min(20000.0, Math.max(20.0, freq));
            return (Math.log10(f / 20.0) / 3.0) * this.width;
        }

        xToFreq(x) {
            var safeWidth = Math.max(1.0, this.width);

            var clampedX = Math.max(
                0.0,
                Math.min(safeWidth, Number(x) || 0.0)
            );

            return 20.0 * Math.pow(
                10.0,
                3.0 * clampedX / safeWidth
            );
        }

        gainToY(dB) {
            var norm =
                (dB - this.minDb)
                / (this.maxDb - this.minDb);

            norm = Math.min(
                1.0,
                Math.max(0.0, norm)
            );

            var topMargin =
                this.height * 0.08;

            var bottomMargin =
                this.height * 0.92;

            return bottomMargin
                - norm
                * (bottomMargin - topMargin);
        }

        yToGain(y) {
            var topMargin =
                this.height * 0.08;

            var bottomMargin =
                this.height * 0.92;

            var safeY = Number(y);

            if (!Number.isFinite(safeY))
                safeY = bottomMargin;

            safeY = Math.max(
                topMargin,
                Math.min(bottomMargin, safeY)
            );

            var norm =
                (bottomMargin - safeY)
                / Math.max(
                    1.0,
                    bottomMargin - topMargin
                );

            norm = Math.max(
                0.0,
                Math.min(1.0, norm)
            );

            return this.minDb
                + norm
                * (this.maxDb - this.minDb);
        }

        // =================================================================
        // Biquad computation (exact port of TempBiquad from EQGraphLED.cpp)
        // =================================================================

        getBiquadMagSq(freq, f0, q, gainDb) {
            freq = Number(freq);
            f0 = Number(f0);
            q = Number(q);
            gainDb = Number(gainDb);

            if (!Number.isFinite(freq) ||
                !Number.isFinite(f0) ||
                !Number.isFinite(q) ||
                !Number.isFinite(gainDb)) {
                return 1.0;
            }

            var nyquist =
                Math.max(100.0, this.sampleRate * 0.5);

            freq = Math.max(
                1.0,
                Math.min(nyquist * 0.999, freq)
            );

            f0 = Math.max(
                20.0,
                Math.min(nyquist * 0.98, f0)
            );

            q = Math.max(
                0.1,
                Math.min(10.0, q)
            );

            gainDb = Math.max(
                -60.0,
                Math.min(60.0, gainDb)
            );

            if (Math.abs(gainDb) < 0.01)
                return 1.0;

            var A     = Math.pow(10.0, gainDb / 40.0);
            var w0    = 2.0 * Math.PI * f0 / this.sampleRate;
            var alpha = Math.sin(w0) / (2.0 * Math.max(0.05, q));
            var cosw0 = Math.cos(w0);
            var a0_inv = 1.0 / (1.0 + alpha / A);

            var b0 = (1.0 + alpha * A) * a0_inv;
            var b1 = (-2.0 * cosw0) * a0_inv;
            var b2 = (1.0 - alpha * A) * a0_inv;
            var a1 = (-2.0 * cosw0) * a0_inv;
            var a2 = (1.0 - alpha / A) * a0_inv;

            var c0 = b0 * b0 + b1 * b1 + b2 * b2;
            var c1 = 2.0 * (b0 * b1 + b1 * b2);
            var c2 = 2.0 * b0 * b2;
            var d0 = 1.0 + a1 * a1 + a2 * a2;
            var d1 = 2.0 * (a1 + a1 * a2);
            var d2 = 2.0 * a2;

            var w   = 2.0 * Math.PI * freq / this.sampleRate;
            var cw  = Math.cos(w);
            var c2w = Math.cos(2.0 * w);
            var num = c0 + c1 * cw + c2 * c2w;
            var den = d0 + d1 * cw + d2 * c2w;

            var magnitudeSq =
                num / Math.max(
                    1.0e-12,
                    Math.abs(den)
                );

            if (!Number.isFinite(magnitudeSq) ||
                magnitudeSq <= 0.0) {
                return 1.0;
            }

            return Math.max(
                1.0e-12,
                Math.min(1.0e12, magnitudeSq)
            );
        }

        getTargetCurveDb(freq) {
            var freqNum = Number(freq);

            if (!Number.isFinite(freqNum) ||
                freqNum <= 0) {
                return this.globalThresh;
            }

            var eqDb = 0.0;

            for (var i = 0;
                 i < this.eqBands.length;
                 ++i) {
                var b = this.eqBands[i];

                if (!b.enabled ||
                    Math.abs(b.gain) < 0.05) {
                    continue;
                }

                var magnitudeSq =
                    this.getBiquadMagSq(
                        freqNum,
                        b.freq,
                        b.q,
                        b.gain
                    );

                var bandDb =
                    10.0 * Math.log10(
                        Math.max(
                            1.0e-12,
                            magnitudeSq
                        )
                    );

                if (Number.isFinite(bandDb)) {
                    eqDb += Math.max(
                        -72.0,
                        Math.min(72.0, bandDb)
                    );
                }
            }

            eqDb = Math.max(
                -120.0,
                Math.min(120.0, eqDb)
            );

            var gradientOffset = 0.0;

            if (this.gradientManager) {
                this.gradientManager.points.forEach(
                    function (gp) {
                        if (!gp.active)
                            return;

                        var centerFreq =
                            Math.max(
                                20.0,
                                Number(gp.centerFreqHz)
                                    || 20.0
                            );

                        var logDistance =
                            Math.abs(
                                Math.log2(
                                    freqNum / centerFreq
                                )
                            );

                        var normalizedDistance =
                            logDistance
                            / Math.max(
                                0.1,
                                Number(gp.radiusOctaves)
                                    || 0.1
                            );

                        if (normalizedDistance < 1.0) {
                            var weight =
                                0.5
                                + 0.5
                                * Math.cos(
                                    normalizedDistance
                                    * Math.PI
                                );

                            gradientOffset +=
                                Math.max(
                                    -60.0,
                                    Math.min(
                                        60.0,
                                        Number(gp.centerGainDb)
                                            || 0.0
                                    )
                                )
                                * weight;
                        }
                    }
                );
            }

            var result =
                eqDb
                + this.globalThresh
                + gradientOffset;

            if (!Number.isFinite(result))
                return this.globalThresh;

            return Math.max(
                -120.0,
                Math.min(120.0, result)
            );
        }

        getTotalTargetDbAtFreq(freq) {
            return this.getTargetCurveDb(freq);
        }

        buildTargetCurveCache() {
            var w = Math.max(1, Math.floor(this.width));

            this.targetDbPerPixel = new Array(w + 1);

            for (var x = 0; x <= w; ++x) {
                this.targetDbPerPixel[x] =
                    this.getTargetCurveDb(
                        this.xToFreq(x)
                    );
            }
        }

        // =================================================================
        // Rendering
        // =================================================================

        drawBackground() {
            var ctx = this.ctx;
            ctx.fillStyle = getComputedStyle(document.documentElement)
                .getPropertyValue('--screen-bg') || '#0d0b08';
            ctx.fillRect(0, 0, this.width, this.height);

            var gradient = ctx.createRadialGradient(
                this.width / 2, this.height / 2, this.width * 0.1,
                this.width / 2, this.height / 2, this.width * 0.7
            );
            gradient.addColorStop(0, 'rgba(13, 11, 8, 0)');
            gradient.addColorStop(1, 'rgba(0, 0, 0, 0.6)');
            ctx.fillStyle = gradient;
            ctx.fillRect(0, 0, this.width, this.height);
        }

        drawGrid() {
            var ctx = this.ctx;
            ctx.font = '11px "GOST Type B", "Courier New", monospace';

            var freqMarks = [50, 100, 200, 500, 1000, 2000, 5000, 10000];
            ctx.strokeStyle = this.colors.grid;
            ctx.fillStyle   = this.colors.gridText;
            ctx.textAlign   = 'center';

            for (var fi = 0; fi < freqMarks.length; fi++) {
                var f = freqMarks[fi];
                var x = Math.round(this.freqToX(f));
                ctx.beginPath();
                ctx.moveTo(x, 0);
                ctx.lineTo(x, this.height);
                ctx.stroke();

                var label = f >= 1000 ? (f / 1000) + 'k' : f.toString();
                ctx.fillText(label, x, this.height - 8);
            }

            var dbRange  = this.maxDb - this.minDb;
            var gridStep = dbRange <= 24 ? 6 : 12;
            ctx.textAlign = 'left';

            for (var db = Math.ceil(this.minDb / gridStep) * gridStep; db <= this.maxDb; db += gridStep) {
                var y     = Math.round(this.gainToY(db));
                var isZero = Math.abs(db) < 0.1;

                ctx.strokeStyle = isZero ? 'rgba(212, 164, 70, 0.25)' : this.colors.grid;
                ctx.beginPath();
                ctx.moveTo(0, y);
                ctx.lineTo(this.width, y);
                ctx.stroke();

                var dbLabel = (db > 0 ? '+' : '') + Math.round(db);
                ctx.fillText(dbLabel, 8, y - 4);
            }
        }

        drawGradientFills() {
            if (!this.gradientManager) return;
            var ctx = this.ctx;

            this.gradientManager.points.forEach(function (gp) {
                if (!gp.active) return;

                var centerX    = this.freqToX(gp.centerFreqHz);
                var octaveWpx  = this.width * 0.100343;
                var radiusPx   = gp.radiusOctaves * octaveWpx;
                var spanPx     = radiusPx * 3.0;

                // Build a radial-style gradient fill (port of C++ ColourGradient loop)
                var stepCount = 21;
                for (var s = 0; s <= stepCount; s++) {
                    var dist   = (s / stepCount - 0.5) * 6.0;
                    var weight = Math.exp(-0.5 * dist * dist);
                    var alphaMult = gp.isSelected ? 0.40 : 0.12;

                    // Blend mode approximation: draw semi-transparent rects
                    ctx.fillStyle = this.hexToRgba(gp.color, weight * alphaMult);
                    ctx.fillRect(centerX - spanPx + (s / stepCount) * spanPx * 2, 0,
                        spanPx * 2 / stepCount + 1, this.height);
                }

                // Boundary lines for selected/hovered
                if (gp.isSelected || this.hoveredGradientId === gp.id) {
                    ctx.strokeStyle = gp.color;
                    ctx.lineWidth = 1;
                    ctx.setLineDash([]);
                    ctx.beginPath();
                    ctx.moveTo(centerX - radiusPx, 0); ctx.lineTo(centerX - radiusPx, this.height);
                    ctx.moveTo(centerX + radiusPx, 0); ctx.lineTo(centerX + radiusPx, this.height);
                    ctx.stroke();

                    ctx.strokeStyle = gp.color;
                    ctx.beginPath();
                    ctx.moveTo(centerX, 0); ctx.lineTo(centerX, this.height);
                    ctx.stroke();
                }
            }.bind(this));
        }

        hexToRgba(hex, alpha) {
            var r, g, b;
            if (hex.length === 4) { // #rgb
                r = parseInt(hex[1] + hex[1], 16);
                g = parseInt(hex[2] + hex[2], 16);
                b = parseInt(hex[3] + hex[3], 16);
            } else {
                r = parseInt(hex.slice(1, 3), 16);
                g = parseInt(hex.slice(3, 5), 16);
                b = parseInt(hex.slice(5, 7), 16);
            }
            return 'rgba(' + r + ',' + g + ',' + b + ',' + (alpha !== undefined ? alpha : 1) + ')';
        }

        drawSpectrumFog() {
            var ctx = this.ctx;

            if (!this.hasNativeAnalysis) {
                this.spectrumData.fill(-100.0);
                this.sidechainData.fill(-100.0);
            }

            // Draw filled fog
            ctx.beginPath();
            var first = true;
            for (var xi = 0; xi <= this.width; xi += 2) {
                var frequency = this.xToFreq(xi);

                var specDb = this.getSpectrumDbAtFrequency(
                    frequency,
                    this.spectrumData
                );

                var y = this.gainToY(specDb);
                if (first) { ctx.moveTo(xi, y); first = false; }
                else { ctx.lineTo(xi, y); }
            }

            ctx.lineTo(this.width, this.height);
            ctx.lineTo(0, this.height);
            ctx.closePath();

            var grad = ctx.createLinearGradient(0, 0, 0, this.height);
            grad.addColorStop(0, 'rgba(244, 196, 114, 0.35)');
            grad.addColorStop(1, 'rgba(244, 196, 114, 0.0)');
            ctx.fillStyle = grad;
            ctx.fill();

            ctx.shadowBlur  = 8;
            ctx.shadowColor = this.colors.phosphor;
            ctx.strokeStyle = this.colors.phosphor;
            ctx.lineWidth   = 1.5;
            ctx.stroke();
            ctx.shadowBlur  = 0;

            // Sidechain spectrum line
            if (this.sidechainData && this.sidechainData.length > 0 && this.sidechainData[0] !== -100) {
                ctx.beginPath();
                var scFirst = true;
                for (var x2 = 0; x2 <= this.width; x2 += 2) {
                    var scFreq = this.xToFreq(x2);

                    var scDb = this.getSpectrumDbAtFrequency(
                        scFreq,
                        this.sidechainData
                    );

                    var sy = this.gainToY(scDb);
                    if (scFirst) { ctx.moveTo(x2, sy); scFirst = false; }
                    else { ctx.lineTo(x2, sy); }
                }
                ctx.strokeStyle = 'rgba(40, 200, 255, 0.6)';
                ctx.lineWidth   = 1.2;
                ctx.stroke();
            }
        }

        drawCompressionDeltasAndTarget() {
            var ctx = this.ctx;

            if (this.targetCurveDirty || this.targetDbPerPixel.length !== this.width + 1) {
                this.buildTargetCurveCache();
                this.targetCurveDirty = false;
            }

            var step = 4;

            for (var x = 0; x <= this.width; x += step) {
                var targetDb = this.targetDbPerPixel[x];
                var yTarget  = this.gainToY(targetDb);

                var delta = 0;
                var freq = this.xToFreq(x);
                delta = this.getInterpolatedDelta(freq);

                var yComp = this.gainToY(targetDb + delta);

                if (delta < -0.1) {
                    ctx.fillStyle = this.colors.downwardFill;
                    ctx.fillRect(x, yTarget, step, yComp - yTarget);
                } else if (delta > 0.1) {
                    ctx.fillStyle = this.colors.upwardFill;
                    ctx.fillRect(x, yComp, step, yTarget - yComp);
                }
            }

            // Target curve line
            ctx.beginPath();
            for (var tx = 0; tx <= this.width; tx += 2) {
                var tDb = this.targetDbPerPixel[tx];
                var ty  = this.gainToY(tDb);
                if (tx === 0) ctx.moveTo(tx, ty);
                else ctx.lineTo(tx, ty);
            }

            ctx.shadowBlur  = 10;
            ctx.shadowColor = this.colors.phosphor;
            ctx.strokeStyle = this.colors.phosphor;
            ctx.lineWidth   = 2.2;
            ctx.stroke();
            ctx.shadowBlur  = 0;

            // Average gain line (port of C++: bins 10..200)
            var avgGain = 0;
            var validBins = 0;
            for (var abi = 10; abi < Math.min(200, this.numBins); ++abi) {
                var deltaDb = this.deltaData[abi];
                if (Math.abs(deltaDb) < 20) {
                    avgGain += deltaDb;
                    ++validBins;
                }
            }

            if (validBins > 0) {
                avgGain /= validBins;
                var avgY = this.gainToY(avgGain);

                ctx.strokeStyle = 'rgba(100, 200, 255, 0.5)';
                ctx.setLineDash([6, 4]);
                ctx.beginPath();
                for (var ax = 0; ax < this.width; ax += 8) {
                    ctx.moveTo(ax, avgY);
                    ctx.lineTo(ax + 4, avgY);
                }
                ctx.stroke();
                ctx.setLineDash([]);

                ctx.fillStyle = 'rgba(255, 255, 255, 0.9)';
                ctx.font      = 'bold 10px "GOST Type B", monospace';
                ctx.textAlign = 'left';
                ctx.fillText('AVG: ' + avgGain.toFixed(1) + ' dB', 12, avgY - 8);
            }
        }

        getInterpolatedDelta(freq) {
            return this.getArrayValueAtFrequency(
                freq,
                this.deltaData,
                0.0
            );
        }

        drawEQNodes() {
            var ctx = this.ctx;

            for (var i = 0; i < 8; ++i) {
                var b = this.eqBands[i];
                if (!b.enabled && this.draggingNode !== i) continue;

                var nodeDb = this.getTotalTargetDbAtFreq(b.freq);
                var pos    = { x: this.freqToX(b.freq), y: this.gainToY(nodeDb) };

                var isHovered = (this.hoveredNode === i || this.draggingNode === i);
                var qRadius   = Math.max(10, 35.0 / Math.max(0.1, b.q));
                var intensity = (this.draggingNode === i) ? 1.0 : (isHovered ? 0.85 : 0.5);

                if (isHovered) {
                    ctx.strokeStyle = this.colors.phosphor + '40';
                    ctx.lineWidth   = 1.2;
                    ctx.beginPath();
                    ctx.arc(pos.x, pos.y, qRadius, 0, Math.PI * 2);
                    ctx.stroke();
                } else {
                    ctx.fillStyle = this.colors.phosphor + '15';
                    ctx.beginPath();
                    ctx.arc(pos.x, pos.y, qRadius, 0, Math.PI * 2);
                    ctx.fill();
                }

                // Center glowing dot
                ctx.fillStyle    = this.colors.phosphor;
                ctx.shadowBlur   = isHovered ? 12 : 6;
                ctx.shadowColor  = this.colors.phosphor;
                ctx.beginPath();
                ctx.arc(pos.x, pos.y, isHovered ? 6 : 4.5, 0, Math.PI * 2);
                ctx.fill();
                ctx.shadowBlur   = 0;

                // Band number
                ctx.fillStyle    = '#0a0908';
                ctx.font         = 'bold 9px monospace';
                ctx.textAlign    = 'center';
                ctx.textBaseline = 'middle';
                ctx.fillText((i + 1).toString(), pos.x, pos.y);
            }
        }

        drawGradientMarkers() {
            if (!this.gradientManager) return;
            var ctx = this.ctx;

            this.gradientManager.points.forEach(function (gp) {
                if (!gp.active) return;

                var gradTotalDb = this.getTotalTargetDbAtFreq(gp.centerFreqHz);
                var center      = { x: this.freqToX(gp.centerFreqHz), y: this.gainToY(gradTotalDb) };
                var intensity   = gp.isSelected ? 1.0 : (this.hoveredGradientId === gp.id ? 0.8 : 0.4);

                // Outer ellipse
                ctx.strokeStyle = gp.color;
                ctx.lineWidth   = gp.isSelected ? 2.5 : 1.5;
                ctx.beginPath();
                ctx.arc(center.x, center.y, 10, 0, Math.PI * 2);
                ctx.stroke();

                // Inner glow dot
                ctx.fillStyle      = gp.color;
                ctx.shadowBlur     = gp.isSelected ? 14 : 8;
                ctx.shadowColor    = gp.color;
                ctx.beginPath();
                ctx.arc(center.x, center.y, 5, 0, Math.PI * 2);
                ctx.fill();
                ctx.shadowBlur     = 0;

                // Octaves text
                if (gp.isSelected || this.hoveredGradientId === gp.id) {
                    ctx.fillStyle = gp.color;
                    ctx.font      = 'bold 10px "GOST Type B", monospace';
                    ctx.textAlign = 'center';
                    ctx.fillText(gp.radiusOctaves.toFixed(1) + ' oct', center.x, center.y + 20);
                }
            }.bind(this));
        }

        drawThreshold() {
            var ctx = this.ctx;
            var threshY = Math.round(this.gainToY(this.globalThresh));

            ctx.strokeStyle = this.colors.thresholdLine;
            ctx.lineWidth   = 1.5;
            ctx.setLineDash([4, 4]);
            ctx.beginPath();
            ctx.moveTo(0, threshY);
            ctx.lineTo(this.width, threshY);
            ctx.stroke();
            ctx.setLineDash([]);

            ctx.fillStyle = this.colors.threshold;
            ctx.font      = 'bold 10px "GOST Type B", "Courier New", monospace';
            ctx.textAlign = 'left';
            ctx.fillText('THRESHOLD ' + this.globalThresh.toFixed(1) + ' dB', 12, threshY - 6);
        }

        drawScanlines() {
            if (!this.scanlineCanvas)
                return;

            this.ctx.drawImage(
                this.scanlineCanvas,
                0,
                0,
                this.width,
                this.height
            );
        }

        drawTooltip() {
            var tooltipFreq = -1;
            var tooltipGain = 0;
            var tooltipPos  = null;
            var tooltipColor = '#ffffff';

            if (this.draggingGradientId >= 0 || this.hoveredGradientId >= 0) {
                var id = this.draggingGradientId >= 0 ? this.draggingGradientId : this.hoveredGradientId;
                var gp = this.gradientManager ? this.gradientManager.getPoint(id) : null;
                if (gp) {
                    tooltipFreq  = gp.centerFreqHz;
                    tooltipGain  = this.getTotalTargetDbAtFreq(gp.centerFreqHz);
                    tooltipPos   = { x: this.freqToX(tooltipFreq), y: this.gainToY(tooltipGain) };
                    tooltipColor = gp.color;
                }
            } else if (this.draggingNode >= 0 || this.hoveredNode >= 0) {
                var nid = this.draggingNode >= 0 ? this.draggingNode : this.hoveredNode;
                var nb  = this.eqBands[nid];
                tooltipFreq  = nb.freq;
                tooltipGain  = this.getTotalTargetDbAtFreq(nb.freq);
                tooltipPos   = { x: this.freqToX(tooltipFreq), y: this.gainToY(tooltipGain) };
                tooltipColor = this.colors.phosphor;
            }

            if (!tooltipPos) return;

            var ctx   = this.ctx;
            var fStr  = tooltipFreq >= 1000.0
                ? (tooltipFreq / 1000.0).toFixed(2) + ' kHz'
                : Math.round(tooltipFreq) + ' Hz';
            var gStr  = tooltipGain.toFixed(1) + ' dB';
            var text  = fStr + ' | ' + gStr;

            var textW = 100;
            var textH = 22;
            var bx    = tooltipPos.x - textW / 2.0;
            var by    = tooltipPos.y - 35.0;

            if (by < 5)  by = tooltipPos.y + 15;
            if (bx < 5)  bx = 5;
            if (bx + textW > this.width - 5) bx = this.width - textW - 5;

            ctx.fillStyle = 'rgba(20, 18, 15, 0.95)';
            ctx.fillRect(bx, by, textW, textH);

            ctx.strokeStyle = tooltipColor;
            ctx.lineWidth   = 1.2;
            ctx.strokeRect(bx, by, textW, textH);

            ctx.fillStyle    = '#ffffff';
            ctx.font         = 'bold 11px "GOST Type B", monospace';
            ctx.textAlign    = 'center';
            ctx.textBaseline = 'middle';
            ctx.fillText(text, bx + textW / 2, by + textH / 2);
        }

        render() {
            this.ctx.clearRect(0, 0, this.width, this.height);
            this.drawBackground();
            this.drawGrid();
            this.drawGradientFills();
            this.drawSpectrumFog();
            this.drawCompressionDeltasAndTarget();
            this.drawEQNodes();
            this.drawGradientMarkers();
            this.drawThreshold();
            this.drawTooltip();
            this.drawScanlines();
        }

        startLoop() {
            var self = this;
            self.targetCurveDirty = true;
            var previousFrameTime = 0;
            var frameIntervalMs = 1000.0 / 20.0;

            var loop = function (timestamp) {
                if (timestamp - previousFrameTime >= frameIntervalMs) {
                    previousFrameTime = timestamp;

                    if (self.targetCurveDirty) {
                        self.buildTargetCurveCache();
                        self.targetCurveDirty = false;
                    }

                    self.render();
                }

                self.animationFrameId = requestAnimationFrame(loop);
            };

            self.animationFrameId = requestAnimationFrame(loop);

            // Subscribe to real-time analysis data from C++
            if (JuceBridge && JuceBridge.isJuceAvailable()) {
                JuceBridge.onAnalysisReady(function (data) {
                    if (!data || !data.spectrum || data.spectrum.length === 0)
                        return;

                    self.updateAnalysisData(data);
                });
            }
        }

        // =================================================================
        // Event handling (port of EQGraphLED mouse handlers)
        // =================================================================

        initEvents() {
            var self = this;

            if (typeof ResizeObserver !== 'undefined') {
                this.resizeObserver = new ResizeObserver(function () {
                    self.resize();
                });
                this.resizeObserver.observe(this.canvas);
            } else {
                window.addEventListener('resize', function () {
                    self.resize();
                });
            }

            function addGradientFromEvent(event) {
                var pos =
                    self.getEventPosition(event);

                if (!pos.inside)
                    return false;

                var freq =
                    self.xToFreq(pos.x);

                var gainDb =
                    self.yToGain(pos.y)
                    - self.globalThresh;

                gainDb = Math.max(
                    -60.0,
                    Math.min(60.0, gainDb)
                );

                var pointId =
                    self.createGradientAt(
                        freq,
                        gainDb
                    );

                return pointId >= 0;
            }

            this.canvas.addEventListener('mousedown', function (e) {
                var pos  = self.getEventPosition(e);

                if (!pos.inside)
                    return;

                /*
                    Reliable gradient creation:

                    Shift + left click — primary method.
                    Right click — additional fallback.
                */
                var wantsGradient =
                    (e.button === 0 && e.shiftKey) ||
                    e.button === 2;

                if (wantsGradient) {
                    e.preventDefault();
                    e.stopPropagation();

                    addGradientFromEvent(e);
                    return;
                }

                /*
                    Only left mouse button below.
                */
                if (e.button !== 0)
                    return;

                /*
                    1. Existing gradient marker.
                */
                if (self.gradientManager) {
                    for (var gi = 0;
                         gi < self.gradientManager.points.length;
                         ++gi) {
                        var gp =
                            self.gradientManager.points[gi];

                        if (!gp.active)
                            continue;

                        var gradPos = {
                            x: self.freqToX(
                                gp.centerFreqHz
                            ),

                            y: self.gainToY(
                                self.getTotalTargetDbAtFreq(
                                    gp.centerFreqHz
                                )
                            )
                        };

                        if (Math.hypot(
                                pos.x - gradPos.x,
                                pos.y - gradPos.y) < 20.0) {
                            self.draggingGradientId =
                                gp.id;

                            self.gradientManager
                                .setActivePoint(
                                    gp.id
                                );

                            if (JuceBridge &&
                                JuceBridge.isJuceAvailable()) {
                                JuceBridge.beginGesture(
                                    'GRADIENT_' +
                                    gp.id +
                                    '_CENTER_FREQ'
                                );

                                JuceBridge.beginGesture(
                                    'GRADIENT_' +
                                    gp.id +
                                    '_CENTER_GAIN'
                                );
                            }

                            if (self.onGradientSelectionChanged)
                                self.onGradientSelectionChanged();

                            return;
                        }
                    }
                }

                /*
                    2. Existing EQ node.
                */
                for (
                    var ei = 0;
                    ei < self.eqBands.length;
                    ++ei
                ) {
                    var band =
                        self.eqBands[ei];

                    if (!band.enabled)
                        continue;

                    var nodeX =
                        self.freqToX(band.freq);

                    var nodeY =
                        self.gainToY(
                            self.getTotalTargetDbAtFreq(
                                band.freq
                            )
                        );

                    if (Math.hypot(
                            pos.x - nodeX,
                            pos.y - nodeY) < 18.0) {
                        self.draggingNode = ei;
                        self.dragStartFreq =
                            band.freq;
                        self.dragStartGain =
                            band.gain;

                        if (JuceBridge &&
                            JuceBridge.isJuceAvailable()) {
                            JuceBridge.beginGesture(
                                'BAND_' + ei +
                                '_FREQ'
                            );

                            JuceBridge.beginGesture(
                                'BAND_' + ei +
                                '_GAIN'
                            );
                        }

                        return;
                    }
                }

                /*
                    3. Threshold line.
                */
                var thresholdY =
                    self.gainToY(
                        self.globalThresh
                    );

                if (Math.abs(
                        pos.y - thresholdY) < 12.0) {
                    self.isDraggingThresh = true;

                    if (JuceBridge &&
                        JuceBridge.isJuceAvailable()) {
                        JuceBridge.beginGesture(
                            'GLOBAL_THRESH'
                        );
                    }

                    return;
                }

                /*
                    4. If a gradient is selected, the first
                       click on empty space only clears
                       the selection.
                */
                if (self.gradientManager &&
                    self.gradientManager
                        .hasActivePoint()) {
                    self.gradientManager
                        .clearActive();

                    if (self.onGradientSelectionChanged)
                        self.onGradientSelectionChanged();

                    return;
                }

                /*
                    5. Ordinary left click creates an EQ
                       point.
                */
                var newFreq =
                    self.xToFreq(pos.x);

                var newGain =
                    self.yToGain(pos.y)
                    - self.globalThresh;

                newGain = Math.max(
                    -60.0,
                    Math.min(60.0, newGain)
                );

                self.createEQBandAt(
                    newFreq,
                    newGain
                );
            });

            self.canvas.addEventListener(
                'contextmenu',
                function (e) {
                    /*
                        mousedown normally creates the
                        gradient. This handler primarily
                        suppresses the browser context menu.
                    */
                    e.preventDefault();
                    e.stopPropagation();
                }
            );

            // Double click to delete
            self.canvas.addEventListener('dblclick', function (e) {
                var pos  = self.getEventPosition(e);

                if (!pos.inside)
                    return;

                // Delete EQ node
                for (var di = 0; di < 8; ++di) {
                    if (!self.eqBands[di].enabled) continue;
                    var dnX = self.freqToX(self.eqBands[di].freq);
                    var dnY = self.gainToY(self.getTotalTargetDbAtFreq(self.eqBands[di].freq));
                    if (Math.hypot(pos.x - dnX, pos.y - dnY) < 15.0) {
                        self.eqBands[di].enabled = false;
                        self.targetCurveDirty = true;
                        if (JuceBridge && JuceBridge.isJuceAvailable()) {
                            JuceBridge.setParameter('BAND_' + di + '_ENABLE', 0.0);
                        }
                        return;
                    }
                }

                // Delete gradient point
                if (self.gradientManager) {
                    for (var gi2 = 0; gi2 < self.gradientManager.points.length; ++gi2) {
                        var gp2 = self.gradientManager.points[gi2];
                        var gpx = self.freqToX(gp2.centerFreqHz);
                        var gpy = self.gainToY(self.getTotalTargetDbAtFreq(gp2.centerFreqHz));
                            if (Math.hypot(pos.x - gpx, pos.y - gpy) < 15.0) {
                                var deletedGradientId = gp2.id;

                                self.gradientManager.removePoint(
                                    deletedGradientId
                                );

                                self.targetCurveDirty = true;

                                if (
                                    JuceBridge &&
                                    JuceBridge.isJuceAvailable()
                                ) {
                                    JuceBridge.setParameter(
                                        'GRADIENT_' +
                                        deletedGradientId +
                                        '_ENABLE',
                                        0.0
                                    );
                                }

                                if (self.onGradientParamsChanged)
                                    self.onGradientParamsChanged();

                                if (self.onGradientSelectionChanged)
                                    self.onGradientSelectionChanged();

                                return;
                            }
                    }
                }
            });

            // Mouse move (hover + drag)
            window.addEventListener('mousemove', function (e) {
                var pos  = self.getEventPosition(e);

                var isDragging =
                    self.draggingNode >= 0 ||
                    self.draggingGradientId >= 0 ||
                    self.isDraggingThresh;

                if (!pos.inside && !isDragging) {
                    self.hoveredNode = -1;
                    self.hoveredGradientId = -1;
                    return;
                }

                // Hover detection
                self.hoveredNode        = -1;
                self.hoveredGradientId  = -1;

                if (self.gradientManager) {
                    for (var hi = 0; hi < self.gradientManager.points.length; ++hi) {
                        var hp  = self.gradientManager.points[hi];
                        var hpx = self.freqToX(hp.centerFreqHz);
                        var hpy = self.gainToY(self.getTotalTargetDbAtFreq(hp.centerFreqHz));
                        if (Math.hypot(pos.x - hpx, pos.y - hpy) < 15.0) {
                            self.hoveredGradientId = hp.id;
                            break;
                        }
                    }
                }

                if (self.hoveredGradientId < 0) {
                    for (var hn = 0; hn < 8; ++hn) {
                        if (!self.eqBands[hn].enabled) continue;
                        var hnx = self.freqToX(self.eqBands[hn].freq);
                        var hny = self.gainToY(self.getTotalTargetDbAtFreq(self.eqBands[hn].freq));
                        if (Math.hypot(pos.x - hnx, pos.y - hny) < 15.0) {
                            self.hoveredNode = hn;
                            break;
                        }
                    }
                }

                // Drag gradient point
                if (self.draggingGradientId >= 0 && self.gradientManager) {
                    var dgp = self.gradientManager.getPoint(self.draggingGradientId);
                    if (dgp) {
                        var newFreq = Math.min(20000.0, Math.max(20.0, self.xToFreq(pos.x)));
                        var newGain = Math.min(60.0, Math.max(-60.0, self.yToGain(pos.y) - self.globalThresh));

                        var freqChanged = Math.abs(dgp.centerFreqHz - newFreq) > 0.01;
                        var gainChanged = Math.abs(dgp.centerGainDb - newGain) > 0.001;

                        dgp.centerFreqHz = newFreq;
                        dgp.centerGainDb = newGain;

                        self.targetCurveDirty = true;

                        if (JuceBridge && JuceBridge.isJuceAvailable()) {
                            var prefix = 'GRADIENT_' + dgp.id;

                            if (freqChanged) {
                                JuceBridge.queueParameterChange(
                                    prefix + '_CENTER_FREQ',
                                    Math.log10(newFreq / 20.0) / 3.0
                                );
                            }

                            if (gainChanged) {
                                JuceBridge.queueParameterChange(
                                    prefix + '_CENTER_GAIN',
                                    (newGain + 60.0) / 120.0
                                );
                            }
                        }
                        return;
                    }
                }

                // Drag EQ node
                if (self.draggingNode >= 0) {
                    var db = self.eqBands[self.draggingNode];
                    var newFreq = Math.min(20000.0, Math.max(20.0, self.xToFreq(pos.x)));
                    var newGain = Math.min(60.0, Math.max(-60.0, self.yToGain(pos.y) - self.globalThresh));

                    var freqChanged = Math.abs(db.freq - newFreq) > 0.01;
                    var gainChanged = Math.abs(db.gain - newGain) > 0.001;

                    db.freq = newFreq;
                    db.gain = newGain;

                    self.targetCurveDirty = true;

                    if (JuceBridge && JuceBridge.isJuceAvailable()) {
                        var bprefix = 'BAND_' + self.draggingNode;

                        if (freqChanged) {
                            JuceBridge.queueParameterChange(
                                bprefix + '_FREQ',
                                Math.log10(newFreq / 20.0) / 3.0
                            );
                        }

                        if (gainChanged) {
                            JuceBridge.queueParameterChange(
                                bprefix + '_GAIN',
                                (newGain + 60.0) / 120.0
                            );
                        }
                    }
                    return;
                }

                // Drag threshold
                if (self.isDraggingThresh) {
                    var newThresh = Math.min(12.0, Math.max(-48.0, self.yToGain(pos.y)));

                    if (Math.abs(self.globalThresh - newThresh) > 0.01) {
                        self.globalThresh = newThresh;

                        self.targetCurveDirty = true;

                        if (JuceBridge && JuceBridge.isJuceAvailable()) {
                            JuceBridge.queueParameterChange(
                                'GLOBAL_THRESH',
                                (newThresh + 48.0) / 60.0
                            );
                        }
                    }
                }
            });

            window.addEventListener('mouseup', function () {
                if (JuceBridge && JuceBridge.isJuceAvailable()) {
                    if (self.draggingNode >= 0) {
                        var prevNode = self.draggingNode;
                        self.draggingNode = -1;
                        JuceBridge.endGesture('BAND_' + prevNode + '_FREQ');
                        JuceBridge.endGesture('BAND_' + prevNode + '_GAIN');
                    }

                    if (self.draggingGradientId >= 0) {
                        var prevGpId = self.draggingGradientId;
                        self.draggingGradientId = -1;
                        JuceBridge.endGesture('GRADIENT_' + prevGpId + '_CENTER_FREQ');
                        JuceBridge.endGesture('GRADIENT_' + prevGpId + '_CENTER_GAIN');
                    }

                    if (self.isDraggingThresh) {
                        self.isDraggingThresh = false;
                        JuceBridge.endGesture('GLOBAL_THRESH');
                    }
                } else {
                    self.draggingNode       = -1;
                    self.draggingGradientId = -1;
                    self.isDraggingThresh    = false;
                }
            });

            // Wheel events (Q change / octave change / view range)
            self.canvas.addEventListener('wheel', function (e) {
                e.preventDefault();
                var pos  = self.getEventPosition(e);

                if (!pos.inside)
                    return;

                // Ctrl+Wheel → cycle view range
                if (e.ctrlKey) {
                    var depths = [24, 48, 72, 96, 120];
                    var curIdx  = 0;
                    var minDiff = 9999;
                    for (var ci = 0; ci < depths.length; ci++) {
                        var d = Math.abs(depths[ci] - self.baseViewDepth);
                        if (d < minDiff) { minDiff = d; curIdx = ci; }
                    }
                    var direction = e.deltaY > 0 ? 1 : -1;
                    var newIdx    = Math.min(depths.length - 1, Math.max(0, curIdx + direction));
                    if (newIdx !== curIdx) {
                        self.setViewRange(depths[newIdx]);
                        if (JuceBridge && JuceBridge.isJuceAvailable()) {
                            JuceBridge.setParameter('VIEW_RANGE', newIdx / (depths.length - 1));
                        }
                        self.zoomIndicatorAlpha = 1.0;
                    }
                    return;
                }

                // 1. Wheel on gradient → change octaves
                if (self.gradientManager) {
                    for (var gi = 0; gi < self.gradientManager.points.length; ++gi) {
                        var gp2  = self.gradientManager.points[gi];
                        var gpx2 = self.freqToX(gp2.centerFreqHz);
                        var gpy2 = self.gainToY(self.getTotalTargetDbAtFreq(gp2.centerFreqHz));
                        var dist = Math.hypot(pos.x - gpx2, pos.y - gpy2);

            if (dist < 25.0) {
                var factor = Math.exp(-e.deltaY * 0.002);
                gp2.radiusOctaves = Math.min(4.0, Math.max(0.3, gp2.radiusOctaves * factor));
                self.targetCurveDirty = true;
                if (JuceBridge && JuceBridge.isJuceAvailable()) {
                    JuceBridge.setParameter('GRADIENT_' + gp2.id + '_BANDWIDTH',
                        (gp2.radiusOctaves - 0.5) / 3.5);
                }
                return;
            }
                    }
                }

                // 2. Wheel on EQ node → change Q
                var targetNode = (self.draggingNode >= 0) ? self.draggingNode : self.hoveredNode;
                if (targetNode >= 0) {
                    var b2      = self.eqBands[targetNode];
                    var qFactor = Math.exp(-e.deltaY * 0.002);
                    b2.q         = Math.min(10.0, Math.max(0.1, b2.q * qFactor));
                    self.targetCurveDirty = true;
                    if (JuceBridge && JuceBridge.isJuceAvailable()) {
                        JuceBridge.setParameter('BAND_' + targetNode + '_Q',
                            (Math.log10(b2.q) + 1.0) / 2.0);
                    }
                }
            });
        }

        createGradientAt(freq, gainDb) {
            if (!this.gradientManager)
                return -1;

            /*
                Maximum: four gradient slots.
            */
            if (this.gradientManager.points.length >= 4)
                return -1;

            freq = Math.max(
                20.0,
                Math.min(
                    20000.0,
                    Number(freq) || 20.0
                )
            );

            gainDb = Math.max(
                -60.0,
                Math.min(
                    60.0,
                    Number(gainDb) || 0.0
                )
            );

            var pointId =
                this.gradientManager.addPoint(
                    freq,
                    gainDb
                );

            if (!Number.isFinite(Number(pointId)) ||
                Number(pointId) < 0) {
                console.warn(
                    '[SpectrumScreen] ' +
                    'GradientManager.addPoint failed'
                );

                return -1;
            }

            pointId = Number(pointId);

            var point =
                this.gradientManager.getPoint(
                    pointId
                );

            if (!point) {
                console.warn(
                    '[SpectrumScreen] ' +
                    'Created gradient not found:',
                    pointId
                );

                return -1;
            }

            point.active = true;
            point.isSelected = true;
            point.centerFreqHz = freq;
            point.centerGainDb = gainDb;

            if (!Number.isFinite(
                    Number(point.radiusOctaves))) {
                point.radiusOctaves = 1.0;
            }

            this.gradientManager
                .setActivePoint(pointId);

            this.targetCurveDirty = true;

            var prefix =
                'GRADIENT_' + pointId;

            if (JuceBridge &&
                JuceBridge.isJuceAvailable()) {
                JuceBridge.setParameter(
                    prefix + '_ENABLE',
                    1.0
                );

                JuceBridge.setParameter(
                    prefix + '_CENTER_FREQ',
                    Math.max(
                        0.0,
                        Math.min(
                            1.0,
                            Math.log10(
                                freq / 20.0
                            ) / 3.0
                        )
                    )
                );

                JuceBridge.setParameter(
                    prefix + '_CENTER_GAIN',
                    Math.max(
                        0.0,
                        Math.min(
                            1.0,
                            (gainDb + 60.0) / 120.0
                        )
                    )
                );

                JuceBridge.setParameter(
                    prefix + '_BANDWIDTH',
                    Math.max(
                        0.0,
                        Math.min(
                            1.0,
                            (point.radiusOctaves
                                - 0.5) / 3.5
                        )
                    )
                );
            }

            if (this.onGradientSelectionChanged)
                this.onGradientSelectionChanged();

            if (this.onGradientParamsChanged)
                this.onGradientParamsChanged();

            return pointId;
        }

        createEQBandAt(freq, gainDb) {
            freq = Math.max(
                20.0,
                Math.min(
                    20000.0,
                    Number(freq) || 20.0
                )
            );

            gainDb = Math.max(
                -60.0,
                Math.min(
                    60.0,
                    Number(gainDb) || 0.0
                )
            );

            /*
                Do not stack a new band directly
                on top of an existing one.
                If close enough, select that band
                instead.
            */
            for (
                var existing = 0;
                existing < this.eqBands.length;
                ++existing
            ) {
                var current =
                    this.eqBands[existing];

                if (!current.enabled)
                    continue;

                var distanceOctaves =
                    Math.abs(
                        Math.log2(
                            freq / current.freq
                        )
                    );

                if (distanceOctaves < 0.03 &&
                    Math.abs(
                        current.gain - gainDb)
                        < 3.0) {
                    this.draggingNode =
                        existing;

                    return existing;
                }
            }

            for (var i = 0;
                 i < this.eqBands.length;
                 ++i) {
                var band =
                    this.eqBands[i];

                if (band.enabled)
                    continue;

                band.enabled = true;
                band.freq = freq;
                band.gain = gainDb;
                band.q = 1.0;

                this.targetCurveDirty =
                    true;

                if (
                    JuceBridge &&
                    JuceBridge.isJuceAvailable()
                ) {
                    var prefix =
                        'BAND_' + i;

                    JuceBridge.beginGesture(
                        prefix + '_FREQ'
                    );

                    JuceBridge.setParameter(
                        prefix + '_ENABLE',
                        1.0
                    );

                    JuceBridge.setParameter(
                        prefix + '_FREQ',
                        Math.log10(
                            freq / 20.0) / 3.0
                    );

                    JuceBridge.setParameter(
                        prefix + '_GAIN',
                        (gainDb + 60.0) / 120.0
                    );

                    JuceBridge.setParameter(
                        prefix + '_Q',
                        0.5
                    );

                    JuceBridge.endGesture(
                        prefix + '_FREQ'
                    );

                    JuceBridge.endGesture(
                        prefix + '_GAIN'
                    );
                }

                return i;
            }

            return -1;
        }
    };
});
