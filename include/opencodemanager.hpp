#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

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

signals:
    void requestFinished(const QString &response);
    void providersReady(const QJsonArray &providers);
    void error(const QString &message);

private slots:
    void onSessionReply(QNetworkReply *reply);
    void onMessageReply();
    void onProvidersReply();

private:
    QNetworkAccessManager *m_networkManager;
    QString m_sessionId;

    QByteArray buildAuthHeader(const QString &username, const QString &password);
    QString extractTextFromResponse(const QByteArray &data);
};
