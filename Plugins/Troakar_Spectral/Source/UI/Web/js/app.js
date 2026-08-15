// TROAKAR SPECTRAL - Master Orchestrator
// Ported from PluginEditor.cpp — wires GradientManager, GradientFilterOverlay,
// SpectrumScreen, ChickenKnob, BacklitButton, and IN/OUT LINK logic.
(function () {
    var root      = typeof window !== 'undefined' ? window : this;
    var JuceBridge = root.JuceBridge;
    var ChickenKnob = root.ChickenKnob;
    var BacklitButton = root.BacklitButton;
    var SpectrumScreen = root.SpectrumScreen;
    var GradientManager = root.GradientManager;
    var GradientFilterOverlay = root.GradientFilterOverlay;

    window.addEventListener('error', function (event) {
        console.error(
            '[Troakar UI Error]',
            event.message,
            event.filename,
            event.lineno,
            event.colno
        );

        var existing = document.getElementById('troakar-js-error');

        if (existing)
            existing.remove();

        var box = document.createElement('pre');

        box.id = 'troakar-js-error';

        box.textContent =
            'JAVASCRIPT ERROR\n\n'
            + event.message + '\n\n'
            + (event.filename || '') + ':'
            + (event.lineno || 0) + ':'
            + (event.colno || 0);

        box.style.cssText = [
            'position:fixed',
            'z-index:2147483647',
            'left:12px',
            'bottom:12px',
            'max-width:calc(100vw - 24px)',
            'margin:0',
            'padding:12px',
            'background:#3b0707',
            'border:2px solid #ff4b4b',
            'color:#fff',
            'font:12px/1.4 monospace',
            'white-space:pre-wrap',
            'pointer-events:none'
        ].join(';');

        document.body.appendChild(box);
    });

    window.addEventListener(
        'unhandledrejection',
        function (event) {
            console.error(
                '[Troakar Promise Rejection]',
                event.reason
            );

            throw event.reason;
        }
    );

    function startApp() {
        TroakarConsole.info(
            'app.js started'
        );

        TroakarConsole.info(
            'JuceBridge type:',
            typeof JuceBridge
        );

        TroakarConsole.info(
            'JuceBridge.isJuceAvailable:',
            JuceBridge ? JuceBridge.isJuceAvailable() : 'N/A'
        );

        TroakarConsole.info(
            '__JUCE__:',
            window.__JUCE__
                ? 'found, backend keys: '
                  + Object.keys(window.__JUCE__.backend || {}).join(', ')
                : 'not available'
        );

        console.log('Troakar Spectral - Full Engine Initialized');

        // ================================================================
        // 1. Core model + overlay + screen
        // ================================================================
        var gradientManager   = new GradientManager();
        var gradientOverlay   = new GradientFilterOverlay('gradient-overlay', gradientManager);
        var mainScreen        = new SpectrumScreen('main-screen', gradientManager);

        var knobs  = new Map();
        var buttons = new Map();

        function createKnob(key, elementId, parameterId, options) {
            TroakarConsole.debug(
                'Creating knob:',
                key,
                elementId,
                parameterId
            );

            try {
                var knob = new ChickenKnob(
                    elementId,
                    parameterId,
                    options
                );

                knobs.set(key, knob);

                console.log(
                    '[Troakar] Knob created:',
                    key,
                    parameterId
                );

                TroakarConsole.info(
                    'Knob created:',
                    key,
                    parameterId
                );

                return knob;
            }
            catch (error) {
                console.error(
                    '[Troakar] Failed to create knob:',
                    key,
                    parameterId,
                    error
                );

                TroakarConsole.error(
                    'Knob creation failed:',
                    key,
                    parameterId,
                    error
                );

                throw error;
            }
        }

        // ================================================================
        // 2. Knobs (with allowInGradientMode flag from C++)
        // ================================================================
        // I/O knobs — NOT gradient-enabled
        createKnob(
            'in-gain',
            'knob-in',
            'IN_GAIN',
            {
                defaultValue: 0.5,
                type: 'medium',
                allowInGradientMode: false
            }
        );

        createKnob(
            'out-lvl',
            'knob-out',
            'OUT_LVL',
            {
                defaultValue: 0.5,
                type: 'medium',
                allowInGradientMode: false
            }
        );

        createKnob(
            'mix',
            'knob-mix',
            'MIX',
            {
                defaultValue: 1.0,
                type: 'medium',
                allowInGradientMode: false
            }
        );

        // Dynamics knobs — gradient-enabled
        createKnob(
            'amount',
            'knob-amount',
            'AMOUNT',
            {
                defaultValue: 0.65,
                type: 'big',
                allowInGradientMode: true
            }
        );

        createKnob(
            'up-range',
            'knob-up-range',
            'UPWARD_RANGE',
            {
                defaultValue: 0.4,
                type: 'big',
                allowInGradientMode: true
            }
        );

        createKnob(
            'up-sel',
            'knob-up-sel',
            'UP_SEL',
            {
                defaultValue: 0.5,
                type: 'medium',
                allowInGradientMode: true
            }
        );

        createKnob(
            'up-smooth',
            'knob-up-smooth',
            'UP_SMOOTH',
            {
                defaultValue: 0.5,
                type: 'medium',
                allowInGradientMode: true
            }
        );

        createKnob(
            'down-range',
            'knob-down-range',
            'DOWNWARD_RANGE',
            {
                defaultValue: 0.5,
                type: 'big',
                allowInGradientMode: true
            }
        );

        createKnob(
            'down-sel',
            'knob-down-sel',
            'DOWN_SEL',
            {
                defaultValue: 0.5,
                type: 'medium',
                allowInGradientMode: true
            }
        );

        createKnob(
            'down-smooth',
            'knob-down-smooth',
            'DOWN_SMOOTH',
            {
                defaultValue: 0.15,
                type: 'medium',
                allowInGradientMode: true
            }
        );

        // Timing knobs — gradient-enabled
        createKnob(
            'speed',
            'knob-speed',
            'SPECTRAL_SPEED',
            {
                defaultValue: 0.5,
                type: 'big',
                allowInGradientMode: true
            }
        );

        createKnob(
            'attack',
            'knob-attack',
            'ATTACK_MS',
            {
                defaultValue: 0.292,
                min: 0.1,
                max: 200,
                unit: 'ms',
                type: 'medium',
                allowInGradientMode: true
            }
        );

        createKnob(
            'release',
            'knob-release',
            'RELEASE_MS',
            {
                defaultValue: 0.35,
                min: 10,
                max: 500,
                unit: 'ms',
                type: 'medium',
                allowInGradientMode: true
            }
        );

        createKnob(
            'knee',
            'knob-knee',
            'KNEE_WIDTH',
            {
                defaultValue: 1.0,
                min: 0,
                max: 12,
                unit: 'dB',
                type: 'medium',
                allowInGradientMode: true
            }
        );

        // ================================================================
        // 3. Buttons
        // ================================================================
        buttons.set('delta',  new BacklitButton('btn-delta',    'DELTA_MODE', false, 'red'));
        buttons.set('link',   new BacklitButton('btn-link',     'IO_LINK',    false, 'cyan'));
        buttons.set('speed-auto', new BacklitButton('btn-speed-auto', 'SPEED_AUTO', true, 'amber'));

        // ================================================================
        // 4. Dynamic knob binding to active gradient (port of
        //    PluginEditor.cpp::updateKnobStates())
        // ================================================================
        function updateKnobBindings() {
            var activePt  = gradientManager.getActivePoint();
            var isGlobal  = !gradientManager.hasActivePoint();
            var capColor  = activePt ? activePt.color : null;
            var isAuto    = buttons.get('speed-auto').getState();

            // I/O knobs — never gradient
            knobs.get('in-gain').setGradientActive(false, capColor);
            knobs.get('out-lvl').setGradientActive(false, capColor);
            knobs.get('mix').setGradientActive(false, capColor);

            // Dynamics knobs
            knobs.get('amount').setGradientActive(!isGlobal, capColor);
            knobs.get('up-range').setGradientActive(!isGlobal, capColor);
            knobs.get('down-range').setGradientActive(!isGlobal, capColor);
            knobs.get('up-smooth').setGradientActive(!isGlobal, capColor);
            knobs.get('down-smooth').setGradientActive(!isGlobal, capColor);
            knobs.get('up-sel').setGradientActive(!isGlobal, capColor);
            knobs.get('down-sel').setGradientActive(!isGlobal, capColor);

            // Timing knobs
            if (isAuto) {
                knobs.get('speed').setGradientActive(!isGlobal, capColor);
                knobs.get('attack').setGradientActive(false, capColor);
                knobs.get('release').setGradientActive(false, capColor);
                knobs.get('knee').setGradientActive(false, capColor);
            } else {
                knobs.get('speed').setGradientActive(false, capColor);
                knobs.get('attack').setGradientActive(!isGlobal, capColor);
                knobs.get('release').setGradientActive(!isGlobal, capColor);
                knobs.get('knee').setGradientActive(!isGlobal, capColor);
            }

            // Re-bind parameters
            if (!isGlobal && activePt) {
                var prefix = 'GRADIENT_' + activePt.id;
                knobs.get('amount').bindToParameter(prefix + '_AMOUNT');
                knobs.get('up-range').bindToParameter(prefix + '_UP_MAX');
                knobs.get('down-range').bindToParameter(prefix + '_DOWN_MAX');
                knobs.get('up-smooth').bindToParameter(prefix + '_UP_SMOOTH');
                knobs.get('down-smooth').bindToParameter(prefix + '_DOWN_SMOOTH');
                knobs.get('up-sel').bindToParameter(prefix + '_UP_SEL');
                knobs.get('down-sel').bindToParameter(prefix + '_DOWN_SEL');

                if (isAuto) {
                    knobs.get('speed').bindToParameter(prefix + '_SPEED');
                } else {
                    knobs.get('attack').bindToParameter(prefix + '_ATTACK');
                    knobs.get('release').bindToParameter(prefix + '_RELEASE');
                    knobs.get('knee').bindToParameter(prefix + '_KNEE');
                }
            } else {
                // Global mode
                knobs.get('amount').bindToParameter('AMOUNT');
                knobs.get('up-range').bindToParameter('UPWARD_RANGE');
                knobs.get('down-range').bindToParameter('DOWNWARD_RANGE');
                knobs.get('up-smooth').bindToParameter('UP_SMOOTH');
                knobs.get('down-smooth').bindToParameter('DOWN_SMOOTH');
                knobs.get('up-sel').bindToParameter('UP_SEL');
                knobs.get('down-sel').bindToParameter('DOWN_SEL');

                if (isAuto) {
                    knobs.get('speed').bindToParameter('SPECTRAL_SPEED');
                } else {
                    knobs.get('attack').bindToParameter('ATTACK_MS');
                    knobs.get('release').bindToParameter('RELEASE_MS');
                    knobs.get('knee').bindToParameter('KNEE_WIDTH');
                }

                // Set gradient markers on all knobs (port of C++ setGradientMarkers)
                if (gradientManager.points.length > 0) {
                    setGradientMarkersOnKnobs();
                }
            }
        }

        function setGradientMarkersOnKnobs() {
            var markers = {
                amount:    [], upRange: [], downRange: [],
                upSmooth:  [], downSmooth: [], upSel: [], downSel: [],
                speed:     [], attack: [], release: [], knee: []
            };

            gradientManager.points.forEach(function (p) {
                markers.amount.push({ id: p.id, normalizedValue: p.amountPct / 300.0, color: p.color });
                markers.upRange.push({ id: p.id, normalizedValue: p.upMaxDb / 48.0, color: p.color });
                markers.downRange.push({ id: p.id, normalizedValue: (-p.downMaxDb) / 24.0, color: p.color });
                markers.upSmooth.push({ id: p.id, normalizedValue: p.upSmoothPct / 100.0, color: p.color });
                markers.downSmooth.push({ id: p.id, normalizedValue: p.downSmoothPct / 100.0, color: p.color });
                markers.upSel.push({ id: p.id, normalizedValue: (p.upSelectivity + 100) / 200, color: p.color });
                markers.downSel.push({ id: p.id, normalizedValue: (p.downSelectivity + 100) / 200, color: p.color });

                if (p.useAutoSpeed) {
                    markers.speed.push({ id: p.id, normalizedValue: p.speedPct / 100.0, color: p.color });
                } else {
                    markers.attack.push({ id: p.id, normalizedValue: (p.attackMs - 0.1) / 199.9, color: p.color });
                    markers.release.push({ id: p.id, normalizedValue: (p.releaseMs - 10) / 490, color: p.color });
                    markers.knee.push({ id: p.id, normalizedValue: p.kneeWidthDb / 12.0, color: p.color });
                }
            });

            knobs.get('amount').setGradientMarkers(markers.amount);
            knobs.get('up-range').setGradientMarkers(markers.upRange);
            knobs.get('down-range').setGradientMarkers(markers.downRange);
            knobs.get('up-smooth').setGradientMarkers(markers.upSmooth);
            knobs.get('down-smooth').setGradientMarkers(markers.downSmooth);
            knobs.get('up-sel').setGradientMarkers(markers.upSel);
            knobs.get('down-sel').setGradientMarkers(markers.downSel);
            knobs.get('speed').setGradientMarkers(markers.speed);
            knobs.get('attack').setGradientMarkers(markers.attack);
            knobs.get('release').setGradientMarkers(markers.release);
            knobs.get('knee').setGradientMarkers(markers.knee);
        }

        // Connect overlay callbacks
        gradientOverlay.onSelectionChanged = function () {
            updateKnobBindings();
            mainScreen.render();
        };
        gradientOverlay.onPointDeleted = function (pointId) {
            if (JuceBridge && JuceBridge.isJuceAvailable()) {
                JuceBridge.setParameter('GRADIENT_' + pointId + '_ENABLE', 0.0);
            } else {
                console.log('[Dev-Mock] GRADIENT_' + pointId + '_ENABLE -> 0');
            }
            updateKnobBindings();
            mainScreen.render();
        };

        /*
            Batch parameter-driven updates to the spectrum screen
            via requestAnimationFrame. During a host automation
            drag or parameter change flood, this ensures we fetch
            and render at most once per frame instead of dozens of
            times per message loop.
        */
        // ================================================================
        // 4b. JUCE parameter listeners — keep JS in sync with external
        //     parameter changes (e.g. from host automation)
        // ================================================================
        function setupParamListeners() {
            if (!JuceBridge || !JuceBridge.isJuceAvailable()) return;

            // Gradient parameter changes → re-sync gradient manager
            for (var g = 0; g < 4; ++g) {
                var prefix = 'GRADIENT_' + g;
                JuceBridge.onParamUpdate(prefix + '_ENABLE', function () {
                    gradientManager.syncFromJuce(JuceBridge);
                    gradientOverlay.render();
                    updateKnobBindings();
                    mainScreen.targetCurveDirty = true;
                });
                JuceBridge.onParamUpdate(prefix + '_CENTER_FREQ',   function () {
                    if (gradientManager.points.length > 0) {
                        gradientManager.syncFromJuce(JuceBridge);
                        if (gradientManager.points.some(function (p) { return p.isSelected; })) {
                            gradientManager.syncPointToJuce(JuceBridge,
                                gradientManager.getActivePoint().id);
                        }
                        mainScreen.targetCurveDirty = true;
                    }
                });
                JuceBridge.onParamUpdate(prefix + '_CENTER_GAIN',  function () {
                    gradientManager.syncFromJuce(JuceBridge);
                    mainScreen.targetCurveDirty = true;
                });
                JuceBridge.onParamUpdate(prefix + '_BANDWIDTH',    function () {
                    gradientManager.syncFromJuce(JuceBridge);
                    mainScreen.targetCurveDirty = true;
                });
            }

            // Speed auto button changes
            JuceBridge.onParamUpdate('SPEED_AUTO', function (val) {
                buttons.get('speed-auto').setState(val >= 0.5);
                var timingSec  = document.querySelector('.timing-section');
                if (timingSec) {
                    var autoView   = timingSec.querySelector('.timing-view-auto');
                    var manualView = timingSec.querySelector('.timing-view-manual');
                    if (autoView && manualView) {
                        autoView.classList.toggle('active', val >= 0.5);
                        manualView.classList.toggle('active', val < 0.5);
                    }
                }
                updateKnobBindings();
            });

            // EQ band parameter changes — direct sync, skip during drag
            for (let bi = 0; bi < 8; ++bi) {
                let prefix = 'BAND_' + bi;

                JuceBridge.onParamUpdate(
                    prefix + '_ENABLE',
                    function (value) {
                        if (mainScreen.draggingNode === bi)
                            return;

                        mainScreen.eqBands[bi].enabled =
                            Number(value) >= 0.5;

                        mainScreen.targetCurveDirty = true;
                    }
                );

                JuceBridge.onParamUpdate(
                    prefix + '_FREQ',
                    function (value) {
                        if (mainScreen.draggingNode === bi)
                            return;

                        var normalized =
                            Math.max(
                                0.0,
                                Math.min(
                                    1.0,
                                    Number(value) || 0.0
                                )
                            );

                        mainScreen.eqBands[bi].freq =
                            20.0 * Math.pow(
                                1000.0,
                                normalized
                            );

                        mainScreen.targetCurveDirty = true;
                    }
                );

                JuceBridge.onParamUpdate(
                    prefix + '_GAIN',
                    function (value) {
                        if (mainScreen.draggingNode === bi)
                            return;

                        var normalized =
                            Math.max(
                                0.0,
                                Math.min(
                                    1.0,
                                    Number(value) || 0.0
                                )
                            );

                        mainScreen.eqBands[bi].gain =
                            normalized * 120.0 - 60.0;

                        mainScreen.targetCurveDirty = true;
                    }
                );

                JuceBridge.onParamUpdate(
                    prefix + '_Q',
                    function (value) {
                        if (mainScreen.draggingNode === bi)
                            return;

                        var normalized =
                            Math.max(
                                0.0,
                                Math.min(
                                    1.0,
                                    Number(value) || 0.0
                                )
                            );

                        mainScreen.eqBands[bi].q =
                            Math.pow(
                                10.0,
                                normalized * 2.0 - 1.0
                            );

                        mainScreen.targetCurveDirty = true;
                    }
                );
            }

            // Global threshold changes
            JuceBridge.onParamUpdate('GLOBAL_THRESH', function (val) {
                if (mainScreen.isDraggingThresh)
                    return;

                mainScreen.globalThresh = Number(val) * 60.0 - 48.0;
                mainScreen.targetCurveDirty = true;
            });

            // View range changes
            JuceBridge.onParamUpdate('VIEW_RANGE', function (val) {
                var depths = [24, 48, 72, 96, 120];
                var idx = Math.round(val * (depths.length - 1));
                mainScreen.setViewRange(depths[idx]);
                var combo = document.getElementById('view-range-combo');
                if (combo) combo.selectedIndex = idx;
            });
        }
        setupParamListeners();

        // Connect screen callbacks
        mainScreen.onGradientSelectionChanged = function () {
            gradientOverlay.render();
            updateKnobBindings();
        };
        mainScreen.onGradientParamsChanged = function () {
            gradientOverlay.render();
            updateKnobBindings();
            if (JuceBridge && JuceBridge.isJuceAvailable() && gradientManager.points.length > 0) {
                gradientManager.points.forEach(function (p) {
                    if (p.isSelected) {
                        gradientManager.syncPointToJuce(JuceBridge, p.id);
                    }
                });
            }
        };

        // ================================================================
        // 5. IN/OUT LINK logic (port of PluginEditor.cpp)
        // ================================================================
        var isUpdatingLink  = false;
        var lastInGain      = knobs.get('in-gain').value;
        var lastOutLvl      = knobs.get('out-lvl').value;

        knobs.get('in-gain').onValueChange = function (newVal) {
            var isLinked = buttons.get('link').getState();
            if (isLinked && !isUpdatingLink) {
                isUpdatingLink = true;
                var delta    = newVal - lastInGain;
                var newOut   = Math.min(1.0, Math.max(0.0, knobs.get('out-lvl').value - delta));
                knobs.get('out-lvl').setValue(newOut);
                if (JuceBridge) JuceBridge.setParameter('OUT_LVL', newOut);
                lastOutLvl = newOut;
                isUpdatingLink = false;
            }
            lastInGain = newVal;
        };

        knobs.get('out-lvl').onValueChange = function (newVal) {
            var isLinked = buttons.get('link').getState();
            if (isLinked && !isUpdatingLink) {
                isUpdatingLink = true;
                var delta   = newVal - lastOutLvl;
                var newIn   = Math.min(1.0, Math.max(0.0, knobs.get('in-gain').value - delta));
                knobs.get('in-gain').setValue(newIn);
                if (JuceBridge) JuceBridge.setParameter('IN_GAIN', newIn);
                lastInGain = newIn;
                isUpdatingLink = false;
            }
            lastOutLvl = newVal;
        };

        // ================================================================
        // 6. TIMING MODE toggle (AUTO vs MANUAL)
        // ================================================================
        var btnSpeedAuto = document.getElementById('btn-speed-auto');
        if (btnSpeedAuto) {
            btnSpeedAuto.addEventListener('click', function () {
                // BacklitButton already toggled state; just sync views
                var isAuto     = buttons.get('speed-auto').getState();
                var timingSec  = btnSpeedAuto.closest('.timing-section');
                var autoView   = timingSec.querySelector('.timing-view-auto');
                var manualView = timingSec.querySelector('.timing-view-manual');

                btnSpeedAuto.classList.toggle('active', isAuto);
                autoView.classList.toggle('active', isAuto);
                manualView.classList.toggle('active', !isAuto);
            });
        }

        // ================================================================
        // 7. Combo boxes
        // ================================================================
        var fftCombo = document.getElementById('fft-combo');
        if (fftCombo) {
            fftCombo.addEventListener('change', function (e) {
                if (JuceBridge && JuceBridge.isJuceAvailable()) {
                    JuceBridge.setParameter('FFT_MODE', e.target.selectedIndex / 2.0);
                } else {
                    console.log('[Dev-Mock] FFT_MODE -> ' + e.target.selectedIndex);
                }
            });
        }

        var viewRangeCombo = document.getElementById('view-range-combo');
        if (viewRangeCombo) {
            viewRangeCombo.addEventListener('change', function (e) {
                var depths = [24, 48, 72, 96, 120];
                var selectedDepth = depths[e.target.selectedIndex] || 72;
                if (mainScreen) mainScreen.setViewRange(selectedDepth);
                if (JuceBridge && JuceBridge.isJuceAvailable()) {
                    JuceBridge.setParameter('VIEW_RANGE', e.target.selectedIndex / 4.0);
                } else {
                    console.log('[Dev-Mock] VIEW_RANGE -> ' + selectedDepth + 'dB');
                }
            });
        }

        // =====================================================================
        // Fixed artwork at 1300x780, scaled to 900x540 window via CSS.
        // Scale factor: 900/1300 = 540/780 = 0.6923076923
        // No runtime resize — the JUCE editor window is locked.
        // =====================================================================

        // ================================================================
        // 7. Analysis diagnostics
        // ================================================================
        if (JuceBridge &&
            JuceBridge.isJuceAvailable()) {
            JuceBridge.onAnalysisReady(function (data) {
                if (!data || !data.spectrum)
                    return;

                if (mainScreen.analysisPackets % 30 === 0) {
                    var first =
                        Number(data.spectrum[1]) || 0.0;

                    var middle =
                        Number(
                            data.spectrum[
                                Math.floor(
                                    data.spectrum.length
                                        / 2
                                )
                            ]
                        ) || 0.0;

                    console.log(
                        '[Spectrum] packets:',
                        mainScreen.analysisPackets,
                        'bins:',
                        data.spectrum.length,
                        'first:',
                        first,
                        'middle:',
                        middle,
                        'format:',
                        data.spectrumFormat
                    );
                }
            });
        }

        // ================================================================
        // Expose for debugging + initial setup
        // ================================================================
        updateKnobBindings();

        root.troakarApp = {
            knobs:              knobs,
            buttons:            buttons,
            mainScreen:         mainScreen,
            gradientManager:    gradientManager,
            gradientOverlay:    gradientOverlay,
            updateKnobBindings: updateKnobBindings
        };
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', startApp);
    } else {
        startApp();
    }
})();
