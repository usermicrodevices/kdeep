#include "kdeepconfig.hpp"
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(deepConfigLog, "kate.deep.config", QtDebugMsg)

DeepConfig::DeepConfig(QWidget *parent)
    : KTextEditor::ConfigPage(parent)
    , m_config(nullptr)
{
    QFormLayout *layout = new QFormLayout(this);

    m_baseUrlEdit = new QLineEdit(this);
    layout->addRow(i18n("Base URL:"), m_baseUrlEdit);
    m_baseUrlEdit->setPlaceholderText("https://api.deepseek.com");

    m_apiKeyEdit = new QLineEdit(this);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    layout->addRow(i18n("API Key:"), m_apiKeyEdit);

    m_modelCombo = new QComboBox(this);
    m_modelCombo->addItem("deepseek-chat");
    m_modelCombo->addItem("deepseek-coder");
    m_modelCombo->addItem("deepseek-reasoner");
    m_modelCombo->setEditable(true);
    m_modelCombo->setDuplicatesEnabled(false);

    QHBoxLayout *modelLayout = new QHBoxLayout;
    modelLayout->addWidget(m_modelCombo);
    m_refreshModelsBtn = new QPushButton(i18n("Refresh Models"), this);
    modelLayout->addWidget(m_refreshModelsBtn);
    layout->addRow(i18n("Model:"), modelLayout);

    connect(m_refreshModelsBtn, &QPushButton::clicked, this, &DeepConfig::refreshModels);

    m_temperatureSpin = new QDoubleSpinBox(this);
    m_temperatureSpin->setRange(0.0, 2.0);
    m_temperatureSpin->setSingleStep(0.1);
    m_temperatureSpin->setDecimals(1);
    layout->addRow(i18n("Temperature (0-2):"), m_temperatureSpin);

    m_maxTokensSpin = new QSpinBox(this);
    m_maxTokensSpin->setRange(1, 65536);
    m_maxTokensSpin->setSingleStep(256);
    layout->addRow(i18n("Max Tokens:"), m_maxTokensSpin);

    m_useGpt4allCheck = new QCheckBox(i18n("Use GPT4All API Server (local, any compatibility, no API key and model needed)"), this);
    layout->addRow(m_useGpt4allCheck);

    m_usePicolmCheck = new QCheckBox(i18n("Use local PicoLLM engine (requires .gguf model)"), this);
    m_picolmModelPathEdit = new QLineEdit(this);
    m_picolmModelBrowseButton = new QPushButton(i18n("Browse..."), this);

    layout->addRow(m_usePicolmCheck);
    layout->addRow(i18n("GGUF model file:"), m_picolmModelPathEdit);
    layout->addRow(QString(), m_picolmModelBrowseButton);
    m_threadsSpin = new QSpinBox(this);
    m_threadsSpin->setRange(1, 16);
    m_threadsSpin->setValue(4);
    layout->addRow(i18n("Threads:"), m_threadsSpin);

    m_seedSpin = new QSpinBox(this);
    m_seedSpin->setRange(0, 999999);
    m_seedSpin->setValue(42);
    layout->addRow(i18n("Seed:"), m_seedSpin);

    connect(m_picolmModelBrowseButton, &QPushButton::clicked, this, &DeepConfig::browsePicolmModel);

    m_useOpenCodeCheck = new QCheckBox(i18n("Use OpenCode Server (local AI coding agent)"), this);
    layout->addRow(m_useOpenCodeCheck);

    m_openCodeUrlEdit = new QLineEdit(this);
    m_openCodeUrlEdit->setPlaceholderText("http://127.0.0.1:4096");
    layout->addRow(i18n("OpenCode URL:"), m_openCodeUrlEdit);

    m_openCodeUserEdit = new QLineEdit(this);
    m_openCodeUserEdit->setPlaceholderText("opencode");
    layout->addRow(i18n("OpenCode Username:"), m_openCodeUserEdit);

    m_openCodePassEdit = new QLineEdit(this);
    m_openCodePassEdit->setEchoMode(QLineEdit::Password);
    layout->addRow(i18n("OpenCode Password:"), m_openCodePassEdit);

    m_openCodeModelCombo = new QComboBox(this);
    m_openCodeModelCombo->setEditable(true);
    m_openCodeModelCombo->setDuplicatesEnabled(false);

    QHBoxLayout *openCodeModelLayout = new QHBoxLayout;
    openCodeModelLayout->addWidget(m_openCodeModelCombo);
    m_refreshOpenCodeBtn = new QPushButton(i18n("Refresh"), this);
    openCodeModelLayout->addWidget(m_refreshOpenCodeBtn);
    layout->addRow(i18n("OpenCode Model:"), openCodeModelLayout);

    m_openCodeAgentCombo = new QComboBox(this);
    m_openCodeAgentCombo->addItem(i18n("Build (default)"), "build");
    m_openCodeAgentCombo->addItem(i18n("Plan"), "plan");
    m_openCodeAgentCombo->addItem(i18n("General"), "general");
    layout->addRow(i18n("OpenCode Agent:"), m_openCodeAgentCombo);

    m_openCodeEffortCombo = new QComboBox(this);
    m_openCodeEffortCombo->addItem(i18n("Default (model preset)"), "");
    m_openCodeEffortCombo->addItem(i18n("Low"), "low");
    m_openCodeEffortCombo->addItem(i18n("Medium"), "medium");
    m_openCodeEffortCombo->addItem(i18n("High"), "high");
    layout->addRow(i18n("Thinking Effort:"), m_openCodeEffortCombo);

    m_openCodeManager = new OpenCodeManager(this);
    connect(m_refreshOpenCodeBtn, &QPushButton::clicked, this, &DeepConfig::refreshOpenCodeProviders);
    connect(m_openCodeManager, &OpenCodeManager::providersReady, this, [this](const QJsonArray &providers) {
        m_openCodeModelCombo->clear();
        for (const QJsonValue &val : providers) {
            QJsonObject provider = val.toObject();
            QString providerID = provider["id"].toString();
            QJsonObject models = provider["models"].toObject();
            for (auto it = models.begin(); it != models.end(); ++it) {
                QString modelID = it.key();
                QString displayName = it.value().toObject()["name"].toString();
                if (displayName.isEmpty()) displayName = modelID;
                m_openCodeModelCombo->addItem(displayName, providerID + "/" + modelID);
            }
        }
        m_refreshOpenCodeBtn->setEnabled(true);
        if (m_openCodeModelCombo->count() > 0) {
            qCDebug(deepConfigLog) << "OpenCode: loaded" << m_openCodeModelCombo->count() << "models";
        }
    });
    connect(m_openCodeManager, &OpenCodeManager::error, this, [this](const QString &msg) {
        qCDebug(deepConfigLog) << "OpenCode provider fetch error:" << msg;
        m_refreshOpenCodeBtn->setEnabled(true);
    });

    loadSettings();

    connect(m_useGpt4allCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if(checked)
        {
            if(m_usePicolmCheck->isChecked())
                m_usePicolmCheck->setChecked(false);
            if(m_useOpenCodeCheck->isChecked())
                m_useOpenCodeCheck->setChecked(false);
        }
        if(!m_baseUrlEdit->isEnabled())
            m_baseUrlEdit->setEnabled(true);
        m_apiKeyEdit->setEnabled(!checked);
        m_refreshModelsBtn->setEnabled(checked);
        if(m_picolmModelPathEdit->isEnabled())
            m_picolmModelPathEdit->setEnabled(false);
        if(m_picolmModelBrowseButton->isEnabled())
            m_picolmModelBrowseButton->setEnabled(false);
        if(m_threadsSpin->isEnabled())
            m_threadsSpin->setEnabled(false);
        if(m_seedSpin->isEnabled())
            m_seedSpin->setEnabled(false);
        // OpenCode fields
        m_openCodeUrlEdit->setEnabled(!checked);
        m_openCodeUserEdit->setEnabled(!checked);
        m_openCodePassEdit->setEnabled(!checked);
        m_openCodeModelCombo->setEnabled(!checked);
        m_openCodeAgentCombo->setEnabled(!checked);
        m_openCodeEffortCombo->setEnabled(!checked);
        m_refreshOpenCodeBtn->setEnabled(!checked);
    });

    connect(m_usePicolmCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if(checked)
        {
            if(m_useGpt4allCheck->isChecked())
                m_useGpt4allCheck->setChecked(false);
            if(m_useOpenCodeCheck->isChecked())
                m_useOpenCodeCheck->setChecked(false);
        }
        if(m_refreshModelsBtn->isEnabled())
            m_refreshModelsBtn->setEnabled(false);
        m_baseUrlEdit->setEnabled(!checked);
        m_apiKeyEdit->setEnabled(!checked);
        m_picolmModelPathEdit->setEnabled(checked);
        m_picolmModelBrowseButton->setEnabled(checked);
        m_threadsSpin->setEnabled(checked);
        m_seedSpin->setEnabled(checked);
        // OpenCode fields
        m_openCodeUrlEdit->setEnabled(!checked);
        m_openCodeUserEdit->setEnabled(!checked);
        m_openCodePassEdit->setEnabled(!checked);
        m_openCodeModelCombo->setEnabled(!checked);
        m_openCodeAgentCombo->setEnabled(!checked);
        m_openCodeEffortCombo->setEnabled(!checked);
        m_refreshOpenCodeBtn->setEnabled(!checked);
    });

    connect(m_useOpenCodeCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if(checked)
        {
            if(m_useGpt4allCheck->isChecked())
                m_useGpt4allCheck->setChecked(false);
            if(m_usePicolmCheck->isChecked())
                m_usePicolmCheck->setChecked(false);
        }
        // DeepSeek fields
        m_baseUrlEdit->setEnabled(!checked);
        m_apiKeyEdit->setEnabled(!checked);
        m_refreshModelsBtn->setEnabled(!checked);
        // PicoLLM fields
        m_picolmModelPathEdit->setEnabled(!checked);
        m_picolmModelBrowseButton->setEnabled(!checked);
        m_threadsSpin->setEnabled(!checked);
        m_seedSpin->setEnabled(!checked);
        // OpenCode fields
        m_openCodeUrlEdit->setEnabled(checked);
        m_openCodeUserEdit->setEnabled(checked);
        m_openCodePassEdit->setEnabled(checked);
        m_openCodeModelCombo->setEnabled(checked);
        m_openCodeAgentCombo->setEnabled(checked);
        m_openCodeEffortCombo->setEnabled(checked);
        m_refreshOpenCodeBtn->setEnabled(checked);
    });
}

