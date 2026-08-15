// TROAKAR SPECTRAL - Backlit Button Component
(function (root, factory) {
    if (typeof module !== 'undefined' && module.exports) {
        module.exports = factory(root.JuceBridge);
    } else {
        root.BacklitButton = factory(root.JuceBridge);
    }
})(typeof window !== 'undefined' ? window : this, function (JuceBridge) {
    return class BacklitButton {
        constructor(elementId, paramId, initialState, colorClass) {
            this.element = document.getElementById(elementId);
            this.paramId = paramId;
            this.state = initialState !== undefined ? initialState : false;
            this.colorClass = colorClass || 'amber';
            this.removeParameterListener = null;

            if (!this.element.classList.contains('backlit-button')) {
                this.element.classList.add('backlit-button', this.colorClass);
            }

            this.init();
            this.fetchInitialState();
        }

        fetchInitialState() {
            if (this.removeParameterListener) {
                this.removeParameterListener();
                this.removeParameterListener = null;
            }

            if (JuceBridge) {
                const valPromise = JuceBridge.getParameter(this.paramId);
                if (valPromise && typeof valPromise.then === 'function') {
                    valPromise.then((val) => {
                        if (val !== null && val !== undefined) {
                            this.state = Number(val) >= 0.5;
                            this.render();
                        }
                    });
                }
            }
            if (JuceBridge && JuceBridge.isJuceAvailable()) {
                this.removeParameterListener = JuceBridge.onParamUpdate(this.paramId, (val) => {
                    this.state = Number(val) >= 0.5;
                    this.render();
                });
            }
        }

        init() {
            var self = this;

            this.element.addEventListener('click', function () {
                self.state = !self.state;
                self.render();
                if (JuceBridge) JuceBridge.setParameter(self.paramId, self.state ? 1.0 : 0.0);
            });

            this.element.addEventListener('mousedown', function () {
                self.element.classList.add('pressed');
            });

            document.addEventListener('mouseup', function () {
                self.element.classList.remove('pressed');
            });

            this.render();
        }

        render() {
            if (this.state) {
                this.element.classList.add('active');
            } else {
                this.element.classList.remove('active');
            }
        }

        setState(state) {
            this.state = state;
            this.render();
        }

        getState() {
            return this.state;
        }
    };
});
