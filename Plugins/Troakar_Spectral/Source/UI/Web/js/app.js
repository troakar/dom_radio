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
        // 0. Responsive UI Scaling
        // ================================================================
        function updateUiScale() {
            var artboardWidth = 1300;
            var artboardHeight = 780;
            // Рассчитываем множитель (Math.min гарантирует, что интерфейс всегда впишется)
            var scale = Math.min(window.innerWidth / artboardWidth, window.innerHeight / artboardHeight);
            document.documentElement.style.setProperty('--ui-scale', scale);
        }
        window.addEventListener('resize', updateUiScale);
        updateUiScale(); // первичная инициализация масштаба

        // ================================================================
        // 1. Core model + overlay + screen
        // ================================================================
        var gradientManager   = new GradientManager();
        var gradientOverlay   = new GradientFilterOverlay('gradient-overlay', gradientManager);
        var mainScreen        = new SpectrumScreen('main-screen', gradientManager);

        var knobs  = new Map();
        var buttons = new Map();
        var controlsReady = false;

        // Global flag: true while any gradient knob is being dragged.
        // Prevents echo/feedback loops when JUCE echoes back paramUpdate
        // during an active drag gesture.
        var isDraggingGradient = false;

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
                min: 0,
                max: 300,
                skew: 0.65,
                unit: '%',
                decimals: 0,
                defaultValue: Math.pow(100.0 / 300.0, 0.65),
                type: 'big',
                allowInGradientMode: true
            }
        );

        createKnob(
            'up-range',
            'knob-up-range',
            'UPWARD_RANGE',
            {
                min: 0,
                max: 48,
                skew: 0.70,
                unit: 'dB',
                decimals: 1,
                defaultValue: Math.pow(4.0 / 48.0, 0.70),
                type: 'big',
                allowInGradientMode: true
            }
        );

        createKnob(
            'up-sel',
            'knob-up-sel',
            'UP_SEL',
            {
                /*
                    APVTS range: -100 ... 0 ... +100
                    Normalized 0.5 = real 0.
                */
                defaultValue: 0.5,
                min: -100,
                max: 100,
                unit: '%',
                decimals: 0,
                type: 'medium',
                allowInGradientMode: true
            }
        );

        createKnob(
            'up-smooth',
            'knob-up-smooth',
            'UP_SMOOTH',
            {
                min: 0,
                max: 1000,
                unit: 'ms',
                decimals: 0,
                defaultValue: 0.15,
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
                /*
                    APVTS range: -100 ... 0 ... +100
                    Normalized 0.5 = real 0.
                */
                defaultValue: 0.5,
                min: -100,
                max: 100,
                unit: '%',
                decimals: 0,
                type: 'medium',
                allowInGradientMode: true
            }
        );

        createKnob(
            'down-smooth',
            'knob-down-smooth',
            'DOWN_SMOOTH',
            {
                min: 0,
                max: 1000,
                unit: 'ms',
                decimals: 0,
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

        buttons.get('link').onStateChange = function (isLinked, fromHost) {
            var inKnob = knobs.get('in-gain');
            var outKnob = knobs.get('out-lvl');

            if (inKnob) inKnob.setLinked(isLinked);
            if (outKnob) outKnob.setLinked(isLinked);

            if (inKnob) lastInGain = inKnob.value;
            if (outKnob) lastOutLvl = outKnob.value;

            console.log('[Troakar] IO_LINK changed:', isLinked ? 'ON' : 'OFF', fromHost ? '(host/APVTS)' : '(user)');
        };

        // ================================================================
        // 3.1. Timing mode + SILA label helpers
        // ================================================================
        function applyTimingMode(isAuto) {
            const timingSection =
                document.querySelector('.timing-section');

            if (!timingSection)
                return;

            const automaticView =
                timingSection.querySelector('.timing-view-auto');

            const manualView =
                timingSection.querySelector('.timing-view-manual');

            if (automaticView) {
                automaticView.classList.toggle('active', isAuto);
            }

            if (manualView) {
                manualView.classList.toggle('active', !isAuto);
            }

            timingSection.classList.toggle('is-auto-mode', isAuto);
            timingSection.classList.toggle('is-manual-mode', !isAuto);

            if (controlsReady) {
                updateKnobBindings();
            }

            console.log('[Troakar] Compression timing mode:', isAuto ? 'AUTOMATIC' : 'MANUAL');
        }

        function updateSilaLabel() {
            const silaLabel =
                document.getElementById('sila-label');

            if (!silaLabel)
                return;

            const activeGradient =
                gradientManager.getActivePoint();

            const isActive =
                activeGradient && activeGradient.active;

            const color =
                isActive
                    ? activeGradient.color
                    : '#d4a446';

            silaLabel.style.setProperty('--sila-color', color);
            silaLabel.style.color = color;
            silaLabel.style.textShadow =
                '0 1px 0 rgba(0,0,0,0.95),'
                + '0 -1px 0 rgba(255,255,255,0.12),'
                + '0 0 7px ' + color + ','
                + '0 0 15px ' + color + '99';

            silaLabel.classList.toggle('is-gradient-active', !!isActive);
        }

        buttons.get('speed-auto').onStateChange =
            function (isAuto, fromHost) {
                applyTimingMode(isAuto);

                console.log('[Troakar] SPEED_AUTO changed:', isAuto ? 1 : 0, fromHost ? '(host/APVTS)' : '(user)');
            };

        /*
            Δ audition is enabled by the existing DELTA_MODE
            APVTS parameter. Frequency and width are sent by
            SpectrumScreen when the user hovers/drags an EQ or
            gradient point.
        */
        buttons.get('delta').onStateChange =
            function (isEnabled, fromHost) {
                if (!JuceBridge ||
                    !JuceBridge.isJuceAvailable()) {
                    return;
                }

                /*
                    When Δ is turned off, explicitly disable
                    DSP local audition even if an old focus
                    frequency remains in the parameter state.
                */
                if (!isEnabled) {
                    JuceBridge.setParameter(
                        'AUDITION_ENABLE',
                        0.0
                    );
                    return;
                }

                /*
                    If a point is already hovered, moving the
                    mouse will immediately update frequency.
                    DSP remains silent until that focus arrives.
                */
                JuceBridge.setParameter(
                    'AUDITION_ENABLE',
                    0.0
                );

                console.log(
                    '[Troakar] Δ local audition enabled',
                    fromHost ? '(host/APVTS)' : '(user)'
                );
            };

        // ================================================================
        // 4. Dynamic knob binding to active gradient
        // ================================================================
        function setKnobGradientState(knobId, isGradientActive, color) {
            const knob = knobs.get(knobId);

            if (!knob) {
                console.warn(
                    '[Troakar] Missing knob during binding:',
                    knobId
                );
                return;
            }

            if (typeof knob.setGradientActive !== 'function') {
                console.warn(
                    '[Troakar] Knob has no setGradientActive():',
                    knobId
                );
                return;
            }

            knob.setGradientActive(isGradientActive, color);
        }

        function bindKnobToParameter(
            knobId,
            parameterId
        ) {
            const knob = knobs.get(knobId);

            if (!knob) {
                console.warn(
                    '[Troakar] Missing knob during parameter bind:',
                    knobId,
                    '->',
                    parameterId
                );

                return false;
            }

            if (typeof knob.bindToParameter !== 'function') {
                console.warn(
                    '[Troakar] Knob has no bindToParameter():',
                    knobId,
                    '->',
                    parameterId
                );

                return false;
            }

            knob.bindToParameter(parameterId);

            // If this knob is bound to a gradient parameter, wire up
            // the global drag flag so paramUpdate handlers can skip sync.
            if (parameterId && parameterId.indexOf('GRADIENT_') === 0) {
                if (typeof knob.beginDrag === 'function') {
                    var _origBeginDrag = knob.beginDrag.bind(knob);
                    knob.beginDrag = function (event) {
                        isDraggingGradient = true;
                        _origBeginDrag(event);
                    };
                }
                if (typeof knob.endDrag === 'function') {
                    var _origEndDrag = knob.endDrag.bind(knob);
                    knob.endDrag = function (event) {
                        _origEndDrag(event);
                        isDraggingGradient = false;
                    };
                }
            }

            return true;
        }

        function updateKnobBindings() {
            var activePt  = gradientManager.getActivePoint();
            var isGlobal  = !gradientManager.hasActivePoint();
            var capColor  = activePt ? activePt.color : null;
            var isAuto    = buttons.get('speed-auto').getState();

            // I/O knobs — never gradient
            setKnobGradientState('in-gain', false, capColor);
            setKnobGradientState('out-lvl', false, capColor);
            setKnobGradientState('mix', false, capColor);

            // Dynamics knobs
            setKnobGradientState('amount', !isGlobal, capColor);
            setKnobGradientState('up-range', !isGlobal, capColor);
            setKnobGradientState('down-range', !isGlobal, capColor);
            setKnobGradientState('up-smooth', !isGlobal, capColor);
            setKnobGradientState('down-smooth', !isGlobal, capColor);
            setKnobGradientState('up-sel', !isGlobal, capColor);
            setKnobGradientState('down-sel', !isGlobal, capColor);

            // Timing knobs
            if (isAuto) {
                setKnobGradientState('speed', !isGlobal, capColor);
                setKnobGradientState('attack', false, capColor);
                setKnobGradientState('release', false, capColor);
                setKnobGradientState('knee', false, capColor);
            } else {
                setKnobGradientState('speed', false, capColor);
                setKnobGradientState('attack', !isGlobal, capColor);
                setKnobGradientState('release', !isGlobal, capColor);
                setKnobGradientState('knee', !isGlobal, capColor);
            }

            // Re-bind parameters
            if (!isGlobal && activePt) {
                var prefix = 'GRADIENT_' + activePt.id;

                bindKnobToParameter(
                    'amount',
                    prefix + '_AMOUNT'
                );

                bindKnobToParameter(
                    'up-range',
                    prefix + '_UP_MAX'
                );

                bindKnobToParameter(
                    'down-range',
                    prefix + '_DOWN_MAX'
                );

                bindKnobToParameter(
                    'up-smooth',
                    prefix + '_UP_SMOOTH'
                );

                bindKnobToParameter(
                    'down-smooth',
                    prefix + '_DOWN_SMOOTH'
                );

                bindKnobToParameter(
                    'up-sel',
                    prefix + '_UP_SEL'
                );

                bindKnobToParameter(
                    'down-sel',
                    prefix + '_DOWN_SEL'
                );

                if (isAuto) {
                    bindKnobToParameter(
                        'speed',
                        prefix + '_SPEED'
                    );
                } else {
                    bindKnobToParameter(
                        'attack',
                        prefix + '_ATTACK'
                    );

                    bindKnobToParameter(
                        'release',
                        prefix + '_RELEASE'
                    );

                    bindKnobToParameter(
                        'knee',
                        prefix + '_KNEE'
                    );
                }
            } else {
                bindKnobToParameter(
                    'amount',
                    'AMOUNT'
                );

                bindKnobToParameter(
                    'up-range',
                    'UPWARD_RANGE'
                );

                bindKnobToParameter(
                    'down-range',
                    'DOWNWARD_RANGE'
                );

                bindKnobToParameter(
                    'up-smooth',
                    'UP_SMOOTH'
                );

                bindKnobToParameter(
                    'down-smooth',
                    'DOWN_SMOOTH'
                );

                bindKnobToParameter(
                    'up-sel',
                    'UP_SEL'
                );

                bindKnobToParameter(
                    'down-sel',
                    'DOWN_SEL'
                );

                if (isAuto) {
                    bindKnobToParameter(
                        'speed',
                        'SPECTRAL_SPEED'
                    );
                } else {
                    bindKnobToParameter(
                        'attack',
                        'ATTACK_MS'
                    );

                    bindKnobToParameter(
                        'release',
                        'RELEASE_MS'
                    );

                    bindKnobToParameter(
                        'knee',
                        'KNEE_WIDTH'
                    );
                }

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
                if (!p.active) return;
                
                markers.amount.push({ id: p.id, normalizedValue: p.normAmount, color: p.color });
                markers.upRange.push({ id: p.id, normalizedValue: p.normUpMax, color: p.color });
                markers.downRange.push({ id: p.id, normalizedValue: p.normDownMax, color: p.color });
                markers.upSmooth.push({ id: p.id, normalizedValue: p.normUpSmooth, color: p.color });
                markers.downSmooth.push({ id: p.id, normalizedValue: p.normDownSmooth, color: p.color });
                markers.upSel.push({ id: p.id, normalizedValue: p.normUpSel, color: p.color });
                markers.downSel.push({ id: p.id, normalizedValue: p.normDownSel, color: p.color });

                if (p.useAutoSpeed) {
                    markers.speed.push({ id: p.id, normalizedValue: p.normSpeed, color: p.color });
                } else {
                    markers.attack.push({ id: p.id, normalizedValue: p.normAttack, color: p.color });
                    markers.release.push({ id: p.id, normalizedValue: p.normRelease, color: p.color });
                    markers.knee.push({ id: p.id, normalizedValue: p.normKnee, color: p.color });
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

            // Gradient parameter changes → update specific point only
            for (var g = 0; g < 4; ++g) {
                (function (gradientIndex) {
                    var prefix = 'GRADIENT_' + gradientIndex;

                    JuceBridge.onParamUpdate(
                        prefix + '_ENABLE',
                        function (val) {
                            if (isDraggingGradient) return;
                            var pt = gradientManager.getPoint(gradientIndex);
                            if (!pt) return;
                            pt.active = (Number(val) || 0) >= 0.5;

                            gradientOverlay.render();
                            mainScreen.targetCurveDirty = true;

                            if (controlsReady) {
                                updateSilaLabel();
                                updateKnobBindings();
                                setGradientMarkersOnKnobs();
                            }
                        }
                    );

                    JuceBridge.onParamUpdate(prefix + '_CENTER_FREQ', function (val) {
                        if (isDraggingGradient) return;
                        var pt = gradientManager.getPoint(gradientIndex);
                        if (!pt) return;
                        pt.centerFreqHz = 20.0 * Math.pow(10.0, (Number(val) || 0) * 3.0);
                        mainScreen.targetCurveDirty = true;
                    });

                    JuceBridge.onParamUpdate(prefix + '_CENTER_GAIN', function (val) {
                        if (isDraggingGradient) return;
                        var pt = gradientManager.getPoint(gradientIndex);
                        if (!pt) return;
                        pt.centerGainDb = (Number(val) || 0) * 120.0 - 60.0;
                        mainScreen.targetCurveDirty = true;
                    });

                    JuceBridge.onParamUpdate(prefix + '_BANDWIDTH', function (val) {
                        if (isDraggingGradient) return;
                        var pt = gradientManager.getPoint(gradientIndex);
                        if (!pt) return;
                        pt.radiusOctaves = (Number(val) || 0) * 3.5 + 0.5;
                        mainScreen.targetCurveDirty = true;
                    });

                    // Normalized param listeners — just cache the raw value
                    var normalizedParams = [
                        '_AMOUNT', '_UP_MAX', '_DOWN_MAX', '_SPEED',
                        '_UP_SMOOTH', '_DOWN_SMOOTH', '_UP_SEL', '_DOWN_SEL',
                        '_ATTACK', '_RELEASE', '_KNEE', '_AUTO_SPEED'
                    ];
                    normalizedParams.forEach(function (suffix) {
                        JuceBridge.onParamUpdate(prefix + suffix, function () {
                            if (isDraggingGradient) return;
                            var pt = gradientManager.getPoint(gradientIndex);
                            if (!pt) return;
                            var field = 'norm' + suffix.substring(1);
                            if (field in pt) {
                                pt[field] = Number(arguments[0]) || 0;
                            }
                            if (controlsReady) {
                                setGradientMarkersOnKnobs();
                            }
                        });
                    });
                })(g);
            }

            // Speed auto button changes
            JuceBridge.onParamUpdate('SPEED_AUTO', function (val) {
                buttons.get('speed-auto').setState(
                    val >= 0.5,
                    true
                );

                applyTimingMode(val >= 0.5);
            });

            // IO_LINK button changes from host/automation
            JuceBridge.onParamUpdate('IO_LINK', function (val) {
                var isLinked = val >= 0.5;
                var linkBtn = buttons.get('link');
                if (linkBtn) {
                    linkBtn.setState(isLinked, true);
                }
                var inKnob = knobs.get('in-gain');
                var outKnob = knobs.get('out-lvl');
                if (inKnob) inKnob.setLinked(isLinked);
                if (outKnob) outKnob.setLinked(isLinked);
            });

            // DN MAX changes — refresh compression area immediately
            JuceBridge.onParamUpdate('DOWNWARD_RANGE', function () {
                mainScreen.render();
            });
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

            /*
                VIEW_RANGE is a legacy APVTS parameter.
                It remains available for old sessions/presets,
                but does not affect the calibrated graph scale.
            */
            JuceBridge.onParamUpdate(
                'VIEW_RANGE',
                function () {
                    mainScreen.setViewRange(108.0);
                }
            );
        }
        setupParamListeners();

        // Connect screen callbacks
        mainScreen.onGradientSelectionChanged = function () {
            gradientOverlay.render();
            updateSilaLabel();
            updateKnobBindings();
        };
        mainScreen.onGradientParamsChanged = function () {
            gradientOverlay.render();
            updateSilaLabel();
            updateKnobBindings();
            if (JuceBridge && JuceBridge.isJuceAvailable() && gradientManager.points.length > 0) {
                gradientManager.points.forEach(function (p) {
                    if (p.isSelected) {
                        gradientManager.syncPointToJuce(JuceBridge, p.id);
                    }
                });
            }
        };

        mainScreen.onAuditionFocusChanged =
            function (focus) {
                var deltaButton =
                    buttons.get('delta');

                if (!deltaButton ||
                    !deltaButton.getState()) {
                    return;
                }

                if (!JuceBridge ||
                    !JuceBridge.isJuceAvailable()) {
                    return;
                }

                if (!focus ||
                    !focus.active) {
                    JuceBridge.setParameter(
                        'AUDITION_ENABLE',
                        0.0
                    );
                    return;
                }

                /*
                    Frequency:
                        20 Hz ... 20 kHz
                        logarithmic normalized representation.

                    Width:
                        0.10 ... 4.00 octaves
                        linear normalized representation.
                */
                var frequencyNorm =
                    Math.max(
                        0.0,
                        Math.min(
                            1.0,
                            Math.log10(
                                focus.frequencyHz / 20.0
                            ) / 3.0
                        )
                    );

                var widthNorm =
                    Math.max(
                        0.0,
                        Math.min(
                            1.0,
                            (focus.widthOctaves - 0.10)
                            / 3.90
                        )
                    );

                JuceBridge.queueParameterChange(
                    'AUDITION_FREQ',
                    frequencyNorm
                );

                JuceBridge.queueParameterChange(
                    'AUDITION_WIDTH',
                    widthNorm
                );

                JuceBridge.queueParameterChange(
                    'AUDITION_ENABLE',
                    1.0
                );
            };

        // ================================================================
        // 5. IN/OUT LINK logic (port of PluginEditor.cpp)
        // ================================================================
        // ================================================================
        // 5. IN/OUT LINK logic (Inverse Gain-Staging Compensation)
        // ================================================================
        var isUpdatingLink  = false;
        var lastInGain      = knobs.get('in-gain') ? knobs.get('in-gain').value : 0.5;
        var lastOutLvl      = knobs.get('out-lvl') ? knobs.get('out-lvl').value : 0.5;

        knobs.get('in-gain').onValueChange = function (newVal) {
            var isLinked = buttons.get('link') && buttons.get('link').getState();
            if (isLinked && !isUpdatingLink) {
                isUpdatingLink = true;
                var delta    = newVal - lastInGain;
                // Компенсационный гейн-стейджинг: рост In уменьшает Out
                var newOut   = Math.min(1.0, Math.max(0.0, knobs.get('out-lvl').value - delta));
                knobs.get('out-lvl').setValue(newOut);
                if (JuceBridge && JuceBridge.isJuceAvailable()) {
                    JuceBridge.queueParameterChange('OUT_LVL', newOut);
                }
                lastOutLvl = newOut;
                isUpdatingLink = false;
            }
            lastInGain = newVal;
        };

        knobs.get('out-lvl').onValueChange = function (newVal) {
            var isLinked = buttons.get('link') && buttons.get('link').getState();
            if (isLinked && !isUpdatingLink) {
                isUpdatingLink = true;
                var delta   = newVal - lastOutLvl;
                // Компенсационный гейн-стейджинг: рост Out уменьшает In
                var newIn   = Math.min(1.0, Math.max(0.0, knobs.get('in-gain').value - delta));
                knobs.get('in-gain').setValue(newIn);
                if (JuceBridge && JuceBridge.isJuceAvailable()) {
                    JuceBridge.queueParameterChange('IN_GAIN', newIn);
                }
                lastInGain = newIn;
                isUpdatingLink = false;
            }
            lastOutLvl = newVal;
        };

        // ================================================================
        // 6. COMBO BOXES
        // ================================================================
        var displayModeCombo =
            document.getElementById(
                'display-mode-combo'
            );

        if (displayModeCombo) {
            mainScreen.displayMode =
                displayModeCombo.value
                    || 'detector';

            displayModeCombo.addEventListener(
                'change',
                function (event) {
                    mainScreen.displayMode =
                        event.target.value
                            || 'detector';

                    mainScreen.targetCurveDirty =
                        true;

                    mainScreen.render();
                }
            );
        }

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

        var viewRangeCombo =
            document.getElementById(
                'view-range-combo'
            );

        if (viewRangeCombo) {
            viewRangeCombo.disabled = true;
            mainScreen.setViewRange(108.0);
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
        controlsReady = true;

        applyTimingMode(
            buttons.get('speed-auto').getState()
        );

        updateSilaLabel();
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
