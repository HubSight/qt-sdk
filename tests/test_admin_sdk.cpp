#include <hubsight/admin/hubsight_admin.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimeZone>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>

using namespace HubSight::Admin;

class TestHttpServer final : public QObject {
  Q_OBJECT

public:
  explicit TestHttpServer(QObject *parent = nullptr) : QObject(parent) {
    connect(&m_server, &QTcpServer::newConnection, this, [this]() {
      QTcpSocket *socket = m_server.nextPendingConnection();
      m_request.clear();
      m_socket = socket;
      connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        m_request.append(socket->readAll());
        const int headerEnd = m_request.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
          return;
        }
        const QByteArray headers = m_request.left(headerEnd);
        const int contentLengthHeader =
            headers.toLower().indexOf("content-length:");
        if (contentLengthHeader >= 0) {
          const int valueStart =
              contentLengthHeader + QByteArrayLiteral("content-length:").size();
          const int lineEnd = headers.indexOf("\r\n", valueStart);
          const QByteArray value =
              headers.mid(valueStart, lineEnd < 0 ? -1 : lineEnd - valueStart)
                  .trimmed();
          const int contentLength = value.toInt();
          if (m_request.size() < headerEnd + 4 + contentLength) {
            return;
          }
        }

        const QByteArray body = responseBodyFor(m_request);
        QByteArray response =
            "HTTP/1.1 " + QByteArray::number(m_status) +
            (m_status == 200 ? " OK\r\n" : " Service Unavailable\r\n");
        response += "Content-Type: application/json\r\n";
        response +=
            "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        if (m_retryAfter > 0) {
          response +=
              "Retry-After: " + QByteArray::number(m_retryAfter) + "\r\n";
        }
        response += "Connection: close\r\n\r\n";
        response += body;
        socket->write(response);
        socket->disconnectFromHost();
        emit requestReceived();
      });
    });
  }

  bool listen() { return m_server.listen(QHostAddress::LocalHost); }

  QUrl url() const {
    return QUrl(
        QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()));
  }
  QByteArray request() const { return m_request; }
  void setStatus(int status) { m_status = status; }
  void setRetryAfter(int seconds) { m_retryAfter = seconds; }
  void resetRequest() { m_request.clear(); }

signals:
  void requestReceived();

private:
  QByteArray responseBodyFor(const QByteArray &request) const {
    if (m_status == 503) {
      return R"({"status":"error","code":"ADMIN_API_DISABLED","error":"ADMIN_API_DISABLED","maintenance":true,"retry_after_seconds":120,"request_id":"req_maintenance"})";
    }
    if (request.contains("/auth/login")) {
      return R"({"status":"ok","token_type":"Bearer","access_token":"access-token","refresh_token":"refresh-token","expires_in":900,"client_id":"admin-client","user":{"id":"usr_1","username":"admin","role":"admin","is_active":true}})";
    }
    if (request.contains("/auth/refresh")) {
      return R"({"status":"ok","token_type":"Bearer","access_token":"access-token-2","refresh_token":"refresh-token-2","expires_in":900,"client_id":"admin-client"})";
    }
    if (request.contains("/live/capabilities")) {
      return R"({"data":{"max_concurrent_sessions":8,"supported_profiles":["balanced","low"],"supported_transports":["webrtc"],"media_transport":"webrtc","webrtc_supported":true,"trickle_ice_supported":true,"ice_servers":[{"urls":["stun:stun.example.com"]}]}})";
    }
    if (request.contains("/live/cameras/")) {
      return R"({"data":{"camera_id":"cam_1","status":"online","online":true,"available":true}})";
    }
    if (request.contains("/live/cameras")) {
      return R"({"data":[{"camera_id":"cam_1","name":"Front Door","online":true,"supported_profiles":["balanced"]}],"next_cursor":""})";
    }
    if (request.contains("/live/sessions:negotiate")) {
      return R"({"data":{"session_id":"live_1","camera_id":"cam_1","profile":"balanced","media_transport":"webrtc","remote_description":{"type":"answer","sdp":"v=0\\r\\n"},"ice_candidates":[{"candidate":"candidate:1 1 UDP 1 127.0.0.1 5000 typ host","sdpMid":"0","sdpMLineIndex":0}],"webrtc":{"iceServers":[{"urls":["stun:stun.example.com"]}]}}})";
    }
    if (request.contains("/archive/timeline")) {
      return R"({"data":{"items":[{"id":"rec_1","camera_id":"cam_1","start_at":"2026-09-18T10:00:00Z","end_at":"2026-09-18T10:01:00Z","duration_seconds":60,"size_bytes":1024,"created_at":"2026-09-18T10:02:00Z"}],"next_cursor":"cursor_2","has_more":true}})";
    }
    if (request.contains("/archive/cameras/") &&
        request.contains("/available-days")) {
      return R"({"data":{"camera_id":"cam_1","year":2026,"month":9,"days":[1,3,18]}})";
    }
    if (request.contains(":playback-url")) {
      return R"({"data":{"recording_id":"rec_1","url":"https://media.example.com/playback/rec_1","expires_at":"2026-09-18T11:00:00Z"}})";
    }
    if (request.contains(":download-url")) {
      return R"({"data":{"recording_id":"rec_1","url":"https://media.example.com/download/rec_1","expires_at":"2026-09-18T11:00:00Z"}})";
    }
    if (request.contains(":thumbnail-url")) {
      return R"({"data":{"recording_id":"rec_1","url":"https://media.example.com/thumbnail/rec_1","expires_at":"2026-09-18T11:00:00Z"}})";
    }
    if (request.contains("/archive/recordings/")) {
      return R"({"data":{"id":"rec_1","camera_id":"cam_1","start_at":"2026-09-18T10:00:00Z","end_at":"2026-09-18T10:01:00Z","duration_seconds":60,"size_bytes":1024,"created_at":"2026-09-18T10:02:00Z"}})";
    }
    if (request.contains("/notifications/push-config")) {
      return R"({"vapid_public_key":"vapid-key","firebase":{"projectId":"project-1"}})";
    }
    if (request.contains("/notifications/push-subscriptions/current")) {
      return R"({"status":"ok"})";
    }
    if (request.contains(":batch-delete")) {
      return R"({"status":"ok","deleted":2})";
    }
    if (request.contains(":read-all")) {
      return R"({"status":"ok","affected":3})";
    }
    if (request.contains(":clear")) {
      return R"({"status":"ok","deleted":4})";
    }
    if (request.contains(":test")) {
      return R"({"status":"sent"})";
    }
    if (request.contains("/notifications/notification_1")) {
      if (request.startsWith("GET")) {
        return R"({"data":{"id":"notification_1","camera_id":"cam_1","type":"person.entered","title":"Person detected","body":"Front door","category":"security","member_id":"member_1","thumbnail_url":"https://media.example.com/thumb.jpg","is_read":false,"created_at":"2026-09-18T10:00:00Z"}})";
      }
      return R"({"status":"ok"})";
    }
    if (request.contains("/notifications?")) {
      return R"({"data":{"notifications":[{"id":"notification_1","camera_id":"cam_1","type":"person.entered","title":"Person detected","body":"Front door","category":"security","is_read":false,"created_at":"2026-09-18T10:00:00Z"}],"unread_count":2,"next_cursor":"notification_cursor_2","has_more":true}})";
    }
    if (request.contains("/cameras")) {
      return R"({"data":[{"id":"cam_1","name":"Front Door","is_active":true,"is_stopped":false}],"next_cursor":""})";
    }
    return R"({"status":"ok","api_version":"v1","admin_api_enabled":true,"server_time":"2026-09-18T10:00:00Z","features":{}})";
  }

  QTcpServer m_server;
  QPointer<QTcpSocket> m_socket;
  QByteArray m_request;
  int m_status = 200;
  int m_retryAfter = 0;
};

