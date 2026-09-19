#include <hubsight/admin/hubsight_admin.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
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
      m_socket = socket;
      connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        m_request.append(socket->readAll());
        if (!m_request.contains("\r\n\r\n")) {
          return;
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

  QUrl url() const { return m_server.url(); }
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

private:
  QWebSocketServer m_server;
  QTimer m_pingTimer;
  QPointer<QWebSocket> m_socket;
  QByteArray m_lastClientMessage;
  bool m_pongReceived = false;
  bool m_serverAcknowledgementReceived = false;
  bool m_namespaceDisconnectReceived = false;
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
  }

  void endpointCatalogCoversAdminApiV1Surface() {
    const QVector<AdminEndpointDefinition> endpoints = adminEndpointCatalog();
    QCOMPARE(endpoints.size(), 136);
    QCOMPARE(adminEndpointDefinition(AdminEndpoint::RealtimeRelay).pathTemplate,
             QStringLiteral("/relay/admin/v1"));
    QCOMPARE(adminEndpointDefinition(AdminEndpoint::CamerasCreate).method,
             QByteArray("POST"));

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
    QVERIFY(request.contains("X-API-Key: admin-desktop-key"));
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
    QVERIFY(request.contains("X-API-Key: admin-desktop-key"));
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

    client.realtime()->setReconnectEnabled(false);
    QSignalSpy connectedSpy(client.realtime(), &SocketIoClient::connected);
    client.realtime()->connectToServer();
    QVERIFY(connectedSpy.wait(2000));
    QCOMPARE(server.handshakeHeader("X-API-Key"),
             QByteArray("admin-desktop-key"));

    client.realtime()->disconnectFromServer();
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
    QCOMPARE(roundTrip.iceTransportPolicy, WebRtcIceTransportPolicy::Relay);

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
    QCOMPARE(errorSpy.at(0).at(0).value<WebRtcError>().code,
             WebRtcErrorCode::BackendUnavailable);
    QVERIFY(client.closePeerConnection(QStringLiteral("live-session-1")));
    QVERIFY(client.sessionIds().isEmpty());
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
