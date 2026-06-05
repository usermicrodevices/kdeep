#include "plugin.hpp"
#include <algorithm>
#include <QFileDialog>
#include <QInputDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QDir>
#include <QSet>
#include <QTimer>
#include <QMenu>
#include <QMessageBox>

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
    stack_t ss;
    ss.ss_sp = malloc(SIGSTKSZ);
    ss.ss_size = SIGSTKSZ;
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);

    struct sigaction sa;
    sa.sa_handler = sigfpe_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGFPE, &sa, NULL);

    s_sigfpeCaught.store(false, std::memory_order_release);

    QMutexLocker locker(&libraryMutex);

    QString expandedPath = m_modelPath;
    if (expandedPath.startsWith('~')) {
        expandedPath = QDir::homePath() + expandedPath.mid(1);
    }
    expandedPath = QFileInfo(expandedPath).absoluteFilePath();

    if (!QFileInfo::exists(expandedPath)) {
        emit error(QLatin1String("Model file not found: ") + expandedPath);
        signal(SIGFPE, SIG_DFL);
        free(ss.ss_sp);
        return;
    }

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

        tokenizer_t tokenizer;
        if (tokenizer_load(&tokenizer, &model) != 0) {
            model_free(&model);
            emit error(QLatin1String("Failed to load tokenizer"));
            signal(SIGFPE, SIG_DFL);
            free(ss.ss_sp);
            return;
        }

        QByteArray promptBytes = m_prompt.toUtf8();
        if (promptBytes.isEmpty()) {
            tokenizer_free(&tokenizer);
            model_free(&model);
            emit error(QLatin1String("Empty prompt"));
            signal(SIGFPE, SIG_DFL);
            free(ss.ss_sp);
            return;
        }

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

        sampler_t sampler;
        sampler_init(&sampler, m_temperature, m_top_p, m_seed);

        grammar_state_t grammar;
        grammar_init(&grammar, GRAMMAR_NONE, &tokenizer);

        QByteArray outBuffer;
        outBuffer.reserve(256);

        int token = promptTokens[0];
        int pos = 0;
        int totalSteps = nPrompt + m_maxTokens;
        if (totalSteps > model.config.max_seq_len)
            totalSteps = model.config.max_seq_len;

        for (; pos < totalSteps; ++pos) {
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

        grammar_free(&grammar);
        tokenizer_free(&tokenizer);
        model_free(&model);

        QString result = QString::fromUtf8(outBuffer);
        emit finished(result);

    } catch (const std::bad_alloc& e) {
        emit error(QString("Critical: Out of Memory during inference: %1").arg(e.what()));
    } catch (const std::exception& e) {
        emit error(QString("Worker Exception: %1").arg(e.what()));
    } catch (...) {
        emit error("An unknown fatal error occurred in the LLM thread.");
    }

    signal(SIGFPE, SIG_DFL);
    free(ss.ss_sp);
}

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

    QVBoxLayout *mainLayout = qobject_cast<QVBoxLayout*>(m_toolview->layout());
    if (!mainLayout) {
        mainLayout = new QVBoxLayout(m_toolview.get());
        m_toolview->setLayout(mainLayout);
    }
    mainLayout->setContentsMargins(4, 4, 4, 4);

    QLabel *sessionsLabel = new QLabel(i18n("Sessions:"), m_toolview.get());
    mainLayout->addWidget(sessionsLabel);

    m_sessionsList = new QListWidget(m_toolview.get());
    m_sessionsList->setMaximumHeight(120);
    m_sessionsList->setContextMenuPolicy(Qt::CustomContextMenu);
    mainLayout->addWidget(m_sessionsList);

    QHBoxLayout *sessionBtnLayout = new QHBoxLayout;
    m_openProjectBtn = new QPushButton(i18n("Open Project"), m_toolview.get());
    m_newSessionBtn = new QPushButton(i18n("New Session"), m_toolview.get());
    m_refreshSessionsBtn = new QPushButton(i18n("Refresh"), m_toolview.get());
    sessionBtnLayout->addWidget(m_openProjectBtn);
    sessionBtnLayout->addWidget(m_newSessionBtn);
    sessionBtnLayout->addWidget(m_refreshSessionsBtn);
    sessionBtnLayout->addStretch();
    mainLayout->addLayout(sessionBtnLayout);

    QLabel *promptLabel = new QLabel(i18n("Prompt:"), m_toolview.get());
    mainLayout->addWidget(promptLabel);

    m_promptEdit = new QTextEdit(m_toolview.get());
    m_promptEdit->setPlaceholderText(i18n("Enter prompt or leave empty to use document..."));
    m_promptEdit->setMaximumHeight(80);
    mainLayout->addWidget(m_promptEdit);

    QHBoxLayout *promptBtnLayout = new QHBoxLayout;
    m_submitPromptBtn = new QPushButton(i18n("Submit"), m_toolview.get());
    m_askAIButton = new QPushButton(i18n("Ask AI"), m_toolview.get());
    promptBtnLayout->addWidget(m_submitPromptBtn);
    promptBtnLayout->addWidget(m_askAIButton);
    mainLayout->addLayout(promptBtnLayout);

    QLabel *answerLabel = new QLabel(i18n("Answer:"), m_toolview.get());
    mainLayout->addWidget(answerLabel);

    m_previewer = new QTextBrowser(m_toolview.get());
    mainLayout->addWidget(m_previewer);

    qCDebug(deepPluginLog) << "UI widgets created";

    m_networkManager = new NetworkManager(this);
    m_openCodeManager = new OpenCodeManager(this);
    qCDebug(deepPluginLog) << "Managers created";

    connect(m_askAIButton, &QPushButton::clicked,
            this, &DeepAssistantPluginView::onAskAIClicked);
    connect(m_submitPromptBtn, &QPushButton::clicked,
            this, &DeepAssistantPluginView::onPromptSubmit);
    connect(m_newSessionBtn, &QPushButton::clicked,
            this, &DeepAssistantPluginView::onNewSessionClicked);
    connect(m_openProjectBtn, &QPushButton::clicked,
            this, &DeepAssistantPluginView::onOpenProjectClicked);
    connect(m_refreshSessionsBtn, &QPushButton::clicked,
            this, &DeepAssistantPluginView::onRefreshSessionsClicked);
    connect(m_sessionsList, &QListWidget::itemClicked,
            this, &DeepAssistantPluginView::onSessionSelected);
    connect(m_sessionsList, &QListWidget::customContextMenuRequested,
            this, &DeepAssistantPluginView::onSessionContextMenu);

    // Auto-refresh sessions on startup if OpenCode is enabled
    KSharedConfigPtr startupConfig = KSharedConfig::openConfig("kdeeprc");
    KConfigGroup startupGroup(startupConfig, "General");
    if (startupGroup.readEntry("useOpenCode", false)) {
        m_openCodeManager->fetchSessions(
            startupGroup.readEntry("openCodeUrl", "http://127.0.0.1:40647"),
            startupGroup.readEntry("openCodeUser", "opencode"),
            startupGroup.readEntry("openCodePass", QString()));
    }

    connect(m_networkManager, &NetworkManager::requestFinished,
            this, &DeepAssistantPluginView::handleAIResponse);
    connect(m_openCodeManager, &OpenCodeManager::requestFinished,
            this, &DeepAssistantPluginView::handleAIResponse);
    connect(m_openCodeManager, &OpenCodeManager::error,
            this, &DeepAssistantPluginView::handlePicolmError);
    connect(m_openCodeManager, &OpenCodeManager::sessionsReady,
            this, &DeepAssistantPluginView::onSessionsReady);
    connect(m_openCodeManager, &OpenCodeManager::sessionCreated,
            this, [this](const QString &sessionId) {
                m_currentSessionId = sessionId;
                m_openCodeManager->setCurrentSessionId(sessionId);
                m_newSessionBtn->setEnabled(true);
                m_newSessionBtn->setText(i18n("New Session"));
                qCDebug(deepPluginLog) << "Session created, id:" << sessionId;
                // Refresh sessions list
                KSharedConfigPtr config = KSharedConfig::openConfig("kdeeprc");
                KConfigGroup group(config, "General");
                if (group.readEntry("useOpenCode", false)) {
                    m_openCodeManager->fetchSessions(
                        group.readEntry("openCodeUrl", "http://127.0.0.1:4096"),
                        group.readEntry("openCodeUser", "opencode"),
                        group.readEntry("openCodePass", QString()));
                }
            });
    connect(m_openCodeManager, &OpenCodeManager::sessionMessagesReady,
            this, [this](const QJsonArray &messages) {
                QString result;
                for (const QJsonValue &val : messages) {
                    QJsonObject msg = val.toObject();
                    QString role = msg["role"].toString();
                    QJsonArray parts = msg["parts"].toArray();
                    for (const QJsonValue &partVal : parts) {
                        QJsonObject part = partVal.toObject();
                        if (part["type"].toString() == "text") {
                            QString text = part["text"].toString();
                            if (role == "assistant") {
                                result += "**AI:** " + text + "\n\n";
                            } else if (role == "user") {
                                result += "**You:** " + text + "\n\n";
                            }
                        }
                    }
                }
                if (!result.isEmpty()) {
                    m_previewer->setMarkdown(result);
                }
            });
    connect(m_openCodeManager, &OpenCodeManager::progressUpdate,
            this, [this](const QString &status) {
                m_previewer->setMarkdown("**Status:** " + status);
            });

    connect(m_openCodeManager, &OpenCodeManager::permissionRequested,
            this, &DeepAssistantPluginView::handlePermissionRequest);

    connect(m_mainWindow, &KTextEditor::MainWindow::viewChanged,
            this, &DeepAssistantPluginView::onViewChanged);
    qCDebug(deepPluginLog) << "Connections made";

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