DeepConfig::~DeepConfig()
{
    delete m_config;
}

QString DeepConfig::name() const
{
    return i18n("KDeep");
}

QString DeepConfig::fullName() const
{
    return i18n("KDeep AI Assistant");
}

QIcon DeepConfig::icon() const
{
    return QIcon::fromTheme("preview");
}

void DeepConfig::apply()
{
    saveSettings();
    emit changed();
}

void DeepConfig::reset()
{
    loadSettings();
    emit changed();
}

void DeepConfig::defaults()
{
    m_baseUrlEdit->setText("https://api.deepseek.com");
    //m_apiKeyEdit->clear();
    m_apiKeyEdit->setEnabled(true);
    m_modelCombo->setCurrentText("deepseek-coder");
    m_temperatureSpin->setValue(0.7);
    m_maxTokensSpin->setValue(4096);
    m_useGpt4allCheck->setChecked(false);
    m_usePicolmCheck->setChecked(false);
    m_useOpenCodeCheck->setChecked(false);
    m_threadsSpin->setValue(4);
    m_seedSpin->setValue(42);
    m_openCodeUrlEdit->setText(qEnvironmentVariable("OPENCODE_SERVER_URL", "http://127.0.0.1:4096"));
    m_openCodeUserEdit->setText(qEnvironmentVariable("OPENCODE_SERVER_USERNAME", "opencode"));
    m_openCodePassEdit->setText(qEnvironmentVariable("OPENCODE_SERVER_PASSWORD", QString()));
    m_openCodeModelCombo->clear();
    m_openCodeAgentCombo->setCurrentIndex(m_openCodeAgentCombo->findData("build"));
    m_openCodeEffortCombo->setCurrentIndex(0);
    emit changed();
}

