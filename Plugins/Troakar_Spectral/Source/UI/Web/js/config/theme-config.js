// TROAKAR SPECTRAL - Manley-style Theme Configuration
// Centralized color schema preserving mustard/gold accents with military aesthetic
(function (root, factory) {
    if (typeof module !== 'undefined' && module.exports) {
        module.exports = factory();
    } else {
        root.ThemeConfig = factory();
    }
})(typeof window !== 'undefined' ? window : this, function () {
    var ThemeConfig = {
        // Chassis colors (mustard/gold)
        chassis: {
            base: '#d4a446',
            dark: '#b8923d',
            light: '#e8b95c',
            accent: '#f0c872'
        },

        // Panel backgrounds and borders (darker with more contrast)
        panel: {
            background: '#1a1816',
            border: '#0a0908',
            borderThick: '#000000',
            divider: '#3a3430',
            sectionBg: '#0e0c0a',
            sectionBorder: '#d4a446'
        },

        // Typography and text
        text: {
            primary: '#e8e4d9',
            secondary: '#b8a88c',
            label: '#d4a446',
            value: '#f0c872',
            engraved: '#0a0908',
            shadow: 'rgba(244, 196, 114, 0.3)'
        },

        // Knob styling
        knob: {
            body: '#2a2622',
            bodyHighlight: '#3e3832',
            skirt: '#1a1614',
            pointer: '#f4c472',
            pointerGlow: 'rgba(244, 196, 114, 0.8)',
            tick: '#d4a446',
            tickMinor: '#6e5d4a',
            center: '#1a1614',
            shadow: 'rgba(0, 0, 0, 0.9)'
        },

        // LED / backlit button colors
        led: {
            red: {
                on: '#ff3b30',
                glow: 'rgba(255, 59, 48, 0.8)',
                shadow: 'rgba(255, 59, 48, 0.4)'
            },
            amber: {
                on: '#f09511',
                glow: 'rgba(240, 149, 17, 0.8)',
                shadow: 'rgba(240, 149, 17, 0.4)'
            },
            cyan: {
                on: '#28c8d4',
                glow: 'rgba(40, 200, 212, 0.8)',
                shadow: 'rgba(40, 200, 212, 0.4)'
            }
        },

        // CRT screen colors
        screen: {
            background: '#0d0b08',
            bezel: '#050505',
            glare: 'rgba(255, 255, 255, 0.08)',
            phosphor: '#f4c472',
            phosphorDim: '#b8923d',
            grid: 'rgba(212, 164, 70, 0.15)'
        },

        // Sizes and borders
    sizes: {
            borderThin: '1px',
            borderMedium: '2px',
            borderThick: '3px',
            borderExtraThick: '4px',

            fontSmall: '11px',
            fontMedium: '13px',
            fontLarge: '15px',
            fontXLarge: '19px',
            fontTitle: '24px',

            knobSmall: '52px',
            knobMedium: '72px',
            knobBig: '92px',

            chassisWidth: '1300px',
            chassisHeight: '780px',

            buttonPadding: '10px 18px',
            buttonPaddingSmall: '8px 14px'
        },

        fonts: {
            technical: "'GOST Type B', 'Courier New', monospace",
            display: "'Russo One', 'Arial Black', 'Impact', sans-serif",
            primary: "'GOST Type B', 'Helvetica Neue', 'Arial', sans-serif",
            mono: "'GOST Type B', 'Courier New', monospace"
        },

        knobConfigs: {
            'knob-in':          { min: -24, max: 24,   default: 0.5,  unit: 'dB', decimals: 1, type: 'medium', paramId: 'IN_GAIN' },
            'knob-out':         { min: -24, max: 24,   default: 0.5,  unit: 'dB', decimals: 1, type: 'medium', paramId: 'OUT_LVL' },
            'knob-mix':         { min: 0,   max: 100,  default: 1.0,  unit: '%',  decimals: 0, type: 'medium', paramId: 'MIX' },

            'knob-amount':      { min: 0,   max: 300,  skew: 0.65, default: Math.pow(100/300, 0.65), unit: '%',  decimals: 0, type: 'big',    paramId: 'AMOUNT' },

            'knob-up-range':    { min: 0,   max: 48,   skew: 0.70, default: Math.pow(4/48, 0.70),   unit: 'dB', decimals: 1, type: 'big',    paramId: 'UPWARD_RANGE' },
            'knob-up-sel':      { min: -100, max: 100, default: 0.5, unit: '%',  decimals: 0, type: 'medium', paramId: 'UP_SEL' },
            'knob-up-smooth':   { min: 0,   max: 100,  default: 0.5, unit: '%',  decimals: 0, type: 'medium', paramId: 'UP_SMOOTH' },

            'knob-down-range':  { min: 0,   max: 24,   default: 0.5, unit: 'dB', decimals: 1, type: 'big',    paramId: 'DOWNWARD_RANGE' },
            'knob-down-sel':    { min: -100, max: 100, default: 0.5, unit: '%',  decimals: 0, type: 'medium', paramId: 'DOWN_SEL' },
            'knob-down-smooth': { min: 0,   max: 100,  default: 0.15, unit: '%', decimals: 0, type: 'medium', paramId: 'DOWN_SMOOTH' },

            'knob-speed':       { min: 0,   max: 100,  default: 0.5, unit: '%',  decimals: 0, type: 'big',    paramId: 'SPECTRAL_SPEED' },
            'knob-attack':      { min: 0.1, max: 100,  skew: 0.3, default: Math.pow(5/100, 0.3),  unit: 'ms', decimals: 1, type: 'medium', paramId: 'ATTACK_MS' },
            'knob-release':     { min: 10,  max: 1000, skew: 0.3, default: Math.pow(150/1000, 0.3), unit: 'ms', decimals: 0, type: 'medium', paramId: 'RELEASE_MS' },
            'knob-knee':        { min: 0,   max: 12,   default: 0.25, unit: 'dB', decimals: 1, type: 'medium', paramId: 'KNEE_WIDTH' },

            'knob-lookahead':   { min: 0,   max: 10,   default: 0.5, unit: 'ms', decimals: 1, type: 'big',    paramId: 'LOOKAHEAD_MS' }
        }
    };

    return ThemeConfig;
});
