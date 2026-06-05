#include "opencodemanager.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QDebug>
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(deepOpenCodeLog, "kate.deep.opencode", QtDebugMsg)

OpenCodeManager::OpenCodeManager(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
}

QByteArray OpenCodeManager::buildAuthHeader(const QString &username, const QString &password)
{
    QString credentials = username + ":" + password;
    return "Basic " + credentials.toUtf8().toBase64();
}

QString OpenCodeManager::extractTextFromResponse(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        return QString();
    }

    QJsonObject root = doc.object();
    QJsonArray parts = root["parts"].toArray();

    QString result;
    for (const QJsonValue &val : parts) {
        QJsonObject part = val.toObject();
        if (part["type"].toString() == "text") {
            result += part["text"].toString();
        }
    }
    return result;
}

void OpenCodeManager::sendRequest(const QString &baseUrl,
                                  const QString &username,
                                  const QString &password,
                                  const QString &prompt,
                                  const QString &code,
                                  const QString &providerID,
                                  const QString &modelID,
                                  const QString &agent,
                                  const QString &thinkingEffort)
{
    qCDebug(deepOpenCodeLog) << "sendRequest to" << baseUrl << "model:" << providerID << "/" << modelID << "agent:" << agent << "effort:" << thinkingEffort;

    // If no session yet, create one first
    if (m_sessionId.isEmpty()) {
        QUrl createUrl(baseUrl);
        QString path = createUrl.path();
        if (!path.endsWith('/')) path += '/';
        path += "session";
        createUrl.setPath(path);

        QNetworkRequest request(createUrl);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        if (!username.isEmpty()) {
            request.setRawHeader("Authorization", buildAuthHeader(username, password));
        }

        QJsonObject body;
        body["title"] = QStringLiteral("KDeep Code Analysis");

        QJsonDocument doc(body);
        QNetworkReply *reply = m_networkManager->post(request, doc.toJson());

        connect(reply, &QNetworkReply::finished, this, [this, reply, baseUrl, username, password, prompt, code, providerID, modelID, agent, thinkingEffort]() {
            reply->deleteLater();

            if (reply->error() != QNetworkReply::NoError) {
                int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                qCDebug(deepOpenCodeLog) << "Session creation failed:" << reply->errorString() << "HTTP:" << httpCode;
                emit error(QStringLiteral("Failed to create session (HTTP %1): %2").arg(httpCode).arg(reply->errorString()));
                return;
            }

            QJsonDocument responseDoc = QJsonDocument::fromJson(reply->readAll());
            if (!responseDoc.isObject()) {
                emit error(QStringLiteral("Invalid session creation response"));
                return;
            }

            m_sessionId = responseDoc.object()["id"].toString();
            if (m_sessionId.isEmpty()) {
                emit error(QStringLiteral("No session ID in response"));
                return;
            }

            // Now send the message
            QUrl msgUrl(baseUrl);
            QString msgPath = msgUrl.path();
            if (!msgPath.endsWith('/')) msgPath += '/';
            msgPath += "session/" + m_sessionId + "/message";
            msgUrl.setPath(msgPath);

            QNetworkRequest msgRequest(msgUrl);
            msgRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            if (!username.isEmpty()) {
                msgRequest.setRawHeader("Authorization", buildAuthHeader(username, password));
            }

            QString fullPrompt = prompt + QStringLiteral("\n\n```cpp\n") + code + QStringLiteral("\n```");

            QJsonObject parts;
            parts["type"] = "text";
            parts["text"] = fullPrompt;

            QJsonArray partsArray;
            partsArray.append(parts);

            QJsonObject model;
            model["providerID"] = providerID;
            model["modelID"] = modelID;

            QJsonObject msgBody;
            msgBody["parts"] = partsArray;
            msgBody["model"] = model;
            if (!agent.isEmpty()) msgBody["agent"] = agent;
            if (!thinkingEffort.isEmpty()) msgBody["system"] = QStringLiteral("Thinking effort: %1").arg(thinkingEffort);

            QJsonDocument msgDoc(msgBody);
            QNetworkReply *msgReply = m_networkManager->post(msgRequest, msgDoc.toJson());
            connect(msgReply, &QNetworkReply::finished, this, &OpenCodeManager::onMessageReply);
        });
        return;
    }

    // Session exists, send message directly
    QUrl msgUrl(baseUrl);
    QString msgPath = msgUrl.path();
    if (!msgPath.endsWith('/')) msgPath += '/';
    msgPath += "session/" + m_sessionId + "/message";
    msgUrl.setPath(msgPath);

    QNetworkRequest msgRequest(msgUrl);
    msgRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!username.isEmpty()) {
        msgRequest.setRawHeader("Authorization", buildAuthHeader(username, password));
    }

    QString fullPrompt = prompt + QStringLiteral("\n\n```cpp\n") + code + QStringLiteral("\n```");

    QJsonObject textPart;
    textPart["type"] = "text";
    textPart["text"] = fullPrompt;

    QJsonArray partsArray;
    partsArray.append(textPart);

    QJsonObject model;
    model["providerID"] = providerID;
    model["modelID"] = modelID;

    QJsonObject msgBody;
    msgBody["parts"] = partsArray;
    msgBody["model"] = model;
    if (!agent.isEmpty()) msgBody["agent"] = agent;
    if (!thinkingEffort.isEmpty()) msgBody["system"] = QStringLiteral("Thinking effort: %1").arg(thinkingEffort);

    QJsonDocument msgDoc(msgBody);
    QNetworkReply *msgReply = m_networkManager->post(msgRequest, msgDoc.toJson());
    connect(msgReply, &QNetworkReply::finished, this, &OpenCodeManager::onMessageReply);
}

