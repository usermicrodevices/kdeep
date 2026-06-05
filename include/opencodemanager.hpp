#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDebug>
#include <QLoggingCategory>

class OpenCodeManager : public QObject
{
    Q_OBJECT
public:
    explicit OpenCodeManager(QObject *parent = nullptr);

    void sendRequest(const QString &baseUrl,
                     const QString &username,
                     const QString &password,
                     const QString &prompt,
                     const QString &code,
                     const QString &providerID,
                     const QString &modelID,
                     const QString &agent = QString(),
                     const QString &thinkingEffort = QString());

    void fetchProviders(const QString &baseUrl,
                        const QString &username,
                        const QString &password);

    void fetchSessions(const QString &baseUrl,
                       const QString &username,
                       const QString &password);

    void createSession(const QString &baseUrl,
                       const QString &username,
                       const QString &password);

    void fetchSessionMessages(const QString &baseUrl,
                              const QString &username,
                              const QString &password,
                              const QString &sessionId);

    void abortSession(const QString &baseUrl,
                      const QString &username,
                      const QString &password);

    void respondPermission(const QString &baseUrl,
                           const QString &username,
                           const QString &password,
                           const QString &sessionId,
                           const QString &permissionId,
                           bool approved);

    void switchProject(const QString &baseUrl,
                       const QString &username,
                       const QString &password,
                       const QString &directory);

    void renameSession(const QString &baseUrl,
                       const QString &username,
                       const QString &password,
                       const QString &sessionId,
                       const QString &newTitle);

    void deleteSession(const QString &baseUrl,
                       const QString &username,
                       const QString &password,
                       const QString &sessionId);

    void archiveSession(const QString &baseUrl,
                        const QString &username,
                        const QString &password,
                        const QString &sessionId);

    void shareSession(const QString &baseUrl,
                      const QString &username,
                      const QString &password,
                      const QString &sessionId);

    QString currentSessionId() const { return m_sessionId; }
    void setCurrentSessionId(const QString &id) { m_sessionId = id; }

signals:
    void requestFinished(const QString &response);
    void providersReady(const QJsonArray &providers);
    void sessionsReady(const QJsonArray &sessions);
    void sessionCreated(const QString &sessionId);
    void sessionMessagesReady(const QJsonArray &messages);
    void progressUpdate(const QString &status);
    void permissionRequested(const QString &permissionId, const QString &toolName, const QString &description);
    void error(const QString &message);

private slots:
    void onSessionReply(QNetworkReply *reply);
    void onMessageReply();
    void onProvidersReply();
    void onSessionsReply();
    void onSessionMessagesReply();
    void onAbortReply();

private:
    QNetworkAccessManager *m_networkManager;
    QString m_sessionId;

    QByteArray buildAuthHeader(const QString &username, const QString &password);
    QString extractTextFromResponse(const QByteArray &data);
};