class TestSocketIoServer final : public QObject {
  Q_OBJECT

public:
  explicit TestSocketIoServer(QObject *parent = nullptr)
      : QObject(parent),
        m_server(QStringLiteral("HubSight Socket.IO test server"),
                 QWebSocketServer::NonSecureMode, this) {
    m_pingTimer.setInterval(100);
    connect(&m_pingTimer, &QTimer::timeout, this, [this]() {
      if (m_socket) {
        m_socket->sendTextMessage(QStringLiteral("2"));
      }
    });
    connect(&m_server, &QWebSocketServer::newConnection, this, [this]() {
      m_socket = m_server.nextPendingConnection();
      connect(m_socket, &QWebSocket::disconnected, this,
              [this]() { m_pingTimer.stop(); });
      connect(m_socket, &QWebSocket::textMessageReceived, this,
              [this](const QString &message) {
                m_lastClientMessage = message.toUtf8();
                const QByteArray packet = m_lastClientMessage;
                if (packet == QByteArrayLiteral("3")) {
                  m_pongReceived = true;
                  return;
                }
                if (packet.startsWith(QByteArrayLiteral("41"))) {
                  m_namespaceDisconnectReceived = true;
                  return;
                }
                if (packet.startsWith(QByteArrayLiteral("43"))) {
                  m_serverAcknowledgementReceived = true;
                  return;
                }
                if (packet == QByteArrayLiteral("40")) {
                  m_socket->sendTextMessage(
                      QStringLiteral("40{\"sid\":\"namespace-1\"}"));
                  m_pingTimer.start();
                  return;
                }
                if (packet.startsWith(QByteArrayLiteral("42"))) {
                  int position = 2;
                  while (position < packet.size() &&
                         packet.at(position) >= '0' &&
                         packet.at(position) <= '9') {
                    ++position;
                  }
                  if (position > 2) {
                    m_socket->sendTextMessage(
                        QStringLiteral("43") +
                        QString::fromLatin1(packet.mid(2, position - 2)) +
                        QStringLiteral("[{\"accepted\":true}]"));
                  }
                }
              });
      m_socket->sendTextMessage(
          QStringLiteral("0{\"sid\":\"engine-1\",\"upgrades\":[],"
                         "\"pingInterval\":100,\"pingTimeout\":300}"));
    });
  }

  bool listen() { return m_server.listen(QHostAddress::LocalHost); }
  QUrl url() const { return m_server.serverUrl(); }
  QByteArray lastClientMessage() const { return m_lastClientMessage; }
  bool pongReceived() const { return m_pongReceived; }
  bool serverAcknowledgementReceived() const {
    return m_serverAcknowledgementReceived;
  }
  QByteArray handshakeHeader(const QByteArray &name) const {
    return m_socket ? m_socket->request().rawHeader(name) : QByteArray{};
  }
  bool namespaceDisconnectReceived() const {
    return m_namespaceDisconnectReceived;
  }

  void sendEvent() {
    QVERIFY(m_socket);
    m_socket->sendTextMessage(
        QStringLiteral("42[\"camera.updated\",{\"id\":\"cam_1\"}]"));
  }

  void sendEventRequiringAcknowledgement() {
    QVERIFY(m_socket);
    m_socket->sendTextMessage(
        QStringLiteral("421[\"server.notice\",{\"id\":\"notice_1\"}]"));
  }

  void sendNotification() {
    QVERIFY(m_socket);
    m_socket->sendTextMessage(
        QStringLiteral("42[\"notification.new\",{\"id\":\"notification_1\","
                       "\"title\":\"Person detected\",\"is_read\":false,"
                       "\"created_at\":\"2026-09-18T10:00:00Z\"}]"));
  }

  void sendSessionRevoked() {
    QVERIFY(m_socket);
    m_socket->sendTextMessage(
        QStringLiteral("42[\"session:revoked\",{\"session_id\":\"session_1\","
                       "\"reason\":\"admin_revoke\"}]"));
  }

  void closeClient() {
    if (m_socket) {
      m_socket->close();
    }
  }

private:
  QWebSocketServer m_server;
  QTimer m_pingTimer;
  QPointer<QWebSocket> m_socket;
  QByteArray m_lastClientMessage;
  bool m_pongReceived = false;
  bool m_serverAcknowledgementReceived = false;
  bool m_namespaceDisconnectReceived = false;
};

class TestStandardRelayServer final : public QObject {
  Q_OBJECT

public:
  explicit TestStandardRelayServer(QObject *parent = nullptr)
      : QObject(parent),
        m_server(QStringLiteral("HubSight Standard relay test server"),
                 QWebSocketServer::NonSecureMode, this) {
    connect(&m_server, &QWebSocketServer::newConnection, this, [this]() {
      m_socket = m_server.nextPendingConnection();
      m_lastClientMessage.clear();
      m_lastCommand = {};
      ++m_connectionCount;
      connect(m_socket, &QWebSocket::textMessageReceived, this,
              [this](const QString &message) {
                m_lastClientMessage = message.toUtf8();
                QJsonParseError parseError;
                const QJsonDocument document =
                    QJsonDocument::fromJson(m_lastClientMessage, &parseError);
                if (parseError.error != QJsonParseError::NoError ||
                    !document.isObject()) {
                  return;
                }
                m_lastCommand = document.object();
                emit commandReceived();
                const QJsonObject command = m_lastCommand;
                const QString requestId =
                    command.value(QStringLiteral("request_id")).toString();
                if (requestId.isEmpty() || !m_socket) {
                  return;
                }
                const QString name =
                    command.value(QStringLiteral("command")).toString();
                QJsonObject response{{QStringLiteral("request_id"), requestId}};
                if (name == QStringLiteral("subscribe")) {
                  response.insert(QStringLiteral("status"),
                                  QStringLiteral("subscribed"));
                  response.insert(QStringLiteral("accepted"), true);
                } else if (name == QStringLiteral("unsubscribe")) {
                  response.insert(QStringLiteral("status"),
                                  QStringLiteral("unsubscribed"));
                  response.insert(QStringLiteral("accepted"), true);
                } else if (name == QStringLiteral("resume")) {
                  if (m_replayUnavailable) {
                    response.insert(QStringLiteral("status"),
                                    QStringLiteral("replay_unavailable"));
                    response.insert(QStringLiteral("replay_available"), false);
                    response.insert(QStringLiteral("reason"),
                                    QStringLiteral("history expired"));
                  } else {
                    response.insert(QStringLiteral("status"),
                                    QStringLiteral("resumed"));
                    response.insert(QStringLiteral("resumed"), true);
                  }
                } else if (name == QStringLiteral("ping")) {
                  response.insert(QStringLiteral("status"),
                                  QStringLiteral("ok"));
                }
                m_socket->sendTextMessage(QString::fromUtf8(
                    QJsonDocument(response).toJson(QJsonDocument::Compact)));
              });
    });
  }

  bool listen() { return m_server.listen(QHostAddress::LocalHost); }
  QUrl url() const { return m_server.serverUrl(); }
  QByteArray lastClientMessage() const { return m_lastClientMessage; }
  QJsonObject lastCommand() const { return m_lastCommand; }
  int connectionCount() const { return m_connectionCount; }
  QByteArray handshakeHeader(const QByteArray &name) const {
    return m_socket ? m_socket->request().rawHeader(name) : QByteArray{};
  }

  void sendCameraUpdated() {
    sendEvent(
        QStringLiteral("evt_camera_1"), QStringLiteral("camera.updated"),
        QJsonObject{{QStringLiteral("id"), QStringLiteral("cam_1")},
                    {QStringLiteral("status"), QStringLiteral("online")}});
  }

  void sendNotification() {
    sendEvent(QStringLiteral("evt_notification_1"),
              QStringLiteral("notification.new"),
              QJsonObject{
                  {QStringLiteral("id"), QStringLiteral("notification_1")},
                  {QStringLiteral("title"), QStringLiteral("Person detected")},
                  {QStringLiteral("is_read"), false},
                  {QStringLiteral("created_at"),
                   QStringLiteral("2026-09-18T10:00:00Z")}});
  }

  void sendSessionRevoked() {
    sendEvent(QStringLiteral("evt_revoked_1"),
              QStringLiteral("session.revoked"),
              QJsonObject{
                  {QStringLiteral("reason"), QStringLiteral("admin_revoke")}});
  }

  void setReplayUnavailable(bool unavailable) {
    m_replayUnavailable = unavailable;
  }

  void closeClient() {
    if (m_socket) {
      m_socket->close();
    }
  }

signals:
  void commandReceived();

private:
  void sendEvent(const QString &eventId, const QString &topic,
                 const QJsonObject &data) {
    QVERIFY(m_socket);
    const QJsonObject event{
        {QStringLiteral("event_id"), eventId},
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("topic"), topic},
        {QStringLiteral("timestamp"), QStringLiteral("2026-09-18T10:00:00Z")},
        {QStringLiteral("data"), data},
    };
    m_socket->sendTextMessage(
        QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact)));
  }

  QWebSocketServer m_server;
  QPointer<QWebSocket> m_socket;
  QByteArray m_lastClientMessage;
  QJsonObject m_lastCommand;
  bool m_replayUnavailable = false;
  int m_connectionCount = 0;
};

