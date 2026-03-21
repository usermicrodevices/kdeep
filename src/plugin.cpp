#include "plugin.hpp"

Q_LOGGING_CATEGORY(deepseekPluginLog, "kate.deepseek", QtDebugMsg)

K_PLUGIN_FACTORY_WITH_JSON(DeepSeekAssistantPluginFactory, "src/kdeepseek.json", registerPlugin<DeepSeekAssistantPlugin>();)

QObject *DeepSeekAssistantPlugin::createView(KTextEditor::MainWindow *mainWindow)
{
    qCDebug(deepseekPluginLog) << "Creating plugin view";
    return new DeepSeekAssistantPluginView(this, mainWindow);
}

int DeepSeekAssistantPlugin::configPages() const
{
    return 1; // we provide one configuration page
}

KTextEditor::ConfigPage* DeepSeekAssistantPlugin::configPage(int number, QWidget* parent)
{
    qCDebug(deepseekPluginLog) << "configPages called, creating config page";
    if (number == 0) {
        return new DeepSeekConfigPage(parent);
    }
    return nullptr;
}


DeepSeekAssistantPluginView::DeepSeekAssistantPluginView(DeepSeekAssistantPlugin *plugin, KTextEditor::MainWindow *mainwindow)
    : m_mainWindow(mainwindow)
{
    qCDebug(deepseekPluginLog) << "Plugin view constructor started";

    m_toolview.reset(m_mainWindow->createToolView(plugin,
        "kdeepseek",
        KTextEditor::MainWindow::Right,
        QIcon::fromTheme("preview"),
        i18n("DeepSeek Assistant")));
    qCDebug(deepseekPluginLog) << "Toolview created";

    if (!m_toolview->layout()) {
        QVBoxLayout *layout = new QVBoxLayout(m_toolview.get());
        m_toolview->setLayout(layout);
        qCDebug(deepseekPluginLog) << "Layout added to toolview";
    }

    m_previewer = new QTextBrowser(m_toolview.get());
    m_toolview->layout()->addWidget(m_previewer);
    qCDebug(deepseekPluginLog) << "Previewer created and added";

    m_askAIButton = new QPushButton(i18n("Ask DeepSeek"), m_toolview.get());
    m_toolview->layout()->addWidget(m_askAIButton);
    qCDebug(deepseekPluginLog) << "Button created";

    m_networkManager = new NetworkManager(this);
    qCDebug(deepseekPluginLog) << "NetworkManager created";

    connect(m_askAIButton, &QPushButton::clicked,
            this, &DeepSeekAssistantPluginView::onAskAIClicked);
    connect(m_networkManager, &NetworkManager::requestFinished,
            this, &DeepSeekAssistantPluginView::handleAIResponse);
    qCDebug(deepseekPluginLog) << "Connections made";

    connect(m_mainWindow, &KTextEditor::MainWindow::viewChanged,
            this, &DeepSeekAssistantPluginView::onViewChanged);
    qCDebug(deepseekPluginLog) << "View changed signal connected";

    qCDebug(deepseekPluginLog) << "Plugin view constructor finished";
}

DeepSeekAssistantPluginView::~DeepSeekAssistantPluginView()
{
    qCDebug(deepseekPluginLog) << "Plugin view destroyed";
}

void DeepSeekAssistantPluginView::onViewChanged(KTextEditor::View *v)
{
    Q_UNUSED(v);
    // If you want to keep the Markdown preview, uncomment the original code.
}

void DeepSeekAssistantPluginView::onAskAIClicked()
{
    qCDebug(deepseekPluginLog) << "onAskAIClicked called";
    KTextEditor::View *activeView = m_mainWindow->activeView();
    if (!activeView || !activeView->document()) {
        qCWarning(deepseekPluginLog) << "No active document to analyze";
        m_previewer->setPlainText(i18n("No active document to analyze."));
        return;
    }

    // Load settings from KConfig (the config page writes to this file)
    KSharedConfigPtr config = KSharedConfig::openConfig("kdeepseekrc");
    KConfigGroup group(config, "General");
    QString baseUrl = group.readEntry("baseUrl", "https://api.deepseek.com");
    QString apiKey = group.readEntry("apiKey", QString());
    if (apiKey.isEmpty()) {
        m_previewer->setPlainText(i18n("API key not set. Please configure it in Kate's settings (Settings → Configure Kate → Plugins → DeepSeek)."));
        return;
    }

    QString model = group.readEntry("model", "deepseek-coder");
    double temperature = group.readEntry("temperature", 0.7);
    int maxTokens = group.readEntry("maxTokens", 4096);

    QString code = activeView->document()->text();
    QString prompt = i18n("Explain the following C++ code and suggest improvements:");
    qCDebug(deepseekPluginLog) << "Sending request with prompt and code length:" << code.length();

    // Disable button and show waiting message
    m_askAIButton->setEnabled(false);
    m_askAIButton->setText(i18n("Asking..."));
    m_previewer->setPlainText(i18n("Asking DeepSeek, please wait..."));

    m_networkManager->sendRequest(baseUrl, apiKey, prompt, code, model, temperature, maxTokens);
}

void DeepSeekAssistantPluginView::handleAIResponse(const QString &response)
{
    qCDebug(deepseekPluginLog) << "handleAIResponse called, response length:" << response.length();
    m_previewer->setMarkdown(response);
    m_askAIButton->setEnabled(true);
    m_askAIButton->setText(i18n("Ask DeepSeek"));
}

#include "plugin.moc"
