#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::File getWebView2DataFolder()
    {
        auto folder = juce::File::getSpecialLocation (
            juce::File::userApplicationDataDirectory)
            .getChildFile ("troakar.labs")
            .getChildFile ("TroakarSpectral")
            .getChildFile ("WebView2");

        folder.createDirectory();
        return folder;
    }
}

TroakarSpectralAudioProcessorEditor::TroakarSpectralAudioProcessorEditor (TroakarSpectralAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    using WebBrowser = juce::WebBrowserComponent;
    using Options = WebBrowser::Options;

    auto options = Options{}
        .withBackend (Options::Backend::webview2)
        .withWinWebView2Options (
            Options::WinWebView2{}
                .withUserDataFolder (getWebView2DataFolder()))
        .withNativeIntegrationEnabled()
        .withResourceProvider ([this] (const juce::String& path) {
            return getResource (path);
        })
        .withNativeFunction ("paramChange",
            [this] (const juce::Array<juce::var>& args, auto complete) {
                if (args.size() >= 2) {
                    const auto parameterID = args[0].toString();

                    const auto normalizedValue =
                        juce::jlimit (
                            0.0f, 1.0f,
                            static_cast<float> (args[1]));

                    if (auto* param =
                            processor.apvts.getParameter (
                                parameterID)) {
                        /*
                            Note: beginChangeGesture / endChangeGesture
                            are now sent from JS on pointerdown/pointerup
                            via "beginGesture" / "endGesture".
                            We do not call them here to avoid
                            gesture spam on every mouse move.
                        */
                        param->setValueNotifyingHost (
                            normalizedValue);
                    }
                }

                complete ({});
            })
        .withNativeFunction ("getParameter",
            [this] (const juce::Array<juce::var>& args, auto complete) {
                if (args.size() >= 1) {
                    const auto parameterID =
                        args[0].toString();

                    if (auto* param =
                            processor.apvts.getParameter (
                                parameterID)) {
                        complete (
                            juce::var (
                                param->getValue()));
                        return;
                    }
                }

                 complete (0.0f);
             })
        .withNativeFunction ("beginGesture",
            [this] (const juce::Array<juce::var>& args, auto complete) {
                if (args.size() >= 1) {
                    auto parameterID = args[0].toString();

                    if (auto* param =
                            processor.apvts.getParameter (
                                parameterID)) {
                        param->beginChangeGesture();
                    }
                }

                complete ({});
            })
        .withNativeFunction ("endGesture",
            [this] (const juce::Array<juce::var>& args, auto complete) {
                if (args.size() >= 1) {
                    auto parameterID = args[0].toString();

                    if (auto* param =
                            processor.apvts.getParameter (
                                parameterID)) {
                        param->endChangeGesture();
                    }
                }

                complete ({});
            });

    webView = std::make_unique<juce::WebBrowserComponent> (options);
    addAndMakeVisible (*webView);

    // Register APVTS parameter listeners
    for (auto* param : processor.apvts.processor.getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
            processor.apvts.addParameterListener (p->paramID, this);
    }

    webView->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    /*
        Fixed editor window at 900x540. The CSS artboard
        is 1300x780 and is scaled down via CSS transform
        to fit this window.
    */
     /*
        Logical Web UI artboard:
            1300 x 780

        New editor, exactly +10%:
            990 x 594

        990 / 1300 == 594 / 780
        scale = 0.7615384615
    */
    setResizable (false, false);
    setSize (990, 594);

    startTimerHz (10);
}

TroakarSpectralAudioProcessorEditor::~TroakarSpectralAudioProcessorEditor()
{
    for (auto* param : processor.apvts.processor.getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
            processor.apvts.removeParameterListener (p->paramID, this);
    }
}

void TroakarSpectralAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void TroakarSpectralAudioProcessorEditor::resized()
{
    if (webView != nullptr)
        webView->setBounds (getLocalBounds());
}

void TroakarSpectralAudioProcessorEditor::parameterChanged (const juce::String& parameterID, float)
{
    if (! webView)
        return;

    auto* parameter =
        processor.apvts.getParameter (parameterID);

    if (parameter == nullptr)
        return;

    /*
        Always send normalized values to JavaScript.

        The value received by this listener can be the
        parameter's display/raw value depending on the
        parameter implementation. getValue() gives the
        normalized APVTS value expected by JS.
    */
    const float normalizedValue =
        juce::jlimit (
            0.0f,
            1.0f,
            parameter->getValue());

    juce::Component::SafePointer<
        TroakarSpectralAudioProcessorEditor> safeThis (this);

    juce::MessageManager::callAsync (
        [safeThis, parameterID, normalizedValue]()
        {
            if (safeThis != nullptr &&
                safeThis->webView != nullptr)
            {
                auto obj =
                    new juce::DynamicObject();

                obj->setProperty (
                    "id",
                    parameterID);

                obj->setProperty (
                    "value",
                    normalizedValue);

                safeThis->webView
                    ->emitEventIfBrowserIsVisible (
                        "paramUpdate",
                        juce::var (obj));
            }
        });
}

