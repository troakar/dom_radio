#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
#if JUCE_WINDOWS
    juce::File getWebView2DataFolder()
    {
        auto folder =
            juce::File::getSpecialLocation (
                juce::File::userApplicationDataDirectory)
            .getChildFile ("domradio.audio")
            .getChildFile ("DomRadioMaster")
            .getChildFile ("WebView2");

        folder.createDirectory();
        return folder;
    }
#endif

    constexpr int logicalEditorWidth  = 960;
    constexpr int logicalEditorHeight = 455;

    constexpr int minEditorWidth  = logicalEditorWidth / 2;
    constexpr int minEditorHeight = logicalEditorHeight / 2;
    constexpr int maxEditorWidth  = logicalEditorWidth * 2;
    constexpr int maxEditorHeight = logicalEditorHeight * 3;
}

// ============================================================================
// Constructor
// ============================================================================

DomRadioMasterAudioProcessorEditor::
DomRadioMasterAudioProcessorEditor (
    DomRadioMasterAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p)
{
    using WebBrowser = juce::WebBrowserComponent;
    using Options = WebBrowser::Options;

    auto options = Options {}
#if JUCE_WINDOWS
        .withBackend (Options::Backend::webview2)
        .withWinWebView2Options (
            Options::WinWebView2{}
                .withUserDataFolder (getWebView2DataFolder()))
#endif
        .withNativeIntegrationEnabled()
        .withResourceProvider (
            [this] (const juce::String& path)
            {
                return getResource (path);
            })

        // =================================================================
        // JavaScript -> APVTS: parameter change
        // =================================================================
        .withNativeFunction (
            "paramChange",
            [this] (
                const juce::Array<juce::var>& args,
                auto complete)
            {
                if (args.size() >= 2)
                {
                    const auto parameterID =
                        args[0].toString();

                    const auto normalizedValue =
                        juce::jlimit (
                            0.0f,
                            1.0f,
                            static_cast<float> (args[1]));

                    if (auto* parameter =
                            audioProcessor.apvts.getParameter (
                                parameterID))
                    {
                        parameter->setValueNotifyingHost (
                            normalizedValue);
                    }
                }

                complete ({});
            })

        // =================================================================
        // JavaScript -> APVTS: read normalized parameter value
        // =================================================================
        .withNativeFunction (
            "getParameter",
            [this] (
                const juce::Array<juce::var>& args,
                auto complete)
            {
                if (args.size() >= 1)
                {
                    const auto parameterID =
                        args[0].toString();

                    if (auto* parameter =
                            audioProcessor.apvts.getParameter (
                                parameterID))
                    {
                        complete (
                            juce::var (
                                parameter->getValue()));
                        return;
                    }
                }

                complete (0.0f);
            })

        // =================================================================
        // Automation gesture begin
        // =================================================================
        .withNativeFunction (
            "beginGesture",
            [this] (
                const juce::Array<juce::var>& args,
                auto complete)
            {
                if (args.size() >= 1)
                {
                    const auto parameterID =
                        args[0].toString();

                    if (auto* parameter =
                            audioProcessor.apvts.getParameter (
                                parameterID))
                    {
                        parameter->beginChangeGesture();
                    }
                }

                complete ({});
            })

        // =================================================================
        // Automation gesture end
        // =================================================================
        .withNativeFunction (
            "endGesture",
            [this] (
                const juce::Array<juce::var>& args,
                auto complete)
            {
                if (args.size() >= 1)
                {
                    const auto parameterID =
                        args[0].toString();

                    if (auto* parameter =
                            audioProcessor.apvts.getParameter (
                                parameterID))
                    {
                        parameter->endChangeGesture();
                    }
                }

                complete ({});
            })

        // =================================================================
        // Dynamic Window Resize for Drawers
        // =================================================================
        .withNativeFunction (
            "setLogicalSize",
            [this] (
                const juce::Array<juce::var>& args,
                auto complete)
            {
                if (args.size() >= 2)
                {
                    const int logicalW =
                        static_cast<int> (args[0]);

                    const int logicalH =
                        static_cast<int> (args[1]);

                    juce::MessageManager::callAsync (
                        [this, logicalW, logicalH]()
                        {
                            // Обновляем замок пропорций для окна DAW
                            if (auto* constrainer =
                                    getConstrainer())
                            {
                                constrainer->setFixedAspectRatio (
                                    static_cast<double> (logicalW)
                                        / static_cast<double> (logicalH));
                            }

                            // Мгновенно подгоняем высоту окна DAW
                            // под текущую ширину
                            int currentWidth = getWidth();

                            int newHeight = juce::roundToInt (
                                currentWidth * (static_cast<double> (logicalH)
                                                / static_cast<double> (logicalW)));

                            setSize (currentWidth, newHeight);
                        });
                }

                complete ({});
            });

    webView = std::make_unique<WebBrowser> (options);
    addAndMakeVisible (*webView);

    /*
        Подписываемся на все APVTS параметры.

        Это необходимо, чтобы Web UI обновлялся не только от
        собственных действий пользователя, но и от:
        - automation DAW;
        - восстановления preset/state;
        - изменений параметров из host;
        - внутренних ParameterAttachment.
    */
    for (auto* parameter :
         audioProcessor.apvts.processor.getParameters())
    {
        if (auto* parameterWithID =
                dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
        {
            audioProcessor.apvts.addParameterListener (
                parameterWithID->paramID,
                this);
        }
    }

    webView->goToURL (
        WebBrowser::getResourceProviderRoot());

    /*
        Window geometry.

        Web frontend uses a logical 960px-wide artboard whose
        height varies based on open Archive/EQ drawers.
        CSS scale() handles proportional resizing.

        No fixed aspect ratio — height grows dynamically
        when drawers are opened (JS notifies C++ via
        setEditorHeight bridge in Stage 3).
    */
    setResizable (true, true);

    setResizeLimits (
        minEditorWidth,
        minEditorHeight,
        maxEditorWidth,
        maxEditorHeight);

    /*
        Блокируем пропорции на старте.
        Высота далее управляется через web layout + drawer toggles,
        а пропорции окна DAW синхронизируются динамически через
        setLogicalSize bridge из JS.
    */
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio (
            static_cast<double> (logicalEditorWidth)
                / logicalEditorHeight);

    /*
        Restore saved window size from processor state.
        Защита от кривого стейта: гарантируем, что плагин
        откроется минимум 960x455.
    */
    auto savedSize = audioProcessor.getEditorSize();

    int startWidth =
        juce::jmax (
            logicalEditorWidth,
            savedSize.x);

    int startHeight =
        juce::jmax (
            logicalEditorHeight,
            savedSize.y);

    setSize (startWidth, startHeight);

    /*
        Таймер нужен:
        - для установки browser context-menu защиты;
        - для metering/analysis telemetry (30 Гц).
    */
    startTimerHz (30);
}

// ============================================================================
// Destructor
// ============================================================================

DomRadioMasterAudioProcessorEditor::
~DomRadioMasterAudioProcessorEditor()
{
    stopTimer();

    for (auto* parameter :
         audioProcessor.apvts.processor.getParameters())
    {
        if (auto* parameterWithID =
                dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
        {
            audioProcessor.apvts.removeParameterListener (
                parameterWithID->paramID,
                this);
        }
    }

    webView.reset();
}

// ============================================================================
// Component
// ============================================================================

void DomRadioMasterAudioProcessorEditor::paint (
    juce::Graphics& /*graphics*/)
{
    /*
        Фон заметен только до загрузки WebView либо в случае
        временной ошибки embedded browser.
    */
    // Intentionally empty for Stage 1
}

void DomRadioMasterAudioProcessorEditor::resized()
{
    if (webView != nullptr)
        webView->setBounds (getLocalBounds());

    /*
        Сообщаем процессору актуальный размер для сохранения
        в state/preset.
    */
    audioProcessor.setEditorSize (getWidth(), getHeight());
}

// ============================================================================
// APVTS -> JavaScript
// ============================================================================

void DomRadioMasterAudioProcessorEditor::parameterChanged (
    const juce::String& parameterID,
    float /*newValue*/)
{
    if (webView == nullptr)
        return;

    auto* parameter =
        audioProcessor.apvts.getParameter (
            parameterID);

    if (parameter == nullptr)
        return;

    /*
        JavaScript получает только normalized APVTS value: 0...1.

        Это важно, поскольку frontend сам интерпретирует реальные
        диапазоны параметров, unit, skew и формат отображения.
    */
    const auto normalizedValue =
        juce::jlimit (
            0.0f,
            1.0f,
            parameter->getValue());

    juce::Component::SafePointer<
        DomRadioMasterAudioProcessorEditor> safeThis (this);

    juce::MessageManager::callAsync (
        [safeThis, parameterID, normalizedValue]()
        {
            if (safeThis == nullptr ||
                safeThis->webView == nullptr)
            {
                return;
            }

            auto* data =
                new juce::DynamicObject();

            data->setProperty (
                "id",
                parameterID);

            data->setProperty (
                "value",
                normalizedValue);

            safeThis->webView
                ->emitEventIfBrowserIsVisible (
                    "paramUpdate",
                    juce::var (data));
        });
}

// ============================================================================
// Timer
// ============================================================================

void DomRadioMasterAudioProcessorEditor::timerCallback()
{
    if (webView == nullptr)
        return;

    /*
        WebView2 может показывать нативное context menu поверх
        интерфейса. В финальном UI правый клик будет использоваться
        только если понадобится контекстное действие; сейчас
        блокируем стандартное browser menu.
    */
    if (! contextMenuGuardInjected)
    {
        webView->evaluateJavascript (
            R"JS(
                (function () {
                    function blockContextMenu (event) {
                        event.preventDefault();
                        event.stopPropagation();

                        if (event.stopImmediatePropagation)
                            event.stopImmediatePropagation();

                        return false;
                    }

                    document.addEventListener(
                        'contextmenu',
                        blockContextMenu,
                        true
                    );

                    window.addEventListener(
                        'contextmenu',
                        blockContextMenu,
                        true
                    );

                    document.oncontextmenu =
                        function () { return false; };

                    document.body.oncontextmenu =
                        function () { return false; };
                })();
            )JS");

        contextMenuGuardInjected = true;
    }

    /*
        Телеметрия в Web UI: VU meter + saturation indicators.
        Emit событие "telemetry" каждый таймерный тик.
        JS слушает через backend.addEventListener('telemetry', ...).
    */
    auto* telemetry = new juce::DynamicObject();

    const float vuLeft =
        audioProcessor.getMeterLevelLeft();

    const float vuRight =
        audioProcessor.getMeterLevelRight();

    telemetry->setProperty (
        "vuMeter",
        juce::jmax (vuLeft, vuRight));

    telemetry->setProperty (
        "satInput",
        audioProcessor.getInputSaturationLevel());

    telemetry->setProperty (
        "satTape",
        audioProcessor.getTapeSaturationLevel());

    // Спектр реального времени (48 полос)
    float spectrumData[48] = { 0.0f };
    audioProcessor.getSpectrumData (spectrumData, 48);

    juce::Array<juce::var> spectrumArray;
    spectrumArray.ensureStorageAllocated (48);
    for (int i = 0; i < 48; ++i)
        spectrumArray.add (spectrumData[i]);

    telemetry->setProperty ("spectrum", spectrumArray);

    webView->emitEventIfBrowserIsVisible (
        "telemetry",
        juce::var (telemetry));
}

// ============================================================================
// Resource provider
// ============================================================================

std::optional<juce::WebBrowserComponent::Resource>
DomRadioMasterAudioProcessorEditor::getResource (
    const juce::String& requestedPath)
{
    auto path = requestedPath
        .upToFirstOccurrenceOf ("?", false, false)
        .upToFirstOccurrenceOf ("#", false, false)
        .replaceCharacter ('\\', '/');

    while (path.startsWithChar ('/'))
        path = path.substring (1);

    if (path.isEmpty())
        path = "index.html";

    /*
        Basic traversal protection.
    */
    if (path.contains (".."))
        return std::nullopt;

    // Имя файла из URL: css/components/chicken-knob.css -> chicken-knob.css
    auto fileName =
        path.fromLastOccurrenceOf ("/", false, false);

    const char* data = nullptr;
    int dataSize = 0;
    juce::String matchedResource;

    // Сначала пробуем точное имя ресурса (filename.sanitized)
    auto expectedName =
        fileName
            .replaceCharacter ('.', '_')
            .replaceCharacter ('-', '_')
            .replaceCharacter (' ', '_');

    data = BinaryData::getNamedResource (
        expectedName.toRawUTF8(),
        dataSize);

    if (data != nullptr && dataSize > 0)
    {
        matchedResource = expectedName;
    }
    else
    {
        // Fallback: ищем по originalFilenames
        for (int i = 0;
             i < BinaryData::namedResourceListSize; ++i)
        {
            if (BinaryData::originalFilenames[i] != nullptr)
            {
                auto originalPath =
                    juce::String (
                        BinaryData::originalFilenames[i]);

                auto originalFileName =
                    originalPath.fromLastOccurrenceOf (
                        "/", false, false);

                if (originalFileName.equalsIgnoreCase (
                        fileName))
                {
                    auto resourceName =
                        juce::String (
                            BinaryData::namedResourceList[i]);

                    data = BinaryData::getNamedResource (
                        resourceName.toRawUTF8(),
                        dataSize);

                    matchedResource = resourceName;
                    break;
                }
            }
        }

        // Второй fallback: поиск по суффиксу имени ресурса
        if (data == nullptr || dataSize <= 0)
        {
            for (int i = 0;
                 i < BinaryData::namedResourceListSize; ++i)
            {
                auto resName =
                    juce::String (
                        BinaryData::namedResourceList[i]);

                if (resName.equalsIgnoreCase (expectedName))
                {
                    data = BinaryData::getNamedResource (
                        resName.toRawUTF8(),
                        dataSize);

                    matchedResource = resName;
                    break;
                }
            }
        }
    }

    if (data == nullptr || dataSize <= 0)
    {
        DBG ("WEB RESOURCE NOT FOUND");
        DBG ("Requested path: " + requestedPath);
        DBG ("Normalized path: " + path);
        DBG ("Expected BinaryData name: " + expectedName);

        DBG ("Available BinaryData resources:");
        for (int i = 0;
             i < BinaryData::namedResourceListSize; ++i)
            DBG ("  " + juce::String (
                BinaryData::namedResourceList[i]));

        return std::nullopt;
    }

    juce::String mimeType =
        "application/octet-stream";

    if (path.endsWithIgnoreCase (".html"))
        mimeType = "text/html; charset=utf-8";
    else if (path.endsWithIgnoreCase (".css"))
        mimeType = "text/css; charset=utf-8";
    else if (path.endsWithIgnoreCase (".js"))
        mimeType = "application/javascript; charset=utf-8";
    else if (path.endsWithIgnoreCase (".json"))
        mimeType = "application/json; charset=utf-8";
    else if (path.endsWithIgnoreCase (".svg"))
        mimeType = "image/svg+xml";
    else if (path.endsWithIgnoreCase (".png"))
        mimeType = "image/png";
    else if (path.endsWithIgnoreCase (".jpg") ||
             path.endsWithIgnoreCase (".jpeg"))
        mimeType = "image/jpeg";
    else if (path.endsWithIgnoreCase (".woff"))
        mimeType = "font/woff";
    else if (path.endsWithIgnoreCase (".woff2"))
        mimeType = "font/woff2";
    else if (path.endsWithIgnoreCase (".ttf"))
        mimeType = "font/ttf";
    else if (path.endsWithIgnoreCase (".otf"))
        mimeType = "font/otf";

    DBG ("Serving Web resource: " + path + " -> " + matchedResource);

    std::vector<std::byte> bytes (
        reinterpret_cast<const std::byte*> (data),
        reinterpret_cast<const std::byte*> (data + dataSize));

    return juce::WebBrowserComponent::Resource {
        std::move (bytes),
        mimeType
    };
}