void DeepAssistantPluginView::onNewSessionClicked()
{
    qCDebug(deepPluginLog) << "onNewSessionClicked";

    KSharedConfigPtr config = KSharedConfig::openConfig("kdeeprc");
    KConfigGroup group(config, "General");

    if (!group.readEntry("useOpenCode", false)) {
        m_previewer->setPlainText(i18n("New session requires OpenCode backend. Enable it in settings."));
        return;
    }

    m_newSessionBtn->setEnabled(false);
    m_newSessionBtn->setText(i18n("Creating..."));

    m_openCodeManager->createSession(
        group.readEntry("openCodeUrl", "http://127.0.0.1:4096"),
        group.readEntry("openCodeUser", "opencode"),
        group.readEntry("openCodePass", QString()));
}

void DeepAssistantPluginView::onRefreshSessionsClicked()
{
    qCDebug(deepPluginLog) << "onRefreshSessionsClicked";

    KSharedConfigPtr config = KSharedConfig::openConfig("kdeeprc");
    KConfigGroup group(config, "General");

    if (!group.readEntry("useOpenCode", false)) {
        m_previewer->setPlainText(i18n("Sessions require OpenCode backend. Enable it in settings."));
        return;
    }

    m_refreshSessionsBtn->setEnabled(false);
    m_refreshSessionsBtn->setText(i18n("Refreshing..."));

    m_openCodeManager->fetchSessions(
        group.readEntry("openCodeUrl", "http://127.0.0.1:40647"),
        group.readEntry("openCodeUser", "opencode"),
        group.readEntry("openCodePass", QString()));

    // Re-enable button after short delay (network reply will also update list)
    QTimer::singleShot(2000, this, [this]() {
        m_refreshSessionsBtn->setEnabled(true);
        m_refreshSessionsBtn->setText(i18n("Refresh"));
    });
}

