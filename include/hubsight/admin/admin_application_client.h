#pragma once

#include "admin_client.h"
#include "admin_export.h"
#include "sdk_diagnostics.h"

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVector>

#include <memory>

namespace HubSight::Admin {

// App-facing facade. It deliberately does not expose AuthManager,
// AdminTransport, SocketIoClient, or WebRtcClient. JWT rotation, HTTP
// authentication headers, realtime transport selection, and WebRTC registry
// lifecycle are managed by the SDK core. The lower-level AdminClient remains
// available for advanced integrations that explicitly need those primitives.
class HUBSIGHT_ADMIN_EXPORT AdminApplicationClient final : public QObject {
  Q_OBJECT

public:
  explicit AdminApplicationClient(QObject *parent = nullptr);
  explicit AdminApplicationClient(SecureStoragePtr storage,
                                  QObject *parent = nullptr);
  ~AdminApplicationClient() override;

  // One configuration entry point for normal applications. For encrypted
  // deployments, importHscfg/importHscfgFile are preferred because the profile
  // is the single source of truth for HTTP, relay, and WebRTC URLs.
  bool configure(const QUrl &gatewayUrl, const QString &apiKey);
  bool importHscfg(const QByteArray &data, const QString &pin);
  bool importHscfgFile(const QString &path, const QString &pin);
  void clearConfiguration();
  bool isConfigured() const;
  bool hasImportedConfig() const;

  void setHscfgTrustedEd25519PublicKey(const QByteArray &publicKey);
  void setHscfgRequireFullIntegrity(bool required);

  // The facade owns the JWT flow. Applications only handle the optional 2FA
  // challenge; the pre-auth token is retained inside this object.
  void signIn(const QString &username, const QString &password,
              const QJsonObject &deviceInfo = {});
  void verifyTwoFactor(const QString &code, const QString &recoveryCode = {},
                       const QJsonObject &deviceInfo = {});
  // Refresh is normally automatic from expires_in. This method is available
  // for foreground resume/manual recovery flows.
  void refreshSession();
  void signOut();

  bool isAuthenticated() const;
  AdminState state() const;
  AdminUser currentUser() const;

  // Standard JSON relay domain only. Applications never need to select or
  // configure Socket.IO. Realtime connection is automatic after sign-in by
  // default, and can be controlled explicitly when required by a host app.
  RelayRealtimeClient *realtime() const;
  void setAutoConnectRealtime(bool enabled);
  bool autoConnectRealtime() const;
  void connectRealtime();
  void disconnectRealtime(const QString &reason = {});
  bool isRealtimeConnected() const;

  // Typed resource clients. These expose domain operations, not QNetworkReply
  // or the native HTTP client.
  SystemClient *system() const;
  CameraClient *cameras() const;
  LiveClient *live() const;
  ArchiveClient *archive() const;
  NotificationClient *notifications() const;

  // Application-safe diagnostics retained in a bounded in-memory history.
  QVector<SdkDiagnostic> diagnostics() const;
  SdkDiagnostic lastDiagnostic() const;
  void clearDiagnostics();
  void setDiagnosticHistoryLimit(int limit);
  int diagnosticHistoryLimit() const;
  void setDiagnosticLoggingEnabled(bool enabled);
  bool diagnosticLoggingEnabled() const;

signals:
  void stateChanged(HubSight::Admin::AdminState state);
  void authenticated(HubSight::Admin::AdminUser user);
  void twoFactorRequired();
  void signedOut();
  void currentUserChanged(HubSight::Admin::AdminUser user);
  void maintenanceChanged(HubSight::Admin::MaintenanceInfo info);
  void errorOccurred(HubSight::Admin::AdminError error);
  void requestCompleted(QString operation,
                        HubSight::Admin::HttpProtocol protocol);
  void diagnosticOccurred(HubSight::Admin::SdkDiagnostic diagnostic);

private:
  void initialize(SecureStoragePtr storage);
  void record(DiagnosticSeverity severity, DiagnosticSource source,
              const QString &code, const QString &operation,
              const QString &message, const QString &requestId = {},
              const QJsonObject &details = {}, bool retryable = false);
  void recordAdminError(const AdminError &error);
  void emitLocalError(const QString &code, const QString &operation,
                      const QString &message);
  void handleTwoFactorRequired(const QString &preAuthToken);
  void handleAuthenticated(const TokenSet &tokens, const AdminUser &user);
  void scheduleTokenRefresh(const TokenSet &tokens);

  std::unique_ptr<AdminClient> m_client;
  QTimer m_refreshTimer;
  QString m_preAuthToken;
  QVector<SdkDiagnostic> m_diagnostics;
  int m_diagnosticHistoryLimit = 256;
  bool m_diagnosticLoggingEnabled = false;
  bool m_autoConnectRealtime = true;
};

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::AdminApplicationClient *)
