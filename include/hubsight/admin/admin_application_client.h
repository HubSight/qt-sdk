#pragma once

#include "admin_client.h"
#include "admin_export.h"
#include "resources/live_types.h"
#include "sdk_diagnostics.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVector>

#include <functional>
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

  // High-level live session orchestration. These methods own the negotiate,
  // heartbeat, profile, QoE, and release lifecycle. A LiveSession exposes
  // signaling data for an optional media adapter, but applications do not need
  // to construct QNetworkReply, Socket.IO packets, or native WebRTC objects.
  void startLive(const QString &cameraId, const QString &profile = {},
                 const QJsonObject &options = {});
  void stopLive(const QString &sessionId);
  void stopAllLive();
  void changeLiveProfile(const QString &sessionId, const QString &profile,
                         const QJsonObject &options = {});
  void reportLiveQoe(const QString &sessionId, const QJsonObject &qoe);
  LiveSession liveSession(const QString &sessionId) const;
  QStringList liveSessionIds() const;
  void setLiveHeartbeatInterval(int intervalMs);
  int liveHeartbeatInterval() const;

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
  AccountClient *account() const;
  SystemClient *system() const;
  SystemOperationsClient *systemOperations() const;
  CameraClient *cameras() const;
  CameraManagementClient *cameraManagement() const;
  IdentityClient *identity() const;
  IntegrationClient *integrations() const;
  MemberClient *members() const;
  LiveClient *live() const;
  ArchiveClient *archive() const;
  NotificationClient *notifications() const;
  AdminEndpointClient *api() const;

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

  void liveStarting(QString cameraId);
  void liveSessionStarted(HubSight::Admin::LiveSession session);
  void liveSessionChanged(HubSight::Admin::LiveSession session);
  void liveSessionStopped(QString sessionId);
  void liveError(QString sessionId, HubSight::Admin::AdminError error);
  void configurationApplied(bool success);
  void hscfgImportCompleted(bool success);
  void configurationCleared();

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
  bool configureNow(const QUrl &gatewayUrl, const QString &apiKey);
  bool importHscfgNow(const QByteArray &data, const QString &pin);
  bool importHscfgFileNow(const QString &path, const QString &pin);
  void clearConfigurationNow();
  void signOutNow();
  bool beginLiveTeardown(std::function<void()> continuation);
  void finishLiveTeardown();
  void handleLiveSessionNegotiated(const LiveSession &session);
  void handleLiveSessionProfileChanged(const LiveSession &session);
  void handleLiveSessionReleased(const QString &sessionId);
  void handleLiveHeartbeat(const QString &sessionId);
  void handleLiveError(const AdminError &error);
  void sendLiveError(const QString &sessionId, const AdminError &error,
                     bool recordDiagnostic = true,
                     DiagnosticSource source = DiagnosticSource::Http);
  void reportWebRtcBackendUnavailable(const LiveSession &session);
  void heartbeatLiveSessions();
  void clearLiveSessions(bool emitStopped);
  void releaseLiveSessions();
  void requestLiveRelease(const QString &sessionId);
  void retryLiveRelease(const QString &sessionId);

  std::unique_ptr<AdminClient> m_client;
  QTimer m_refreshTimer;
  QTimer m_liveHeartbeatTimer;
  QTimer m_liveTeardownTimer;
  QHash<QString, LiveSession> m_liveSessions;
  QSet<QString> m_liveStartingCameras;
  QSet<QString> m_liveCanceledStartingCameras;
  QSet<QString> m_liveHeartbeatInFlight;
  QSet<QString> m_liveReleaseInFlight;
  QSet<QString> m_liveReleasePending;
  QSet<QString> m_liveReleaseRetryScheduled;
  QHash<QString, int> m_liveReleaseAttempts;
  std::function<void()> m_pendingLiveTeardown;
  QString m_preAuthToken;
  QVector<SdkDiagnostic> m_diagnostics;
  int m_diagnosticHistoryLimit = 256;
  int m_liveHeartbeatIntervalMs = 15000;
  bool m_diagnosticLoggingEnabled = false;
  bool m_autoConnectRealtime = true;
};

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::AdminApplicationClient *)
