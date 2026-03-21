#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

class NetworkManager : public QObject
{
    Q_OBJECT
public:
    explicit NetworkManager(QObject *parent = nullptr);
    
    /**
     * Sends a request to the DeepSeek API.
     * @param apiKey The DeepSeek API key.
     * @param prompt The instruction/prompt for the AI.
     * @param code The code snippet to analyze.
     */
    void sendRequest(const QString &baseUrl, const QString &apiKey, const QString &prompt, const QString &code,
                     const QString &model="deepseek-coder", double temperature=0.7, int maxTokens=2048);

signals:
    /**
     * Emitted when the request is finished.
     * @param response The AI's response text, or an error message.
     */
    void requestFinished(const QString &response);

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager *m_networkManager;
};
