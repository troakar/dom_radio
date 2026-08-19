(function () {
    'use strict';

    var ARTBOARD_WIDTH = 960;

    var BASE_HEIGHT = 455;
        var EQ_EXTRA = 210; 
        var ARCHIVE_EXTRA = 190;

    var knobs = [];

    var KNOB_CONFIGS = [
        { id: 'knob-in-gain', param: 'IN_GAIN', type: 'big', min: -18, max: 18, unit: 'dB', decimals: 1 },
        { id: 'knob-pre-drive', param: 'DRIVE', type: 'medium', min: 1, max: 10, unit: '', decimals: 1 },
        { id: 'knob-tape-drive', param: 'TAPE_DRIVE', type: 'medium', min: 0, max: 100, unit: '%', decimals: 0 },
        { id: 'knob-slew', param: 'TRANSIENT', type: 'medium', min: 0, max: 100, unit: '%', decimals: 0 },

        { id: 'knob-tape-speed', param: 'TAPE_SPEED', type: 'big', min: 3.75, max: 30, unit: 'ips', decimals: 1, skew: 0.55 },
        { id: 'knob-air', param: 'AIR', type: 'medium', min: 0, max: 15, unit: 'dB', decimals: 1 },
        { id: 'knob-bias', param: 'BIAS', type: 'medium', min: -50, max: 50, unit: '%', decimals: 0 },
        { id: 'knob-decay', param: 'DECAY', type: 'medium', min: 0, max: 10, unit: '', decimals: 1 },

        { id: 'knob-mix', param: 'MIX', type: 'medium', min: 0, max: 100, unit: '%', decimals: 0 },
        { id: 'knob-out-lvl', param: 'OUT_LVL', type: 'medium', min: -18, max: 18, unit: 'dB', decimals: 1 },
        { id: 'knob-detail-amount', param: 'DETAIL_AMOUNT', type: 'small', min: 0, max: 100, unit: '%', decimals: 0 },
        { id: 'knob-detail-tilt', param: 'DETAIL_TILT', type: 'small', min: -100, max: 100, unit: '%', decimals: 0 },
        { id: 'knob-iron-core', param: 'IRON_CORE', type: 'small', min: 0, max: 100, unit: '%', decimals: 0 },

        // EQ knobs: small size for compact 2x2 grid
        { id: 'knob-bass', param: 'BASS', type: 'small', min: -18, max: 18, unit: 'dB', decimals: 1 },
        { id: 'knob-treble', param: 'TREBLE', type: 'small', min: -18, max: 18, unit: 'dB', decimals: 1 },
        { id: 'knob-bass-freq', param: 'BASS_FREQ', type: 'small', min: 30, max: 300, unit: 'Hz', decimals: 0 },
        { id: 'knob-treble-freq', param: 'TREBLE_FREQ', type: 'small', min: 1000, max: 15000, unit: 'Hz', decimals: 0 },

        { id: 'knob-age', param: 'AGE', type: 'small', min: 0, max: 50, unit: 'y', decimals: 0 },
        { id: 'knob-oxide', param: 'OXIDE', type: 'small', min: 0, max: 10, unit: '', decimals: 1 },
        { id: 'knob-azimuth', param: 'AZIMUTH', type: 'small', min: 0, max: 10, unit: '', decimals: 1 },
        { id: 'knob-bias-sag', param: 'BIAS_SAG', type: 'small', min: 0, max: 100, unit: '%', decimals: 0 },
        { id: 'knob-scrape', param: 'SCRAPE_FLUTTER', type: 'small', min: 0, max: 100, unit: '%', decimals: 0 },
        { id: 'knob-crosstalk', param: 'CROSSTALK', type: 'small', min: 0, max: 100, unit: '%', decimals: 0 },
        { id: 'knob-wow', param: 'WOW_AMOUNT', type: 'small', min: 0, max: 100, unit: '%', decimals: 0 },
        { id: 'knob-flutter', param: 'FLUTTER_AMOUNT', type: 'small', min: 0, max: 100, unit: '%', decimals: 0 },
        { id: 'knob-tape-noise', param: 'TAPE_NOISE', type: 'small', min: 0, max: 100, unit: '%', decimals: 0 },
        { id: 'knob-hum', param: 'HUM', type: 'small', min: 0, max: 100, unit: '%', decimals: 0 },

        { id: 'knob-noise-profile', param: 'NOISE_PROFILE', type: 'trim', min: 0, max: 100, unit: '%', decimals: 0, defaultValue: 0 }
    ];

    var COMBO_CONFIGS = [
        { id: 'oversampling-combo', param: 'ONLINE_OS' },
        { id: 'tmt-mode-combo', param: 'TMT_MODE' },
        { id: 'drive-type-combo', param: 'DRIVE_TYPE' },
        { id: 'eq-std-combo', param: 'EQ_STD' },
        { id: 'tape-model-combo', param: 'TAPE_MODEL' },
        { id: 'detail-algo-combo', param: 'DETAIL_ALGO' },
        { id: 'noise-mode-combo', param: 'NOISE_MODE' }
    ];

    function getDrawerHeight() {
        var height = BASE_HEIGHT;

        var eqDrawer = document.getElementById('eq-drawer');
        var archiveDrawer = document.getElementById('archive-drawer');

        if (eqDrawer && eqDrawer.classList.contains('is-open')) {
            height += EQ_EXTRA;
        }

        if (archiveDrawer && archiveDrawer.classList.contains('is-open')) {
            height += ARCHIVE_EXTRA;
        }

        return height;
    }

    function updateArtboardSize() {
        var height = getDrawerHeight();

        document.documentElement.style.setProperty(
            '--artboard-height',
            height + 'px'
        );

        var artboard =
            document.getElementById('device-artboard');

        if (artboard) {
            artboard.style.height =
                height + 'px';
        }

        var scale = Math.min(
            window.innerWidth / ARTBOARD_WIDTH,
            window.innerHeight / height
        );

        scale = Math.max(
            0.01,
            Number(scale) || 1.0
        );

        document.documentElement.style.setProperty(
            '--ui-scale',
            scale.toFixed(6)
        );

        // ДОБАВЛЕНО: Синхронизация Aspect Ratio с окном DAW
        if (JuceBridge && typeof JuceBridge.setLogicalSize === 'function') {
            JuceBridge.setLogicalSize(ARTBOARD_WIDTH, height);
        }
    }

    function setDrawerState(
        drawer,
        button,
        shouldOpen
    ) {
        if (!drawer || !button)
            return;

        drawer.classList.toggle(
            'is-open',
            shouldOpen
        );

        drawer.setAttribute(
            'aria-hidden',
            shouldOpen ? 'false' : 'true'
        );

        button.classList.toggle(
            'is-open',
            shouldOpen
        );

        button.setAttribute(
            'aria-expanded',
            shouldOpen ? 'true' : 'false'
        );
    }

    function setupDrawer(buttonId, drawerId) {
        var button = document.getElementById(buttonId);
        var drawer = document.getElementById(drawerId);

        if (!button || !drawer) return;

        button.addEventListener('click', function () {
            var shouldOpen = !drawer.classList.contains('is-open');

            // УДАЛЕНА логика закрытия "другой" шторки!
            // Теперь шторка открывается и закрывается независимо.
            setDrawerState(drawer, button, shouldOpen);
            updateArtboardSize();
        });
    }

    function setupKnobs() {
        KNOB_CONFIGS.forEach(
            function (cfg) {
                var element =
                    document.getElementById(
                        cfg.id
                    );

                if (!element)
                    return;

                var knob =
                    new ChickenKnob(
                        cfg.id,
                        cfg.param,
                        cfg
                    );

                knobs.push(knob);
            }
        );
    }

    function setupComboBoxes() {
        COMBO_CONFIGS.forEach(
            function (cfg) {
                var select =
                    document.getElementById(
                        cfg.id
                    );

                if (!select)
                    return;

                function getNormalized() {
                    var maxIndex =
                        select.options.length - 1;

                    return maxIndex > 0
                        ? (select.selectedIndex / maxIndex)
                        : 0.0;
                }

                function setFromNormalized(value) {
                    var maxIndex =
                        select.options.length - 1;

                    var safeVal =
                        Math.max(
                            0.0,
                            Math.min(
                                1.0,
                                Number(value) || 0.0
                            )
                        );

                    select.selectedIndex =
                        Math.round(
                            safeVal * maxIndex
                        );
                }

                Promise.resolve(
                    JuceBridge.getParameter(
                        cfg.param
                    )
                ).then(
                    function (value) {
                        if (value === null ||
                            value === undefined)
                            return;

                        setFromNormalized(value);
                    }
                );

                // ФИКС ЖЕСТОВ: Обеспечиваем начало и конец жеста для надёжного сохранения стейта DAW!
                var handleSelectionChange = function () {
                    var norm = getNormalized();
                    if (JuceBridge && JuceBridge.isJuceAvailable()) {
                        JuceBridge.beginGesture(cfg.param);
                        JuceBridge.setParameter(cfg.param, norm);
                        JuceBridge.endGesture(cfg.param);
                    }
                };

                select.addEventListener(
                    'change',
                    handleSelectionChange
                );

                select.addEventListener(
                    'input',
                    handleSelectionChange
                );

                JuceBridge.onParamUpdate(
                    cfg.param,
                    function (value) {
                        setFromNormalized(value);
                    }
                );
            }
        );
    }

    function setupTemperatureSlider() {
        var slider =
            document.getElementById(
                'temperature-slider'
            );

        var valueText =
            document.getElementById(
                'temperature-value'
            );

        if (!slider)
            return;

        function updateText(val) {
            var realVal =
                15.0 + val * (50.0 - 15.0);

            if (valueText) {
                valueText.textContent =
                    realVal.toFixed(1) + ' °C';
            }
        }

        Promise.resolve(
            JuceBridge.getParameter(
                'TEMPERATURE'
            )
        ).then(
            function (value) {
                if (value === null ||
                    value === undefined)
                    return;

                slider.value =
                    Number(value) * 100;

                updateText(
                    Number(value)
                );
            }
        );

        // ФИКС ЖЕСТОВ ДЛЯ СЛАЙДЕРА!
        slider.addEventListener(
            'pointerdown',
            function () {
                if (JuceBridge && JuceBridge.isJuceAvailable())
                    JuceBridge.beginGesture('TEMPERATURE');
            }
        );

        slider.addEventListener(
            'pointerup',
            function () {
                if (JuceBridge && JuceBridge.isJuceAvailable())
                    JuceBridge.endGesture('TEMPERATURE');
            }
        );

        slider.addEventListener(
            'input',
            function () {
                var normVal =
                    Number(slider.value) / 100.0;

                updateText(normVal);

                if (JuceBridge && JuceBridge.isJuceAvailable()) {
                    JuceBridge.setParameter(
                        'TEMPERATURE',
                        normVal
                    );
                }
            }
        );

        JuceBridge.onParamUpdate(
            'TEMPERATURE',
            function (value) {
                slider.value =
                    Number(value) * 100;

                updateText(
                    Number(value)
                );
            }
        );
    }

    function setupTelemetry() {
        var ledVu =
            document.getElementById('vu-led-main');

        var ledSatDrive =
            document.getElementById('sat-led-drive');

        var ledSatTape =
            document.getElementById('sat-led-tape');

        if (window.__JUCE__ &&
            window.__JUCE__.backend) {

            window.__JUCE__.backend.addEventListener(
                'telemetry',
                function (data) {
                    if (!data)
                        return;

                    // VU Meter (0.0 to 1.0)
                    if (ledVu) {
                        var vu =
                            Math.max(0, Math.min(1, Number(data.vuMeter) || 0));

                        ledVu.style.width =
                            (vu * 100) + '%';
                    }

                    // Input Saturation (0.0 to 1.0)
                    if (ledSatDrive) {
                        var satInput =
                            Math.max(0, Math.min(1, Number(data.satInput) || 0));

                        ledSatDrive.style.width =
                            (satInput * 100) + '%';
                    }

                    // Tape Saturation (0.0 to 1.0)
                    if (ledSatTape) {
                        var satTape =
                            Math.max(0, Math.min(1, Number(data.satTape) || 0));

                        ledSatTape.style.width =
                            (satTape * 100) + '%';
                    }
                }
            );
        }
    }

    function setupLinkButtons() {
        var btnIn = document.getElementById('btn-in-link');
        var btnOut = document.getElementById('btn-out-link');
        var isLinked = false;

        // Ищем инстансы наших ручек
        var knobIn = knobs.find(k => k.paramId === 'IN_GAIN');
        var knobOut = knobs.find(k => k.paramId === 'OUT_LVL');

        function updateLinkUI() {
            if (btnIn) btnIn.classList.toggle('is-active', isLinked);
            if (btnOut) btnOut.classList.toggle('is-active', isLinked);
        }

        // Если нажать на ЛЮБУЮ из кнопок, включается связь
        function toggleLink(e) {
            e.preventDefault();
            isLinked = !isLinked;
            updateLinkUI();
        }

        if (btnIn) btnIn.addEventListener('click', toggleLink);
        if (btnOut) btnOut.addEventListener('click', toggleLink);

        if (knobIn && knobOut) {
            var isUpdating = false;
            var lastIn = knobIn.value;
            var lastOut = knobOut.value;

            // Следим за изменениями от хоста (DAW)
            if (JuceBridge) {
                JuceBridge.onParamUpdate('IN_GAIN', val => { if (!isUpdating) lastIn = val; });
                JuceBridge.onParamUpdate('OUT_LVL', val => { if (!isUpdating) lastOut = val; });
            }

            // Перехватываем вращение ручки IN GAIN
            var origInChange = knobIn.onValueChange;
            knobIn.onValueChange = function(val) {
                if (origInChange) origInChange(val);

                var delta = val - lastIn; // На сколько повернули ручку
                lastIn = val;

                if (isLinked && !isUpdating) {
                    isUpdating = true;
                    // ИНВЕРСИЯ: отнимаем delta от ручки OUT_LVL
                    var target = Math.max(0, Math.min(1, knobOut.value - delta));

                    knobOut.setValue(target);
                    lastOut = target;

                    if (JuceBridge) JuceBridge.queueParameterChange('OUT_LVL', target);
                    isUpdating = false;
                }
            };

            // Перехватываем вращение ручки OUT LVL
            var origOutChange = knobOut.onValueChange;
            knobOut.onValueChange = function(val) {
                if (origOutChange) origOutChange(val);

                var delta = val - lastOut;
                lastOut = val;

                if (isLinked && !isUpdating) {
                    isUpdating = true;
                    // ИНВЕРСИЯ: отнимаем delta от ручки IN GAIN
                    var target = Math.max(0, Math.min(1, knobIn.value - delta));

                    knobIn.setValue(target);
                    lastIn = target;

                    if (JuceBridge) JuceBridge.queueParameterChange('IN_GAIN', target);
                    isUpdating = false;
                }
            };
        }
    }

    function startApp() {
        window.addEventListener(
            'resize',
            updateArtboardSize
        );

        setupDrawer(
            'btn-archive-toggle',
            'archive-drawer'
        );

        setupDrawer(
            'btn-eq-toggle',
            'eq-drawer'
        );

        setupKnobs();
        setupComboBoxes();
        setupTemperatureSlider();
        setupTelemetry();
        setupLinkButtons();

        // Инициализация графического EQ-монитора
        if (typeof EQMonitor !== 'undefined' && document.getElementById('eq-canvas')) {
            new EQMonitor('eq-canvas');
        }

        updateArtboardSize();
    }

    if (document.readyState === 'loading') {
        document.addEventListener(
            'DOMContentLoaded',
            startApp,
            { once: true }
        );
    } else {
        startApp();
    }
})();
