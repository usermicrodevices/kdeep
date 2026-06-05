#include "plugin.hpp"

Q_LOGGING_CATEGORY(deepPluginLog, "kate.deep", QtDebugMsg)

std::atomic<bool> PicoLLMWorker::s_sigfpeCaught{false};

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

static void sigfpe_signal_handler(int sig) {
    Q_UNUSED(sig);
    PicoLLMWorker::s_sigfpeCaught.store(true, std::memory_order_release);
}

static QMutex libraryMutex;

void PicoLLMWorker::run() {
    // Setup alternate signal stack for safe SIGFPE handling
    stack_t ss;
    ss.ss_sp = malloc(SIGSTKSZ);
    ss.ss_size = SIGSTKSZ;
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);

    // Install SIGFPE handler that uses alternate stack
    struct sigaction sa;
    sa.sa_handler = sigfpe_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGFPE, &sa, NULL);

    s_sigfpeCaught.store(false, std::memory_order_release);

    QMutexLocker locker(&libraryMutex);

    // Expand ~ to home directory
    QString expandedPath = m_modelPath;
    if (expandedPath.startsWith('~')) {
        expandedPath = QDir::homePath() + expandedPath.mid(1);
    }
    expandedPath = QFileInfo(expandedPath).absoluteFilePath();

    // Check if file exists
    if (!QFileInfo::exists(expandedPath)) {
        emit error(QLatin1String("Model file not found: ") + expandedPath);
        signal(SIGFPE, SIG_DFL);
        free(ss.ss_sp);
        return;
    }

    // Load model
    model_t model;
    try {
        if (model_load(&model, expandedPath.toUtf8().constData(), 0) != 0) {
            emit error(QLatin1String("Failed to load model from: ") + expandedPath);
            signal(SIGFPE, SIG_DFL);
            free(ss.ss_sp);
            return;
        }
    } catch (const std::exception& err) {
        qCDebug(deepPluginLog) << "C++ exception on model_load: " << err.what();
        emit error(QLatin1String("C++ exception on model_load: ") + QLatin1String(err.what()));
        signal(SIGFPE, SIG_DFL);
        free(ss.ss_sp);
        return;
    } catch (...) {
        qCDebug(deepPluginLog) << "Unknown exception on model_load";
        emit error(QLatin1String("Unknown exception on model_load"));
        signal(SIGFPE, SIG_DFL);
        free(ss.ss_sp);
        return;
    }

    try {
        // Validate model configuration
        if (model.config.vocab_size <= 0) {
            model_free(&model);
            emit error(QLatin1String("Model has invalid vocabulary size"));
            signal(SIGFPE, SIG_DFL);
            free(ss.ss_sp);
            return;
        }
        if (model.config.max_seq_len <= 0) {
            model_free(&model);
            emit error(QLatin1String("Model has invalid max sequence length"));
            signal(SIGFPE, SIG_DFL);
            free(ss.ss_sp);
            return;
        }

        tensor_set_threads(m_threads);

        // Load tokenizer
        tokenizer_t tokenizer;
        if (tokenizer_load(&tokenizer, &model) != 0) {
            model_free(&model);
            emit error(QLatin1String("Failed to load tokenizer"));
            signal(SIGFPE, SIG_DFL);
            free(ss.ss_sp);
            return;
        }

        // Check for empty prompt
        QByteArray promptBytes = m_prompt.toUtf8();
        if (promptBytes.isEmpty()) {
            tokenizer_free(&tokenizer);
            model_free(&model);
            emit error(QLatin1String("Empty prompt"));
            signal(SIGFPE, SIG_DFL);
            free(ss.ss_sp);
            return;
        }

        // Encode prompt using RAII vector
        int maxPromptTokens = promptBytes.size() + 3;
        std::vector<int> promptTokens(maxPromptTokens);
        int nPrompt = tokenizer_encode(&tokenizer, promptBytes.constData(),
                                       promptTokens.data(), maxPromptTokens, 1);
        if (nPrompt <= 0) {
            tokenizer_free(&tokenizer);
            model_free(&model);
            emit error(QLatin1String("Failed to encode prompt"));
            signal(SIGFPE, SIG_DFL);
            free(ss.ss_sp);
            return;
        }

        // Init sampler
        sampler_t sampler;
        sampler_init(&sampler, m_temperature, m_top_p, m_seed);

        // Init grammar (disabled for now)
        grammar_state_t grammar;
        grammar_init(&grammar, GRAMMAR_NONE, &tokenizer);

        // Generation buffer using QByteArray for RAII
        QByteArray outBuffer;
        outBuffer.reserve(256);

        int token = promptTokens[0];
        int pos = 0;
        int totalSteps = nPrompt + m_maxTokens;
        if (totalSteps > model.config.max_seq_len)
            totalSteps = model.config.max_seq_len;

        for (; pos < totalSteps; ++pos) {
            // Check for SIGFPE after each forward pass
            if (s_sigfpeCaught.load(std::memory_order_acquire)) {
                grammar_free(&grammar);
                tokenizer_free(&tokenizer);
                model_free(&model);
                throw std::runtime_error("Floating point exception (division by zero in model library)");
            }

            float *logits = model_forward(&model, token, pos);

            int next;
            if (pos < nPrompt - 1) {
                next = promptTokens[pos + 1];
            } else {
                grammar_apply(&grammar, logits, model.config.vocab_size);
                next = sampler_sample(&sampler, logits, model.config.vocab_size);
                grammar_advance(&grammar, &tokenizer, next);

                const char *piece = tokenizer_decode(&tokenizer, token, next);
                outBuffer.append(piece);

                if (next == (int)tokenizer.eos_id)
                    break;
                if (grammar_is_complete(&grammar))
                    break;
            }
            token = next;
        }

        // Cleanup
        grammar_free(&grammar);
        tokenizer_free(&tokenizer);
        model_free(&model);

        // Emit result
        QString result = QString::fromUtf8(outBuffer);
        emit finished(result);

    } catch (const std::bad_alloc& e) {
        emit error(QString("Critical: Out of Memory during inference: %1").arg(e.what()));
    } catch (const std::exception& e) {
        emit error(QString("Worker Exception: %1").arg(e.what()));
    } catch (...) {
        emit error("An unknown fatal error occurred in the LLM thread.");
    }

    // Restore default signal handler and cleanup
    signal(SIGFPE, SIG_DFL);
    free(ss.ss_sp);
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

    m_openCodeManager = new OpenCodeManager(this);
    qCDebug(deepPluginLog) << "OpenCodeManager created";

    connect(m_askAIButton, &QPushButton::clicked,
            this, &DeepAssistantPluginView::onAskAIClicked);
    connect(m_networkManager, &NetworkManager::requestFinished,
            this, &DeepAssistantPluginView::handleAIResponse);
    connect(m_openCodeManager, &OpenCodeManager::requestFinished,
            this, &DeepAssistantPluginView::handleAIResponse);
    connect(m_openCodeManager, &OpenCodeManager::error,
            this, &DeepAssistantPluginView::handlePicolmError);
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
    m_picolmThread->start(QThread::LowPriority);

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

    bool useOpenCode = group.readEntry("useOpenCode", false);
    if (useOpenCode) {
        QString openCodeUrl = group.readEntry("openCodeUrl", "http://127.0.0.1:4096");
        QString openCodeUser = group.readEntry("openCodeUser", "opencode");
        QString openCodePass = group.readEntry("openCodePass", QString());
        QString openCodeModel = group.readEntry("openCodeModel", QString());
        QString openCodeAgent = group.readEntry("openCodeAgent", "build");
        QString openCodeEffort = group.readEntry("openCodeEffort", QString());

        if (openCodeUrl.isEmpty()) {
            m_previewer->setPlainText(i18n("OpenCode URL not set. Please configure it in Kate's settings."));
            return;
        }

        if (openCodeModel.isEmpty()) {
            m_previewer->setPlainText(i18n("OpenCode model not set. Please configure it in Kate's settings."));
            return;
        }

        // Split "providerID/modelID"
        int slashIdx = openCodeModel.indexOf('/');
        QString providerID = (slashIdx > 0) ? openCodeModel.left(slashIdx) : openCodeModel;
        QString modelID = (slashIdx > 0) ? openCodeModel.mid(slashIdx + 1) : openCodeModel;

        QString code = activeView->document()->text();
        QString prompt = i18n("Explain the following C++ code and suggest improvements:");

        m_askAIButton->setEnabled(false);
        m_askAIButton->setText(i18n("Asking..."));
        m_previewer->setPlainText(i18n("Asking OpenCode, please wait..."));

        m_openCodeManager->sendRequest(openCodeUrl, openCodeUser, openCodePass, prompt, code, providerID, modelID, openCodeAgent, openCodeEffort);
        return;
    }

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