void DeepAssistantPluginView::onSessionContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_sessionsList->itemAt(pos);
    if (!item) return;

    QString sessionId = item->data(Qt::UserRole).toString();
    if (sessionId.isEmpty()) return;

    KSharedConfigPtr config = KSharedConfig::openConfig("kdeeprc");
    KConfigGroup group(config, "General");
    QString openCodeUrl = group.readEntry("openCodeUrl", "http://127.0.0.1:40647");
    QString openCodeUser = group.readEntry("openCodeUser", "opencode");
    QString openCodePass = group.readEntry("openCodePass", QString());

    QMenu menu(m_sessionsList);

    QAction *renameAction = menu.addAction(i18n("Rename"));
    QAction *shareAction = menu.addAction(i18n("Share"));
    QAction *archiveAction = menu.addAction(i18n("Archive"));
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(i18n("Delete"));
    deleteAction->setIcon(QIcon::fromTheme("edit-delete"));

    QAction *selectedAction = menu.exec(m_sessionsList->viewport()->mapToGlobal(pos));
    if (!selectedAction) return;

    if (selectedAction == renameAction) {
        bool ok;
        QString newTitle = QInputDialog::getText(m_toolview.get(),
            i18n("Rename Session"),
            i18n("New title:"),
            QLineEdit::Normal,
            item->text(),
            &ok);
        if (ok && !newTitle.isEmpty()) {
            m_openCodeManager->renameSession(openCodeUrl, openCodeUser, openCodePass, sessionId, newTitle);
            m_previewer->setMarkdown(i18n("**Session renamed to:** %1").arg(newTitle));
            QTimer::singleShot(1000, this, [this, openCodeUrl, openCodeUser, openCodePass]() {
                m_openCodeManager->fetchSessions(openCodeUrl, openCodeUser, openCodePass);
            });
        }
    } else if (selectedAction == shareAction) {
        m_openCodeManager->shareSession(openCodeUrl, openCodeUser, openCodePass, sessionId);
        m_previewer->setMarkdown(i18n("**Sharing session...**"));
    } else if (selectedAction == archiveAction) {
        m_openCodeManager->archiveSession(openCodeUrl, openCodeUser, openCodePass, sessionId);
        m_previewer->setMarkdown(i18n("**Session archived.**"));
        QTimer::singleShot(1000, this, [this, openCodeUrl, openCodeUser, openCodePass]() {
            m_openCodeManager->fetchSessions(openCodeUrl, openCodeUser, openCodePass);
        });
    } else if (selectedAction == deleteAction) {
        QMessageBox::StandardButton reply = QMessageBox::question(m_toolview.get(),
            i18n("Delete Session"),
            i18n("Are you sure you want to delete this session?"),
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            m_openCodeManager->deleteSession(openCodeUrl, openCodeUser, openCodePass, sessionId);
            if (sessionId == m_currentSessionId) {
                m_currentSessionId.clear();
                m_openCodeManager->setCurrentSessionId(QString());
            }
            m_previewer->setMarkdown(i18n("**Session deleted.**"));
            QTimer::singleShot(1000, this, [this, openCodeUrl, openCodeUser, openCodePass]() {
                m_openCodeManager->fetchSessions(openCodeUrl, openCodeUser, openCodePass);
            });
        }
    }
}

