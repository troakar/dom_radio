// TROAKAR SPECTRAL - In-App Debug Console
(function (root) {
    'use strict';

    if (root.TroakarConsole)
        return;

    var MAX_LINES = 500;
    var entries = [];
    var listeners = [];
    var originalConsole = {};
    var enableDebugLogs = true;
    var isEnabled = true;

    var panel = null;
    var output = null;
    var status = null;
    var toggleButton = null;
    var initialized = false;

    function now() {
        var date = new Date();

        return date.toLocaleTimeString([], {
            hour: '2-digit',
            minute: '2-digit',
            second: '2-digit'
        }) + '.' + String(date.getMilliseconds()).padStart(3, '0');
    }

    function stringify(value) {
        if (value === undefined)
            return 'undefined';

        if (value === null)
            return 'null';

        if (typeof value === 'string')
            return value;

        if (value instanceof Error) {
            return value.stack || (
                value.name + ': ' + value.message
            );
        }

        if (typeof value === 'object') {
            try {
                return JSON.stringify(value, function (key, nestedValue) {
                    if (nestedValue instanceof Error)
                        return nestedValue.stack || nestedValue.message;

                    return nestedValue;
                }, 2);
            } catch (error) {
                return '[Object cannot be serialized]';
            }
        }

        return String(value);
    }

    function formatArguments(args) {
        var result = [];

        for (var i = 0; i < args.length; ++i)
            result.push(stringify(args[i]));

        return result.join(' ');
    }

    function notify(entry) {
        for (var i = 0; i < listeners.length; ++i) {
            try {
                listeners[i](entry);
            } catch (error) {
                // Listener errors must not break the logger.
            }
        }
    }

    function addEntry(level, args) {
        if (!isEnabled)
            return;

        if (!enableDebugLogs &&
            (level === 'debug' || level === 'log')) {
            return;
        }

        var entry = {
            time: now(),
            level: level,
            text: formatArguments(args)
        };

        entries.push(entry);

        if (entries.length > MAX_LINES)
            entries.splice(0, entries.length - MAX_LINES);

        notify(entry);
        renderEntry(entry);

        if (status)
            status.textContent =
                entries.length + ' lines';
    }

    function renderEntry(entry) {
        if (!output)
            return;

        var line = document.createElement('div');

        line.className =
            'troakar-console-line troakar-console-' +
            entry.level;

        var time = document.createElement('span');
        time.className = 'troakar-console-time';
        time.textContent = '[' + entry.time + ']';

        var level = document.createElement('span');
        level.className = 'troakar-console-level';
        level.textContent = '[' + entry.level.toUpperCase() + ']';

        var text = document.createElement('span');
        text.className = 'troakar-console-text';
        text.textContent = entry.text;

        line.appendChild(time);
        line.appendChild(level);
        line.appendChild(text);

        output.appendChild(line);

        while (output.childNodes.length > MAX_LINES)
            output.removeChild(output.firstChild);

        output.scrollTop = output.scrollHeight;
    }

    function clear() {
        entries.length = 0;

        if (output)
            output.innerHTML = '';

        if (status)
            status.textContent = '0 lines';
    }

    function copy() {
        var text = entries.map(function (entry) {
            return '[' + entry.time + '] '
                + '[' + entry.level.toUpperCase() + '] '
                + entry.text;
        }).join('\n');

        if (navigator.clipboard &&
            typeof navigator.clipboard.writeText === 'function') {
            navigator.clipboard.writeText(text)
                .then(function () {
                    addEntry('info', ['Log copied to clipboard']);
                })
                .catch(function (error) {
                    addEntry('error', [
                        'Clipboard error:',
                        error
                    ]);
                });
        } else {
            var textarea = document.createElement('textarea');
            textarea.value = text;
            textarea.style.position = 'fixed';
            textarea.style.left = '-9999px';

            document.body.appendChild(textarea);
            textarea.select();

            try {
                document.execCommand('copy');
                addEntry('info', ['Log copied to clipboard']);
            } catch (error) {
                addEntry('error', [
                    'Clipboard is unavailable:',
                    error
                ]);
            }

            textarea.remove();
        }
    }

    function setVisible(visible) {
        if (!panel)
            return;

        panel.classList.toggle(
            'troakar-console-hidden',
            !visible
        );

        if (toggleButton)
            toggleButton.classList.toggle(
                'troakar-console-hidden',
                visible
            );
    }

    function toggle() {
        if (!panel)
            return;

        setVisible(
            panel.classList.contains(
                'troakar-console-hidden'
            )
        );
    }

    function createStyles() {
        if (document.getElementById('troakar-console-styles'))
            return;

        var style = document.createElement('style');
        style.id = 'troakar-console-styles';

        style.textContent = [
            '#troakar-debug-console {',
            '    position: fixed;',
            '    z-index: 2147483647;',
            '    left: 12px;',
            '    right: 12px;',
            '    bottom: 12px;',
            '    height: 230px;',
            '    display: flex;',
            '    flex-direction: column;',
            '    background: rgba(8, 7, 6, 0.97);',
            '    border: 1px solid #d4a446;',
            '    box-shadow: 0 0 0 1px #21190c, 0 0 18px rgba(212, 164, 70, 0.35);',
            '    color: #e8e4d9;',
            '    font-family: Consolas, "Courier New", monospace;',
            '    font-size: 12px;',
            '}',
            '',
            '#troakar-debug-console.troakar-console-hidden {',
            '    display: none;',
            '}',
            '',
            '.troakar-console-header {',
            '    flex: 0 0 30px;',
            '    display: flex;',
            '    align-items: center;',
            '    gap: 7px;',
            '    padding: 0 8px;',
            '    background: #1a1816;',
            '    border-bottom: 1px solid #6d5220;',
            '    user-select: none;',
            '}',
            '',
            '.troakar-console-title {',
            '    flex: 1;',
            '    color: #f0c872;',
            '    font-weight: bold;',
            '    letter-spacing: 1px;',
            '}',
            '',
            '.troakar-console-status {',
            '    color: #94836a;',
            '    font-size: 10px;',
            '}',
            '',
            '.troakar-console-button {',
            '    padding: 3px 8px;',
            '    border: 1px solid #765a25;',
            '    background: #29231b;',
            '    color: #e8c875;',
            '    cursor: pointer;',
            '    font: inherit;',
            '}',
            '',
            '.troakar-console-button:hover {',
            '    background: #4a3820;',
            '}',
            '',
            '.troakar-console-output {',
            '    flex: 1;',
            '    overflow-y: auto;',
            '    padding: 6px 8px;',
            '    white-space: pre-wrap;',
            '    word-break: break-word;',
            '}',
            '',
            '.troakar-console-line {',
            '    display: grid;',
            '    grid-template-columns: 86px 62px minmax(0, 1fr);',
            '    gap: 6px;',
            '    margin-bottom: 2px;',
            '    line-height: 1.35;',
            '}',
            '',
            '.troakar-console-time {',
            '    color: #756d5d;',
            '}',
            '',
            '.troakar-console-level {',
            '    color: #d4a446;',
            '}',
            '',
            '.troakar-console-text {',
            '    color: #e8e4d9;',
            '}',
            '',
            '.troakar-console-warn .troakar-console-level,',
            '.troakar-console-warn .troakar-console-text {',
            '    color: #f0b84a;',
            '}',
            '',
            '.troakar-console-error .troakar-console-level,',
            '.troakar-console-error .troakar-console-text {',
            '    color: #ff6868;',
            '}',
            '',
            '.troakar-console-debug .troakar-console-level,',
            '.troakar-console-debug .troakar-console-text {',
            '    color: #7ed7e8;',
            '}',
            '',
            '.troakar-console-info .troakar-console-level {',
            '    color: #8fcf8f;',
            '}',
            '',
            '#troakar-console-toggle {',
            '    position: fixed;',
            '    z-index: 2147483646;',
            '    right: 12px;',
            '    bottom: 12px;',
            '    padding: 6px 10px;',
            '    border: 1px solid #d4a446;',
            '    background: #17130e;',
            '    color: #f0c872;',
            '    cursor: pointer;',
            '    font: 11px Consolas, monospace;',
            '}',
            '',
            '#troakar-console-toggle.troakar-console-hidden {',
            '    display: none;',
            '}'
        ].join('\n');

        document.head.appendChild(style);
    }

    function createUI() {
        if (initialized || !document.body)
            return;

        initialized = true;
        createStyles();

        panel = document.createElement('section');
        panel.id = 'troakar-debug-console';

        var header = document.createElement('div');
        header.className = 'troakar-console-header';

        var title = document.createElement('span');
        title.className = 'troakar-console-title';
        title.textContent = 'TROAKAR DEBUG CONSOLE';

        status = document.createElement('span');
        status.className = 'troakar-console-status';
        status.textContent = entries.length + ' lines';

        var clearButton = document.createElement('button');
        clearButton.className = 'troakar-console-button';
        clearButton.textContent = 'CLEAR';
        clearButton.onclick = clear;

        var copyButton = document.createElement('button');
        copyButton.className = 'troakar-console-button';
        copyButton.textContent = 'COPY';
        copyButton.onclick = copy;

        var hideButton = document.createElement('button');
        hideButton.className = 'troakar-console-button';
        hideButton.textContent = 'HIDE';
        hideButton.onclick = function () {
            setVisible(false);
        };

        header.appendChild(title);
        header.appendChild(status);
        header.appendChild(clearButton);
        header.appendChild(copyButton);
        header.appendChild(hideButton);

        output = document.createElement('div');
        output.className = 'troakar-console-output';

        panel.appendChild(header);
        panel.appendChild(output);

        toggleButton = document.createElement('button');
        toggleButton.id = 'troakar-console-toggle';
        toggleButton.textContent = 'DEBUG';
        toggleButton.onclick = function () {
            setVisible(true);
        };

        document.body.appendChild(panel);
        document.body.appendChild(toggleButton);

        // Re-render entries recorded before DOM was ready.
        var oldEntries = entries.slice();

        output.innerHTML = '';

        for (var i = 0; i < oldEntries.length; ++i)
            renderEntry(oldEntries[i]);

        addEntry('info', ['In-app console initialized']);
    }

    function installConsoleHook() {
        var levels = [
            'log',
            'info',
            'warn',
            'error',
            'debug'
        ];

        levels.forEach(function (level) {
            originalConsole[level] =
                console[level].bind(console);

            console[level] = function () {
                var args = Array.prototype.slice.call(arguments);

                addEntry(level, args);

                try {
                    originalConsole[level].apply(
                        console,
                        args
                    );
                } catch (error) {}
            };
        });
    }

    function installErrorHooks() {
        root.addEventListener('error', function (event) {
            addEntry('error', [
                'Uncaught error:',
                event.message || event.error,
                event.filename
                    ? event.filename + ':' +
                      event.lineno + ':' +
                      event.colno
                    : ''
            ]);
        });

        root.addEventListener(
            'unhandledrejection',
            function (event) {
                addEntry('error', [
                    'Unhandled Promise rejection:',
                    event.reason
                ]);
            }
        );
    }

    function installKeyboardShortcut() {
        root.addEventListener('keydown', function (event) {
            if (event.ctrlKey &&
                event.altKey &&
                event.key.toLowerCase() === 'd') {
                event.preventDefault();

                if (!panel)
                    createUI();

                toggle();
            }
        });
    }

    installConsoleHook();
    installErrorHooks();
    installKeyboardShortcut();

    root.TroakarConsole = {
        init: createUI,

        setDebugEnabled: function (enabled) {
            enableDebugLogs = !!enabled;
        },

        setEnabled: function (enabled) {
            isEnabled = !!enabled;
        },

        log: function () {
            addEntry(
                'log',
                Array.prototype.slice.call(arguments)
            );
        },

        info: function () {
            addEntry(
                'info',
                Array.prototype.slice.call(arguments)
            );
        },

        warn: function () {
            addEntry(
                'warn',
                Array.prototype.slice.call(arguments)
            );
        },

        error: function () {
            addEntry(
                'error',
                Array.prototype.slice.call(arguments)
            );
        },

        debug: function () {
            addEntry(
                'debug',
                Array.prototype.slice.call(arguments)
            );
        },

        clear: clear,

        show: function () {
            createUI();
            setVisible(true);
        },

        hide: function () {
            createUI();
            setVisible(false);
        },

        toggle: function () {
            createUI();
            toggle();
        },

        getEntries: function () {
            return entries.slice();
        },

        addListener: function (callback) {
            if (typeof callback !== 'function')
                return function () {};

            listeners.push(callback);

            return function () {
                var index = listeners.indexOf(callback);

                if (index >= 0)
                    listeners.splice(index, 1);
            };
        }
    };

    if (document.readyState === 'loading') {
        document.addEventListener(
            'DOMContentLoaded',
            createUI,
            { once: true }
        );
    } else {
        createUI();
    }
})(typeof window !== 'undefined' ? window : this);
