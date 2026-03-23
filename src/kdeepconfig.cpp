#include "kdeepconfig.hpp"

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
    m_maxTokensSpin->setRange(1, 8192);
    m_maxTokensSpin->setSingleStep(100);
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

    // Load current settings
    loadSettings();

    // Connect checkbox to dynamically hide/show API key field
    connect(m_useGpt4allCheck, &QCheckBox::toggled, this, [this](bool checked) {
        //ATTENTION: DO NOT CALL LINE BELOW - API KEY NOT POSSIBLE RESTORE LATER - IT WILL LOSE
        //if(checked){m_apiKeyEdit->clear();}
        if(checked)
        {
            if(m_usePicolmCheck->isChecked())
                m_usePicolmCheck->setChecked(false);
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
    });

    connect(m_usePicolmCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if(checked)
        {
            if(m_useGpt4allCheck->isChecked())
                m_useGpt4allCheck->setChecked(false);
        }
        if(m_refreshModelsBtn->isEnabled())
            m_refreshModelsBtn->setEnabled(false);
        m_baseUrlEdit->setEnabled(!checked);
        m_apiKeyEdit->setEnabled(!checked);
        m_picolmModelPathEdit->setEnabled(checked);
        m_picolmModelBrowseButton->setEnabled(checked);
        m_threadsSpin->setEnabled(checked);
        m_seedSpin->setEnabled(checked);
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
    m_threadsSpin->setValue(4);
    m_seedSpin->setValue(42);
    emit changed();
}

void DeepConfig::loadSettings()
{
    delete m_config; // avoid leak if called multiple times
    KSharedConfigPtr config = KSharedConfig::openConfig("kdeeprc");
    m_config = new KConfigGroup(config, "General");

    m_baseUrlEdit->setText(m_config->readEntry("baseUrl", "https://api.deepseek.com"));
    m_apiKeyEdit->setText(m_config->readEntry("apiKey", QString()));
    m_modelCombo->setCurrentText(m_config->readEntry("model", "deepseek-coder"));
    m_temperatureSpin->setValue(m_config->readEntry("temperature", 0.7));
    m_maxTokensSpin->setValue(m_config->readEntry("maxTokens", 4096));

    m_useGpt4allCheck->setChecked(m_config->readEntry("useGpt4all", false));

    m_usePicolmCheck->setChecked(m_config->readEntry("usePicolm", false));
    if (m_useGpt4allCheck->isChecked() && m_usePicolmCheck->isChecked())
    {
        qWarning() << "useGpt4all and usePicolm can't same time both be true";
        qWarning() << "auto correct it: useGpt4all=true and usePicolm=false";
        m_usePicolmCheck->setChecked(false);
    }

    m_picolmModelPathEdit->setText(m_config->readEntry("picolmModelPath", QString()));
    m_threadsSpin->setValue(m_config->readEntry("threads", 4));
    m_seedSpin->setValue(m_config->readEntry("seed", 42));

    if (m_useGpt4allCheck->isChecked())
    {
        m_apiKeyEdit->setEnabled(false);
        m_picolmModelBrowseButton->setEnabled(false);
        m_picolmModelPathEdit->setEnabled(false);
        m_threadsSpin->setEnabled(false);
        m_seedSpin->setEnabled(false);
    }
    else if (m_usePicolmCheck->isChecked())
    {
        m_baseUrlEdit->setEnabled(false);
        m_apiKeyEdit->setEnabled(false);
        m_refreshModelsBtn->setEnabled(false);
    }
    else
    {
        m_picolmModelBrowseButton->setEnabled(false);
        m_picolmModelPathEdit->setEnabled(false);
        m_threadsSpin->setEnabled(false);
        m_seedSpin->setEnabled(false);
        m_refreshModelsBtn->setEnabled(false);
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
    group.writeEntry("picolmModelPath", m_picolmModelPathEdit->text());
    group.writeEntry("threads", m_threadsSpin->value());
    group.writeEntry("seed", m_seedSpin->value());
    group.sync();
}

void DeepConfig::browsePicolmModel()
{
    QString file = QFileDialog::getOpenFileName(this, i18n("Select PicoLLM model file"),
                                                QString(), i18n("GGUF files (*.gguf)"));
    if (!file.isEmpty())
        m_picolmModelPathEdit->setText(file);
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
    if (!url.path().endsWith("/v1") && !url.path().contains("/v1")) {
        url.setPath(url.path() + (url.path().endsWith('/') ? "v1/models" : "/v1/models"));
    } else {
        url.setPath(url.path() + (url.path().endsWith('/') ? "models" : "/models"));
    }

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
