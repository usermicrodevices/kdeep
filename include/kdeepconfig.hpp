#pragma once
#include <QFormLayout>
#include <QFileDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QIcon>
#include <QPushButton>
#include <QHBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QDebug>
#include <QUrl>

#include <KLocalizedString>
#include <KConfigGroup>
#include <KSharedConfig>

#include <KTextEditor/ConfigPage>

#include "opencodemanager.hpp"

class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class KConfigGroup;
class OpenCodeManager;

class DeepConfig : public KTextEditor::ConfigPage
{
    Q_OBJECT
public:
    explicit DeepConfig(QWidget *parent = nullptr);
    ~DeepConfig() override;

    QString name() const override;
    QString fullName() const override;
    QIcon icon() const override;

public Q_SLOTS:
    void apply() override;
    void reset() override;
    void defaults() override;

private:
    void loadSettings();
    void saveSettings();

    QLineEdit *m_baseUrlEdit;
    QLineEdit *m_apiKeyEdit;
    QComboBox *m_modelCombo;
    QDoubleSpinBox *m_temperatureSpin;
    QSpinBox *m_maxTokensSpin;
    QCheckBox *m_useGpt4allCheck;
    QPushButton *m_refreshModelsBtn;
    KConfigGroup *m_config;
    QCheckBox   *m_usePicolmCheck;
    QLineEdit   *m_picolmModelPathEdit;
    QPushButton *m_picolmModelBrowseButton;
    QSpinBox *m_threadsSpin;
    QSpinBox *m_seedSpin;

    QCheckBox   *m_useOpenCodeCheck;
    QLineEdit   *m_openCodeUrlEdit;
    QLineEdit   *m_openCodeUserEdit;
    QLineEdit   *m_openCodePassEdit;
    QComboBox   *m_openCodeModelCombo;
    QComboBox   *m_openCodeAgentCombo;
    QComboBox   *m_openCodeEffortCombo;
    QPushButton *m_refreshOpenCodeBtn;
    OpenCodeManager *m_openCodeManager;

private slots:
    void refreshModels();
    void browsePicolmModel();
    void refreshOpenCodeProviders();

};