void DeepAssistantPluginView::onSessionSelected(QListWidgetItem *item)
{
    if (!item) return;

    QString sessionId = item->data(Qt::UserRole).toString();
    if (sessionId.isEmpty()) return;

    qCDebug(deepPluginLog) << "Session selected:" << sessionId;
    m_currentSessionId = sessionId;
    m_openCodeManager->setCurrentSessionId(sessionId);

    // Refresh visual highlighting of all items
    for (int i = 0; i < m_sessionsList->count(); ++i) {
        QListWidgetItem *it = m_sessionsList->item(i);
        bool isCurrent = (it->data(Qt::UserRole).toString() == sessionId);
        it->setSelected(isCurrent);
        QFont font = it->font();
        font.setBold(isCurrent);
        it->setFont(font);
        it->setBackground(isCurrent ? QBrush(QColor(100, 149, 237, 80)) : QBrush());
    }

    KSharedConfigPtr config = KSharedConfig::openConfig("kdeeprc");
    KConfigGroup group(config, "General");

    m_openCodeManager->fetchSessionMessages(
        group.readEntry("openCodeUrl", "http://127.0.0.1:4096"),
        group.readEntry("openCodeUser", "opencode"),
        group.readEntry("openCodePass", QString()),
        sessionId);
}

void DeepAssistantPluginView::onSessionsReady(const QJsonArray &sessions)
{
    m_sessionsList->clear();
    QSet<QString> seenIds;

    for (const QJsonValue &val : sessions) {
        QJsonObject session = val.toObject();
        QString id = session["id"].toString();
        if (id.isEmpty() || seenIds.contains(id)) continue;
        seenIds.insert(id);

        QString title = session["title"].toString();
        QString directory = session["directory"].toString();

        if (title.isEmpty()) title = id.left(12) + "...";

        // Extract project name from directory path
        QString projectName;
        if (!directory.isEmpty()) {
            QDir dir(directory);
            projectName = dir.dirName();
        }

        // Build display text: title [uid] (project)
        QString displayText = title;
        displayText += " [" + id.left(8) + "]";
        if (!projectName.isEmpty()) {
            displayText += " (" + projectName + ")";
        }

        QListWidgetItem *item = new QListWidgetItem(displayText, m_sessionsList);
        item->setData(Qt::UserRole, id);
        item->setToolTip(id + "\n" + directory);

        if (id == m_currentSessionId) {
            item->setSelected(true);
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
            item->setBackground(QBrush(QColor(100, 149, 237, 80))); // Light blue highlight
        }
    }

    qCDebug(deepPluginLog) << "Sessions list updated:" << m_sessionsList->count() << "items, current:" << m_currentSessionId;
}

