#include "opencodemanager.hpp"

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

    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);

    if (!doc.isObject()) {
        emit error(QStringLiteral("Invalid response format"));
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray parts = root["parts"].toArray();

    QString textResult;
    bool hasPendingPermission = false;

    for (const QJsonValue &partVal : parts) {
        QJsonObject part = partVal.toObject();
        QString type = part["type"].toString();

        if (type == "text") {
            textResult += part["text"].toString();
        } else if (type == "tool") {
            QJsonObject state = part["state"].toObject();
            QString status = state["status"].toString();
            QString callID = part["callID"].toString();
            QString toolName = part["tool"].toString();

            if (status == "pending" && !callID.isEmpty()) {
                hasPendingPermission = true;
                QString description;
                QJsonObject input = state["input"].toObject();
                if (toolName == "bash") {
                    description = QStringLiteral("Execute: %1").arg(input["command"].toString());
                } else if (toolName == "edit") {
                    description = QStringLiteral("Edit: %1").arg(input["filePath"].toString());
                } else if (toolName == "write") {
                    description = QStringLiteral("Write: %1").arg(input["filePath"].toString());
                } else {
                    description = QStringLiteral("Tool: %1").arg(toolName);
                }
                emit permissionRequested(callID, toolName, description);
            }
        }
    }

    if (!textResult.isEmpty()) {
        emit requestFinished(textResult);
    } else if (!hasPendingPermission) {
        emit error(QStringLiteral("No text content in response"));
    }
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

void OpenCodeManager::fetchSessions(const QString &baseUrl,
                                    const QString &username,
                                    const QString &password)
{
    QUrl url(baseUrl);
    QString path = url.path();
    if (!path.endsWith('/')) path += '/';
    path += "session";
    url.setPath(path);

    qCDebug(deepOpenCodeLog) << "Fetching sessions from:" << url.toString();

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!username.isEmpty()) {
        request.setRawHeader("Authorization", buildAuthHeader(username, password));
    }

    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, &OpenCodeManager::onSessionsReply);
}

void OpenCodeManager::onSessionsReply()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        emit error(QStringLiteral("Invalid reply object"));
        return;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qCDebug(deepOpenCodeLog) << "Sessions request failed:" << reply->errorString() << "HTTP:" << httpCode;
        emit error(QStringLiteral("Failed to fetch sessions (HTTP %1): %2").arg(httpCode).arg(reply->errorString()));
        return;
    }

    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (!doc.isArray()) {
        emit error(QStringLiteral("Invalid sessions response (not JSON array)"));
        return;
    }

    QJsonArray sessions = doc.array();
    qCDebug(deepOpenCodeLog) << "Found" << sessions.size() << "sessions";
    emit sessionsReady(sessions);
}

void OpenCodeManager::createSession(const QString &baseUrl,
                                    const QString &username,
                                    const QString &password)
{
    QUrl url(baseUrl);
    QString path = url.path();
    if (!path.endsWith('/')) path += '/';
    path += "session";
    url.setPath(path);

    qCDebug(deepOpenCodeLog) << "Creating session at:" << url.toString();

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!username.isEmpty()) {
        request.setRawHeader("Authorization", buildAuthHeader(username, password));
    }

    QJsonObject body;
    body["title"] = QStringLiteral("KDeep Session");

    QJsonDocument doc(body);
    QNetworkReply *reply = m_networkManager->post(request, doc.toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
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

        QString sessionId = responseDoc.object()["id"].toString();
        if (sessionId.isEmpty()) {
            emit error(QStringLiteral("No session ID in response"));
            return;
        }

        qCDebug(deepOpenCodeLog) << "Session created:" << sessionId;
        emit sessionCreated(sessionId);
    });
}

void OpenCodeManager::fetchSessionMessages(const QString &baseUrl,
                                           const QString &username,
                                           const QString &password,
                                           const QString &sessionId)
{
    QUrl url(baseUrl);
    QString path = url.path();
    if (!path.endsWith('/')) path += '/';
    path += "session/" + sessionId + "/message";
    url.setPath(path);

    qCDebug(deepOpenCodeLog) << "Fetching messages for session:" << sessionId;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!username.isEmpty()) {
        request.setRawHeader("Authorization", buildAuthHeader(username, password));
    }

    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, &OpenCodeManager::onSessionMessagesReply);
}