class AdminSdkTest final : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() {
    qRegisterMetaType<AdminError>();
    qRegisterMetaType<AdminEndpoint>();
    qRegisterMetaType<HttpProtocol>();
    qRegisterMetaType<MaintenanceInfo>();
    qRegisterMetaType<SystemStatus>();
    qRegisterMetaType<TokenSet>();
    qRegisterMetaType<AdminUser>();
    qRegisterMetaType<CameraPage>();
    qRegisterMetaType<RecordingSegment>();
    qRegisterMetaType<ArchiveTimelinePage>();
    qRegisterMetaType<ArchiveAvailableDays>();
    qRegisterMetaType<ArchiveUrlResult>();
    qRegisterMetaType<Notification>();
    qRegisterMetaType<NotificationPage>();
    qRegisterMetaType<NotificationActionResult>();
    qRegisterMetaType<NotificationPushConfig>();
    qRegisterMetaType<RealtimeEvent>();
    qRegisterMetaType<RelayEvent>();
    qRegisterMetaType<RelayError>();
    qRegisterMetaType<SocketIoError>();
  }

  void endpointCatalogCoversAdminApiV1Surface() {
    const QVector<AdminEndpointDefinition> endpoints = adminEndpointCatalog();
    QCOMPARE(endpoints.size(), 136);
    QCOMPARE(adminEndpointDefinition(AdminEndpoint::RealtimeRelay).pathTemplate,
             QStringLiteral("/relay/admin/v1"));
    QCOMPARE(adminEndpointDefinition(AdminEndpoint::CamerasCreate).method,
             QByteArray("POST"));
    QCOMPARE(adminEndpointDefinition(AdminEndpoint::ArchiveTimeline).phase,
             QStringLiteral("Phase 2"));
    QCOMPARE(adminEndpointDefinition(AdminEndpoint::NotificationsList).phase,
             QStringLiteral("Phase 2"));

    AdminClient client;
    QSignalSpy errorSpy(&client, &AdminClient::errorOccurred);
    client.api()->cameraPatch(
        QJsonObject{{QStringLiteral("camera_id"), QStringLiteral("cam_1")}});
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(errorSpy.at(0).at(0).value<AdminError>().serverCode,
             QStringLiteral("SDK_ENDPOINT_NOT_IMPLEMENTED"));
  }

  void statusUsesAdminHeadersAndNamespace() {
    TestHttpServer server;
    QVERIFY(server.listen());

    AdminClient client;
    QVERIFY(client.setGatewayUrl(server.url()));
    client.setApiKey(QStringLiteral("admin-desktop-key"));

    QSignalSpy statusSpy(client.system(), &SystemClient::statusReceived);
    QSignalSpy completionSpy(&client, &AdminClient::requestCompleted);
    client.system()->fetchStatus();
    QVERIFY(completionSpy.wait(2000));
    QCOMPARE(statusSpy.count(), 1);
    QCOMPARE(completionSpy.count(), 1);
    QCOMPARE(completionSpy.at(0).at(0).toString(),
             QStringLiteral("system.status"));
    QCOMPARE(completionSpy.at(0).at(1).value<HttpProtocol>(),
             HttpProtocol::Http1_1);

    const QByteArray request = server.request();
    QVERIFY(request.startsWith("GET /api/admin/v1/system/status HTTP/1.1"));
    QVERIFY(request.toLower().contains("x-api-key: admin-desktop-key"));
    QVERIFY(!request.contains("Authorization:"));
    QVERIFY(!request.contains("Cookie:"));
    QVERIFY(!request.contains("api_key="));
    QVERIFY(!request.contains("token="));
  }

  void protectedRequestUsesBearerAndRotatesRefreshToken() {
    TestHttpServer server;
    QVERIFY(server.listen());

    auto storage = std::make_shared<InMemorySecureStorage>();
    AdminClient client(storage);
    QVERIFY(client.setGatewayUrl(server.url()));
    client.setApiKey(QStringLiteral("admin-desktop-key"));

    QSignalSpy loginSpy(client.auth(), &AuthManager::loginSucceeded);
    client.auth()->login(QStringLiteral("admin"), QStringLiteral("password"));
    QVERIFY(loginSpy.wait(2000));
    QCOMPARE(
        storage->read(QStringLiteral("hubsight.admin.refresh_token")).value(),
        QByteArray("refresh-token"));

    server.resetRequest();
    QSignalSpy refreshSpy(client.auth(), &AuthManager::tokenRefreshed);
    client.auth()->refresh();
    QVERIFY(refreshSpy.wait(2000));
    QCOMPARE(
        storage->read(QStringLiteral("hubsight.admin.refresh_token")).value(),
        QByteArray("refresh-token-2"));
    QVERIFY(server.request().startsWith(
        "POST /api/admin/v1/auth/refresh HTTP/1.1"));
    QVERIFY(!server.request().contains("Authorization:"));

    server.resetRequest();
    QSignalSpy cameraSpy(client.cameras(), &CameraClient::pageReceived);
    client.cameras()->list();
    QVERIFY(cameraSpy.wait(2000));
    const QByteArray request = server.request();
    QVERIFY(request.startsWith("GET /api/admin/v1/cameras?limit=50 HTTP/1.1"));
    QVERIFY(request.toLower().contains("x-api-key: admin-desktop-key"));
    QVERIFY(request.contains("Authorization: Bearer access-token-2"));
    QVERIFY(!request.contains("?api_key="));
    QVERIFY(!request.contains("?token="));
  }

  void socketIoBaseSupportsHandshakeEventsAndAck() {
    TestSocketIoServer server;
    QVERIFY(server.listen());

    SocketIoClient client;
    QVERIFY(client.setBaseUrl(server.url()));
    client.setReconnectEnabled(false);

    QSignalSpy connectedSpy(&client, &SocketIoClient::connected);
    QSignalSpy eventSpy(&client, &SocketIoClient::eventReceived);
    QSignalSpy eventAcknowledgementSpy(
        &client, &SocketIoClient::eventAcknowledgementRequested);
    QSignalSpy acknowledgementSpy(&client,
                                  &SocketIoClient::acknowledgementReceived);

    client.connectToServer();
    QVERIFY(connectedSpy.wait(2000));
    QVERIFY(client.isConnected());
    QTRY_VERIFY_WITH_TIMEOUT(server.pongReceived(), 2000);
    QCOMPARE(connectedSpy.at(0).at(0).toString(), QStringLiteral("/"));
    QCOMPARE(connectedSpy.at(0).at(1).toString(),
             QStringLiteral("namespace-1"));

    server.sendEvent();
    QVERIFY(eventSpy.wait(2000));
    QCOMPARE(eventSpy.at(0).at(0).toString(), QStringLiteral("camera.updated"));
    const QJsonArray eventArguments = eventSpy.at(0).at(1).toJsonArray();
    QCOMPARE(eventArguments.size(), 1);
    QCOMPARE(eventArguments.at(0).toObject().value(QStringLiteral("id")),
             QJsonValue(QStringLiteral("cam_1")));

    server.sendEventRequiringAcknowledgement();
    QVERIFY(eventAcknowledgementSpy.wait(2000));
    QCOMPARE(eventAcknowledgementSpy.at(0).at(1).toString(),
             QStringLiteral("server.notice"));
    const quint64 serverAcknowledgementId =
        eventAcknowledgementSpy.at(0).at(0).toULongLong();
    QJsonArray serverAcknowledgementArguments;
    serverAcknowledgementArguments.append(
        QJsonObject{{QStringLiteral("received"), true}});
    QVERIFY(client.acknowledge(serverAcknowledgementId,
                               serverAcknowledgementArguments));
    QTRY_VERIFY_WITH_TIMEOUT(server.serverAcknowledgementReceived(), 2000);

    QJsonArray arguments;
    arguments.append(
        QJsonObject{{QStringLiteral("camera_id"), QStringLiteral("cam_1")}});
    const quint64 acknowledgementId =
        client.emitEvent(QStringLiteral("camera.subscribe"), arguments, true);
    QVERIFY(acknowledgementId != 0);
    QVERIFY(acknowledgementSpy.wait(2000));
    QCOMPARE(acknowledgementSpy.at(0).at(0).toULongLong(), acknowledgementId);
    QCOMPARE(
        acknowledgementSpy.at(0).at(1).toJsonArray().at(0).toObject().value(
            QStringLiteral("accepted")),
        QJsonValue(true));

    client.disconnectFromServer();
    QTRY_VERIFY_WITH_TIMEOUT(server.namespaceDisconnectReceived(), 2000);
    QCOMPARE(client.state(), SocketIoClient::State::Disconnected);
  }

  void adminClientConfiguresRealtimeBase() {
    TestSocketIoServer server;
    QVERIFY(server.listen());

    QUrl gateway = server.url();
    gateway.setScheme(QStringLiteral("http"));
    AdminClient client;
    QVERIFY(client.setGatewayUrl(gateway));
    client.setApiKey(QStringLiteral("admin-desktop-key"));
    QCOMPARE(client.realtime()->baseUrl().scheme(), QStringLiteral("ws"));
    QCOMPARE(client.realtime()->path(), QStringLiteral("/socket.io/"));
    QVERIFY(client.socketIoRealtime());
    QCOMPARE(client.socketIoRealtime()->transport(), client.realtime());

    client.realtime()->setReconnectEnabled(false);
    QSignalSpy connectedSpy(client.realtime(), &SocketIoClient::connected);
    client.realtime()->connectToServer();
    QVERIFY(connectedSpy.wait(2000));
    QCOMPARE(server.handshakeHeader("X-API-Key"),
             QByteArray("admin-desktop-key"));

    client.realtime()->disconnectFromServer();
  }

  void socketIoRealtimeDomainSupportsRoomsAndTypedEvents() {
    TestSocketIoServer server;
    QVERIFY(server.listen());

    SocketIoClient transport;
    QVERIFY(transport.setBaseUrl(server.url()));
    transport.setReconnectDelays(10, 50);
    transport.setMaxReconnectAttempts(3);
    SocketIoRealtimeClient realtime(&transport);

    QSignalSpy connectedSpy(&transport, &SocketIoClient::connected);
    QSignalSpy roomJoinedSpy(&realtime, &SocketIoRealtimeClient::roomJoined);
    QSignalSpy roomLeftSpy(&realtime, &SocketIoRealtimeClient::roomLeft);
    QSignalSpy roomErrorSpy(&realtime,
                            &SocketIoRealtimeClient::roomOperationFailed);
    QSignalSpy errorSpy(&realtime, &SocketIoRealtimeClient::errorOccurred);
    QSignalSpy cameraSpy(&realtime, &SocketIoRealtimeClient::cameraEvent);
    QSignalSpy notificationSpy(&realtime,
                               &SocketIoRealtimeClient::notificationReceived);
    QSignalSpy revokedSpy(&realtime, &SocketIoRealtimeClient::sessionRevoked);

    transport.connectToServer();
    QVERIFY(connectedSpy.wait(2000));

    const quint64 joinId = realtime.subscribeRoom(QStringLiteral("room_1"));
    QVERIFY(joinId != 0);
    QVERIFY(roomJoinedSpy.wait(2000));
    QCOMPARE(realtime.rooms(), QStringList{QStringLiteral("room_1")});
    QVERIFY(server.lastClientMessage().contains("join_room"));

    server.sendEvent();
    QVERIFY(cameraSpy.wait(2000));
    const RealtimeEvent cameraEvent =
        cameraSpy.at(0).at(0).value<RealtimeEvent>();
    QCOMPARE(cameraEvent.name, QStringLiteral("camera.updated"));
    QCOMPARE(cameraEvent.data.value(QStringLiteral("id")),
             QJsonValue(QStringLiteral("cam_1")));

    server.sendNotification();
    QVERIFY(notificationSpy.wait(2000));
    const Notification notification =
        notificationSpy.at(0).at(0).value<Notification>();
    QCOMPARE(notification.id, QStringLiteral("notification_1"));
    QCOMPARE(notification.title, QStringLiteral("Person detected"));

    server.sendSessionRevoked();
    QVERIFY(revokedSpy.wait(2000));
    QCOMPARE(revokedSpy.at(0).at(0).value<RealtimeEvent>().data.value(
                 QStringLiteral("reason")),
             QJsonValue(QStringLiteral("admin_revoke")));

    const quint64 leaveId = realtime.unsubscribeRoom(QStringLiteral("room_1"));
    QVERIFY(leaveId != 0);
    QVERIFY(roomLeftSpy.wait(2000));
    QVERIFY(realtime.rooms().isEmpty());
    QVERIFY(server.lastClientMessage().contains("leave_room"));

    QCOMPARE(realtime.subscribeRoom(QStringLiteral("bad room")), quint64(0));
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(errorSpy.at(0).at(0).value<SocketIoError>().code,
             SocketIoErrorCode::InvalidConfiguration);

    const quint64 reconnectJoinId =
        realtime.subscribeRoom(QStringLiteral("room_2"));
    QVERIFY(reconnectJoinId != 0);
    QVERIFY(roomJoinedSpy.wait(2000));
    server.closeClient();
    QTRY_VERIFY_WITH_TIMEOUT(connectedSpy.count() >= 2, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(roomJoinedSpy.count() >= 3, 3000);
    QCOMPARE(realtime.rooms(), QStringList{QStringLiteral("room_2")});
    QVERIFY(server.lastClientMessage().contains("join_room"));

    QCOMPARE(roomErrorSpy.count(), 0);
    transport.disconnectFromServer();
  }

  void socketIoSecurityEventsInvalidateAdminSession() {
    TestHttpServer httpServer;
    QVERIFY(httpServer.listen());
    TestSocketIoServer socketServer;
    QVERIFY(socketServer.listen());

    AdminClient client;
    QVERIFY(client.setGatewayUrl(httpServer.url()));
    client.setApiKey(QStringLiteral("admin-desktop-key"));

    QSignalSpy loginSpy(client.auth(), &AuthManager::loginSucceeded);
    client.auth()->login(QStringLiteral("admin"), QStringLiteral("password"));
    QVERIFY(loginSpy.wait(2000));
    QVERIFY(client.auth()->isAuthenticated());

    QVERIFY(client.realtime()->setBaseUrl(socketServer.url()));
    client.realtime()->setReconnectEnabled(false);
    QSignalSpy connectedSpy(client.realtime(), &SocketIoClient::connected);
    QSignalSpy revokedSpy(client.socketIoRealtime(),
                          &SocketIoRealtimeClient::sessionRevoked);
    client.realtime()->connectToServer();
    QVERIFY(connectedSpy.wait(2000));

    QVERIFY(client.webrtc()->createPeerConnection(QStringLiteral("session_1")));
    QCOMPARE(client.webrtc()->sessionIds(),
             QStringList{QStringLiteral("session_1")});

    socketServer.sendSessionRevoked();
    QVERIFY(revokedSpy.wait(2000));
    QTRY_VERIFY_WITH_TIMEOUT(!client.auth()->isAuthenticated(), 2000);
    QVERIFY(client.webrtc()->sessionIds().isEmpty());
    QCOMPARE(client.realtime()->state(), SocketIoClient::State::Disconnected);
  }

  void hscfgImporterRejectsLegacyContainerAndBadPin() {
    AdminClient client;
    QSignalSpy errorSpy(&client, &AdminClient::errorOccurred);

    QVERIFY(!client.importHscfg(QByteArrayLiteral("HSCFG\x01"),
                                QStringLiteral("123456")));
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(errorSpy.at(0).at(0).value<AdminError>().serverCode,
             QStringLiteral("HSCFG_INVALID_MAGIC"));

    QVERIFY(!client.importHscfg(QByteArrayLiteral("HSCFG\x02"),
                                QStringLiteral("12345")));
    QCOMPARE(errorSpy.count(), 2);
    QCOMPARE(errorSpy.at(1).at(0).value<AdminError>().serverCode,
             QStringLiteral("HSCFG_INVALID_PIN"));
  }

  void hscfgBackendFixtureAppliesSingleSourceOfTruth() {
    const QByteArray fixture = QByteArray::fromBase64(QByteArrayLiteral(
        "SFNDRkcC62YBnv9NJdNqnDvnM39KrMScnTVM0oSK/n5dtPV85TL0S3HCkSDiL/UE1O"
        "Cha0eTOfC4mWFRm8bF/Jk2pfLTEsqnJGz6eHz0UICdNstDr7WMlIGzG2OIYDZ8oFCL"
        "Sj96hykstZT5J1V/r7i3ihyvvZbQ6hcXsPerySmJfZBcK0pIkqy46y7vuufs2+fl8I"
        "SL9Q1bWOw1g+8dBY4OD9H7Gjdwl2NT9apwhyYN4Unyt9cYxOYXyYCrOtDDW01ODp2M"
        "6wCdByNaTwZZKyz9h3LumsCL4cwpvjkLJcmd1b8PhVQb89qeTAw3+i5zA1md4ITnmk"
        "z7zFZCgQ23sBFhfu+QD2U2kvyF45CNeSIWcNQCN36fN9y0wgLwjT4uTNIrF5wMxqQU"
        "849PXgt2tWmr4FezKRLceM4uZl5o1s+tJKCXKwawaDUEwRaPfg0uYX1CtrJYYiptiz"
        "vHzoj5fU2YYY7rsFPKEqsmrTAXgrDi+xnz4IJSu3sGkNpYpnMzHE+Q5+LWkjQ3K4MN"
        "wXyP2/CLsNfLpkvsnMSMSk67SvCKFD/0nuzoXGHLKRLlRnOLV7pxLRQ9zQZEd+xFYq"
        "d7NxlPL8x+ok6nu+EMtzqhHDpyzizw7zHbR3Rmin09YM/4DylZiuozUI14qKu+J0kj"
        "fTqoQ5fGfgIiKDHNQkgkOz3BcNDHeXHPObyN19JiwDcvCBdl25iwrlfi31m+W1cckWm"
        "ddZkhU3ocO3CV5V+pD4iczf5QIQr8enJpqtSmeU4G9x/QsSOq3IaHO1LfI3dDe4Oe/"
        "O3c//1GYY0KUhwMR2oqOwfdxgCsU7t7/lsy/0uhffL+LkQhz0uC1r49IjALMYwwTjK"
        "T8wzD4Q+LyFlrjciCckQfrFBTBRYwJtJP+ROpAdKd/gFy8ODkGJA1Ou17iWfaDieGc"
        "ZPTfhjuOXQdRuytGdXTbM2tdHfXvkrxeRnJYO3Ky8zRjrbv8HoXIzrPrTapaOAdQ5zP"
        "pmjSN5VOQrvXFL+4W9Yz8EBZOX3LX247ijeDHdOybvXu8kC584nHERVQs4RhMjTgc5"
        "luNlyAs492/CcWYXChpgWJyTOpDOdF9FiiWBvofNjy3pqMG7AIbKneIQz8KIFo9xby"
        "h6ClPwabeV6whJeZs0E7e4HT7uQjumYXPMjMbdd/TSy+tnh9KlvKCqdaoAyUMNXcM5W"
        "eXN+aIa48cTF0yxBNWixMaLAWqY2MT1DB7eke60/51oBCeGsgU7ojQ7ApCvPne9uaU"
        "2IqjXPYixX6PF9WIlNDI6EqoaQ8PYAS98/phjCdb3JV/jwM6X/RczHDxwplEFkDNMb"
        "ZzWIm6urLWrK1AFMXVicEs7uPYWqJAi03oJY5JQupboo//Pgv1+9WeDVhrhQOaubH8"
        "UgDBeAwM3IrCEJjEPf/avPLY9QFtqD7dn31RAge3q/KtuZYswMkg8sB"));

    AdminClient client;
    QSignalSpy errorSpy(&client, &AdminClient::errorOccurred);
    QVERIFY(client.importHscfg(fixture, QStringLiteral("123456")));
    QCOMPARE(errorSpy.count(), 0);
    QVERIFY(client.hasImportedConfig());
    QCOMPARE(client.state(), AdminState::Unauthenticated);
    QCOMPARE(client.gatewayUrl(),
             QUrl(QStringLiteral("http://127.0.0.1:43127")));
    QCOMPARE(client.importedConfig().identity.clientId,
             QStringLiteral("client_qt_interop"));
    QCOMPARE(client.importedConfig().identity.apiKey,
             QStringLiteral("test-admin-api-key"));
    QCOMPARE(static_cast<int>(client.importedConfigIntegrity()),
             static_cast<int>(HscfgIntegrityState::SignatureUnavailable));
    QCOMPARE(client.realtime()->baseUrl(),
             QUrl(QStringLiteral("ws://127.0.0.1:43127")));
    QCOMPARE(client.realtime()->path(),
             QStringLiteral("/relay/admin/v1/socket.io/"));
    QCOMPARE(client.relay()->baseUrl(),
             QUrl(QStringLiteral("ws://127.0.0.1:43127")));
    QCOMPARE(client.relay()->path(), QStringLiteral("/relay/admin/v1"));
    QCOMPARE(client.webrtc()->mediaBaseUrl(),
             QUrl(QStringLiteral("http://127.0.0.1:43127")));
    QCOMPARE(client.webrtc()->signalingUrl(),
             QUrl(QStringLiteral("http://127.0.0.1:43127/webrtc")));
    QCOMPARE(client.webrtc()->mediaPort(), 8555);
  }

  void standardRelayRejectsInsecureNonLoopbackUrl() {
    StandardRelayClient relay;
    QSignalSpy errorSpy(&relay, &StandardRelayClient::errorOccurred);
    QVERIFY(!relay.setBaseUrl(QUrl(QStringLiteral("ws://relay.example.com"))));
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(errorSpy.at(0).at(0).value<RelayError>().code,
             RelayErrorCode::InvalidConfiguration);
  }

  void standardRelayDomainSupportsTopicsEventsAndResume() {
    TestStandardRelayServer server;
    QVERIFY(server.listen());

    StandardRelayClient transport;
    QVERIFY(transport.setBaseUrl(server.url()));
    transport.setHeader(QByteArrayLiteral("X-API-Key"),
                        QByteArrayLiteral("admin-desktop-key"));
    transport.setReconnectDelays(10, 50);
    transport.setMaxReconnectAttempts(3);
    RelayRealtimeClient realtime(&transport);

    QSignalSpy connectedSpy(&transport, &StandardRelayClient::connected);
    QSignalSpy subscribedSpy(&realtime, &RelayRealtimeClient::topicSubscribed);
    QSignalSpy unsubscribedSpy(&realtime,
                               &RelayRealtimeClient::topicUnsubscribed);
    QSignalSpy eventSpy(&realtime, &RelayRealtimeClient::eventReceived);
    QSignalSpy cameraSpy(&realtime, &RelayRealtimeClient::cameraEvent);
    QSignalSpy notificationSpy(&realtime,
                               &RelayRealtimeClient::notificationReceived);
    QSignalSpy snapshotSpy(
        &realtime, &RelayRealtimeClient::snapshotReconciliationRequired);
    QSignalSpy replaySpy(&realtime, &RelayRealtimeClient::replayCompleted);
    QSignalSpy errorSpy(&realtime, &RelayRealtimeClient::errorOccurred);

    transport.connectToServer();
    QVERIFY(connectedSpy.wait(2000));
    QCOMPARE(server.handshakeHeader("X-API-Key"),
             QByteArray("admin-desktop-key"));

    const quint64 subscribeId =
        realtime.subscribeTopic(QStringLiteral("camera.updated"));
    QVERIFY(subscribeId != 0);
    QVERIFY(subscribedSpy.wait(2000));
    QCOMPARE(server.lastCommand().value(QStringLiteral("command")),
             QJsonValue(QStringLiteral("subscribe")));
    QVERIFY(!server.lastClientMessage().startsWith("0"));
    QCOMPARE(
        server.lastCommand().value(QStringLiteral("topics")).toArray().at(0),
        QJsonValue(QStringLiteral("camera.updated")));

    server.sendCameraUpdated();
    QVERIFY(cameraSpy.wait(2000));
    const RelayEvent cameraEvent = cameraSpy.at(0).at(0).value<RelayEvent>();
    QCOMPARE(cameraEvent.eventId, QStringLiteral("evt_camera_1"));
    QCOMPARE(cameraEvent.data.value(QStringLiteral("id")),
             QJsonValue(QStringLiteral("cam_1")));
    QCOMPARE(realtime.lastEventId(), QStringLiteral("evt_camera_1"));
    QCOMPARE(eventSpy.count(), 1);

    server.sendNotification();
    QVERIFY(notificationSpy.wait(2000));
    const Notification notification =
        notificationSpy.at(0).at(0).value<Notification>();
    QCOMPARE(notification.id, QStringLiteral("notification_1"));
    QCOMPARE(notification.title, QStringLiteral("Person detected"));

    QCOMPARE(realtime.subscribeTopic(QStringLiteral("room.any")), quint64(0));
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(errorSpy.at(0).at(0).value<RelayError>().code,
             RelayErrorCode::InvalidConfiguration);

    server.closeClient();
    QTRY_VERIFY_WITH_TIMEOUT(connectedSpy.count() >= 2, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(snapshotSpy.count() >= 1, 3000);
    QCOMPARE(snapshotSpy.at(0).at(0).toString(),
             QStringLiteral("evt_notification_1"));
    QTRY_VERIFY_WITH_TIMEOUT(subscribedSpy.count() >= 2, 3000);
    QCOMPARE(realtime.topics(), QStringList{QStringLiteral("camera.updated")});

    const quint64 resumeId = realtime.requestResume();
    QVERIFY(resumeId != 0);
    QTRY_VERIFY_WITH_TIMEOUT(
        server.lastCommand().value(QStringLiteral("command")) ==
            QJsonValue(QStringLiteral("resume")),
        2000);
    QCOMPARE(server.lastCommand().value(QStringLiteral("last_event_id")),
             QJsonValue(QStringLiteral("evt_notification_1")));
    QTRY_VERIFY_WITH_TIMEOUT(replaySpy.count() >= 1, 2000);
    QCOMPARE(replaySpy.at(0).at(0).toBool(), true);

    server.setReplayUnavailable(true);
    const quint64 unavailableResumeId = realtime.requestResume();
    QVERIFY(unavailableResumeId != 0);
    QTRY_VERIFY_WITH_TIMEOUT(replaySpy.count() >= 2, 2000);
    QCOMPARE(replaySpy.at(1).at(0).toBool(), false);
    QCOMPARE(replaySpy.at(1).at(1).toString(),
             QStringLiteral("history expired"));

    const quint64 unsubscribeId =
        realtime.unsubscribeTopic(QStringLiteral("camera.updated"));
    QVERIFY(unsubscribeId != 0);
    QVERIFY(unsubscribedSpy.wait(2000));
    QCOMPARE(realtime.topics(), QStringList{});

    transport.disconnectFromServer(QStringLiteral("test complete"));
  }

  void standardRelaySecurityEventsInvalidateAdminSession() {
    TestHttpServer httpServer;
    QVERIFY(httpServer.listen());
    TestStandardRelayServer relayServer;
    QVERIFY(relayServer.listen());

    AdminClient client;
    QVERIFY(client.setGatewayUrl(httpServer.url()));
    client.setApiKey(QStringLiteral("admin-desktop-key"));

    QSignalSpy loginSpy(client.auth(), &AuthManager::loginSucceeded);
    client.auth()->login(QStringLiteral("admin"), QStringLiteral("password"));
    QVERIFY(loginSpy.wait(2000));
    QVERIFY(client.auth()->isAuthenticated());

    QVERIFY(client.relay()->setBaseUrl(relayServer.url()));
    client.relay()->setReconnectEnabled(false);
    QSignalSpy connectedSpy(client.relay(), &StandardRelayClient::connected);
    QSignalSpy revokedSpy(client.relayRealtime(),
                          &RelayRealtimeClient::sessionRevoked);
    client.relay()->connectToServer();
    QVERIFY(connectedSpy.wait(2000));

    QVERIFY(client.webrtc()->createPeerConnection(QStringLiteral("session_1")));
    QCOMPARE(client.webrtc()->sessionIds(),
             QStringList{QStringLiteral("session_1")});

    relayServer.sendSessionRevoked();
    QVERIFY(revokedSpy.wait(2000));
    QTRY_VERIFY_WITH_TIMEOUT(!client.auth()->isAuthenticated(), 2000);
    QVERIFY(client.webrtc()->sessionIds().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(client.relay()->state(),
                              StandardRelayClient::State::Disconnected, 2000);
  }

  void webrtcFoundationValidatesDtosAndTracksSessions() {
    WebRtcConfiguration configuration;
    WebRtcIceServer iceServer;
    iceServer.urls = {QStringLiteral("stun:stun.example.com"),
                      QStringLiteral("turns:turn.example.com?transport=tcp")};
    iceServer.username = QStringLiteral("turn-user");
    iceServer.credential = QStringLiteral("turn-secret");
    configuration.iceServers.append(iceServer);
    configuration.iceTransportPolicy = WebRtcIceTransportPolicy::Relay;

    QString reason;
    QVERIFY(configuration.isValid(&reason));
    const WebRtcConfiguration roundTrip =
        WebRtcConfiguration::fromJson(configuration.toJson());
    QCOMPARE(roundTrip.iceServers.size(), 1);
    QCOMPARE(roundTrip.iceServers.at(0).username, QStringLiteral("turn-user"));
    QCOMPARE(static_cast<int>(roundTrip.iceTransportPolicy),
             static_cast<int>(WebRtcIceTransportPolicy::Relay));

    WebRtcSessionDescription offer;
    offer.type = WebRtcSdpType::Offer;
    offer.sdp = QStringLiteral("v=0\\r\\n");
    QCOMPARE(WebRtcSessionDescription::fromJson(offer.toJson()).sdp, offer.sdp);
    QVERIFY(!WebRtcSessionDescription::fromJson(
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("bogus")},
                             {QStringLiteral("sdp"), QStringLiteral("v=0")}})
                 .isValid(&reason));

    WebRtcIceServer unsafe;
    unsafe.urls = {QStringLiteral("turn://user:password@turn.example.com")};
    QVERIFY(!unsafe.isValid(&reason));

    WebRtcClient client;
    QSignalSpy errorSpy(&client, &WebRtcClient::errorOccurred);
    WebRtcPeerConnection *peer = client.createPeerConnection(
        QStringLiteral("live-session-1"), configuration);
    QVERIFY(peer);
    QCOMPARE(client.sessionIds(),
             QStringList{QStringLiteral("live-session-1")});
    QVERIFY(!peer->hasBackend());
    QVERIFY(!peer->createOffer());
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(static_cast<int>(errorSpy.at(0).at(0).value<WebRtcError>().code),
             static_cast<int>(WebRtcErrorCode::BackendUnavailable));
    QVERIFY(client.closePeerConnection(QStringLiteral("live-session-1")));
    QVERIFY(client.sessionIds().isEmpty());
  }

  void liveClientCoversSessionLifecycle() {
    TestHttpServer server;
    QVERIFY(server.listen());

    AdminClient client;
    QVERIFY(client.setGatewayUrl(server.url()));
    client.setApiKey(QStringLiteral("admin-desktop-key"));

    QSignalSpy capabilitiesSpy(client.live(),
                               &LiveClient::capabilitiesReceived);
    client.live()->fetchCapabilities();
    QVERIFY(capabilitiesSpy.wait(2000));
    QCOMPARE(capabilitiesSpy.at(0)
                 .at(0)
                 .value<LiveCapabilities>()
                 .maxConcurrentSessions,
             8);

    QSignalSpy camerasSpy(client.live(), &LiveClient::camerasReceived);
    client.live()->listCameras();
    QVERIFY(camerasSpy.wait(2000));
    QCOMPARE(camerasSpy.at(0).at(0).value<LiveCameraPage>().items.size(), 1);
    QVERIFY(server.request().contains("GET /api/admin/v1/live/cameras?"));

    QSignalSpy negotiatedSpy(client.live(), &LiveClient::sessionNegotiated);
    client.live()->negotiate(QStringLiteral("cam_1"),
                             QStringLiteral("balanced"));
    QVERIFY(negotiatedSpy.wait(2000));
    const LiveSession session = negotiatedSpy.at(0).at(0).value<LiveSession>();
    QCOMPARE(session.sessionId, QStringLiteral("live_1"));
    QVERIFY(session.hasRemoteDescription);
    QCOMPARE(static_cast<int>(session.remoteDescription.type),
             static_cast<int>(WebRtcSdpType::Answer));
    QCOMPARE(session.remoteCandidates.size(), 1);

    QSignalSpy heartbeatSpy(client.live(),
                            &LiveClient::sessionHeartbeatReceived);
    client.live()->heartbeat(QStringLiteral("live_1"),
                             QJsonObject{{QStringLiteral("buffer_ms"), 40}});
    QVERIFY(heartbeatSpy.wait(2000));
    QCOMPARE(heartbeatSpy.at(0).at(0).toString(), QStringLiteral("live_1"));

    QSignalSpy profileSpy(client.live(), &LiveClient::sessionProfileChanged);
    client.live()->changeProfile(QStringLiteral("live_1"),
                                 QStringLiteral("low"));
    QVERIFY(profileSpy.wait(2000));
    QCOMPARE(profileSpy.at(0).at(0).value<LiveSession>().sessionId,
             QStringLiteral("live_1"));

    QSignalSpy statsSpy(client.live(), &LiveClient::sessionStatsReceived);
    client.live()->fetchSessionStats(QStringLiteral("live_1"));
    QVERIFY(statsSpy.wait(2000));
    QCOMPARE(statsSpy.at(0).at(0).value<LiveSessionStats>().sessionId,
             QStringLiteral("live_1"));

    QSignalSpy qoeSpy(client.live(), &LiveClient::qoeReported);
    client.live()->reportQoe(
        QStringLiteral("live_1"),
        QJsonObject{{QStringLiteral("rtt_ms"), 25},
                    {QStringLiteral("packet_loss"), 0.01}});
    QVERIFY(qoeSpy.wait(2000));
    QCOMPARE(qoeSpy.at(0).at(0).toString(), QStringLiteral("live_1"));

    QSignalSpy cameraStatusSpy(client.live(),
                               &LiveClient::cameraStatusReceived);
    client.live()->fetchCameraStatus(QStringLiteral("cam_1"));
    QVERIFY(cameraStatusSpy.wait(2000));
    QCOMPARE(cameraStatusSpy.at(0).at(0).value<LiveCameraStatus>().cameraId,
             QStringLiteral("cam_1"));

    QSignalSpy releasedSpy(client.live(), &LiveClient::sessionReleased);
    client.live()->release(QStringLiteral("live_1"));
    QVERIFY(releasedSpy.wait(2000));
    QCOMPARE(releasedSpy.at(0).at(0).toString(), QStringLiteral("live_1"));
  }

  void archiveClientUsesNormativeAdminContract() {
    TestHttpServer server;
    QVERIFY(server.listen());

    AdminClient client;
    QVERIFY(client.setGatewayUrl(server.url()));
    client.setApiKey(QStringLiteral("admin-desktop-key"));

    QSignalSpy loginSpy(client.auth(), &AuthManager::loginSucceeded);
    client.auth()->login(QStringLiteral("admin"), QStringLiteral("password"));
    QVERIFY(loginSpy.wait(2000));

    const QDateTime from(QDate(2026, 9, 18), QTime(10, 0),
                         QTimeZone(QTimeZone::UTC));
    const QDateTime to(QDate(2026, 9, 18), QTime(11, 0),
                       QTimeZone(QTimeZone::UTC));

    QSignalSpy timelineSpy(client.archive(), &ArchiveClient::timelineReceived);
    client.archive()->fetchTimeline(from, to, QStringLiteral("cam_1"),
                                    QStringLiteral("cursor_1"), 25);
    QVERIFY(timelineSpy.wait(2000));
    const ArchiveTimelinePage page =
        timelineSpy.at(0).at(0).value<ArchiveTimelinePage>();
    QCOMPARE(page.items.size(), 1);
    QCOMPARE(page.items.at(0).id, QStringLiteral("rec_1"));
    QCOMPARE(page.items.at(0).durationSeconds, 60);
    QCOMPARE(page.nextCursor, QStringLiteral("cursor_2"));
    QVERIFY(page.hasMore);
    const QByteArray timelineRequest = server.request();
    QVERIFY(timelineRequest.startsWith("GET /api/admin/v1/archive/timeline?"));
    QVERIFY(timelineRequest.contains("from=2026-09-18T10"));
    QVERIFY(timelineRequest.contains("to=2026-09-18T11"));
    QVERIFY(timelineRequest.contains("camera_id=cam_1"));
    QVERIFY(timelineRequest.contains("cursor=cursor_1"));
    QVERIFY(timelineRequest.contains("limit=25"));
    QVERIFY(timelineRequest.toLower().contains("x-api-key: admin-desktop-key"));
    QVERIFY(timelineRequest.contains("Authorization: Bearer access-token"));

    QSignalSpy daysSpy(client.archive(), &ArchiveClient::availableDaysReceived);
    client.archive()->fetchAvailableDays(QStringLiteral("cam_1"), 2026, 9);
    QVERIFY(daysSpy.wait(2000));
    const ArchiveAvailableDays days =
        daysSpy.at(0).at(0).value<ArchiveAvailableDays>();
    QCOMPARE(days.cameraId, QStringLiteral("cam_1"));
    QCOMPARE(days.year, 2026);
    QCOMPARE(days.month, 9);
    QCOMPARE(days.days, QVector<int>({1, 3, 18}));
    QVERIFY(server.request().startsWith(
        "GET /api/admin/v1/archive/cameras/cam_1/available-days?"));
    QVERIFY(server.request().contains("year=2026"));
    QVERIFY(server.request().contains("month=9"));

    QSignalSpy recordingSpy(client.archive(),
                            &ArchiveClient::recordingReceived);
    client.archive()->fetchRecording(QStringLiteral("rec_1"));
    QVERIFY(recordingSpy.wait(2000));
    QCOMPARE(recordingSpy.at(0).at(0).value<RecordingSegment>().cameraId,
             QStringLiteral("cam_1"));
    QVERIFY(server.request().startsWith(
        "GET /api/admin/v1/archive/recordings/rec_1 HTTP/1.1"));

    QSignalSpy playbackSpy(client.archive(),
                           &ArchiveClient::playbackUrlReceived);
    client.archive()->requestPlaybackUrl(
        QStringLiteral("rec_1"),
        QJsonObject{{QStringLiteral("profile"), QStringLiteral("native")}});
    QVERIFY(playbackSpy.wait(2000));
    QCOMPARE(playbackSpy.at(0).at(0).value<ArchiveUrlResult>().url,
             QStringLiteral("https://media.example.com/playback/rec_1"));
    QVERIFY(server.request().startsWith(
        "POST /api/admin/v1/archive/recordings/rec_1:playback-url HTTP/1.1"));

    QSignalSpy downloadSpy(client.archive(),
                           &ArchiveClient::downloadUrlReceived);
    client.archive()->requestDownloadUrl(QStringLiteral("rec_1"));
    QVERIFY(downloadSpy.wait(2000));
    QCOMPARE(downloadSpy.at(0).at(0).value<ArchiveUrlResult>().recordingId,
             QStringLiteral("rec_1"));
    QVERIFY(server.request().startsWith(
        "POST /api/admin/v1/archive/recordings/rec_1:download-url HTTP/1.1"));

    QSignalSpy thumbnailSpy(client.archive(),
                            &ArchiveClient::thumbnailUrlReceived);
    client.archive()->requestThumbnailUrl(QStringLiteral("rec_1"));
    QVERIFY(thumbnailSpy.wait(2000));
    QCOMPARE(thumbnailSpy.at(0).at(0).value<ArchiveUrlResult>().url,
             QStringLiteral("https://media.example.com/thumbnail/rec_1"));
    QVERIFY(server.request().startsWith(
        "POST /api/admin/v1/archive/recordings/rec_1:thumbnail-url HTTP/1.1"));
  }

  void archiveClientRejectsInvalidInput() {
    AdminClient client;
    QSignalSpy errorSpy(client.archive(), &ArchiveClient::errorOccurred);
    client.archive()->fetchTimeline({}, {}, {}, {}, 100);
    client.archive()->fetchAvailableDays(QStringLiteral("cam_1"), 2026, 13);
    client.archive()->requestPlaybackUrl({});
    QCOMPARE(errorSpy.count(), 3);
    QCOMPARE(errorSpy.at(0).at(0).value<AdminError>().serverCode,
             QStringLiteral("INVALID_INPUT"));
  }

  void notificationClientUsesNormativeAdminContract() {
    TestHttpServer server;
    QVERIFY(server.listen());

    AdminClient client;
    QVERIFY(client.setGatewayUrl(server.url()));
    client.setApiKey(QStringLiteral("admin-desktop-key"));

    QSignalSpy loginSpy(client.auth(), &AuthManager::loginSucceeded);
    client.auth()->login(QStringLiteral("admin"), QStringLiteral("password"));
    QVERIFY(loginSpy.wait(2000));

    QSignalSpy pageSpy(client.notifications(),
                       &NotificationClient::pageReceived);
    client.notifications()->list(
        QStringLiteral("notification_cursor_1"), 25,
        QJsonObject{{QStringLiteral("unread_only"), true},
                    {QStringLiteral("category"), QStringLiteral("security")}});
    QVERIFY(pageSpy.wait(2000));
    const NotificationPage page = pageSpy.at(0).at(0).value<NotificationPage>();
    QCOMPARE(page.items.size(), 1);
    QCOMPARE(page.items.at(0).id, QStringLiteral("notification_1"));
    QCOMPARE(page.unreadCount, 2);
    QCOMPARE(page.nextCursor, QStringLiteral("notification_cursor_2"));
    QVERIFY(page.hasMore);
    QVERIFY(server.request().startsWith("GET /api/admin/v1/notifications?"));
    QVERIFY(server.request().contains("limit=25"));
    QVERIFY(server.request().contains("cursor=notification_cursor_1"));
    QVERIFY(server.request().contains("unread_only=true"));
    QVERIFY(server.request().contains("category=security"));
    QVERIFY(
        server.request().toLower().contains("x-api-key: admin-desktop-key"));
    QVERIFY(server.request().contains("Authorization: Bearer access-token"));

    QSignalSpy notificationSpy(client.notifications(),
                               &NotificationClient::notificationReceived);
    client.notifications()->fetch(QStringLiteral("notification_1"));
    QVERIFY(notificationSpy.wait(2000));
    QCOMPARE(notificationSpy.at(0).at(0).value<Notification>().title,
             QStringLiteral("Person detected"));
    QVERIFY(server.request().startsWith(
        "GET /api/admin/v1/notifications/notification_1 HTTP/1.1"));

    QSignalSpy patchSpy(client.notifications(),
                        &NotificationClient::notificationPatched);
    client.notifications()->patch(
        QStringLiteral("notification_1"),
        QJsonObject{{QStringLiteral("is_read"), true}});
    QVERIFY(patchSpy.wait(2000));
    QCOMPARE(patchSpy.at(0).at(0).toString(), QStringLiteral("notification_1"));
    QVERIFY(server.request().startsWith(
        "PATCH /api/admin/v1/notifications/notification_1 HTTP/1.1"));
    QVERIFY(server.request().contains("\"is_read\":true"));

    QSignalSpy allReadSpy(client.notifications(),
                          &NotificationClient::allNotificationsRead);
    client.notifications()->markAllRead(
        QJsonObject{{QStringLiteral("scope"), QStringLiteral("current_user")}});
    QVERIFY(allReadSpy.wait(2000));
    QCOMPARE(
        allReadSpy.at(0).at(0).value<NotificationActionResult>().affectedCount,
        3);
    QVERIFY(server.request().startsWith(
        "POST /api/admin/v1/notifications:read-all HTTP/1.1"));

    QSignalSpy deleteSpy(client.notifications(),
                         &NotificationClient::notificationDeleted);
    client.notifications()->remove(QStringLiteral("notification_1"));
    QVERIFY(deleteSpy.wait(2000));
    QCOMPARE(deleteSpy.at(0).at(0).toString(),
             QStringLiteral("notification_1"));
    QVERIFY(server.request().startsWith(
        "DELETE /api/admin/v1/notifications/notification_1 HTTP/1.1"));
    QVERIFY(server.request().contains("\"confirmation\":\"yes\""));

    QSignalSpy batchDeleteSpy(client.notifications(),
                              &NotificationClient::notificationsBatchDeleted);
    client.notifications()->removeBatch(
        {QStringLiteral("notification_1"), QStringLiteral("notification_2")});
    QVERIFY(batchDeleteSpy.wait(2000));
    QCOMPARE(batchDeleteSpy.at(0)
                 .at(0)
                 .value<NotificationActionResult>()
                 .affectedCount,
             2);
    QVERIFY(server.request().startsWith(
        "POST /api/admin/v1/notifications:batch-delete HTTP/1.1"));
    QVERIFY(server.request().contains("\"notification_ids\""));
    QVERIFY(server.request().contains("\"confirmation\":\"yes\""));

    QSignalSpy clearSpy(client.notifications(),
                        &NotificationClient::notificationsCleared);
    client.notifications()->clear();
    QVERIFY(clearSpy.wait(2000));
    QCOMPARE(
        clearSpy.at(0).at(0).value<NotificationActionResult>().affectedCount,
        4);
    QVERIFY(server.request().startsWith(
        "POST /api/admin/v1/notifications:clear HTTP/1.1"));
    QVERIFY(server.request().contains("\"confirmation\":\"yes\""));

    QSignalSpy testSpy(client.notifications(), &NotificationClient::testSent);
    client.notifications()->sendTest(
        QJsonObject{{QStringLiteral("channel"), QStringLiteral("desktop")}});
    QVERIFY(testSpy.wait(2000));
    QCOMPARE(testSpy.at(0).at(0).value<NotificationActionResult>().status,
             QStringLiteral("sent"));
    QVERIFY(server.request().startsWith(
        "POST /api/admin/v1/notifications:test HTTP/1.1"));

    QSignalSpy configSpy(client.notifications(),
                         &NotificationClient::pushConfigReceived);
    client.notifications()->fetchPushConfig();
    QVERIFY(configSpy.wait(2000));
    QCOMPARE(
        configSpy.at(0).at(0).value<NotificationPushConfig>().vapidPublicKey,
        QStringLiteral("vapid-key"));
    QVERIFY(server.request().startsWith(
        "GET /api/admin/v1/notifications/push-config HTTP/1.1"));

    QSignalSpy putSubscriptionSpy(client.notifications(),
                                  &NotificationClient::pushSubscriptionUpdated);
    client.notifications()->upsertPushSubscription(
        QJsonObject{{QStringLiteral("endpoint"),
                     QStringLiteral("https://push.example.com/subscription")},
                    {QStringLiteral("user_agent"), QStringLiteral("Qt")}});
    QVERIFY(putSubscriptionSpy.wait(2000));
    QVERIFY(server.request().startsWith(
        "PUT /api/admin/v1/notifications/push-subscriptions/current HTTP/1.1"));

    QSignalSpy removeSubscriptionSpy(
        client.notifications(), &NotificationClient::pushSubscriptionRemoved);
    client.notifications()->removePushSubscription();
    QVERIFY(removeSubscriptionSpy.wait(2000));
    QVERIFY(server.request().startsWith(
        "DELETE /api/admin/v1/notifications/push-subscriptions/current "
        "HTTP/1.1"));
  }

  void notificationClientRejectsInvalidInput() {
    AdminClient client;
    QSignalSpy errorSpy(client.notifications(),
                        &NotificationClient::errorOccurred);
    client.notifications()->list({}, 0);
    client.notifications()->fetch({});
    client.notifications()->patch(QStringLiteral("notification_1"), {});
    client.notifications()->remove(QStringLiteral("notification_1"), {});
    client.notifications()->removeBatch({});
    client.notifications()->upsertPushSubscription({});
    QCOMPARE(errorSpy.count(), 6);
    QCOMPARE(errorSpy.at(0).at(0).value<AdminError>().serverCode,
             QStringLiteral("INVALID_INPUT"));
  }

  void rejectsNonLoopbackHttpGateway() {
    AdminClient client;
    QSignalSpy errorSpy(&client, &AdminClient::errorOccurred);
    QVERIFY(!client.setGatewayUrl(
        QUrl(QStringLiteral("http://gateway.example.com"))));
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(errorSpy.at(0).at(0).value<AdminError>().serverCode,
             QStringLiteral("INVALID_GATEWAY_URL"));
  }

  void maintenanceEntersMaintenanceState() {
    TestHttpServer server;
    QVERIFY(server.listen());
    server.setStatus(503);
    server.setRetryAfter(120);

    AdminClient client;
    QVERIFY(client.setGatewayUrl(server.url()));
    client.setApiKey(QStringLiteral("admin-desktop-key"));

    QSignalSpy maintenanceSpy(&client, &AdminClient::maintenanceChanged);
    QSignalSpy errorSpy(&client, &AdminClient::errorOccurred);
    client.system()->fetchStatus();
    QVERIFY(maintenanceSpy.wait(2000));
    QCOMPARE(client.state(), AdminState::Maintenance);
    QCOMPARE(
        maintenanceSpy.at(0).at(0).value<MaintenanceInfo>().retryAfterSeconds,
        120);
    QVERIFY(errorSpy.count() >= 1);
  }
};

QTEST_MAIN(AdminSdkTest)
#include "test_admin_sdk.moc"
