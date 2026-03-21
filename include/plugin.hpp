#pragma once

#include <memory>

#include <QTextBrowser>
#include <QPushButton>
#include <QIcon>
#include <QLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QDebug>
#include <QLoggingCategory>

#include <KPluginFactory>
#include <KXMLGUIClient>

#include <KTextEditor/Document>
#include <KTextEditor/MainWindow>
#include <KTextEditor/Plugin>
#include <KTextEditor/View>

#include "deepseekconfigpage.hpp"
#include "networkmanager.hpp"

class NetworkManager;
class DeepSeekAssistantPluginView;

class DeepSeekAssistantPlugin : public KTextEditor::Plugin
{
    Q_OBJECT
public:
    explicit DeepSeekAssistantPlugin(QObject *parent, const QList<QVariant> & = QList<QVariant>())
    : KTextEditor::Plugin(parent)
    {
    }

    QObject *createView(KTextEditor::MainWindow *mainWindow) override;

    // New overrides for KF6
    int configPages() const override;
    KTextEditor::ConfigPage* configPage(int number, QWidget* parent) override;
};

class DeepSeekAssistantPluginView : public QObject, public KXMLGUIClient
{
    Q_OBJECT
public:
    explicit DeepSeekAssistantPluginView(DeepSeekAssistantPlugin *plugin, KTextEditor::MainWindow *mainwindow);
    ~DeepSeekAssistantPluginView();

public slots:
    void onViewChanged(KTextEditor::View *v);
    void onAskAIClicked();
    void handleAIResponse(const QString &response);

private:
    KTextEditor::MainWindow *m_mainWindow = nullptr;
    std::unique_ptr<QWidget> m_toolview;
    QTextBrowser *m_previewer = nullptr;

    QPushButton *m_askAIButton = nullptr;
    NetworkManager *m_networkManager = nullptr;
};