void DeepAssistantPluginView::onPromptSubmit()
{
    qCDebug(deepPluginLog) << "onPromptSubmit";

    QString prompt = m_promptEdit->toPlainText().trimmed();

    if (prompt.isEmpty()) {
        updatePromptWithDocument();
        prompt = m_promptEdit->toPlainText().trimmed();
    }

    if (prompt.isEmpty()) {
        m_previewer->setPlainText(i18n("No prompt provided and no document open."));
        return;
    }

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

        if (openCodeModel.isEmpty()) {
            m_previewer->setPlainText(i18n("OpenCode model not set."));
            return;
        }

        int slashIdx = openCodeModel.indexOf('/');
        QString providerID = (slashIdx > 0) ? openCodeModel.left(slashIdx) : openCodeModel;
        QString modelID = (slashIdx > 0) ? openCodeModel.mid(slashIdx + 1) : openCodeModel;

        m_submitPromptBtn->setEnabled(false);
        m_askAIButton->setEnabled(false);
        m_previewer->setPlainText(i18n("Processing..."));

        m_openCodeManager->sendRequest(openCodeUrl, openCodeUser, openCodePass, prompt, QString(),
                                       providerID, modelID, openCodeAgent, openCodeEffort);
    } else {
        KTextEditor::View *activeView = m_mainWindow->activeView();
        QString code = activeView && activeView->document() ? activeView->document()->text() : QString();

        bool usePicolm = group.readEntry("usePicolm", false);
        if (usePicolm) {
            QString modelPath = group.readEntry("picolmModelPath", QString());
            if (!modelPath.isEmpty()) {
                m_submitPromptBtn->setEnabled(false);
                m_askAIButton->setEnabled(false);
                m_previewer->setPlainText(i18n("Processing..."));
                runPicolm(modelPath, group);
                return;
            }
        }

        QString baseUrl = group.readEntry("baseUrl", "https://api.deepseek.com");
        bool useGpt4all = group.readEntry("useGpt4all", false);
        QString apiKey;
        if (!useGpt4all) {
            apiKey = group.readEntry("apiKey", QString());
        }

        if (!useGpt4all && apiKey.isEmpty()) {
            m_previewer->setPlainText(i18n("API key not set."));
            return;
        }

        QString model = group.readEntry("model", "deepseek-coder");
        double temperature = group.readEntry("temperature", 0.7);
        int maxTokens = group.readEntry("maxTokens", 4096);

        m_submitPromptBtn->setEnabled(false);
        m_askAIButton->setEnabled(false);
        m_previewer->setPlainText(i18n("Processing..."));

        m_networkManager->sendRequest(baseUrl, apiKey, prompt, code, model, temperature, maxTokens, useGpt4all);
    }
}

