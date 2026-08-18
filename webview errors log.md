## Goal
- Fix the broken bridge crash (`backend.getNativeFunction is not a function`) causing all knobs after `in-gain` to not create, and also restore the fixed 1300×780 layout that was broken by the previous adaptive CSS.

## Constraints & Preferences
- Fixed-layout only: no `reflow` (`width:100%/height:100%/min-height:0/clamp/flex:1`).
- JUCE editor must keep 5:3 aspect ratio (1300:780).
- Chassis scaled as a unit via CSS `transform: scale()` from JS — no per-widget canvas resize.
- No `mainScreen.resize()` on every chassis resize.
- JUCE `setSizeLimits` must be multiples of 75%/150% of 1300×780.

## Progress
### Done
- **CSS (`base.css`)**: Removed all adaptive rules (`min-height:0`, `min-width:0`, `flex:1`, `clamp`, `width:100%`/`height:100%` on `.crt-screen`/`.timing-container`/etc). Added fixed 1300×780 block at end with `!important` on `.device-chassis`, `flex-shrink:0` on all panels, body reset to `display:block`.
- **CSS (`crt-screen.css`)**: Removed `width:100%/height:100%` from `.crt-screen` and canvas; `.crt-screen` now has fixed `width:1110px; height:438px`.
- **`app.js`**: Added JS error overlay (`window.error` + `unhandledrejection`). Added `createKnob()` helper with try/catch logging. Replaced all 16 `knobs.set(...)` with `createKnob(...)`. Replaced `resizeInterface()` with `resizeFixedChassis()` using `clientWidth/clientHeight`, removed `mainScreen.resize()`, added `ResizeObserver`.
- **`ChickenKnob.js`**: Added element-not-found guard after `getElementById`. Added rotator/pointer/texture validation after `buildDOM()`. Rewrote `fetchInitialValue()` with try/catch, `Promise.resolve().then()`, `.catch()`, value clamping.
- **JUCE C++**: `PluginEditor.cpp` already had `setResizeLimits(975,585,1950,1170)`, `setFixedAspectRatio(1300/780)`, proper `setSize()`. `PluginProcessor.setEditorSize()` already computes height from width. No C++ changes needed.

### In Progress
- **Root cause fix**: Diagnostics show `backend.getNativeFunction is not a function`. The bridge was wrong — native functions must be called via the JUCE frontend helper's `getNativeFunction`, not `backend.getNativeFunction()`.
- **`juce-bridge.js` rewrite**: Implementing `getNativeFunction` locally using `backend.emitEvent('__juce__invoke', {name, params, resultId})` pattern, matching the official JUCE frontend helper implementation.
- **C++ diagnostics**: Added detailed `DBG` logging to `paramChange` and `getParameter` native functions.
- **`DebugConsole.js`**: Created independent debug console module (loads first, before bridge), intercepts console, catches errors/promise rejections, renders overlay with Clear/Copy/Hide, `Ctrl+Alt+D` shortcut.

### Blocked
- (none)

## Key Decisions
- **Native function calling convention**: JUCE's `backend` object only has `addEventListener`/`emitEvent`/`emitByBackend`. `getNativeFunction` is a frontend helper function (from `juce-framework-frontend` npm module), not a backend method. Must implement locally using `emitEvent('__juce__invoke', ...)` + Promise mechanism.
- **`paramCache` in bridge**: Added synchronous cache updated from `paramUpdate` events and `getParameter` results. Needed because `GradientManager.syncFromJuce()` calls `juce.getParameter()` synchronously and expects a number, while the native bridge is async.
- **Canvas size**: `.crt-screen` given explicit `1110×438px` because removing `width:100%/height:100%` would leave canvas at browser default 300×150.
- **Error overlay priority**: Both DebugConsole and the simpler app.js error overlay coexist — DebugConsole is more comprehensive, the app.js overlay was the user's original request.

## Next Steps
1. Rebuild the plugin and check logs for: `registered native functions: paramChange, getParameter` and `Native integration connected`.
2. Verify knobs now show correct parameter values (not all 0.0).
3. Test parameter changes from UI → JUCE → `[WebView] paramChange: IN_GAIN = 0.57` in C++ output.
4. Verify parameter pushes from C++ → JS → `[paramUpdate]` in console.
5. Fine-tune canvas dimensions if 1110×438 doesn't fit cleanly in the screen container.

## Critical Context
- Error from logs: `backend.getNativeFunction is not a function` at `juce-bridge.js:65:13`.
- Backend keys observed: `listeners` only (no `getNativeFunction`).
- `addEventListener` works — so `paramUpdate` and `analysisData` events are functional.
- `getNativeFunction` must be implemented locally via `__juce__invoke` emitEvent pattern (see JUCE `modules/juce_gui_extra/native/javascript/index.js`).
- JUCE `__juce__complete` event resolves the Promises from native function calls.
- `paramCache` returns synchronous number for `GradientManager.syncFromJuce()`.
- All knobs create successfully now (createKnob helper confirmed).
- DebugConsole loads before juce-bridge.js — ready for diagnostics.

## Relevant Files
- `Source/UI/Web/js/juce-bridge.js` — rewritten bridge implementing `getNativeFunction` locally
- `Source/UI/Web/js/config/DebugConsole.js` — new debug console module (load first)
- `Source/UI/Web/js/app.js` — error overlay, createKnob helper, fixed chassis resize
- `Source/UI/Web/js/components/ChickenKnob.js` — safety checks, safe `fetchInitialValue()`
- `Source/UI/Web/css/base.css` — reverted adaptive CSS, added fixed 1300×780 block
- `Source/UI/Web/css/components/crt-screen.css` — fixed canvas dimensions
- `Source/UI/Web/index.html` — added DebugConsole.js as first script
- `Source/PluginEditor.cpp` — native function registration with detailed DBG logging
- `Source/PluginEditor.h` — header (already correct, fixed aspect ratio)
- `Source/PluginProcessor.cpp` — `setEditorSize()` already computes height from width (no change needed)
- `Source/PluginProcessor.h` — header (no change needed)
- `CMakeLists.txt` — uses `CONFIGURE_DEPENDS` with `js/config/*.js` glob (auto-includes DebugConsole.js)