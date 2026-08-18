// TROAKAR SPECTRAL - JUCE Bridge
// Uses low-level JUCE WebView2 native integration API.
// The backend object provides addEventListener/emitEvent,
// but getNativeFunction must be implemented locally since
// we do not import the juce-framework-frontend npm module.
(function (root, factory) {
    if (typeof module !== 'undefined' && module.exports) {
        module.exports = factory(root);
    } else {
        root.JuceBridge = factory(root);
    }
})(typeof window !== 'undefined' ? window : this, function (root) {
    'use strict';

    function getBackend() {
        if (!root.__JUCE__)
            return null;

        if (!root.__JUCE__.backend)
            return null;

        return root.__JUCE__.backend;
    }

    function getInitialisationData() {
        if (!root.__JUCE__)
            return null;

        return root.__JUCE__.initialisationData || null;
    }

    /*
        JUCE registers native function names in:
            window.__JUCE__.initialisationData.__juce__functions

        The backend object itself does NOT have a
        getNativeFunction method. We implement it here
        following the official JUCE frontend helper pattern:

        1. Check the function name is registered
        2. Create a Promise
        3. Emit "__juce__invoke" via backend.emitEvent
        4. C++ calls complete() which triggers
           "__juce__complete" event
        5. Promise resolves with the result
    */
    var promiseId = 0;
    var pendingPromises = new Map();
    var nativeFunctionCache = new Map();

    function getNativeFunction(name) {
        if (nativeFunctionCache.has(name))
            return nativeFunctionCache.get(name);

        var backend = getBackend();

        if (!backend ||
            typeof backend.emitEvent !== 'function') {
            if (typeof TroakarConsole !== 'undefined')
                TroakarConsole.error(
                    'Backend emitEvent unavailable for:',
                    name
                );

            return null;
        }

        var initData = getInitialisationData();

        if (initData &&
            Array.isArray(initData.__juce__functions) &&
            initData.__juce__functions.indexOf(name) === -1) {
            if (typeof TroakarConsole !== 'undefined')
                TroakarConsole.warn(
                    'Native function not registered on backend:',
                    name
                );
        }

        var fn = function () {
            var promiseIdValue = promiseId++;
            var args = Array.prototype.slice.call(arguments);

            var promise = new Promise(function (resolve, reject) {
                pendingPromises.set(promiseIdValue, {
                    resolve: resolve,
                    reject: reject
                });
            });

            try {
                backend.emitEvent(
                    '__juce__invoke',
                    {
                        name: name,
                        params: args,
                        resultId: promiseIdValue
                    }
                );
            } catch (error) {
                pendingPromises.delete(promiseIdValue);

                if (typeof TroakarConsole !== 'undefined')
                    TroakarConsole.error(
                        'emitEvent failed:',
                        name,
                        error
                    );

                return Promise.reject(error);
            }

            // Timeout safeguard
            setTimeout(function () {
                if (pendingPromises.has(promiseIdValue)) {
                    pendingPromises.delete(promiseIdValue);
                    reject(new Error(
                        'Native function timed out: ' + name
                    ));
                }
            }, 5000);

            return promise;
        };

        nativeFunctionCache.set(name, fn);
        return fn;
    }

    // Listen for "__juce__complete" events that resolve Promises
    var backend = getBackend();

    if (backend && typeof backend.addEventListener === 'function') {
        backend.addEventListener(
            '__juce__complete',
            function (data) {
                if (!data ||
                    data.promiseId === undefined)
                    return;

                var pending = pendingPromises.get(data.promiseId);

                if (!pending)
                    return;

                pendingPromises.delete(data.promiseId);

                if (typeof TroakarConsole !== 'undefined')
                    TroakarConsole.debug(
                        'Native call completed:',
                        data.promiseId
                    );

                pending.resolve(data.result);
            }
        );
    }

    const parameterListeners = new Map();

    /*
        Synchronous cache of last-known parameter values.
        Updated by paramUpdate events and by getParameter Promise results.
        Allows GradientManager.syncFromJuce() to read values without await.
    */
    const paramCache = new Map();

    const cachedAnalysisData = {
        spectrum: [],
        sidechain: [],
        delta: [],
        detector: [],
        effectiveTarget: [],

        rms: 0.0,
        peak: 0.0,

        fftSize: 512,
        numBins: 257,
        sampleRate: 44100.0,

        spectrumFormat: 'linearMagnitude',
        sidechainFormat: 'linearMagnitude',
        deltaFormat: 'decibels',
        detectorFormat: 'decibels',
        effectiveTargetFormat: 'decibels',
        hasAnalysis: false
    };

    function dispatchParameterUpdate(data) {
        if (typeof TroakarConsole !== 'undefined')
            TroakarConsole.debug(
                'paramUpdate:',
                data
            );

        if (!data || !data.id)
            return;

        var id = String(data.id);
        var value = Number(data.value);

        paramCache.set(id, Number.isFinite(value) ? value : 0.0);

        var listeners = parameterListeners.get(id);

        if (!listeners)
            return;

        for (var i = 0; i < listeners.length; ++i) {
            try {
                listeners[i](Number.isFinite(value) ? value : 0.0);
            } catch (error) {
                console.error(
                    '[JuceBridge] paramUpdate listener failed:',
                    id,
                    error
                );

                if (typeof TroakarConsole !== 'undefined')
                    TroakarConsole.error(
                        'paramUpdate listener failed:',
                        id,
                        error
                    );
            }
        }
    }

    function updateCachedAnalysis(data) {
        if (!data)
            return;

        cachedAnalysisData.spectrum =
            data.spectrum || [];

        cachedAnalysisData.sidechain =
            data.sidechain || [];

        cachedAnalysisData.delta =
            data.delta || [];

        cachedAnalysisData.detector =
            data.detector || [];

        cachedAnalysisData.effectiveTarget =
            data.effectiveTarget || [];

        cachedAnalysisData.rms =
            Number(data.rms) || 0.0;

        cachedAnalysisData.peak =
            Number(data.peak) || 0.0;

        cachedAnalysisData.fftSize =
            Number(data.fftSize) || 512;

        cachedAnalysisData.numBins =
            Number(data.numBins)
                || cachedAnalysisData.spectrum.length;

        cachedAnalysisData.sampleRate =
            Number(data.sampleRate) || 44100.0;

        cachedAnalysisData.spectrumFormat =
            data.spectrumFormat
                || 'linearMagnitude';

        cachedAnalysisData.sidechainFormat =
            data.sidechainFormat
                || cachedAnalysisData.spectrumFormat;

        cachedAnalysisData.deltaFormat =
            data.deltaFormat
                || 'decibels';

        cachedAnalysisData.detectorFormat =
            data.detectorFormat
                || 'decibels';

        cachedAnalysisData.effectiveTargetFormat =
            data.effectiveTargetFormat
                || 'decibels';

        cachedAnalysisData.hasAnalysis =
            data.hasAnalysis === true;
    }

    if (typeof TroakarConsole !== 'undefined')
        TroakarConsole.info(
            'JUCE backend:',
            backend ? 'found' : 'missing'
        );

    if (backend) {
        var keys = Object.keys(backend);

        if (typeof TroakarConsole !== 'undefined')
            TroakarConsole.info(
                'backend keys:',
                keys.join(', ')
            );

        // Check if JUCE native functions are registered
        var initData = getInitialisationData();

        if (initData &&
            Array.isArray(initData.__juce__functions)) {
            if (typeof TroakarConsole !== 'undefined')
                TroakarConsole.info(
                    'registered native functions:',
                    initData.__juce__functions.join(', ')
                );
        } else {
            if (typeof TroakarConsole !== 'undefined')
                TroakarConsole.warn(
                    '__juce__functions not in init data'
                );
        }
    }

    /*
        C++ -> JS events:
            webView->emitEventIfBrowserIsVisible("paramUpdate", ...)
            webView->emitEventIfBrowserIsVisible("analysisData", ...)
    */
    if (backend && typeof backend.addEventListener === 'function') {
        backend.addEventListener(
            'paramUpdate',
            dispatchParameterUpdate
        );

        backend.addEventListener(
            'analysisData',
            updateCachedAnalysis
        );

        if (typeof TroakarConsole !== 'undefined')
            TroakarConsole.info(
                'Native integration connected'
            );

        console.log(
            '[JuceBridge] Native integration connected.'
        );
    } else {
        if (typeof TroakarConsole !== 'undefined')
            TroakarConsole.warn(
                '__JUCE__.backend or addEventListener unavailable'
            );

        console.warn(
            '[JuceBridge] __JUCE__.backend or addEventListener unavailable.'
        );
    }

    return class JuceBridge {
        static isJuceAvailable() {
            return getBackend() !== null;
        }

        static setParameter(paramId, normalizedValue) {
            const fn = getNativeFunction('paramChange');

            const id = String(paramId);
            const value = Math.max(
                0.0,
                Math.min(1.0, Number(normalizedValue) || 0.0)
            );

            if (typeof TroakarConsole !== 'undefined')
                TroakarConsole.debug(
                    'setParameter:',
                    id,
                    value
                );

            if (!fn) {
                if (typeof TroakarConsole !== 'undefined')
                    TroakarConsole.warn(
                        'Native function paramChange unavailable'
                    );

                console.warn(
                    '[JuceBridge] Cannot set parameter:',
                    id,
                    value
                );

                return Promise.resolve();
            }

            paramCache.set(id, value);

            try {
                return Promise.resolve(
                    fn(id, value)
                );
            } catch (error) {
                console.error(
                    '[JuceBridge] paramChange failed:',
                    id,
                    error
                );

                if (typeof TroakarConsole !== 'undefined')
                    TroakarConsole.error(
                        'paramChange failed:',
                        id,
                        error
                    );

                return Promise.reject(error);
            }
        }

        static getParameter(paramId) {
            const id = String(paramId);

            if (typeof TroakarConsole !== 'undefined')
                TroakarConsole.debug(
                    'getParameter:',
                    id
                );

            /*
                Return cached value synchronously if available.
                This supports GradientManager.syncFromJuce()
                which expects a number, not a Promise.
            */
            if (paramCache.has(id)) {
                const cached = paramCache.get(id);

                if (typeof TroakarConsole !== 'undefined')
                    TroakarConsole.debug(
                        'getParameter cached:',
                        id,
                        cached
                    );

                return cached;
            }

            const fn = getNativeFunction('getParameter');

            if (!fn) {
                if (typeof TroakarConsole !== 'undefined')
                    TroakarConsole.warn(
                        'Native function getParameter unavailable:',
                        id
                    );

                console.warn(
                    '[JuceBridge] Cannot read parameter:',
                    id
                );

                return Promise.resolve(0.0);
            }

            try {
                return Promise.resolve(
                    fn(id)
                ).then(function (value) {
                    const number = Number(value);
                    const safeValue =
                        Number.isFinite(number)
                            ? number
                            : 0.0;

                    paramCache.set(id, safeValue);

                    if (typeof TroakarConsole !== 'undefined')
                        TroakarConsole.debug(
                            'getParameter resolved:',
                            id,
                            safeValue
                        );

                    return safeValue;
                });
            } catch (error) {
                console.error(
                    '[JuceBridge] getParameter failed:',
                    id,
                    error
                );

                if (typeof TroakarConsole !== 'undefined')
                    TroakarConsole.error(
                        'getParameter failed:',
                        id,
                        error
                    );

                return Promise.resolve(0.0);
            }
        }

        static onParamUpdate(paramId, callback) {
            const id = String(paramId);

            if (!parameterListeners.has(id))
                parameterListeners.set(id, []);

            parameterListeners.get(id).push(callback);

            return function () {
                var list = parameterListeners.get(id);

                if (list) {
                    var index = list.indexOf(callback);

                    if (index >= 0)
                        list.splice(index, 1);

                    if (list.length === 0)
                        parameterListeners.delete(id);
                }
            };
        }

        static beginGesture(paramId) {
            const id = String(paramId);
            const fn = getNativeFunction('beginGesture');

            if (typeof TroakarConsole !== 'undefined')
                TroakarConsole.debug('beginGesture:', id);

            if (fn) {
                try {
                    fn(id);
                } catch (error) {
                    if (typeof TroakarConsole !== 'undefined')
                        TroakarConsole.error(
                            'beginGesture failed:',
                            id,
                            error
                        );
                }
            }
        }

        static endGesture(paramId) {
            const id = String(paramId);
            const fn = getNativeFunction('endGesture');

            if (typeof TroakarConsole !== 'undefined')
                TroakarConsole.debug('endGesture:', id);

            if (fn) {
                try {
                    fn(id);
                } catch (error) {
                    if (typeof TroakarConsole !== 'undefined')
                        TroakarConsole.error(
                            'endGesture failed:',
                            id,
                            error
                        );
                }
            }
        }

        static setLogicalSize(w, h) {
            const fn = getNativeFunction('setLogicalSize');
            if (fn) {
                try {
                    fn(w, h);
                } catch (error) {
                    console.warn('[JuceBridge] setLogicalSize failed', error);
                }
            }
        }

        static onAnalysisReady(callback) {
            const currentBackend = getBackend();

            if (!currentBackend ||
                typeof currentBackend.addEventListener !== 'function') {
                return function () {};
            }

            const listener = function (data) {
                updateCachedAnalysis(data);
                callback(cachedAnalysisData);
            };

            const token = currentBackend.addEventListener(
                'analysisData',
                listener
            );

            return function () {
                if (typeof currentBackend.removeEventListener === 'function')
                    currentBackend.removeEventListener(token);
            };
        }

        /*
            Batched parameter updates for high-frequency drag.
            Only sends at most one native call per animation frame.
            Used by SpectrumScreen and ChickenKnob to prevent
            flooding the WebView2 message loop during drag.
        */
        static pendingParams = new Map();
        static pendingFrameId = null;

        static queueParameterChange(paramId, normalizedValue) {
            const id = String(paramId);

            JuceBridge.pendingParams.set(
                id,
                Math.max(
                    0.0,
                    Math.min(1.0, Number(normalizedValue) || 0.0)
                )
            );

            if (JuceBridge.pendingFrameId !== null)
                return;

            JuceBridge.pendingFrameId =
                requestAnimationFrame(function () {
                    var snapshot = [];

                    JuceBridge.pendingParams.forEach(
                        function (value, key) {
                            snapshot.push({
                                id: key,
                                value: value
                            });
                        }
                    );

                    JuceBridge.pendingParams.clear();
                    JuceBridge.pendingFrameId = null;

                    for (var i = 0;
                         i < snapshot.length;
                         ++i) {
                        JuceBridge.setParameter(
                            snapshot[i].id,
                            snapshot[i].value
                        );
                    }
                });
        }

        static getAnalysisData() {
            return cachedAnalysisData;
        }
    };
});
