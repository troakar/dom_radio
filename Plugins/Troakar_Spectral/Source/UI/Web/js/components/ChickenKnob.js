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
            this.type       = (options.type !== undefined)    ? options.type    : (this.config.type    || 'medium');

            // Normalized value 0..1
            this.value = (options.defaultValue !== undefined)
                ? options.defaultValue
                : (this.config.default !== undefined ? this.config.default : 0.5);

            this.valueDisplay = document.getElementById(elementId + '-value');

            this.isDragging          = false;
            this.dragStartY          = 0;
            this.dragStartValue      = 0;
            this.dragPointerId       = null;
            this.dragScale           = 1.0;
            this.lastSentValue       = this.value;
            this.pendingParameterValue = null;
            this.parameterFramePending  = false;

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
            var radius = this.type === 'big' ? 44 : (this.type === 'medium' ? 32 : 24);

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

            if (this.type === 'medium') {
                innerHTML =
                    '<div class="knob-shadow"></div>' +
                    '<div class="knob-rotator">' +
                        '<div class="knob-texture ribbed"></div>' +
                        '<div class="knob-pointer white-line"></div>' +
                    '</div>';
            } else if (this.type === 'big') {
                var pathD = '';
                for (var j = 0; j <= 360; j += 2) {
                    var a  = j * Math.PI / 180;
                    var r  = 44 + Math.sin(a * 8) * 2.5;
                    var px = 50 + r * Math.cos(a);
                    var py = 50 + r * Math.sin(a);
                    pathD += (j === 0 ? 'M ' + px.toFixed(2) + ' ' + py.toFixed(2)
                                      : ' L ' + px.toFixed(2) + ' ' + py.toFixed(2));
                }
                pathD += ' Z';

                innerHTML =
                    '<div class="knob-shadow big-shadow"></div>' +
                    '<div class="knob-rotator">' +
                        '<svg viewBox="0 0 100 100" class="scalloped-bg">' +
                            '<path d="' + pathD + '" fill="var(--knob-base)" stroke="var(--panel-border)" stroke-width="1.5"/>' +
                        '</svg>' +
                        '<div class="knob-center-dome"></div>' +
                        '<div class="knob-pointer white-line big-line"></div>' +
                    '</div>';
            } else {
                innerHTML =
                    '<div class="knob-shadow"></div>' +
                    '<div class="knob-base metal-cone"></div>' +
                    '<div class="knob-rotator">' +
                        '<div class="knob-pointer black-line"></div>' +
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

            this.isDragging      = true;
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

            var sensitivity = 180.0;

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
        }

        updateRotation() {
            var angle = -135 + this.value * 270;
            if (this.rotator) {
                this.rotator.style.transform = 'rotate(' + angle + 'deg)';
            }
        }

        updateDisplay() {
            if (this.valueDisplay) {
                var realVal    = this.min + this.value * (this.max - this.min);
                this.valueDisplay.textContent =
                    realVal.toFixed(this.decimals) + (this.unit ? ' ' + this.unit : '');
            }
        }

        setValue(normalizedValue) {
            this.value = Math.min(1.0, Math.max(0.0, normalizedValue));
            this.updateRotation();
            this.updateDisplay();
        }

        // =================================================================
        // Порт GradientKnob.cpp методов
        // =================================================================

        bindToParameter(paramId) {
            if (this.paramId === paramId)
                return;

            this.paramId = paramId;
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
            var label = this.element.nextElementSibling;
            if (label && label.classList.contains('dymo-label')) {
                if (linked) {
                    label.style.color = 'var(--chassis-base, #d4a446)';
                    label.innerHTML += ' <span style="font-size:9px;">L</span>';
                } else {
                    label.style.color = '';
                    label.innerHTML = label.innerHTML.replace(/ <span[^>]*>L<\/span>/, '');
                }
            }
        }
    };
});