void OpenCodeManager::onSessionMessagesReply()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        emit error(QStringLiteral("Invalid reply object"));
        return;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qCDebug(deepOpenCodeLog) << "Messages request failed:" << reply->errorString() << "HTTP:" << httpCode;
        emit error(QStringLiteral("Failed to fetch messages (HTTP %1): %2").arg(httpCode).arg(reply->errorString()));
        return;
    }

    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (!doc.isArray()) {
        emit error(QStringLiteral("Invalid messages response (not JSON array)"));
        return;
    }

    QJsonArray messages = doc.array();
    qCDebug(deepOpenCodeLog) << "Found" << messages.size() << "messages";
    emit sessionMessagesReady(messages);
}

void OpenCodeManager::abortSession(const QString &baseUrl,
                                   const QString &username,
                                   const QString &password)
{
    if (m_sessionId.isEmpty()) {
        emit error(QStringLiteral("No session to abort"));
        return;
    }

    QUrl url(baseUrl);
    QString path = url.path();
    if (!path.endsWith('/')) path += '/';
    path += "session/" + m_sessionId + "/abort";
    url.setPath(path);

    qCDebug(deepOpenCodeLog) << "Aborting session:" << m_sessionId;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!username.isEmpty()) {
        request.setRawHeader("Authorization", buildAuthHeader(username, password));
    }

    QJsonObject body;
    QJsonDocument doc(body);
    QNetworkReply *reply = m_networkManager->post(request, doc.toJson());
    connect(reply, &QNetworkReply::finished, this, &OpenCodeManager::onAbortReply);
}

void OpenCodeManager::onAbortReply()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        emit error(QStringLiteral("Invalid reply object"));
        return;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qCDebug(deepOpenCodeLog) << "Abort failed:" << reply->errorString() << "HTTP:" << httpCode;
        emit error(QStringLiteral("Failed to abort session (HTTP %1): %2").arg(httpCode).arg(reply->errorString()));
        return;
    }

    qCDebug(deepOpenCodeLog) << "Session aborted successfully";
}

void OpenCodeManager::respondPermission(const QString &baseUrl,
                                        const QString &username,
                                        const QString &password,
                                        const QString &sessionId,
                                        const QString &permissionId,
                                        bool approved)
{
    QUrl url(baseUrl);
    QString path = url.path();
    if (!path.endsWith('/')) path += '/';
    path += "session/" + sessionId + "/permissions/" + permissionId;
    url.setPath(path);

    qCDebug(deepOpenCodeLog) << "Responding to permission" << permissionId << "approved:" << approved;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!username.isEmpty()) {
        request.setRawHeader("Authorization", buildAuthHeader(username, password));
    }

    QJsonObject body;
    body["approved"] = approved;

    QJsonDocument doc(body);
    QNetworkReply *reply = m_networkManager->post(request, doc.toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, approved, baseUrl, username, password, sessionId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qCDebug(deepOpenCodeLog) << "Permission response failed:" << reply->errorString() << "HTTP:" << httpCode;
            emit error(QStringLiteral("Failed to respond to permission (HTTP %1): %2").arg(httpCode).arg(reply->errorString()));
        } else {
            qCDebug(deepOpenCodeLog) << "Permission responded successfully, approved:" << approved;
            emit progressUpdate(approved ? QStringLiteral("Tool approved, waiting for AI...") : QStringLiteral("Tool denied, waiting for AI..."));
        }
    });
}

void OpenCodeManager::switchProject(const QString &baseUrl,
                                    const QString &username,
                                    const QString &password,
                                    const QString &directory)
{
    qCDebug(deepOpenCodeLog) << "Switching project to:" << directory;

    // First, try to PATCH config to change the project directory
    QUrl url(baseUrl);
    QString path = url.path();
    if (!path.endsWith('/')) path += '/';
    path += "config";
    url.setPath(path);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!username.isEmpty()) {
        request.setRawHeader("Authorization", buildAuthHeader(username, password));
    }

    QJsonObject body;
    body["directory"] = directory;

    QJsonDocument doc(body);
    QNetworkReply *reply = m_networkManager->sendCustomRequest(request, "PATCH", doc.toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, directory]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qCDebug(deepOpenCodeLog) << "Project switch failed:" << reply->errorString() << "HTTP:" << httpCode;
            emit error(QStringLiteral("Failed to switch project (HTTP %1): %2").arg(httpCode).arg(reply->errorString()));
        } else {
            qCDebug(deepOpenCodeLog) << "Project switched to:" << directory;
            // Reset session so next request creates a new one in the new directory
            m_sessionId.clear();
        }
    });
}