void DeepConfig::loadSettings()
{
    delete m_config;
    KSharedConfigPtr config = KSharedConfig::openConfig("kdeeprc");
    m_config = new KConfigGroup(config, "General");

    m_baseUrlEdit->setText(m_config->readEntry("baseUrl", "https://api.deepseek.com"));
    m_apiKeyEdit->setText(m_config->readEntry("apiKey", QString()));
    m_modelCombo->setCurrentText(m_config->readEntry("model", "deepseek-coder"));
    m_temperatureSpin->setValue(m_config->readEntry("temperature", 0.7));
    m_maxTokensSpin->setValue(m_config->readEntry("maxTokens", 4096));

    m_useGpt4allCheck->setChecked(m_config->readEntry("useGpt4all", false));

    m_usePicolmCheck->setChecked(m_config->readEntry("usePicolm", false));
    m_useOpenCodeCheck->setChecked(m_config->readEntry("useOpenCode", false));

    if (m_useGpt4allCheck->isChecked() && m_usePicolmCheck->isChecked())
    {
        qWarning() << "useGpt4all and usePicolm can't same time both be true";
        qWarning() << "auto correct it: useGpt4all=true and usePicolm=false";
        m_usePicolmCheck->setChecked(false);
    }
    if (m_useGpt4allCheck->isChecked() && m_useOpenCodeCheck->isChecked())
    {
        qWarning() << "useGpt4all and useOpenCode can't same time both be true";
        m_useOpenCodeCheck->setChecked(false);
    }
    if (m_usePicolmCheck->isChecked() && m_useOpenCodeCheck->isChecked())
    {
        qWarning() << "usePicolm and useOpenCode can't same time both be true";
        m_useOpenCodeCheck->setChecked(false);
    }

    m_picolmModelPathEdit->setText(m_config->readEntry("picolmModelPath", QString()));
    m_threadsSpin->setValue(m_config->readEntry("threads", 4));
    m_seedSpin->setValue(m_config->readEntry("seed", 42));

    m_openCodeUrlEdit->setText(m_config->readEntry("openCodeUrl", qEnvironmentVariable("OPENCODE_SERVER_URL", "http://127.0.0.1:4096")));
    m_openCodeUserEdit->setText(m_config->readEntry("openCodeUser", qEnvironmentVariable("OPENCODE_SERVER_USERNAME", "opencode")));
    m_openCodePassEdit->setText(m_config->readEntry("openCodePass", qEnvironmentVariable("OPENCODE_SERVER_PASSWORD", QString())));
    QString savedModel = m_config->readEntry("openCodeModel", QString());
    if (!savedModel.isEmpty()) {
        int idx = m_openCodeModelCombo->findData(savedModel);
        if (idx >= 0) m_openCodeModelCombo->setCurrentIndex(idx);
        else m_openCodeModelCombo->setEditText(savedModel);
    }
    QString savedAgent = m_config->readEntry("openCodeAgent", "build");
    int agentIdx = m_openCodeAgentCombo->findData(savedAgent);
    if (agentIdx >= 0) m_openCodeAgentCombo->setCurrentIndex(agentIdx);
    QString savedEffort = m_config->readEntry("openCodeEffort", "");
    int effortIdx = m_openCodeEffortCombo->findData(savedEffort);
    if (effortIdx >= 0) m_openCodeEffortCombo->setCurrentIndex(effortIdx);

    if (m_useGpt4allCheck->isChecked())
    {
        m_apiKeyEdit->setEnabled(false);
        m_picolmModelBrowseButton->setEnabled(false);
        m_picolmModelPathEdit->setEnabled(false);
        m_threadsSpin->setEnabled(false);
        m_seedSpin->setEnabled(false);
        m_openCodeUrlEdit->setEnabled(false);
        m_openCodeUserEdit->setEnabled(false);
        m_openCodePassEdit->setEnabled(false);
        m_openCodeModelCombo->setEnabled(false);
        m_openCodeAgentCombo->setEnabled(false);
        m_openCodeEffortCombo->setEnabled(false);
        m_refreshOpenCodeBtn->setEnabled(false);
    }
    else if (m_usePicolmCheck->isChecked())
    {
        m_baseUrlEdit->setEnabled(false);
        m_apiKeyEdit->setEnabled(false);
        m_refreshModelsBtn->setEnabled(false);
        m_openCodeUrlEdit->setEnabled(false);
        m_openCodeUserEdit->setEnabled(false);
        m_openCodePassEdit->setEnabled(false);
        m_openCodeModelCombo->setEnabled(false);
        m_openCodeAgentCombo->setEnabled(false);
        m_openCodeEffortCombo->setEnabled(false);
        m_refreshOpenCodeBtn->setEnabled(false);
    }
    else if (m_useOpenCodeCheck->isChecked())
    {
        m_baseUrlEdit->setEnabled(false);
        m_apiKeyEdit->setEnabled(false);
        m_refreshModelsBtn->setEnabled(false);
        m_picolmModelBrowseButton->setEnabled(false);
        m_picolmModelPathEdit->setEnabled(false);
        m_threadsSpin->setEnabled(false);
        m_seedSpin->setEnabled(false);
    }
    else
    {
        m_picolmModelBrowseButton->setEnabled(false);
        m_picolmModelPathEdit->setEnabled(false);
        m_threadsSpin->setEnabled(false);
        m_seedSpin->setEnabled(false);
        m_refreshModelsBtn->setEnabled(false);
        m_openCodeUrlEdit->setEnabled(false);
        m_openCodeUserEdit->setEnabled(false);
        m_openCodePassEdit->setEnabled(false);
        m_openCodeModelCombo->setEnabled(false);
        m_openCodeAgentCombo->setEnabled(false);
        m_openCodeEffortCombo->setEnabled(false);
        m_refreshOpenCodeBtn->setEnabled(false);
    }
}

