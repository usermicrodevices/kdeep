#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <QNetworkRequest>

#include "networkmanager.hpp"

NetworkManager::NetworkManager(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &NetworkManager::onReplyFinished);
}

void NetworkManager::sendRequest(const QString &baseUrl, const QString &apiKey, const QString &prompt, const QString &code,
                                 const QString &model, double temperature, int maxTokens)
{
    QUrl url;
    QString path = "/chat/completions";
    if (baseUrl.endsWith('/')) { // remove leading slash from path
        url = QUrl(baseUrl + path.mid(1));
    } else {
        url = QUrl(baseUrl + path);
    }
    //QUrl url("https://api.deepseek.com/v1/chat/completions");
    //QUrl url("https://api.deepseek.com/chat/completions");
    QNetworkRequest request(url);

    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(apiKey).toUtf8());

    QJsonObject userMessage;
    userMessage["role"] = "user";
    userMessage["content"] = QString("%1\n\n```cpp\n%2\n```").arg(prompt, code);

    QJsonArray messages;
    messages.append(userMessage);

    QJsonObject jsonPayload;
    jsonPayload["model"] = model;
    jsonPayload["messages"] = messages;
    jsonPayload["temperature"] = temperature;
    jsonPayload["max_tokens"] = maxTokens;

    QJsonDocument doc(jsonPayload);
    QByteArray data = doc.toJson();

    m_networkManager->post(request, data);
}

void NetworkManager::onReplyFinished(QNetworkReply *reply)
{
    // Get HTTP status code (will be 0 for non-HTTP errors)
    int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray responseData = reply->readAll();

    // True network errors: no HTTP status code and a transport error
    if (httpCode == 0 && reply->error() != QNetworkReply::NoError) {
        emit requestFinished(QString("Network error: %1").arg(reply->errorString()));
        reply->deleteLater();
        return;
    }

    // HTTP error (4xx, 5xx)
    if (httpCode < 200 || httpCode >= 300) { // Try to parse the error JSON returned by the API
        QJsonDocument jsonResponse = QJsonDocument::fromJson(responseData);
        if (jsonResponse.isObject() && jsonResponse.object().contains("error")) {
            QJsonObject errorObj = jsonResponse.object()["error"].toObject();
            QString errorMsg = errorObj["message"].toString("Unknown API error");
            emit requestFinished(QString("API error (HTTP %1): %2").arg(httpCode).arg(errorMsg));
        } else { // Fallback: just show the raw response
            emit requestFinished(QString("HTTP error %1: %2").arg(httpCode).arg(QString(responseData)));
        }
        reply->deleteLater();
        return;
    }

    QJsonDocument jsonResponse = QJsonDocument::fromJson(responseData);
    if (!jsonResponse.isObject()) {
        emit requestFinished("Error: Invalid JSON response from API");
        reply->deleteLater();
        return;
    }

    QJsonObject root = jsonResponse.object();

    if (root.contains("error")) {
        QJsonObject errorObj = root["error"].toObject();
        QString errorMsg = errorObj["message"].toString("Unknown API error");
        emit requestFinished(QString("API error: %1").arg(errorMsg));
        reply->deleteLater();
        return;
    }

    QJsonArray choices = root["choices"].toArray();
    if (choices.isEmpty()) {
        emit requestFinished("Error: No choices in API response");
        reply->deleteLater();
        return;
    }

    QJsonObject firstChoice = choices[0].toObject();
    QJsonObject message = firstChoice["message"].toObject();
    QString content = message["content"].toString();

    if (content.isEmpty()) {
        emit requestFinished("Error: Empty content in API response");
    } else {
        emit requestFinished(content);
    }

    reply->deleteLater();
}
