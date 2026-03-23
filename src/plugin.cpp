#include "plugin.hpp"

Q_LOGGING_CATEGORY(deepPluginLog, "kate.deep", QtDebugMsg)

K_PLUGIN_FACTORY_WITH_JSON(DeepAssistantPluginFactory, "src/kdeep.json", registerPlugin<DeepAssistantPlugin>();)

QObject *DeepAssistantPlugin::createView(KTextEditor::MainWindow *mainWindow)
{
    qCDebug(deepPluginLog) << "Creating plugin view";
    return new DeepAssistantPluginView(this, mainWindow);
}

int DeepAssistantPlugin::configPages() const
{
    return 1;
}

KTextEditor::ConfigPage* DeepAssistantPlugin::configPage(int number, QWidget* parent)
{
    qCDebug(deepPluginLog) << "configPages called, creating config page";
    if (number == 0) {
        return new DeepConfig(parent);
    }
    return nullptr;
}

// ------------------------------------------------------------
// PicoLLMWorker implementation
// ------------------------------------------------------------
void PicoLLMWorker::setParams(const QString &modelPath,
                              const QString &prompt,
                              int maxTokens,
                              float temperature,
                              float top_p,
                              uint64_t seed,
                              int threads)
{
    m_modelPath = modelPath;
    m_prompt = prompt;
    m_maxTokens = maxTokens;
    m_temperature = temperature;
    m_top_p = top_p;
    m_seed = seed;
    m_threads = threads;
}

void PicoLLMWorker::run()
{
    // Load model
    model_t model;
    if (model_load(&model, m_modelPath.toUtf8().constData(), 0) != 0) {
        emit error(QLatin1String("Failed to load model"));
        return;
    }

    tensor_set_threads(m_threads);

    // Load tokenizer
    tokenizer_t tokenizer;
    if (tokenizer_load(&tokenizer, &model) != 0) {
        model_free(&model);
        emit error(QLatin1String("Failed to load tokenizer"));
        return;
    }

    // Init sampler
    sampler_t sampler;
    sampler_init(&sampler, m_temperature, m_top_p, m_seed);

    // Init grammar (disabled for now)
    grammar_state_t grammar;
    grammar_init(&grammar, GRAMMAR_NONE, &tokenizer);

    // Encode prompt
    QByteArray promptBytes = m_prompt.toUtf8();
    int maxPromptTokens = promptBytes.size() + 3;
    int *promptTokens = (int*)malloc(maxPromptTokens * sizeof(int));
    int nPrompt = tokenizer_encode(&tokenizer, promptBytes.constData(),
                                   promptTokens, maxPromptTokens, 1);

    // Generation buffer
    size_t outCap = 256;
    char *outStr = (char*)malloc(outCap);
    outStr[0] = '\0';
    size_t outLen = 0;

    int token = promptTokens[0];
    int pos = 0;
    int totalSteps = nPrompt + m_maxTokens;
    if (totalSteps > model.config.max_seq_len)
        totalSteps = model.config.max_seq_len;

    for (; pos < totalSteps; ++pos) {
        float *logits = model_forward(&model, token, pos);

        int next;
        if (pos < nPrompt - 1) {
            next = promptTokens[pos + 1];
        } else {
            grammar_apply(&grammar, logits, model.config.vocab_size);
            next = sampler_sample(&sampler, logits, model.config.vocab_size);
            grammar_advance(&grammar, &tokenizer, next);

            const char *piece = tokenizer_decode(&tokenizer, token, next);
            size_t pieceLen = strlen(piece);
            if (outLen + pieceLen + 1 > outCap) {
                outCap *= 2;
                outStr = (char*)realloc(outStr, outCap);
            }
            memcpy(outStr + outLen, piece, pieceLen);
            outLen += pieceLen;
            outStr[outLen] = '\0';

            if (next == (int)tokenizer.eos_id)
                break;
            if (grammar_is_complete(&grammar))
                break;
        }
        token = next;
    }

    // Cleanup
    free(promptTokens);
    grammar_free(&grammar);
    tokenizer_free(&tokenizer);
    model_free(&model);

    // Emit result
    QString result = QString::fromUtf8(outStr);
    free(outStr);
    emit finished(result);
}