void DeepConfig::saveSettings()
{
    KSharedConfigPtr config = KSharedConfig::openConfig("kdeeprc");
    KConfigGroup group(config, "General");
    group.writeEntry("baseUrl", m_baseUrlEdit->text());
    group.writeEntry("apiKey", m_apiKeyEdit->text());
    group.writeEntry("model", m_modelCombo->currentText());
    group.writeEntry("temperature", m_temperatureSpin->value());
    group.writeEntry("maxTokens", m_maxTokensSpin->value());
    group.writeEntry("useGpt4all", m_useGpt4allCheck->isChecked());
    group.writeEntry("usePicolm", m_usePicolmCheck->isChecked());
    group.writeEntry("useOpenCode", m_useOpenCodeCheck->isChecked());
    group.writeEntry("picolmModelPath", m_picolmModelPathEdit->text());
    group.writeEntry("threads", m_threadsSpin->value());
    group.writeEntry("seed", m_seedSpin->value());
    group.writeEntry("openCodeUrl", m_openCodeUrlEdit->text());
    group.writeEntry("openCodeUser", m_openCodeUserEdit->text());
    group.writeEntry("openCodePass", m_openCodePassEdit->text());
    group.writeEntry("openCodeModel", m_openCodeModelCombo->currentData().toString());
    group.writeEntry("openCodeAgent", m_openCodeAgentCombo->currentData().toString());
    group.writeEntry("openCodeEffort", m_openCodeEffortCombo->currentData().toString());
    group.sync();
}

