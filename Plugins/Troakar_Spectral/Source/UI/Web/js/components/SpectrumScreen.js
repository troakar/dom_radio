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

            /*
                Fixed TROAKAR SPECTRAL display range.

                    top:    +12 dB
                    bottom: -96 dB
                    total:  108 dB

                This must never be altered by VIEW_RANGE,
                Ctrl + Wheel, presets, or host automation.
            */
            this.displayMaxDb   = 12.0;
            this.displayMinDb   = -96.0;
            this.displayRangeDb = 108.0;

            /*
                Compatibility aliases.
                Existing renderer methods use minDb/maxDb,
                so retain these names but keep them immutable.
            */
            this.maxDb = this.displayMaxDb;
            this.minDb = this.displayMinDb;

            /*
                Legacy field retained so old external code
                cannot fail if it reads baseViewDepth.
            */
            this.baseViewDepth = this.displayRangeDb;

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
            this.deltaData =
                new Float32Array(this.numBins).fill(0.0);

            this.detectorData =
                new Float32Array(
                    this.numBins
                ).fill(-120.0);

            this.effectiveTargetData =
                new Float32Array(
                    this.numBins
                ).fill(0.0);

            /*
                displayMode:

                'detector':
                    Shows the exact DSP detector
                    envelope and effective target.

                'spectrum':
                    Shows the raw input FFT magnitude.
                    Legacy mode — may not visually
                    align with DSP compressor action.
            */
            this.displayMode = 'detector';

            /*
                Avoid accessing .length on undefined during
                the first render frame.
            */
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

            /*
                Called by app.js while Δ audition is active.

                Arguments:
                    frequencyHz
                    widthOctaves
                    sourceType: 'eq' | 'gradient' | 'none'
                    sourceId
            */
            this.onAuditionFocusChanged = null;

            this.lastAuditionFrequencyHz = -1.0;
            this.lastAuditionWidthOctaves = -1.0;
            this.lastAuditionSource = '';
            this.lastAuditionSourceId = -1;

            this.resize();
            this.lastRightClickTime = 0;
            this.installContextMenuGuard();
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

            const incomingDetector =
                Array.isArray(data.detector)
                    ? data.detector
                    : [];

            const incomingEffectiveTarget =
                Array.isArray(data.effectiveTarget)
                    ? data.effectiveTarget
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

                this.detectorData =
                    new Float32Array(
                        this.numBins
                    );

                this.effectiveTargetData =
                    new Float32Array(
                        this.numBins
                    );
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

                if (i < incomingDetector.length) {
                    const detectorValue =
                        Number(
                            incomingDetector[i]
                        );

                    this.detectorData[i] =
                        Number.isFinite(
                            detectorValue
                        )
                            ? Math.max(
                                -120.0,
                                Math.min(
                                    24.0,
                                    detectorValue
                                )
                            )
                            : -120.0;
                } else {
                    this.detectorData[i] =
                        -120.0;
                }

                if (i < incomingEffectiveTarget.length) {
                    const targetValue =
                        Number(
                            incomingEffectiveTarget[i]
                        );

                    this.effectiveTargetData[i] =
                        Number.isFinite(
                            targetValue
                        )
                            ? Math.max(
                                -120.0,
                                Math.min(
                                    120.0,
                                    targetValue
                                )
                            )
                            : this.getTargetCurveDb(
                                this.xToFreq(
                                    i
                                    / Math.max(
                                        1,
                                        this.numBins
                                            - 1
                                    )
                                    * this.width
                                )
                            );
                } else {
                    this.effectiveTargetData[i] =
                        0.0;
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

        frequencyToBinForScale(
            freq,
            arrayLength,
            scale
        ) {
            var bins = Math.max(
                1,
                Number(arrayLength)
                    || this.numBins
            );

            var safeFreq = Math.max(
                20.0,
                Math.min(
                    this.sampleRate * 0.5,
                    Number(freq) || 20.0
                )
            );

            if (scale === 'logarithmic') {
                var maxFreq = Math.max(
                    20.0,
                    Math.min(
                        20000.0,
                        this.sampleRate * 0.5
                    )
                );

                var normalized =
                    Math.log10(safeFreq / 20.0)
                    / Math.log10(maxFreq / 20.0);

                return Math.max(
                    0.0,
                    Math.min(
                        bins - 1,
                        normalized * (bins - 1)
                    )
                );
            }

            return this.frequencyToBin(
                safeFreq,
                bins
            );
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
            for (let g = 0; g < 4; ++g) {
                if (syncSerial !== this.parameterSyncSerial) return;
                let p = 'GRADIENT_' + g;
                await get(p + '_ENABLE', 0);
                await get(p + '_CENTER_FREQ', 0.5);
                await get(p + '_CENTER_GAIN', 0.5);
                await get(p + '_BANDWIDTH', 0.5);
                await get(p + '_AMOUNT', 0.5);
                await get(p + '_UP_MAX', 0.5);
                await get(p + '_DOWN_MAX', 0.5);
                await get(p + '_SPEED', 0.5);
                await get(p + '_UP_SMOOTH', 0.5);
                await get(p + '_DOWN_SMOOTH', 0.5);
                await get(p + '_UP_SEL', 0.5);
                await get(p + '_DOWN_SEL', 0.5);
                await get(p + '_ATTACK', 0.5);
                await get(p + '_RELEASE', 0.5);
                await get(p + '_KNEE', 0.5);
                await get(p + '_AUTO_SPEED', 1);
            }
            if (syncSerial !== this.parameterSyncSerial) return;

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
            /*
                Intentionally fixed.

                Kept as a harmless compatibility method because
                app.js or old preset/UI code may still call it.
                No caller is allowed to change the display range.
            */
            this.displayMaxDb   = 12.0;
            this.displayMinDb   = -96.0;
            this.displayRangeDb = 108.0;

            this.maxDb = this.displayMaxDb;
            this.minDb = this.displayMinDb;
            this.baseViewDepth = this.displayRangeDb;

            this.targetCurveDirty = true;
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
            var db = Number(dB);

            if (!Number.isFinite(db))
                db = this.displayMinDb;

            db = Math.max(
                this.displayMinDb,
                Math.min(
                    this.displayMaxDb,
                    db
                )
            );

            var normalized =
                (db - this.displayMinDb)
                / this.displayRangeDb;

            var topMargin =
                this.height * 0.08;

            var bottomMargin =
                this.height * 0.92;

            return bottomMargin
                - normalized
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
                Math.min(
                    bottomMargin,
                    safeY
                )
            );

            var normalized =
                (bottomMargin - safeY)
                / Math.max(
                    1.0,
                    bottomMargin - topMargin
                );

            normalized = Math.max(
                0.0,
                Math.min(1.0, normalized)
            );

            return this.displayMinDb
                + normalized
                * this.displayRangeDb;
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

            /*
                GLOBAL_THRESH is the absolute base target level.

                It shifts the complete target system:
                - yellow target curve;
                - EQ nodes;
                - gradient points;
                - target tooltip;
                - compression target zones.

                It does NOT shift spectrum/analyser data.
            */
            var result =
                this.globalThresh
                + this.getEQCurveDb(freqNum)
                + this.getGradientOffsetDb(freqNum);

            if (!Number.isFinite(result))
                return this.globalThresh;

            return Math.max(
                -96.0,
                Math.min(12.0, result)
            );
        }

        /*
            Single authoritative UI geometry.

            Every visual target-related object must use this:
            - yellow target curve;
            - EQ node position;
            - gradient point position;
            - node hit testing;
            - tooltips;
            - drag coordinate conversion.

            Do NOT use effectiveTargetData here.
            That is runtime DSP telemetry, not UI geometry.
        */
        getUiTargetDbAtFreq(freq) {
            return this.getTargetCurveDb(freq);
        }

        /*
            Compatibility alias for older methods in this class.
        */
        getTotalTargetDbAtFreq(freq) {
            return this.getUiTargetDbAtFreq(freq);
        }

        /*
            Calculate the absolute target dB at freq with a
            specific EQ band excluded from the sum.

            Used during drag so the dragged band's local gain
            can be solved from:
                bandGain
                =
                desiredAbsoluteTargetDb
                -
                allOtherContributions(freq)
        */
        getUiTargetDbWithoutEqBand(
            frequencyHz,
            excludedBandIndex
        ) {
            var freq = Math.max(
                20.0,
                Number(frequencyHz) || 20.0
            );

            var result =
                (Number(this.globalThresh) || 0.0)
                + this.getGradientOffsetDb(freq);

            for (var i = 0;
                 i < this.eqBands.length;
                 ++i) {
                if (i === excludedBandIndex)
                    continue;

                result += this.getSingleEqBandDb(
                    freq,
                    this.eqBands[i]
                );
            }

            return result;
        }

        /*
            Absolute target dB at freq with a specific gradient
            excluded from the calculation.

            Used during gradient drag so the dragged gradient's
            local gain can be solved from:
                gradientGain
                =
                desiredAbsoluteTargetDb
                -
                allOtherContributions(freq)
        */
        getUiTargetDbWithoutGradient(
            frequencyHz,
            excludedId
        ) {
            var freq = Math.max(
                20.0,
                Number(frequencyHz) || 20.0
            );

            var result =
                (Number(this.globalThresh) || 0.0)
                + this.getEQCurveDb(freq)
                + this.getGradientOffsetDb(
                    freq,
                    excludedId
                );

            return result;
        }

        /*
            EQ bell contribution for a single band at a given freq.

            Uses the same getBiquadMagSq / log-magnitude conversion
            that getTargetCurveDb() uses internally, so both produce
            identical results.
        */
        getSingleEqBandDb(freq, band) {
            if (!band ||
                !band.enabled ||
                Math.abs(band.gain) < 0.05) {
                return 0.0;
            }

            var safeFreq = Math.max(
                20.0,
                Number(freq) || 20.0
            );

            var bandFreq = Math.max(
                20.0,
                Number(band.freq) || 1000.0
            );

            var q = Math.max(
                0.1,
                Number(band.q) || 1.0
            );

            var gain = Number(band.gain) || 0.0;

            var magnitudeSq =
                this.getBiquadMagSq(
                    safeFreq,
                    bandFreq,
                    q,
                    gain
                );

            var bandDb =
                10.0 * Math.log10(
                    Math.max(
                        1.0e-12,
                        magnitudeSq
                    )
                );

            if (!Number.isFinite(bandDb))
                return 0.0;

            return Math.max(
                -72.0,
                Math.min(72.0, bandDb)
            );
        }

        /*
            Refactored EQ curve that uses getSingleEqBandDb.
        */
        getEQCurveDb(freq) {
            var total = 0.0;

            for (var i = 0;
                 i < this.eqBands.length;
                 ++i) {
                total += this.getSingleEqBandDb(
                    freq,
                    this.eqBands[i]
                );
            }

            return Math.max(
                -120.0,
                Math.min(120.0, total)
            );
        }

        /*
            Gradient contribution at a given frequency.
        */
        getGradientOffsetDb(freq, excludedId) {
            var freqNum =
                Math.max(
                    20.0,
                    Number(freq) || 20.0
                );

            var exId =
                (excludedId === undefined
                    || excludedId === null)
                    ? -1
                    : Number(excludedId);

            var offset = 0.0;

            if (this.gradientManager) {
                this.gradientManager.points.forEach(
                    function (gp) {
                        if (!gp.active
                            || gp.id === exId)
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

                            offset +=
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

            return offset;
        }

        /*
            Unified EQ node screen position.

            Used by drawEQNodes(), mousedown, mousemove hover,
            and dblclick to guarantee identical coordinates.
        */
        getEqNodeScreenPosition(bandIndex) {
            var band =
                this.eqBands[bandIndex];

            if (!band || !band.enabled)
                return null;

            return {
                x: this.freqToX(
                    band.freq
                ),

                y: this.gainToY(
                    this.getUiTargetDbAtFreq(
                        band.freq
                    )
                )
            };
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

            /*
                Fixed 108 dB display grid:

                +12
                  0
                -12
                -24
                -36
                -48
                -60
                -72
                -84
                -96
            */
            var gridValues = [
                 12,
                  0,
                -12,
                -24,
                -36,
                -48,
                -60,
                -72,
                -84,
                -96
            ];

            ctx.textAlign = 'left';

            for (var di = 0;
                 di < gridValues.length;
                 ++di) {
                var db = gridValues[di];
                var y = Math.round(
                    this.gainToY(db)
                );

                var isZero =
                    Math.abs(db) < 0.1;

                ctx.strokeStyle = isZero
                    ? 'rgba(212, 164, 70, 0.30)'
                    : this.colors.grid;

                ctx.beginPath();
                ctx.moveTo(0, y);
                ctx.lineTo(this.width, y);
                ctx.stroke();

                var dbLabel =
                    (db > 0 ? '+' : '')
                    + db;

                ctx.fillText(
                    dbLabel,
                    8,
                    y - 4
                );
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

        drawDetectorFog() {
            var ctx = this.ctx;

            if (!this.hasNativeAnalysis ||
                !this.detectorData ||
                this.detectorData.length === 0) {
                return;
            }

            ctx.save();

            /*
                Fill below the exact DSP detector curve.
            */
            ctx.beginPath();

            var first = true;

            for (var x = 0;
                 x <= this.width;
                 x += 2) {
                var frequency =
                    this.xToFreq(x);

                var detectorDb =
                    this.getArrayValueAtFrequency(
                        frequency,
                        this.detectorData,
                        -120.0
                    );

                var y =
                    this.gainToY(detectorDb);

                if (first) {
                    ctx.moveTo(x, y);
                    first = false;
                } else {
                    ctx.lineTo(x, y);
                }
            }

            ctx.lineTo(
                this.width,
                this.height
            );

            ctx.lineTo(0, this.height);
            ctx.closePath();

            var fill =
                ctx.createLinearGradient(
                    0,
                    0,
                    0,
                    this.height
                );

            fill.addColorStop(
                0,
                'rgba(244, 196, 114, 0.34)'
            );

            fill.addColorStop(
                1,
                'rgba(244, 196, 114, 0.01)'
            );

            ctx.fillStyle = fill;
            ctx.fill();

            /*
                Exact detector contour.
            */
            ctx.beginPath();
            first = true;

            for (var dx = 0;
                 dx <= this.width;
                 dx += 2) {
                var detectorFreq =
                    this.xToFreq(dx);

                var detectorValue =
                    this.getArrayValueAtFrequency(
                        detectorFreq,
                        this.detectorData,
                        -120.0
                    );

                var detectorY =
                    this.gainToY(
                        detectorValue
                    );

                if (first) {
                    ctx.moveTo(
                        dx,
                        detectorY
                    );

                    first = false;
                } else {
                    ctx.lineTo(
                        dx,
                        detectorY
                    );
                }
            }

            ctx.strokeStyle =
                this.colors.phosphor;

            ctx.lineWidth = 1.8;
            ctx.shadowColor =
                this.colors.phosphor;

            ctx.shadowBlur = 8;
            ctx.stroke();

            ctx.shadowBlur = 0;
            ctx.restore();
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

        drawCompressionDeltaPolygon(isDownward) {
            var ctx = this.ctx;
            var step = 2;
            var segments = [];
            var current = null;

            for (var x = 0;
                 x <= this.width;
                 x += step) {
                var frequency =
                    this.xToFreq(x);

                var detectorDb =
                    this.getArrayValueAtFrequency(
                        frequency,
                        this.detectorData,
                        -120.0
                    );

                var deltaDb =
                    this.getInterpolatedDelta(
                        frequency
                    );

                if (!Number.isFinite(deltaDb))
                    deltaDb = 0.0;

                var isActive = isDownward
                    ? deltaDb < -0.05
                    : deltaDb > 0.05;

                if (!isActive) {
                    if (current && current.length > 1)
                        segments.push(current);

                    current = null;
                    continue;
                }

                if (!current)
                    current = [];

                current.push({
                    x: x,
                    detectorDb: detectorDb,
                    processedDb: detectorDb + deltaDb
                });
            }

            if (current && current.length > 1)
                segments.push(current);

            for (var s = 0;
                 s < segments.length;
                 ++s) {
                var segment = segments[s];

                ctx.save();
                ctx.beginPath();

                /*
                    Detector side of polygon.
                */
                for (var i = 0;
                     i < segment.length;
                     ++i) {
                    var topPoint = segment[i];
                    var detectorY =
                        this.gainToY(
                            topPoint.detectorDb
                        );

                    if (i === 0) {
                        ctx.moveTo(topPoint.x, detectorY);
                    } else {
                        ctx.lineTo(topPoint.x, detectorY);
                    }
                }

                /*
                    Processed side, traversed backwards
                    to close the polygon.
                */
                for (var j = segment.length - 1;
                     j >= 0;
                     --j) {
                    var bottomPoint = segment[j];
                    var processedY =
                        this.gainToY(
                            bottomPoint.processedDb
                        );

                    ctx.lineTo(
                        bottomPoint.x,
                        processedY
                    );
                }

                ctx.closePath();

                ctx.fillStyle = isDownward
                    ? this.colors.downwardFill
                    : this.colors.upwardFill;

                ctx.fill();
                ctx.restore();
            }
        }

        drawCompressionDeltasAndTarget() {
            var ctx = this.ctx;

            if (this.targetCurveDirty ||
                this.targetDbPerPixel.length !==
                    this.width + 1) {
                this.buildTargetCurveCache();
                this.targetCurveDirty = false;
            }

            var useNativeDetector =
                this.displayMode === 'detector' &&
                this.hasNativeAnalysis &&
                this.detectorData &&
                this.detectorData.length > 0;

            var useNativeTarget =
                useNativeDetector &&
                this.effectiveTargetData &&
                this.effectiveTargetData.length > 0;

            /*
                DSP-Authoritative Compression Areas.
                The fill between the detector curve and the
                processed (post-dsp) detector curve represents
                the actual gain reduction applied by DSP.

                This uses deltaData (compressionDeltaData from
                the engine), which already includes DN MAX
                clamping. When DN MAX = 0, no downward delta
                exists and the area disappears.

                The effectiveTarget line is drawn separately
                as a contour — it does NOT drive fill areas.
            */
            if (useNativeDetector) {
                /*
                    Both polygons use the same frequency mapping:
                    x -> frequency -> detector/delta interpolation.

                    No independent vertical strips, therefore no
                    visual horizontal band displacement.
                */
                this.drawCompressionDeltaPolygon(true);
                this.drawCompressionDeltaPolygon(false);

                /*
                    Processed detector contour.
                */
                ctx.beginPath();

                var processedFirst = true;

                for (var px = 0;
                     px <= this.width;
                     px += 2) {
                    var processedFreq =
                        this.xToFreq(px);

                    var baseDetector =
                        this.getArrayValueAtFrequency(
                            processedFreq,
                            this.detectorData,
                            -120.0
                        );

                    var gainDelta =
                        this.getInterpolatedDelta(
                            processedFreq
                        );

                    if (!Number.isFinite(gainDelta))
                        gainDelta = 0.0;

                    var processedDb =
                        baseDetector + gainDelta;

                    var py =
                        this.gainToY(processedDb);

                    if (processedFirst) {
                        ctx.moveTo(px, py);
                        processedFirst = false;
                    } else {
                        ctx.lineTo(px, py);
                    }
                }

                ctx.strokeStyle =
                    'rgba(255, 105, 90, 0.90)';

                ctx.lineWidth = 1.4;
                ctx.stroke();
            }

            /*
                3. Draw the exact effective DSP target.
            */
            ctx.beginPath();

            var targetFirst = true;

            for (var tx = 0;
                 tx <= this.width;
                 tx += 2) {
                var targetFreq =
                    this.xToFreq(tx);

                /*
                    The main yellow target curve is UI geometry,
                    not a resampled DSP telemetry buffer.

                    This guarantees that nodes and the line always
                    occupy exactly the same coordinate system.
                */
                var displayTargetDb =
                    this.getUiTargetDbAtFreq(
                        targetFreq
                    );

                var ty =
                    this.gainToY(
                        displayTargetDb
                    );

                if (targetFirst) {
                    ctx.moveTo(tx, ty);
                    targetFirst = false;
                } else {
                    ctx.lineTo(tx, ty);
                }
            }

            ctx.shadowBlur = 10;
            ctx.shadowColor =
                this.colors.phosphor;

            ctx.strokeStyle =
                this.colors.phosphor;

            ctx.lineWidth = 2.2;
            ctx.stroke();
            ctx.shadowBlur = 0;

            /*
                DSP TARGET — prominent technical headline.

                Use the same UI target geometry as the yellow
                target curve and EQ/gradient points.
            */
            var labelTargetDb =
                this.getUiTargetDbAtFreq(
                    1000.0
                );

            var labelText =
                useNativeTarget
                    ? 'DSP TARGET'
                    : 'TARGET';

            var labelY =
                this.gainToY(
                    labelTargetDb
                )
                - 12;

            ctx.save();

            ctx.font =
                '16px "GOST Type B", "Russo One", "Courier New", monospace';

            ctx.textAlign =
                'left';

            ctx.textBaseline =
                'middle';

            ctx.lineWidth =
                2.0;

            ctx.strokeStyle =
                'rgba(8, 7, 5, 0.94)';

            ctx.strokeText(
                labelText,
                14,
                labelY
            );

            ctx.fillStyle =
                '#f6cd67';

            ctx.shadowColor =
                'rgba(255, 185, 55, 0.78)';

            ctx.shadowBlur =
                7;

            ctx.fillText(
                labelText,
                14,
                labelY
            );

            ctx.restore();

            /*
                Average applied gain.
            */
            var avgGain = 0.0;
            var validBins = 0;

            for (var abi = 10;
                 abi < Math.min(
                     200,
                     this.deltaData.length
                 );
                 ++abi) {
                var deltaDb =
                    Number(
                        this.deltaData[abi]
                    );

                if (Number.isFinite(deltaDb) &&
                    Math.abs(deltaDb) < 48.0) {
                    avgGain += deltaDb;
                    ++validBins;
                }
            }

            if (validBins > 0) {
                avgGain /= validBins;

                ctx.fillStyle =
                    'rgba(255, 255, 255, 0.90)';

                ctx.font =
                    'bold 10px "GOST Type B", monospace';

                ctx.textAlign = 'left';

                ctx.fillText(
                    'GAIN: '
                        + avgGain.toFixed(1)
                        + ' dB',
                    12,
                    18
                );
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

                var pos =
                    this.getEqNodeScreenPosition(i);

                if (!pos) continue;

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

                /*
                    Larger, high-contrast EQ band marker.

                    The number is deliberately readable at the
                    fixed 990 x 594 editor scale.
                */
                var nodeRadius =
                    isHovered ? 11.0 : 9.0;

                ctx.fillStyle =
                    this.colors.phosphor;

                ctx.shadowBlur =
                    isHovered ? 16 : 9;

                ctx.shadowColor =
                    this.colors.phosphor;

                ctx.beginPath();
                ctx.arc(
                    pos.x,
                    pos.y,
                    nodeRadius,
                    0,
                    Math.PI * 2
                );
                ctx.fill();

                ctx.shadowBlur = 0;

                /*
                    Dark inner disc makes the enlarged number
                    readable over the target curve and FFT fog.
                */
                ctx.fillStyle =
                    'rgba(10, 9, 8, 0.94)';

                ctx.beginPath();
                ctx.arc(
                    pos.x,
                    pos.y,
                    nodeRadius - 2.0,
                    0,
                    Math.PI * 2
                );
                ctx.fill();

                ctx.strokeStyle =
                    isHovered
                        ? '#fff0c4'
                        : 'rgba(255, 231, 174, 0.88)';

                ctx.lineWidth =
                    isHovered ? 1.4 : 1.0;

                ctx.beginPath();
                ctx.arc(
                    pos.x,
                    pos.y,
                    nodeRadius,
                    0,
                    Math.PI * 2
                );
                ctx.stroke();

                /*
                    Large band number.
                */
                ctx.fillStyle =
                    '#fff4d5';

                ctx.font =
                    'bold '
                    + (isHovered ? '13px' : '12px')
                    + ' "GOST Type B", "Courier New", monospace';

                ctx.textAlign =
                    'center';

                ctx.textBaseline =
                    'middle';

                ctx.fillText(
                    (i + 1).toString(),
                    pos.x,
                    pos.y + 0.5
                );
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
            this.ctx.clearRect(
                0,
                0,
                this.width,
                this.height
            );

            this.drawBackground();
            this.drawGrid();
            this.drawGradientFills();

            if (this.displayMode === 'detector') {
                this.drawDetectorFog();
            } else {
                this.drawSpectrumFog();
            }

            this.drawCompressionDeltasAndTarget();
            this.drawEQNodes();
            this.drawGradientMarkers();

            /*
                In detector mode the exact per-bin threshold
                is already drawn. Drawing a separate global
                threshold line would be misleading.
            */
            if (this.displayMode !== 'detector')
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
        // Local Δ Audition Focus
        // =================================================================

        notifyAuditionFocus(
            frequencyHz,
            widthOctaves,
            sourceType,
            sourceId
        ) {
            if (typeof this.onAuditionFocusChanged !== 'function')
                return;

            var freq = Number(frequencyHz);
            var width = Number(widthOctaves);

            if (!Number.isFinite(freq) ||
                !Number.isFinite(width) ||
                freq <= 0.0 ||
                width <= 0.0) {
                this.clearAuditionFocus();
                return;
            }

            freq = Math.max(
                20.0,
                Math.min(20000.0, freq)
            );

            width = Math.max(
                0.10,
                Math.min(4.0, width)
            );

            var type =
                String(sourceType || 'none');

            var id =
                Number.isFinite(Number(sourceId))
                    ? Number(sourceId)
                    : -1;

            /*
                Avoid flooding JS -> C++ calls while the mouse is
                stationary or the visual renderer is repainting.
            */
            var frequencyChanged =
                Math.abs(
                    freq - this.lastAuditionFrequencyHz
                ) > 0.5;

            var widthChanged =
                Math.abs(
                    width - this.lastAuditionWidthOctaves
                ) > 0.01;

            var sourceChanged =
                type !== this.lastAuditionSource
                || id !== this.lastAuditionSourceId;

            if (!frequencyChanged &&
                !widthChanged &&
                !sourceChanged) {
                return;
            }

            this.lastAuditionFrequencyHz =
                freq;

            this.lastAuditionWidthOctaves =
                width;

            this.lastAuditionSource =
                type;

            this.lastAuditionSourceId =
                id;

            this.onAuditionFocusChanged({
                active: true,
                frequencyHz: freq,
                widthOctaves: width,
                sourceType: type,
                sourceId: id
            });
        }

        clearAuditionFocus() {
            if (this.lastAuditionSource === 'none')
                return;

            this.lastAuditionFrequencyHz = -1.0;
            this.lastAuditionWidthOctaves = -1.0;
            this.lastAuditionSource = 'none';
            this.lastAuditionSourceId = -1;

            if (typeof this.onAuditionFocusChanged === 'function') {
                this.onAuditionFocusChanged({
                    active: false,
                    frequencyHz: 1000.0,
                    widthOctaves: 1.0,
                    sourceType: 'none',
                    sourceId: -1
                });
            }
        }

        getEQAuditionWidthOctaves(q) {
            /*
                Approximation intended for listening ergonomics,
                not for drawing the exact biquad bandwidth.

                Higher Q = narrower audition area.
            */
            var safeQ = Math.max(
                0.1,
                Math.min(
                    10.0,
                    Number(q) || 1.0
                )
            );

            return Math.max(
                0.15,
                Math.min(
                    3.0,
                    1.25 / safeQ
                )
            );
        }

        // =================================================================
        // Event handling (port of EQGraphLED mouse handlers)
        // =================================================================

        installContextMenuGuard() {
            const canvas =
                this.canvas;

            if (!canvas)
                return;

            this.boundContextMenuGuard =
                function (event) {
                    const target =
                        event.target;

                    const isCanvas =
                        target === canvas ||
                        (target
                            && typeof target
                                .closest === 'function'
                            && target.closest(
                                '#main-screen'
                            ));

                    if (!isCanvas)
                        return;

                    event.preventDefault();
                    event.stopPropagation();

                    if (typeof event
                        .stopImmediatePropagation
                        === 'function') {
                        event.stopImmediatePropagation();
                    }

                    return false;
                };

            document.addEventListener(
                'contextmenu',
                this.boundContextMenuGuard,
                true
            );

            window.addEventListener(
                'contextmenu',
                this.boundContextMenuGuard,
                true
            );
        }

        canProcessRightClick() {
            const now =
                performance.now();

            if (now - this.lastRightClickTime
                < 150) {
                return false;
            }

            this.lastRightClickTime =
                now;

            return true;
        }

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

            this.canvas.addEventListener(
                'pointerdown',
                function (event) {
                    if (event.button !== 2)
                        return;

                    event.preventDefault();
                    event.stopPropagation();

                    if (typeof event
                        .stopImmediatePropagation
                        === 'function') {
                        event.stopImmediatePropagation();
                    }

                    if (!self.canProcessRightClick())
                        return;

                    if (self.canvas
                        .setPointerCapture &&
                        event.pointerId
                            !== undefined) {
                        try {
                            self.canvas
                                .setPointerCapture(
                                    event.pointerId
                                );
                        } catch (error) {}
                    }

                    const created =
                        addGradientFromEvent(
                            event
                        );

                    console.log(
                        '[SpectrumScreen] Right ' +
                        'pointer:',
                        'x=', event.clientX,
                        'y=', event.clientY,
                        'created=', created
                    );
                },
                {
                    capture: true,
                    passive: false
                }
            );

            this.canvas.addEventListener('mousedown', function (e) {
                if (e.button === 2) {
                    e.preventDefault();
                    e.stopPropagation();
                    return;
                }

                var pos  = self.getEventPosition(e);

                if (!pos.inside)
                    return;

                /*
                    Shift + left click — gradient
                    creation method.
                */
                var wantsGradient =
                    e.button === 0 &&
                    e.shiftKey;

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

                    var nodePos =
                        self.getEqNodeScreenPosition(
                            ei
                        );

                    if (!nodePos)
                        continue;

                    if (Math.hypot(
                            pos.x - nodePos.x,
                            pos.y - nodePos.y) < 23.0) {
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

            // Double click to delete
            self.canvas.addEventListener('dblclick', function (e) {
                var pos  = self.getEventPosition(e);

                if (!pos.inside)
                    return;

                // Delete EQ node
                for (var di = 0; di < 8; ++di) {
                    if (!self.eqBands[di].enabled) continue;
                    var dnPos = self.getEqNodeScreenPosition(di);
                    if (!dnPos) continue;
                    if (Math.hypot(pos.x - dnPos.x, pos.y - dnPos.y) < 20.0) {
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

                        if (!gp2 || !gp2.active)
                            continue;

                        var gpx = self.freqToX(gp2.centerFreqHz);
                        var gpy = self.gainToY(self.getTotalTargetDbAtFreq(gp2.centerFreqHz));
                            if (Math.hypot(pos.x - gpx, pos.y - gpy) < 20.0) {
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
                        if (Math.hypot(pos.x - hpx, pos.y - hpy) < 20.0) {
                            self.hoveredGradientId = hp.id;
                            break;
                        }
                    }
                }

                if (self.hoveredGradientId < 0) {
                    for (var hn = 0; hn < 8; ++hn) {
                        if (!self.eqBands[hn].enabled) continue;
                        var hPos = self.getEqNodeScreenPosition(hn);
                        if (!hPos) continue;
                        if (Math.hypot(pos.x - hPos.x, pos.y - hPos.y) < 20.0) {
                            self.hoveredNode = hn;
                            break;
                        }
                    }
                }

                /*
                    Update local Δ audition focus from the exact
                    visual object currently under the pointer.
                */
                if (self.hoveredGradientId >= 0 &&
                    self.gradientManager) {
                    var auditionGradient =
                        self.gradientManager.getPoint(
                            self.hoveredGradientId
                        );

                    if (auditionGradient &&
                        auditionGradient.active) {
                        self.notifyAuditionFocus(
                            auditionGradient.centerFreqHz,
                            auditionGradient.radiusOctaves,
                            'gradient',
                            auditionGradient.id
                        );
                    }
                } else if (self.hoveredNode >= 0) {
                    var auditionBand =
                        self.eqBands[
                            self.hoveredNode
                        ];

                    if (auditionBand &&
                        auditionBand.enabled) {
                        self.notifyAuditionFocus(
                            auditionBand.freq,
                            self.getEQAuditionWidthOctaves(
                                auditionBand.q
                            ),
                            'eq',
                            auditionBand.id
                        );
                    }
                } else if (!isDragging) {
                    self.clearAuditionFocus();
                }

                // Drag gradient point
                if (self.draggingGradientId >= 0 && self.gradientManager) {
                    var dgp = self.gradientManager.getPoint(self.draggingGradientId);
                    if (dgp) {
                        var newFreq =
                            Math.min(
                                20000.0,
                                Math.max(
                                    20.0,
                                    self.xToFreq(pos.x)
                                )
                            );

                        /*
                            yToGain() returns the absolute target
                            dB coordinate the gradient point should
                            occupy. Solve for the local gain:
                                gradientGain
                                =
                                desiredAbsoluteTargetDb
                                -
                                allOtherContributions(freq)
                        */
                        var desiredAbsoluteTargetDb =
                            self.yToGain(pos.y);

                        var targetWithoutThisGradient =
                            self.getUiTargetDbWithoutGradient(
                                newFreq,
                                dgp.id
                            );

                        var newGain =
                            desiredAbsoluteTargetDb
                            - targetWithoutThisGradient;

                        newGain =
                            Math.max(
                                -60.0,
                                Math.min(
                                    60.0,
                                    newGain
                                )
                            );

                        var freqChanged =
                            Math.abs(
                                dgp.centerFreqHz
                                - newFreq
                            ) > 0.01;

                        var gainChanged =
                            Math.abs(
                                dgp.centerGainDb
                                - newGain
                            ) > 0.001;

                        dgp.centerFreqHz =
                            newFreq;

                        dgp.centerGainDb =
                            newGain;

                        self.targetCurveDirty = true;

                        self.notifyAuditionFocus(
                            newFreq,
                            dgp.radiusOctaves,
                            'gradient',
                            dgp.id
                        );

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

                    var newFreq =
                        Math.min(
                            20000.0,
                            Math.max(
                                20.0,
                                self.xToFreq(pos.x)
                            )
                        );

                    /*
                        yToGain() returns the absolute target
                        dB coordinate the node should occupy.
                    */
                    var desiredAbsoluteTargetDb =
                        self.yToGain(pos.y);

                    /*
                        Target curve underneath this EQ node
                        with the node itself excluded.

                        Therefore:
                            band gain
                            =
                            desiredAbsoluteTargetDb
                            -
                            allOtherContributions(freq)
                    */
                    var targetWithoutThisBand =
                        self.getUiTargetDbWithoutEqBand(
                            newFreq,
                            self.draggingNode
                        );

                    var newGain =
                        desiredAbsoluteTargetDb
                        - targetWithoutThisBand;

                    newGain =
                        Math.max(
                            -60.0,
                            Math.min(
                                60.0,
                                newGain
                            )
                        );

                    var freqChanged =
                        Math.abs(
                            db.freq - newFreq
                        ) > 0.01;

                    var gainChanged =
                        Math.abs(
                            db.gain - newGain
                        ) > 0.001;

                    db.freq =
                        newFreq;

                    db.gain =
                        newGain;

                    self.targetCurveDirty = true;

                    self.notifyAuditionFocus(
                        newFreq,
                        self.getEQAuditionWidthOctaves(
                            db.q
                        ),
                        'eq',
                        db.id
                    );

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

                /*
                    Ctrl + Wheel used to cycle VIEW_RANGE.

                    The renderer now has one calibrated display:
                    +12 dB ... -96 dB (108 dB total).

                    Consume the gesture so browser zoom is
                    prevented, but intentionally do not change
                    display scale or write VIEW_RANGE.
                */
                if (e.ctrlKey) {
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

        getFreeGradientSlot() {
            if (!this.gradientManager ||
                !Array.isArray(
                    this.gradientManager.points
                )) {
                return -1;
            }

            for (let i = 0;
                 i < this.gradientManager.points.length;
                 ++i) {
                const point =
                    this.gradientManager.points[i];

                if (!point || !point.active)
                    return i;
            }

            return -1;
        }

        createGradientAt(freq, gainDb) {
            if (!this.gradientManager)
                return -1;

            /*
                Count only active points.
                Inactive points may still occupy
                array slots — they should be reused.
            */
            const activeCount =
                this.gradientManager.points.filter(
                    function (point) {
                        return point && point.active;
                    }
                ).length;

            if (activeCount >= 4) {
                console.warn(
                    '[SpectrumScreen] ' +
                    'All gradient slots occupied'
                );

                return -1;
            }

            freq = Math.max(
                20.0,
                Math.min(
                    20000.0,
                    Number(freq) || 1000.0
                )
            );

            gainDb = Math.max(
                -60.0,
                Math.min(
                    60.0,
                    Number(gainDb) || 0.0
                )
            );

            let pointId = -1;

            try {
                pointId =
                    this.gradientManager.addPoint(
                        freq,
                        gainDb
                    );
            } catch (error) {
                console.error(
                    '[SpectrumScreen] ' +
                    'addPoint failed:',
                    error
                );
            }

            /*
                If addPoint returned -1 but a free
                inactive slot exists, activate it
                directly.
            */
            if (!Number.isFinite(Number(pointId)) ||
                Number(pointId) < 0) {
                pointId =
                    this.getFreeGradientSlot();
            }

            if (pointId < 0) {
                console.warn(
                    '[SpectrumScreen] ' +
                    'No free gradient slot'
                );

                return -1;
            }

            pointId = Number(pointId);

            let point =
                this.gradientManager.getPoint(
                    pointId
                );

            /*
                Fallback: use array index directly
                if getPoint failed.
            */
            if (!point &&
                this.gradientManager.points
                    [pointId]) {
                point =
                    this.gradientManager.points[
                        pointId
                    ];
            }

            if (!point) {
                console.error(
                    '[SpectrumScreen] ' +
                    'Gradient point missing:',
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
                point.radiusOctaves = 1.5;
            }

            this.gradientManager
                .setActivePoint(pointId);

            this.targetCurveDirty = true;

            const prefix =
                'GRADIENT_' + pointId;

            if (JuceBridge &&
                JuceBridge.isJuceAvailable()) {
                const freqNorm =
                    Math.max(
                        0.0,
                        Math.min(
                            1.0,
                            Math.log10(
                                freq / 20.0
                            ) / 3.0
                        )
                    );

                const gainNorm =
                    Math.max(
                        0.0,
                        Math.min(
                            1.0,
                            (gainDb + 60.0)
                            / 120.0
                        )
                    );

                const bandwidthNorm =
                    Math.max(
                        0.0,
                        Math.min(
                            1.0,
                            (point.radiusOctaves
                                - 0.5) / 3.5
                        )
                    );

                console.log(
                    '[SpectrumScreen] Creating ' +
                    'gradient:',
                    pointId,
                    'freq=', freq,
                    'freqNorm=', freqNorm,
                    'gain=', gainDb,
                    'gainNorm=', gainNorm
                );

                /*
                    Enable the slot first.
                */
                JuceBridge.setParameter(
                    prefix + '_ENABLE',
                    1.0
                );

                JuceBridge.setParameter(
                    prefix + '_CENTER_FREQ',
                    freqNorm
                );

                JuceBridge.setParameter(
                    prefix + '_CENTER_GAIN',
                    gainNorm
                );

                JuceBridge.setParameter(
                    prefix + '_BANDWIDTH',
                    bandwidthNorm
                );
            }

            if (this.onGradientSelectionChanged)
                this.onGradientSelectionChanged();

            if (this.onGradientParamsChanged)
                this.onGradientParamsChanged();

            this.render();

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
