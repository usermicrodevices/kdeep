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
     * Sends a request to the DeepSeek (or GPT4All) API.
     * @param baseUrl API endpoint base URL (e.g., "https://api.deepseek.com")
     * @param apiKey API key (ignored if skipAuth is true)
     * @param prompt The instruction/prompt for the AI.
     * @param code The code snippet to analyze.
     * @param model Model name (e.g., "deepseek-coder")
     * @param temperature Sampling temperature (0.0–2.0)
     * @param maxTokens Maximum tokens in response
     * @param skipAuth If true, do not send Authorization header
     */
    void sendRequest(const QString &baseUrl, const QString &apiKey,
                     const QString &prompt, const QString &code,
                     const QString &model = "deepseek-coder",
                     double temperature = 0.7, int maxTokens = 2048,
                     bool skipAuth = false);

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