void DeepAssistantPluginView::updatePromptWithDocument()
{
    KTextEditor::View *activeView = m_mainWindow->activeView();
    if (!activeView || !activeView->document()) return;

    QString docText = activeView->document()->text();
    if (docText.isEmpty()) return;

    QString currentPrompt = m_promptEdit->toPlainText();
    if (currentPrompt.isEmpty()) {
        m_promptEdit->setPlainText(i18n("Analyze the following code:\n\n```cpp\n%1\n```").arg(docText));
    }
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
    m_submitPromptBtn->setEnabled(true);

    KSharedConfigPtr config = KSharedConfig::openConfig("kdeeprc");
    KConfigGroup group(config, "General");
    if (group.readEntry("useOpenCode", false)) {
        m_openCodeManager->fetchSessions(
            group.readEntry("openCodeUrl", "http://127.0.0.1:4096"),
            group.readEntry("openCodeUser", "opencode"),
            group.readEntry("openCodePass", QString()));
    }
}

void DeepAssistantPluginView::handlePicolmError(const QString &message)
{
    m_previewer->setPlainText(i18n("Error: %1", message));
    m_askAIButton->setEnabled(true);
    m_askAIButton->setText(i18n("Ask AI"));
    m_submitPromptBtn->setEnabled(true);
    m_newSessionBtn->setEnabled(true);
    m_newSessionBtn->setText(i18n("New Session"));
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

    m_askAIButton->setEnabled(false);
    m_askAIButton->setText(i18n("Asking..."));

    m_picolmWorker->setParams(modelPath, code, maxTokens, temperature, top_p, seed, threads);
    QMetaObject::invokeMethod(m_picolmWorker, "run", Qt::QueuedConnection);
}

void DeepAssistantPluginView::handlePermissionRequest(const QString &permissionId,
                                                     const QString &toolName,
                                                     const QString &description)
{
    qCDebug(deepPluginLog) << "Permission requested:" << toolName << description;

    m_previewer->setMarkdown(QStringLiteral("**AI requests permission:** %1\n\n%2").arg(toolName, description));

    QDialog dialog(m_toolview.get());
    dialog.setWindowTitle(i18n("AI Tool Permission Required"));
    dialog.setMinimumWidth(400);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QLabel *label = new QLabel(i18n("<b>AI wants to use tool:</b> %1<br><br>%2", toolName, description), &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No, &dialog);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    KSharedConfigPtr config = KSharedConfig::openConfig("kdeeprc");
    KConfigGroup group(config, "General");
    QString openCodeUrl = group.readEntry("openCodeUrl", "http://127.0.0.1:40647");
    QString openCodeUser = group.readEntry("openCodeUser", "opencode");
    QString openCodePass = group.readEntry("openCodePass", QString());

    bool approved = (dialog.exec() == QDialog::Accepted);
    m_openCodeManager->respondPermission(openCodeUrl, openCodeUser, openCodePass,
                                          m_openCodeManager->currentSessionId(),
                                          permissionId, approved);

    m_previewer->setMarkdown(approved
        ? QStringLiteral("**Status:** Tool approved, waiting for AI...")
        : QStringLiteral("**Status:** Tool denied, waiting for AI..."));
}

void DeepAssistantPluginView::onOpenProjectClicked()
{
    QString dir = QFileDialog::getExistingDirectory(m_toolview.get(),
        i18n("Select Git Repository or Project Directory"),
        QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (dir.isEmpty()) {
        return;
    }

    qCDebug(deepPluginLog) << "Opening project:" << dir;
    m_currentProjectPath = dir;

    KSharedConfigPtr config = KSharedConfig::openConfig("kdeeprc");
    KConfigGroup group(config, "General");

    m_openCodeManager->switchProject(
        group.readEntry("openCodeUrl", "http://127.0.0.1:40647"),
        group.readEntry("openCodeUser", "opencode"),
        group.readEntry("openCodePass", QString()),
        dir);

    m_previewer->setMarkdown(i18n("**Project opened:** %1\n\nUse *New Session* or *Ask AI* to start.", dir));
}

#include "plugin.moc"