void TroakarSpectralAudioProcessorEditor::timerCallback()
{
    if (!webView)
        return;

    /*
        Inject a capture-phase contextmenu guard into
        the WebView2 document.  This prevents the
        browser's native right-click context menu
        from appearing, which would otherwise steal
        events from the gradient-creation handler.
    */
    if (!contextMenuGuardInjected)
    {
        webView->evaluateJavascript (
            R"JS(
                (function () {
                    function blockContextMenu (e) {
                        e.preventDefault();
                        e.stopPropagation();
                        if (e.stopImmediatePropagation)
                            e.stopImmediatePropagation();
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
                        function () {
                            return false;
                        };
                    document.body.oncontextmenu =
                        function () {
                            return false;
                        };
                })();
            )JS");

        contextMenuGuardInjected = true;
    }

    auto buildArray = [] (
        const std::atomic<float>* source,
        int count)
    {
        juce::Array<juce::var> result;
        result.ensureStorageAllocated (count);

        for (int i = 0; i < count; ++i)
        {
            result.add (
                source[i].load (
                    std::memory_order_relaxed));
        }

        return result;
    };

    auto buildUiArray = [] (
        const std::atomic<float>* source,
        int sourceCount,
        int outputCount)
    {
        juce::Array<juce::var> result;

        if (source == nullptr ||
            sourceCount <= 0 ||
            outputCount <= 0)
            return result;

        result.ensureStorageAllocated (outputCount);

        for (int i = 0; i < outputCount; ++i)
        {
            const auto normalizedIndex =
                outputCount > 1
                    ? static_cast<float> (i)
                        / static_cast<float> (outputCount - 1)
                    : 0.0f;

            const auto sourceIndex = juce::jlimit (
                0,
                sourceCount - 1,
                juce::roundToInt (
                    normalizedIndex
                    * static_cast<float> (sourceCount - 1)));

            auto value =
                source[sourceIndex].load (
                    std::memory_order_relaxed);

            if (! std::isfinite (value))
                value = 0.0f;

            result.add (value);
        }

        return result;
    };

    const int fftSize =
        juce::jmax (2, processor.getCurrentFFTSize());

    const auto spectrumBins =
        juce::jlimit (
            2,
            TroakarSpectralAudioProcessor::MAX_FFT_BINS,
            processor.getSpectrumBinCount());

    const auto sidechainBins =
        juce::jlimit (
            2,
            TroakarSpectralAudioProcessor::MAX_FFT_BINS,
            processor.getSidechainBinCount());

    const auto deltaBins =
        juce::jlimit (
            2,
            TroakarSpectralAudioProcessor::MAX_FFT_BINS,
            processor.getCompressionDeltaBinCount());

    const auto detectorBins =
        juce::jlimit (
            2,
            TroakarSpectralAudioProcessor::MAX_FFT_BINS,
            processor.getDetectorBinCount());

    const auto effectiveTargetBins =
        juce::jlimit (
            2,
            TroakarSpectralAudioProcessor::MAX_FFT_BINS,
            processor.getEffectiveTargetBinCount());

    /*
        Send a lighter representation to the WebView.
        The processor keeps full FFT resolution; the UI
        only needs 512 bins for rendering.
    */
    constexpr int uiBins = 512;

    auto* data = new juce::DynamicObject();

    data->setProperty (
        "spectrum",
        buildUiArray (
            processor.spectrumDataLeft,
            spectrumBins,
            uiBins));

    data->setProperty (
        "sidechain",
        buildUiArray (
            processor.sidechainData,
            sidechainBins,
            uiBins));

    data->setProperty (
        "delta",
        buildUiArray (
            processor.compressionDeltaData,
            deltaBins,
            uiBins));

    data->setProperty (
        "detector",
        buildUiArray (
            processor.detectorData,
            detectorBins,
            uiBins));

    data->setProperty (
        "effectiveTarget",
        buildUiArray (
            processor.effectiveTargetData,
            effectiveTargetBins,
            uiBins));

    data->setProperty ("spectrumBinCount", spectrumBins);
    data->setProperty ("sidechainBinCount", sidechainBins);
    data->setProperty ("deltaBinCount", deltaBins);
    data->setProperty ("detectorBinCount", detectorBins);
    data->setProperty ("effectiveTargetBinCount", effectiveTargetBins);

    data->setProperty ("fftSize", fftSize);
    data->setProperty ("numBins", uiBins);

    data->setProperty (
        "sampleRate",
        processor.getSampleRate() > 0.0
            ? processor.getSampleRate()
            : 44100.0);

    data->setProperty (
        "spectrumFormat", "linearMagnitude");

    data->setProperty (
        "sidechainFormat", "linearMagnitude");

    data->setProperty (
        "deltaFormat", "decibels");

    data->setProperty (
        "detectorFormat", "decibels");

    data->setProperty (
        "effectiveTargetFormat", "decibels");

    data->setProperty ("hasAnalysis", true);

    webView->emitEventIfBrowserIsVisible (
        "analysisData",
        juce::var (data));
}

std::optional<juce::WebBrowserComponent::Resource>
TroakarSpectralAudioProcessorEditor::getResource (const juce::String& requestedPath)
{
    // Согласно документации JUCE provider получает:
    // "/" или "/css/base.css", а не полный URL.
    auto path = requestedPath
        .upToFirstOccurrenceOf ("?", false, false)
        .upToFirstOccurrenceOf ("#", false, false)
        .replaceCharacter ('\\', '/');

    while (path.startsWithChar ('/'))
        path = path.substring (1);

    if (path.isEmpty())
        path = "index.html";

    if (path.contains (".."))
    {
        DBG ("Blocked invalid resource path: " + path);
        return std::nullopt;
    }

    // Имя файла из URL: css/components/chicken-knob.css -> chicken-knob.css
    auto fileName = path.fromLastOccurrenceOf ("/", false, false);

    const char* data = nullptr;
    int dataSize = 0;
    juce::String matchedResource;

    // Сначала пробуем точное имя ресурса (filename.sanitized)
    auto expectedName = fileName
        .replaceCharacter ('.', '_')
        .replaceCharacter ('-', '_')
        .replaceCharacter (' ', '_');

    data = BinaryData::getNamedResource (expectedName.toRawUTF8(), dataSize);

    if (data != nullptr && dataSize > 0)
    {
        matchedResource = expectedName;
    }
    else
    {
        // Fallback: ищем по originalFilenames
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            if (BinaryData::originalFilenames[i] != nullptr)
            {
                auto originalPath = juce::String (BinaryData::originalFilenames[i]);
                auto originalFileName = originalPath.fromLastOccurrenceOf ("/", false, false);

                if (originalFileName.equalsIgnoreCase (fileName))
                {
                    auto resourceName = juce::String (BinaryData::namedResourceList[i]);
                    data = BinaryData::getNamedResource (resourceName.toRawUTF8(), dataSize);
                    matchedResource = resourceName;
                    break;
                }
            }
        }

        // Второй fallback: поиск по суффиксу имени ресурса
        if (data == nullptr || dataSize <= 0)
        {
            for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            {
                auto resName = juce::String (BinaryData::namedResourceList[i]);
                if (resName.equalsIgnoreCase (expectedName))
                {
                    data = BinaryData::getNamedResource (resName.toRawUTF8(), dataSize);
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
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            DBG ("  " + juce::String (BinaryData::namedResourceList[i]));

        return std::nullopt;
    }

    juce::String mimeType = "application/octet-stream";
    if (path.endsWithIgnoreCase (".html")) mimeType = "text/html; charset=utf-8";
    else if (path.endsWithIgnoreCase (".css")) mimeType = "text/css; charset=utf-8";
    else if (path.endsWithIgnoreCase (".js")) mimeType = "application/javascript; charset=utf-8";
    else if (path.endsWithIgnoreCase (".json")) mimeType = "application/json; charset=utf-8";
    else if (path.endsWithIgnoreCase (".svg")) mimeType = "image/svg+xml";
    else if (path.endsWithIgnoreCase (".png")) mimeType = "image/png";
    else if (path.endsWithIgnoreCase (".jpg") || path.endsWithIgnoreCase (".jpeg")) mimeType = "image/jpeg";
    else if (path.endsWithIgnoreCase (".ttf")) mimeType = "font/ttf";
    else if (path.endsWithIgnoreCase (".otf")) mimeType = "font/otf";
    else if (path.endsWithIgnoreCase (".woff")) mimeType = "font/woff";
    else if (path.endsWithIgnoreCase (".woff2")) mimeType = "font/woff2";

    DBG ("Serving Web resource: " + path + " -> " + matchedResource);

    std::vector<std::byte> bytes (reinterpret_cast<const std::byte*> (data),
                                  reinterpret_cast<const std::byte*> (data + dataSize));

    return juce::WebBrowserComponent::Resource { std::move (bytes), mimeType };
}