// ------------------------------------------------------------
// DeepAssistantPluginView implementation
// ------------------------------------------------------------
DeepAssistantPluginView::DeepAssistantPluginView(DeepAssistantPlugin *plugin, KTextEditor::MainWindow *mainwindow)
    : m_mainWindow(mainwindow)
{
    qCDebug(deepPluginLog) << "Plugin view constructor started";

    m_toolview.reset(m_mainWindow->createToolView(plugin,
        "kdeep",
        KTextEditor::MainWindow::Right,
        QIcon::fromTheme("preview"),
        i18n("KDeep AI Assistant")));
    qCDebug(deepPluginLog) << "Toolview created";

    if (!m_toolview->layout()) {
        QVBoxLayout *layout = new QVBoxLayout(m_toolview.get());
        m_toolview->setLayout(layout);
        qCDebug(deepPluginLog) << "Layout added to toolview";
    }

    m_previewer = new QTextBrowser(m_toolview.get());
    m_toolview->layout()->addWidget(m_previewer);
    qCDebug(deepPluginLog) << "Previewer created and added";

    m_askAIButton = new QPushButton(i18n("Ask AI"), m_toolview.get());
    m_toolview->layout()->addWidget(m_askAIButton);
    qCDebug(deepPluginLog) << "Button created";

    m_networkManager = new NetworkManager(this);
    qCDebug(deepPluginLog) << "NetworkManager created";

    connect(m_askAIButton, &QPushButton::clicked,
            this, &DeepAssistantPluginView::onAskAIClicked);
    connect(m_networkManager, &NetworkManager::requestFinished,
            this, &DeepAssistantPluginView::handleAIResponse);
    qCDebug(deepPluginLog) << "Connections made";

    connect(m_mainWindow, &KTextEditor::MainWindow::viewChanged,
            this, &DeepAssistantPluginView::onViewChanged);
    qCDebug(deepPluginLog) << "View changed signal connected";

    // Setup PicoLLM worker thread
    m_picolmThread = new QThread(this);
    m_picolmWorker = new PicoLLMWorker;
    m_picolmWorker->moveToThread(m_picolmThread);
    connect(m_picolmThread, &QThread::finished, m_picolmWorker, &QObject::deleteLater);
    connect(m_picolmWorker, &PicoLLMWorker::finished, this, &DeepAssistantPluginView::handleAIResponse);
    connect(m_picolmWorker, &PicoLLMWorker::error, this, &DeepAssistantPluginView::handlePicolmError);
    m_picolmThread->start();

    qCDebug(deepPluginLog) << "Plugin view constructor finished";
}

DeepAssistantPluginView::~DeepAssistantPluginView()
{
    if (m_picolmThread) {
        m_picolmThread->quit();
        m_picolmThread->wait();
    }
    qCDebug(deepPluginLog) << "Plugin view destroyed";
}

void DeepAssistantPluginView::onViewChanged(KTextEditor::View *v)
{
    Q_UNUSED(v);
}

void DeepAssistantPluginView::onAskAIClicked()
{
    qCDebug(deepPluginLog) << "onAskAIClicked called";
    KTextEditor::View *activeView = m_mainWindow->activeView();
    if (!activeView || !activeView->document()) {
        qCWarning(deepPluginLog) << "No active document to analyze";
        m_previewer->setPlainText(i18n("No active document to analyze."));
        return;
    }

    // Load settings from KConfig
    KSharedConfigPtr config = KSharedConfig::openConfig("kdeeprc");
    KConfigGroup group(config, "General");

    bool usePicolm = group.readEntry("usePicolm", false);
    if (usePicolm) {
        QString modelPath = group.readEntry("picolmModelPath", QString());
        if (modelPath.isEmpty()) {
            m_previewer->setPlainText(i18n("PicoLLM model file not set. Please configure it in Kate's settings."));
            return;
        }
        m_askAIButton->setEnabled(false);
        m_askAIButton->setText(i18n("Asking..."));
        m_previewer->setPlainText(i18n("Running local PicoLLM, please wait..."));
        runPicolm(modelPath, group);
        return;
    }

    QString baseUrl = group.readEntry("baseUrl", "https://api.deepseek.com");
    bool useGpt4all = group.readEntry("useGpt4all", false);
    QString apiKey;
    if (!useGpt4all) {
        apiKey = group.readEntry("apiKey", QString());
    }

    if (!useGpt4all && apiKey.isEmpty()) {
        m_previewer->setPlainText(i18n("API key not set. Please configure it in Kate's settings (Settings → Configure Kate → Plugins → KDeep)."));
        return;
    }

    QString model = group.readEntry("model", "deepseek-coder");
    double temperature = group.readEntry("temperature", 0.7);
    int maxTokens = group.readEntry("maxTokens", 4096);

    QString code = activeView->document()->text();
    QString prompt = i18n("Explain the following C++ code and suggest improvements:");
    qCDebug(deepPluginLog) << "Sending request with prompt and code length:" << code.length();

    m_askAIButton->setEnabled(false);
    m_askAIButton->setText(i18n("Asking..."));
    m_previewer->setPlainText(i18n("Asking AI, please wait..."));

    m_networkManager->sendRequest(baseUrl, apiKey, prompt, code, model, temperature, maxTokens, useGpt4all);
}

void DeepAssistantPluginView::handleAIResponse(const QString &response)
{
    qCDebug(deepPluginLog) << "handleAIResponse called, response length:" << response.length();
    m_previewer->setMarkdown(response);
    m_askAIButton->setEnabled(true);
    m_askAIButton->setText(i18n("Ask AI"));
}

void DeepAssistantPluginView::handlePicolmError(const QString &message)
{
    m_previewer->setPlainText(i18n("PicoLLM error: %1", message));
    m_askAIButton->setEnabled(true);
    m_askAIButton->setText(i18n("Ask AI"));
}

void DeepAssistantPluginView::runPicolm(const QString &modelPath, const KConfigGroup &group)
{
    KTextEditor::View *activeView = m_mainWindow->activeView();
    if (!activeView) return;

    QString code = activeView->document()->text();
    double temperature = group.readEntry("temperature", 0.7);
    int maxTokens = group.readEntry("maxTokens", 4096);
    int seed = group.readEntry("seed", 42);
    int threads = group.readEntry("threads", 4);
    float top_p = 0.9f;  // fixed for now, could be made configurable later

    // Disable button
    m_askAIButton->setEnabled(false);
    m_askAIButton->setText(i18n("Asking..."));

    // Start worker
    m_picolmWorker->setParams(modelPath, code, maxTokens, temperature, top_p, seed, threads);
    QMetaObject::invokeMethod(m_picolmWorker, "run", Qt::QueuedConnection);
}

#include "plugin.moc"