void OpenCodeManager::renameSession(const QString &baseUrl,
                                    const QString &username,
                                    const QString &password,
                                    const QString &sessionId,
                                    const QString &newTitle)
{
    QUrl url(baseUrl);
    QString path = url.path();
    if (!path.endsWith('/')) path += '/';
    path += "session/" + sessionId;
    url.setPath(path);

    qCDebug(deepOpenCodeLog) << "Renaming session" << sessionId << "to:" << newTitle;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!username.isEmpty()) {
        request.setRawHeader("Authorization", buildAuthHeader(username, password));
    }

    QJsonObject body;
    body["title"] = newTitle;

    QJsonDocument doc(body);
    QNetworkReply *reply = m_networkManager->sendCustomRequest(request, "PATCH", doc.toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, sessionId, newTitle]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qCDebug(deepOpenCodeLog) << "Rename failed:" << reply->errorString() << "HTTP:" << httpCode;
            emit error(QStringLiteral("Failed to rename session (HTTP %1): %2").arg(httpCode).arg(reply->errorString()));
        } else {
            qCDebug(deepOpenCodeLog) << "Session renamed to:" << newTitle;
        }
    });
}

void OpenCodeManager::deleteSession(const QString &baseUrl,
                                    const QString &username,
                                    const QString &password,
                                    const QString &sessionId)
{
    QUrl url(baseUrl);
    QString path = url.path();
    if (!path.endsWith('/')) path += '/';
    path += "session/" + sessionId;
    url.setPath(path);

    qCDebug(deepOpenCodeLog) << "Deleting session" << sessionId;

    QNetworkRequest request(url);
    if (!username.isEmpty()) {
        request.setRawHeader("Authorization", buildAuthHeader(username, password));
    }

    QNetworkReply *reply = m_networkManager->deleteResource(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, sessionId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qCDebug(deepOpenCodeLog) << "Delete failed:" << reply->errorString() << "HTTP:" << httpCode;
            emit error(QStringLiteral("Failed to delete session (HTTP %1): %2").arg(httpCode).arg(reply->errorString()));
        } else {
            qCDebug(deepOpenCodeLog) << "Session deleted:" << sessionId;
        }
    });
}

void OpenCodeManager::archiveSession(const QString &baseUrl,
                                     const QString &username,
                                     const QString &password,
                                     const QString &sessionId)
{
    QUrl url(baseUrl);
    QString path = url.path();
    if (!path.endsWith('/')) path += '/';
    path += "session/" + sessionId;
    url.setPath(path);

    qCDebug(deepOpenCodeLog) << "Archiving session" << sessionId;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!username.isEmpty()) {
        request.setRawHeader("Authorization", buildAuthHeader(username, password));
    }

    QJsonObject body;
    body["archived"] = true;

    QJsonDocument doc(body);
    QNetworkReply *reply = m_networkManager->sendCustomRequest(request, "PATCH", doc.toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, sessionId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qCDebug(deepOpenCodeLog) << "Archive failed:" << reply->errorString() << "HTTP:" << httpCode;
            emit error(QStringLiteral("Failed to archive session (HTTP %1): %2").arg(httpCode).arg(reply->errorString()));
        } else {
            qCDebug(deepOpenCodeLog) << "Session archived:" << sessionId;
        }
    });
}

void OpenCodeManager::shareSession(const QString &baseUrl,
                                   const QString &username,
                                   const QString &password,
                                   const QString &sessionId)
{
    QUrl url(baseUrl);
    QString path = url.path();
    if (!path.endsWith('/')) path += '/';
    path += "session/" + sessionId + "/share";
    url.setPath(path);

    qCDebug(deepOpenCodeLog) << "Sharing session" << sessionId;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!username.isEmpty()) {
        request.setRawHeader("Authorization", buildAuthHeader(username, password));
    }

    QJsonObject body;
    QJsonDocument doc(body);
    QNetworkReply *reply = m_networkManager->post(request, doc.toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, sessionId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qCDebug(deepOpenCodeLog) << "Share failed:" << reply->errorString() << "HTTP:" << httpCode;
            emit error(QStringLiteral("Failed to share session (HTTP %1): %2").arg(httpCode).arg(reply->errorString()));
        } else {
            QByteArray responseData = reply->readAll();
            QJsonDocument responseDoc = QJsonDocument::fromJson(responseData);
            QString shareUrl;
            if (responseDoc.isObject()) {
                shareUrl = responseDoc.object()["url"].toString();
            }
            qCDebug(deepOpenCodeLog) << "Session shared:" << sessionId << "url:" << shareUrl;
            emit progressUpdate(QStringLiteral("Session shared: %1").arg(shareUrl));
        }
    });
}
