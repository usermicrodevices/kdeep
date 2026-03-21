#pragma once
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QIcon>

#include <KLocalizedString>
#include <KConfigGroup>
#include <KSharedConfig>

#include <KTextEditor/ConfigPage>

class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class KConfigGroup;

class DeepSeekConfigPage : public KTextEditor::ConfigPage
{
    Q_OBJECT
public:
    explicit DeepSeekConfigPage(QWidget *parent = nullptr);
    ~DeepSeekConfigPage() override;

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
    KConfigGroup *m_config;
};
