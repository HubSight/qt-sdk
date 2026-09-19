#pragma once

#include "../include/hubsight/admin/admin_types.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QTimer>
#include <QUrl>

namespace HubSight::Admin {

enum class RequestAuth {
  ApiKeyOnly,
  Protected,
};

struct TransportRequest {
  QNetworkAccessManager::Operation method = QNetworkAccessManager::GetOperation;
  QString path;
  QString operation;
  RequestAuth authentication = RequestAuth::ApiKeyOnly;
  QByteArray body;
  QString idempotencyKey;
  int timeoutMs = 15000;
};

struct TransportResponse {
  QString operation;
  HttpProtocol protocol = HttpProtocol::Unknown;
  int httpStatus = 0;
  QNetworkReply::NetworkError networkError = QNetworkReply::NoError;
  QByteArray body;
  QString requestId;
  int retryAfterSeconds = 0;
  bool timedOut = false;
  QString networkErrorText;

  bool isHttpSuccess() const { return httpStatus >= 200 && httpStatus < 300; }
};

class AdminTransport final : public QObject {
  Q_OBJECT

public:
  explicit AdminTransport(QObject *parent = nullptr);

  bool setGatewayUrl(const QUrl &gatewayUrl);
  QUrl gatewayUrl() const;
  bool setApiKey(const QString &apiKey);
  void setAccessToken(const QString &accessToken);

  bool isConfigured() const;
  AdminError configurationError(const QString &operation) const;
  AdminError errorFor(const TransportResponse &response,
                      const QString &operation) const;

  quint64 send(const TransportRequest &request);
  void cancel(quint64 requestId);
  void cancelAll();

signals:
  void finished(quint64 requestId, HubSight::Admin::TransportResponse response);
  void maintenanceDetected(HubSight::Admin::MaintenanceInfo info);
  void allRequestsCanceled();

private:
  struct ReplyState {
    QNetworkReply *reply = nullptr;
    QTimer *timer = nullptr;
    TransportRequest request;
    bool timedOut = false;
  };

  QUrl endpoint(const QString &path) const;
  QNetworkRequest makeRequest(const TransportRequest &request,
                              quint64 requestId,
                              const QString &requestIdHeader) const;
  void finish(quint64 requestId);
  void abortForTimeout(quint64 requestId);

  QNetworkAccessManager *m_manager = nullptr;
  QUrl m_gatewayUrl;
  QString m_apiKey;
  QString m_accessToken;
  quint64 m_nextRequestId = 1;
  QHash<quint64, ReplyState> m_replies;
};

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::TransportResponse)
