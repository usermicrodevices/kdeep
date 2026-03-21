#include "deepseekconfigpage.hpp"

DeepSeekConfigPage::DeepSeekConfigPage(QWidget *parent)
    : KTextEditor::ConfigPage(parent)
    , m_config(nullptr)   // initialize to nullptr
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
    layout->addRow(i18n("Model:"), m_modelCombo);

    m_temperatureSpin = new QDoubleSpinBox(this);
    m_temperatureSpin->setRange(0.0, 2.0);
    m_temperatureSpin->setSingleStep(0.1);
    m_temperatureSpin->setDecimals(1);
    layout->addRow(i18n("Temperature (0-2):"), m_temperatureSpin);

    m_maxTokensSpin = new QSpinBox(this);
    m_maxTokensSpin->setRange(1, 8192);
    m_maxTokensSpin->setSingleStep(100);
    layout->addRow(i18n("Max Tokens:"), m_maxTokensSpin);

    // Load current settings
    loadSettings();
}

DeepSeekConfigPage::~DeepSeekConfigPage()
{
    delete m_config;
}

QString DeepSeekConfigPage::name() const
{
    return i18n("DeepSeek");
}

QString DeepSeekConfigPage::fullName() const
{
    return i18n("DeepSeek AI Assistant");
}

QIcon DeepSeekConfigPage::icon() const
{
    return QIcon::fromTheme("preview");
}

void DeepSeekConfigPage::apply()
{
    saveSettings();
    emit changed();
}

void DeepSeekConfigPage::reset()
{
    loadSettings();
    emit changed();
}

void DeepSeekConfigPage::defaults()
{
    m_baseUrlEdit->setText("https://api.deepseek.com");
    m_apiKeyEdit->clear();
    m_modelCombo->setCurrentText("deepseek-coder");
    m_temperatureSpin->setValue(0.7);
    m_maxTokensSpin->setValue(4096);
    emit changed();
}

void DeepSeekConfigPage::loadSettings()
{
    KSharedConfigPtr config = KSharedConfig::openConfig("kdeepseekrc");
    m_config = new KConfigGroup(config, "General");
    m_baseUrlEdit->setText(m_config->readEntry("baseUrl", "https://api.deepseek.com"));
    m_apiKeyEdit->setText(m_config->readEntry("apiKey", QString()));
    m_modelCombo->setCurrentText(m_config->readEntry("model", "deepseek-coder"));
    m_temperatureSpin->setValue(m_config->readEntry("temperature", 0.7));
    m_maxTokensSpin->setValue(m_config->readEntry("maxTokens", 4096));
}

void DeepSeekConfigPage::saveSettings()
{
    KSharedConfigPtr config = KSharedConfig::openConfig("kdeepseekrc");
    KConfigGroup group(config, "General");
    group.writeEntry("baseUrl", m_baseUrlEdit->text());
    group.writeEntry("apiKey", m_apiKeyEdit->text());
    group.writeEntry("model", m_modelCombo->currentText());
    group.writeEntry("temperature", m_temperatureSpin->value());
    group.writeEntry("maxTokens", m_maxTokensSpin->value());
    group.sync();
}
