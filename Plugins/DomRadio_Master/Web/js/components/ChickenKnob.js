// TROAKAR SPECTRAL - Dynamic Chicken Knob Component
// Ported from GradientKnob.cpp / ChickenKnob.js
(function (root, factory) {
    if (typeof module !== 'undefined' && module.exports) {
        module.exports = factory(root.JuceBridge, root.ThemeConfig);
    } else {
        root.ChickenKnob = factory(root.JuceBridge, root.ThemeConfig);
    }
})(typeof window !== 'undefined' ? window : this, function (JuceBridge, ThemeConfig) {

    return class ChickenKnob {
        constructor(elementId, paramId, options) {
            if (typeof TroakarConsole !== 'undefined')
                TroakarConsole.debug(
                    'ChickenKnob constructor:',
                    elementId,
                    paramId
                );

            options      = options || {};
            this.elementId = elementId;
            this.element   = document.getElementById(elementId);
            this.paramId   = paramId;

            if (!this.element) {
                console.error(
                    '[ChickenKnob] Element not found:',
                    elementId
                );

                return;
            }

            this.config = (ThemeConfig && ThemeConfig.knobConfigs && ThemeConfig.knobConfigs[elementId]) || {};

            this.min        = (options.min !== undefined)     ? options.min     : (this.config.min     !== undefined ? this.config.min     : 0);
            this.max        = (options.max !== undefined)     ? options.max     : (this.config.max     !== undefined ? this.config.max     : 1);
            this.unit       = (options.unit !== undefined)    ? options.unit    : (this.config.unit    || '');
            this.decimals   = (options.decimals !== undefined)? options.decimals: (this.config.decimals!== undefined ? this.config.decimals: 1);
            this.skew       = (options.skew !== undefined)    ? options.skew    : (this.config.skew    !== undefined ? this.config.skew    : 1.0);
            this.type       = (options.type !== undefined)    ? options.type    : (this.config.type    || 'medium');

            // Normalized value 0..1
            this.defaultValue = (options.defaultValue !== undefined)
                ? options.defaultValue
                : 0.5;

            this.value = this.defaultValue;

            this.valueDisplay = document.getElementById(elementId + '-value');

            this.isDragging          = false;
            this.dragStartY          = 0;
            this.dragStartValue      = 0;
            this.dragPointerId       = null;
            this.dragScale           = 1.0;
            this.lastSentValue       = this.value;
            this.pendingParameterValue = null;
            this.parameterFramePending  = false;
            this.manualEditor = null;

            // Gradient mode state (port of GradientKnob.cpp)
            this.allowGradientMode = options.allowInGradientMode !== undefined
                ? options.allowInGradientMode
                : true;

            this.isSmall          = this.type === 'small';
            this.isGradientSelected = false;
            this.isLockedState    = false;
            this.isLinkedState    = false;
            this.activeCapColor   = '#b4afa0'; // default light grey
            this.gradientMarkers  = [];

            this.onValueChange = null;

            this.removeParameterListener = null;

            if (this.element) {
                this.element.classList.add('chicken-knob', this.type);
                this.buildDOM();

                this.rotator  = this.element.querySelector('.knob-rotator');
                this.pointer  = this.element.querySelector('.knob-pointer');
                this.texture  = this.element.querySelector('.knob-texture');

                if (!this.rotator) {
                    throw new Error(
                        'Knob rotator was not created for #' + elementId
                    );
                }

                if (typeof TroakarConsole !== 'undefined')
                    TroakarConsole.debug(
                        'ChickenKnob DOM built:',
                        elementId,
                        'rotator found: yes'
                    );

                this.init();
                this.fetchInitialValue();
            }
        }

        buildDOM() {
            var radius = this.type === 'big' ? 36 : (this.type === 'medium' ? 26 : 18);

            var ticksHTML = '<div class="knob-ticks">';
            for (var i = -135; i <= 135; i += 9) {
                var isMajor    = (i + 135) % 27 === 0;
                var length     = isMajor ? 5 : 3;
                var thickness  = isMajor ? 2 : 1.5;
                var color      = isMajor ? 'var(--knob-tick-major)' : 'var(--knob-tick-minor)';
                ticksHTML += '<div style="position:absolute; top:50%; left:50%; width:' + thickness
                           + 'px; height:' + length + 'px; background:' + color
                           + '; transform: translate(-50%, -50%) rotate(' + i + 'deg) translateY(-' + radius + 'px); border-radius:1px;"></div>';
            }
            ticksHTML += '</div>';

            var innerHTML = '';

            // ФИКС СВЕТА: .knob-static-shadow и .knob-bar-base стоят снаружи .knob-rotator!
            // Их освещение зафиксировано сверху вниз и НЕ крутится!
            if (this.type === 'big' || this.type === 'medium') {
                innerHTML =
                    '<div class="knob-static-shadow"></div>' +
                    '<div class="knob-bar-base"></div>' + 
                    '<div class="knob-rotator">' +
                        '<div class="knob-bar-protrusion"></div>' +
                        '<div class="knob-pointer-line"></div>' +
                    '</div>';
            } else {
                innerHTML =
                    '<div class="knob-static-shadow"></div>' +
                    '<div class="knob-knurled-edge"></div>' +
                    '<div class="knob-rotator">' +
                        '<div class="knob-black-inset"></div>' +
                        '<div class="knob-metal-cap"></div>' +
                        '<div class="knob-pointer-dot"></div>' +
                    '</div>';
            }

            this.element.innerHTML = ticksHTML + '<div class="knob-body">' + innerHTML + '</div>';
        }

        fetchInitialValue() {
            var self = this;

            if (typeof TroakarConsole !== 'undefined')
                TroakarConsole.debug(
                    'Reading parameter:',
                    this.paramId
                );

            if (!this.element)
                return;

            if (this.removeParameterListener) {
                this.removeParameterListener();
                this.removeParameterListener = null;
            }

            if (!JuceBridge)
            {
                this.updateRotation();
                this.updateDisplay();
                return;
            }

            var result;

            try {
                result = JuceBridge.getParameter(this.paramId);

                if (typeof TroakarConsole !== 'undefined')
                    TroakarConsole.debug(
                        'getParameter returned for',
                        this.paramId,
                        typeof result
                    );
            }
            catch (error) {
                console.error(
                    '[ChickenKnob] getParameter failed:',
                    this.paramId,
                    error
                );

                if (typeof TroakarConsole !== 'undefined')
                    TroakarConsole.error(
                        'getParameter failed:',
                        this.paramId,
                        error
                    );

                this.updateRotation();
                this.updateDisplay();
                return;
            }

            Promise.resolve(result)
                .then(function (value) {
                    if (typeof TroakarConsole !== 'undefined')
                        TroakarConsole.debug(
                            'Parameter resolved:',
                            self.paramId,
                            value
                        );

                    if (value === null || value === undefined)
                        return;

                    var normalizedValue = Number(value);

                    if (!Number.isFinite(normalizedValue))
                        return;

                    self.value = Math.max(
                        0.0,
                        Math.min(1.0, normalizedValue)
                    );

                    self.updateRotation();
                    self.updateDisplay();
                })
                .catch(function (error) {
                    console.error(
                        '[ChickenKnob] native parameter read failed:',
                        self.paramId,
                        error
                    );

                    if (typeof TroakarConsole !== 'undefined')
                        TroakarConsole.error(
                            'Parameter read failed:',
                            self.paramId,
                            error
                        );
                });

            if (JuceBridge.isJuceAvailable()) {
                this.removeParameterListener =
                    JuceBridge.onParamUpdate(
                        this.paramId,
                        function (value) {
                            /*
                                Ignore host feedback while user is
                                actively dragging the knob. The
                                pointer position is the source of
                                truth during drag — applying
                                feedback would create a jump loop.
                            */
                            if (self.isDragging)
                                return;

                            var normalizedValue = Number(value);

                            if (!Number.isFinite(normalizedValue))
                                return;

                            self.value = Math.max(
                                0.0,
                                Math.min(1.0, normalizedValue)
                            );

                            self.updateRotation();
                            self.updateDisplay();
                        }
                    );
            }
        }

        getPointerScale() {
            /*
                The chassis is scaled as a unit via CSS
                transform: scale(). clientY from pointer
                events is in screen pixels, but knob
                drag math operates in the 1300×780
                logical coordinate space. We need the
                scale factor to convert screen pixels
                → logical pixels.
            */
            var chassis = document.querySelector('.device-chassis');

            if (!chassis)
                return 1.0;

            var rect = chassis.getBoundingClientRect();

            /*
                chassis has width:1300px in CSS.
                rect.width is the rendered (screen) width.
                Their ratio is the scale.
               */
            var logicalWidth =
                chassis.offsetWidth || 1300;

            if (rect.width === 0 ||
                logicalWidth <= 0)
                return 1.0;

            return rect.width / logicalWidth;
        }

        beginDrag(event) {
            event.preventDefault();
            event.stopPropagation();

            /*
                Ctrl + click открывает точный ручной ввод
                вместо обычного вертикального drag-жеста.
            */
            if (event.ctrlKey || event.metaKey) {
                this.openManualEditor(event);
                return;
            }

            this.isDragging = true;
            this.dragPointerId   = event.pointerId;
            this.dragStartY      = event.clientY;
            this.dragStartValue  = this.value;
            this.lastSentValue   = this.value;
            this.dragScale       = this.getPointerScale();

            this.element.classList.add('is-dragging');

            if (this.element.setPointerCapture) {
                try {
                    this.element.setPointerCapture(event.pointerId);
                } catch (error) {}
            }

            /*
                Notify C++ that a gesture is beginning
                so it can call beginChangeGesture once,
                rather than on every paramChange call.
            */
            if (JuceBridge && JuceBridge.isJuceAvailable()) {
                JuceBridge.beginGesture(this.paramId);
            }
        }

        drag(event) {
            if (!this.isDragging)
                return;

            if (this.dragPointerId !== null &&
                event.pointerId !== this.dragPointerId)
                return;

            event.preventDefault();
            event.stopPropagation();

            /*
                Было 180.
                285 даёт примерно на 37% более медленную реакцию:
                ручка требует большего вертикального перемещения,
                поэтому ощущается тяжелее и точнее.
            */
            var sensitivity = 285.0;

            /*
                clientY is in screen pixels.
                Divide by scale to get logical movement
                in the 1300×780 coordinate space.
            */
            var logicalDelta =
                (this.dragStartY - event.clientY)
                / Math.max(0.1, this.dragScale);

            var nextValue =
                this.dragStartValue
                + logicalDelta / sensitivity;

            nextValue = Math.max(
                0.0,
                Math.min(1.0, nextValue)
            );

            /*
                Don't send the same value repeatedly.
            */
            if (Math.abs(nextValue - this.lastSentValue) < 0.001)
                return;

            this.lastSentValue = nextValue;
            this.value = nextValue;

            this.updateRotation();
            this.updateDisplay();

            if (typeof this.onValueChange === 'function') {
                this.onValueChange(nextValue);
            }

            this.queueParameterChange(nextValue);
        }

        endDrag(event) {
            if (!this.isDragging)
                return;

            event.preventDefault();
            event.stopPropagation();

            this.isDragging = false;

            if (event.pointerId !== undefined &&
                this.element.releasePointerCapture) {
                try {
                    this.element.releasePointerCapture(event.pointerId);
                } catch (error) {}
            }

            this.dragPointerId = null;
            this.element.classList.remove('is-dragging');

            /*
                Send begin/end gesture on C++ side, then
                flush the final accumulated parameter value.
            */
            if (JuceBridge && JuceBridge.isJuceAvailable()) {
                this.flushParameterChange();
                JuceBridge.endGesture(this.paramId);
            } else {
                this.flushParameterChange();
            }
        }

        queueParameterChange(value) {
            this.pendingParameterValue = value;

            if (this.parameterFramePending)
                return;

            this.parameterFramePending = true;

            requestAnimationFrame(function () {
                this.parameterFramePending = false;

                if (!this.isDragging) {
                    this.pendingParameterValue = null;
                    return;
                }

                this.flushParameterChange();
            }.bind(this));
        }

        flushParameterChange() {
            if (this.pendingParameterValue === null ||
                this.pendingParameterValue === undefined)
                return;

            var value = this.pendingParameterValue;

            this.pendingParameterValue = null;

            if (JuceBridge) {
                JuceBridge.setParameter(
                    this.paramId,
                    value
                );
            }
        }

        /*
            Преобразование текущего normalized APVTS value
            в реальную единицу ручки.

            Формула должна быть зеркальная к normalisedValueForReal().
        */
        realValueForNormalised(normalizedValue) {
            var value = Math.max(
                0.0,
                Math.min(1.0, Number(normalizedValue) || 0.0)
            );

            var linearValue =
                this.skew !== 1.0 && value > 0.0
                    ? Math.pow(value, 1.0 / this.skew)
                    : value;

            return this.min
                + linearValue
                * (this.max - this.min);
        }

        /*
            Обратное преобразование:
            реальное значение из поля -> normalized 0…1 для APVTS.

            При skew != 1 нельзя просто использовать linear ratio:
            необходимо применить степень skew.
        */
        normalisedValueForReal(realValue) {
            var range = this.max - this.min;

            if (!Number.isFinite(range) ||
                Math.abs(range) < 1.0e-12) {
                return 0.0;
            }

            var linear =
                (Number(realValue) - this.min)
                / range;

            linear = Math.max(
                0.0,
                Math.min(1.0, linear)
            );

            var normalized =
                this.skew !== 1.0 && linear > 0.0
                    ? Math.pow(linear, this.skew)
                    : linear;

            return Math.max(
                0.0,
                Math.min(1.0, normalized)
            );
        }

        formatManualValue(realValue) {
            var safeValue = Number(realValue);

            if (!Number.isFinite(safeValue))
                safeValue = 0.0;

            return safeValue.toFixed(this.decimals)
                + (this.unit ? ' ' + this.unit : '');
        }

        parseManualValue(input) {
            if (input === null || input === undefined)
                return NaN;

            /*
                Поддерживаются:
                    -12.5
                    -12,5
                    -12.5 dB
                    100 %
                    25 ms
            */
            var normalizedText =
                String(input)
                    .trim()
                    .replace(',', '.')
                    .replace(/\s+/g, '');

            /*
                Оставляем число, знак и десятичную точку.
                Единицы dB / % / ms отбрасываются.
            */
            var match =
                normalizedText.match(
                    /[-+]?(?:\d+\.?\d*|\.\d+)/
                );

            if (!match)
                return NaN;

            return Number(match[0]);
        }

        closeManualEditor() {
            if (!this.manualEditor)
                return;

            if (this.manualEditor.parentNode) {
                this.manualEditor.parentNode.removeChild(
                    this.manualEditor
                );
            }

            this.manualEditor = null;
        }

        applyManualValue(inputValue) {
            var requestedValue =
                this.parseManualValue(inputValue);

            if (!Number.isFinite(requestedValue))
                return false;

            /*
                Ручной ввод не выходит за реальный диапазон
                конкретного параметра.
            */
            var clampedValue = Math.max(
                this.min,
                Math.min(this.max, requestedValue)
            );

            var normalizedValue =
                this.normalisedValueForReal(
                    clampedValue
                );

            /*
                setValue обновляет графику и value-display.
            */
            this.setValue(normalizedValue);

            if (JuceBridge &&
                JuceBridge.isJuceAvailable()) {
                JuceBridge.beginGesture(this.paramId);

                JuceBridge.setParameter(
                    this.paramId,
                    normalizedValue
                );

                JuceBridge.endGesture(this.paramId);
            } else if (JuceBridge) {
                JuceBridge.setParameter(
                    this.paramId,
                    normalizedValue
                );
            }

            return true;
        }

        openManualEditor(event) {
            /*
                Не допускаем нескольких окон для одной ручки.
            */
            this.closeManualEditor();

            var editor =
                document.createElement('div');

            editor.className =
                'knob-manual-editor';

            editor.setAttribute(
                'role',
                'dialog'
            );

            editor.setAttribute(
                'aria-label',
                'Manual value input'
            );

            var input =
                document.createElement('input');

            input.className =
                'knob-manual-input';

            input.type = 'text';
            input.inputMode = 'decimal';
            input.spellcheck = false;

            input.setAttribute(
                'aria-label',
                'Manual parameter value'
            );

            var currentRealValue =
                this.realValueForNormalised(
                    this.value
                );

            input.value =
                this.formatManualValue(
                    currentRealValue
                );

            var applyButton =
                document.createElement('button');

            applyButton.type = 'button';
            applyButton.className =
                'knob-manual-apply';
            applyButton.textContent = '✓';
            applyButton.title = 'Apply value';

            var cancelButton =
                document.createElement('button');

            cancelButton.type = 'button';
            cancelButton.className =
                'knob-manual-cancel';
            cancelButton.textContent = '×';
            cancelButton.title = 'Cancel';

            editor.appendChild(input);
            editor.appendChild(applyButton);
            editor.appendChild(cancelButton);

            document.body.appendChild(editor);

            this.manualEditor = editor;

            /*
                Позиция около ручки, но не за пределы окна.
            */
            var rect =
                this.element.getBoundingClientRect();

            var editorWidth =
                editor.offsetWidth || 178;

            var editorHeight =
                editor.offsetHeight || 42;

            var left =
                rect.left
                + rect.width * 0.5
                - editorWidth * 0.5;

            var top =
                rect.bottom + 8;

            if (top + editorHeight >
                window.innerHeight - 8) {
                top = rect.top - editorHeight - 8;
            }

            left = Math.max(
                8,
                Math.min(
                    window.innerWidth
                    - editorWidth - 8,
                    left
                )
            );

            top = Math.max(8, top);

            editor.style.left =
                Math.round(left) + 'px';

            editor.style.top =
                Math.round(top) + 'px';

            var commit = function () {
                if (this.applyManualValue(input.value)) {
                    this.closeManualEditor();
                } else {
                    input.focus();
                    input.select();
                }
            }.bind(this);

            applyButton.addEventListener(
                'click',
                function (clickEvent) {
                    clickEvent.preventDefault();
                    clickEvent.stopPropagation();
                    commit();
                }
            );

            cancelButton.addEventListener(
                'click',
                function (clickEvent) {
                    clickEvent.preventDefault();
                    clickEvent.stopPropagation();
                    this.closeManualEditor();
                }.bind(this)
            );

            input.addEventListener(
                'keydown',
                function (keyEvent) {
                    if (keyEvent.key === 'Enter') {
                        keyEvent.preventDefault();
                        commit();
                        return;
                    }

                    if (keyEvent.key === 'Escape') {
                        keyEvent.preventDefault();
                        this.closeManualEditor();
                    }
                }.bind(this)
            );

            /*
                Клик вне окна закрывает его.
                Небольшая задержка нужна, чтобы текущий Ctrl-click
                не закрыл popup мгновенно.
            */
            setTimeout(function () {
                var closeOnOutsidePointer =
                    function (outsideEvent) {
                        if (!this.manualEditor)
                            return;

                        if (this.manualEditor.contains(
                                outsideEvent.target
                            ) ||
                            this.element.contains(
                                outsideEvent.target
                            )) {
                            return;
                        }

                        document.removeEventListener(
                            'pointerdown',
                            closeOnOutsidePointer,
                            true
                        );

                        this.closeManualEditor();
                    }.bind(this);

                document.addEventListener(
                    'pointerdown',
                    closeOnOutsidePointer,
                    true
                );
            }.bind(this), 0);

            input.focus();
            input.select();
        }

        init() {
            this.updateRotation();
            this.updateDisplay();

            this.element.addEventListener(
                'pointerdown',
                this.beginDrag.bind(this)
            );

            this.element.addEventListener(
                'pointermove',
                this.drag.bind(this)
            );

            this.element.addEventListener(
                'pointerup',
                this.endDrag.bind(this)
            );

            this.element.addEventListener(
                'pointercancel',
                this.endDrag.bind(this)
            );

            this.element.addEventListener(
                'lostpointercapture',
                this.endDrag.bind(this)
            );

            this.element.addEventListener(
                'dblclick',
                this.resetToDefault.bind(this)
            );
        }

        updateRotation() {
            var angle = -135 + this.value * 270;
            if (this.rotator) {
                this.rotator.style.transform = 'rotate(' + angle + 'deg)';
            }
        }

        updateDisplay() {
            if (!this.valueDisplay)
                return;

            var realValue =
                this.realValueForNormalised(
                    this.value
                );

            /*
                Убираем следы floating-point арифметики возле нуля.
            */
            if (Math.abs(realValue) <
                Math.pow(
                    10,
                    -Math.max(0, this.decimals)
                ) * 0.5) {
                realValue = 0;
            }

            var prefix = '';

            if (this.min < 0 &&
                this.max > 0 &&
                realValue > 0) {
                prefix = '+';
            }

            this.valueDisplay.textContent =
                prefix
                + realValue.toFixed(this.decimals)
                + (this.unit
                    ? ' ' + this.unit
                    : '');
        }

        resetToDefault(event) {
            if (event) {
                event.preventDefault();
                event.stopPropagation();
            }

            var targetValue =
                Math.max(
                    0.0,
                    Math.min(
                        1.0,
                        this.defaultValue
                    )
                );

            if (JuceBridge
                && JuceBridge.isJuceAvailable()) {
                JuceBridge.beginGesture(
                    this.paramId
                );
            }

            this.setValue(
                targetValue
            );

            if (JuceBridge
                && JuceBridge.isJuceAvailable()) {
                JuceBridge.setParameter(
                    this.paramId,
                    targetValue
                );
                JuceBridge.endGesture(
                    this.paramId
                );

            } else if (JuceBridge) {
                JuceBridge.setParameter(
                    this.paramId,
                    targetValue
                );
            }
        }

        setValue(normalizedValue) {
            this.value = Math.min(1.0, Math.max(0.0, normalizedValue));
            this.lastSentValue = this.value;
            this.updateRotation();
            this.updateDisplay();

            if (typeof this.onValueChange === 'function') {
                this.onValueChange(this.value);
            }
        }

        // =================================================================
        // Порт GradientKnob.cpp методов
        // =================================================================

        bindToParameter(paramId) {
            if (this.paramId === paramId)
                return;

            this.paramId = paramId;
            this.lastSentValue = this.value;
            this.pendingParameterValue = null;
            this.fetchInitialValue();
        }

        setGradientActive(active, capColor) {
            if (!this.allowGradientMode) {
                this.setLocked(active);
                return;
            }

            this.isGradientSelected = active;
            this.activeCapColor = active
                ? (capColor || '#b4afa0')
                : '#b4afa0';

            // Repaint pointer color
            if (this.pointer) {
                if (this.isGradientSelected) {
                    this.pointer.style.background = this.activeCapColor;
                    this.pointer.style.boxShadow  = '0 0 10px ' + this.activeCapColor;
                } else {
                    this.pointer.style.background = '';
                    this.pointer.style.boxShadow  = '';
                }
            }

            if (this.isLockedState) {
                this.element.style.filter = 'grayscale(100%) brightness(0.6)';
            } else {
                this.element.style.filter = '';
            }
        }

        setGradientMarkers(markers) {
            this.gradientMarkers = markers || [];
        }

        setLocked(locked) {
            this.isLockedState = locked;
            this.element.style.opacity = locked ? '0.4' : '1.0';
            if (this.texture) {
                this.texture.style.display = locked ? 'none' : '';
            }
        }

        setLinked(linked) {
            this.isLinkedState = linked;
            var parent = this.element.closest('.knob-group') || this.element.parentElement;
            var label = parent ? parent.querySelector('.dymo-label, .knob-label') : null;
            if (label) {
                if (linked) {
                    label.style.color = 'var(--chassis-base, #d4a446)';
                    if (!label.querySelector('.link-badge')) {
                        label.innerHTML += ' <span class="link-badge" style="font-size:9px; color:var(--led-cyan-on, #28c8d4);">[L]</span>';
                    }
                } else {
                    label.style.color = '';
                    var badge = label.querySelector('.link-badge');
                    if (badge) badge.remove();
                }
            }
        }
    };
});