void OpenCodeManager::onSessionReply(QNetworkReply *reply)
{
    reply->deleteLater();
    // Handled in lambda above
}

void OpenCodeManager::onMessageReply()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        emit error(QStringLiteral("Invalid reply object"));
        return;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit error(QStringLiteral("OpenCode message failed: %1").arg(reply->errorString()));
        return;
    }

    QString text = extractTextFromResponse(reply->readAll());
    if (text.isEmpty()) {
        emit error(QStringLiteral("Empty response from OpenCode"));
        return;
    }

    emit requestFinished(text);
}

void OpenCodeManager::fetchProviders(const QString &baseUrl,
                                     const QString &username,
                                     const QString &password)
{
    QUrl url(baseUrl);
    QString path = url.path();
    if (!path.endsWith('/')) path += '/';
    path += "config/providers";
    url.setPath(path);

    qCDebug(deepOpenCodeLog) << "Fetching providers from:" << url.toString();
    qCDebug(deepOpenCodeLog) << "Auth user:" << username << "pass empty:" << password.isEmpty();

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!username.isEmpty()) {
        request.setRawHeader("Authorization", buildAuthHeader(username, password));
    }

    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, &OpenCodeManager::onProvidersReply);
}

void OpenCodeManager::onProvidersReply()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        emit error(QStringLiteral("Invalid reply object"));
        return;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qCDebug(deepOpenCodeLog) << "Providers request failed:" << reply->errorString() << "HTTP:" << httpCode;
        emit error(QStringLiteral("Failed to fetch providers (HTTP %1): %2").arg(httpCode).arg(reply->errorString()));
        return;
    }

    QByteArray responseData = reply->readAll();
    qCDebug(deepOpenCodeLog) << "Providers response size:" << responseData.size();

    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (!doc.isObject()) {
        emit error(QStringLiteral("Invalid providers response (not JSON object)"));
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray providers = root["providers"].toArray();
    qCDebug(deepOpenCodeLog) << "Found" << providers.size() << "providers";
    emit providersReady(providers);
}