void DeepConfig::browsePicolmModel()
{
    QString file = QFileDialog::getOpenFileName(this, i18n("Select PicoLLM model file"),
                                                QDir::homePath(), i18n("GGUF files (*.gguf)"));
    if (!file.isEmpty()) {
        if (file.startsWith(QDir::homePath())) {
            file = "~" + file.mid(QDir::homePath().length());
        }
        m_picolmModelPathEdit->setText(file);
    }
}

void DeepConfig::refreshModels()
{
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (btn) btn->setEnabled(false);

    QString baseUrl = m_baseUrlEdit->text();
    if (baseUrl.isEmpty()) {
        if (btn) btn->setEnabled(true);
        m_modelCombo->setFocus();
        return;
    }

    QUrl url(baseUrl);
    QString path = url.path();
    if (!path.endsWith('/')) path += '/';
    if (!path.contains("/v1/")) {
        path += "v1/";
    }
    path += "models";
    url.setPath(path);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkAccessManager *nam = new QNetworkAccessManager(this);
    QNetworkReply *reply = nam->get(request);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, nam, btn]() {

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);

            if (doc.isArray()) {
                QJsonArray models = doc.array();
                for (const QJsonValue &val : models) {
                    QJsonObject obj = val.toObject();
                    QString modelId = obj["id"].toString();
                    if (!modelId.isEmpty()) {
                        if (m_modelCombo->findText(modelId) < 0)
                        {
                            m_modelCombo->addItem(modelId);
                            m_modelCombo->setCurrentText(modelId);
                        }
                    }
                }
            } else if (doc.isObject()) {
                QJsonObject root = doc.object();
                if (root.contains("data") && root["data"].isArray()) {
                    QJsonArray models = root["data"].toArray();
                    for (const QJsonValue &val : models) {
                        QJsonObject obj = val.toObject();
                        QString modelId = obj["id"].toString();
                        if (!modelId.isEmpty()) {
                            if (m_modelCombo->findText(modelId) < 0)
                            {
                                m_modelCombo->addItem(modelId);
                                m_modelCombo->setCurrentText(modelId);
                            }
                        }
                    }
                }
            }
        } else {
            qWarning() << "Failed to fetch models:" << reply->errorString();
        }

        if (btn) btn->setEnabled(true);
        m_modelCombo->setFocus();

        reply->deleteLater();
        nam->deleteLater();
    });
}

void DeepConfig::refreshOpenCodeProviders()
{
    QString url = m_openCodeUrlEdit->text();
    if (url.isEmpty()) {
        m_openCodeUrlEdit->setFocus();
        return;
    }

    QString username = m_openCodeUserEdit->text();
    QString password = m_openCodePassEdit->text();

    m_refreshOpenCodeBtn->setEnabled(false);
    m_openCodeManager->fetchProviders(url, username, password);
}
