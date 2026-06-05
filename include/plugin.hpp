#pragma once

#include <algorithm>
#include <atomic>
#include <csignal>
#include <vector>

#include <memory>
#include <exception>
#include <stdexcept>

#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QMutex>
#include <QTextBrowser>
#include <QTextEdit>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QIcon>
#include <QLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QDebug>
#include <QLoggingCategory>
#include <KPluginFactory>
#include <KXMLGUIClient>
#include <KTextEditor/Document>
#include <KTextEditor/MainWindow>
#include <KTextEditor/Plugin>
#include <KTextEditor/View>

// includes from thirdparty/picolm
extern "C" {
#include "model.h"
#include "tensor.h"
#include "tokenizer.h"
#include "sampler.h"
#include "grammar.h"
}

#include "kdeepconfig.hpp"
#include "networkmanager.hpp"
#include "opencodemanager.hpp"

class NetworkManager;
class OpenCodeManager;
class DeepAssistantPluginView;

class DeepAssistantPlugin : public KTextEditor::Plugin
{
    Q_OBJECT
public:
    explicit DeepAssistantPlugin(QObject *parent, const QList<QVariant> & = QList<QVariant>())
        : KTextEditor::Plugin(parent)
    {
    }

    QObject *createView(KTextEditor::MainWindow *mainWindow) override;
    int configPages() const override;
    KTextEditor::ConfigPage* configPage(int number, QWidget* parent) override;
};

class PicoLLMWorker : public QObject
{
    Q_OBJECT
public:
    static std::atomic<bool> s_sigfpeCaught;
    
    void setParams(const QString &modelPath,
                   const QString &prompt,
                   int maxTokens,
                   float temperature,
                   float top_p,
                   uint64_t seed,
                   int threads);
public slots:
    void run();
signals:
    void finished(const QString &output);
    void error(const QString &message);
private:
    QString m_modelPath;
    QString m_prompt;
    int m_maxTokens;
    float m_temperature;
    float m_top_p;
    uint64_t m_seed;
    int m_threads;
};

class DeepAssistantPluginView : public QObject, public KXMLGUIClient
{
    Q_OBJECT
public:
    explicit DeepAssistantPluginView(DeepAssistantPlugin *plugin, KTextEditor::MainWindow *mainwindow);
    ~DeepAssistantPluginView();

public slots:
    void onViewChanged(KTextEditor::View *v);
    void onAskAIClicked();
    void handleAIResponse(const QString &response);
    void handlePicolmError(const QString &message);

private slots:
    void runPicolm(const QString &modelPath, const KConfigGroup &group);
    void onNewSessionClicked();
    void onOpenProjectClicked();
    void onRefreshSessionsClicked();
    void onSessionSelected(QListWidgetItem *item);
    void onSessionContextMenu(const QPoint &pos);
    void onSessionsReady(const QJsonArray &sessions);
    void onPromptSubmit();
    void updatePromptWithDocument();
    void handlePermissionRequest(const QString &permissionId, const QString &toolName, const QString &description);

private:
    KTextEditor::MainWindow *m_mainWindow = nullptr;
    std::unique_ptr<QWidget> m_toolview;
    QTextBrowser *m_previewer = nullptr;
    QPushButton *m_askAIButton = nullptr;
    NetworkManager *m_networkManager = nullptr;
    OpenCodeManager *m_openCodeManager = nullptr;

    QListWidget *m_sessionsList = nullptr;
    QPushButton *m_newSessionBtn = nullptr;
    QPushButton *m_openProjectBtn = nullptr;
    QPushButton *m_refreshSessionsBtn = nullptr;

    QTextEdit *m_promptEdit = nullptr;
    QPushButton *m_submitPromptBtn = nullptr;

    QString m_currentSessionId;
    QString m_currentProjectPath;

    QThread *m_picolmThread = nullptr;
    PicoLLMWorker *m_picolmWorker = nullptr;
};
