#include "admin_transport.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkCookieJar>
#include <QUuid>

namespace HubSight::Admin {
namespace {

class CookieFreeJar final : public QNetworkCookieJar {
public:
  explicit CookieFreeJar(QObject *parent = nullptr)
      : QNetworkCookieJar(parent) {}

protected:
  QList<QNetworkCookie> cookiesForUrl(const QUrl &) const override {
    return {};
  }
  bool setCookiesFromUrl(const QList<QNetworkCookie> &, const QUrl &) override {
    return true;
  }
};

QString withoutTrailingSlash(QString value) {
  while (value.endsWith(QLatin1Char('/'))) {
    value.chop(1);
  }
  return value;
}

QString responseCode(const QJsonObject &json) {
  const QString code = json.value(QStringLiteral("code")).toString();
  return code.isEmpty() ? json.value(QStringLiteral("error")).toString() : code;
}

QString responseMessage(const QJsonObject &json, const QString &code) {
  const QString message = json.value(QStringLiteral("message_en")).toString();
  if (!message.isEmpty()) {
    return message;
  }
  const QString generic = json.value(QStringLiteral("message")).toString();
  if (!generic.isEmpty()) {
    return generic;
  }
  const QString error = json.value(QStringLiteral("error")).toString();
  if (!error.isEmpty() && error != code) {
    return error;
  }
  return code.isEmpty() ? QStringLiteral("Admin API request failed") : code;
}

} // namespace

AdminTransport::AdminTransport(QObject *parent)
    : QObject(parent), m_manager(new QNetworkAccessManager(this)) {
  // Admin API explicitly forbids cookie-based authentication. This also
  // prevents a server Set-Cookie response from changing later requests.
  m_manager->setCookieJar(new CookieFreeJar(m_manager));
  qRegisterMetaType<TransportResponse>();
}

bool AdminTransport::setGatewayUrl(const QUrl &gatewayUrl) {
  if (!gatewayUrl.isValid() ||
      (gatewayUrl.scheme() != QStringLiteral("http") &&
       gatewayUrl.scheme() != QStringLiteral("https")) ||
      gatewayUrl.host().isEmpty()) {
    return false;
  }

  QUrl normalized = gatewayUrl;
  normalized.setQuery(QString());
  normalized.setFragment(QString());
  normalized.setPath(withoutTrailingSlash(normalized.path()));
  m_gatewayUrl = normalized;
  return true;
}

QUrl AdminTransport::gatewayUrl() const { return m_gatewayUrl; }

bool AdminTransport::setApiKey(const QString &apiKey) {
  m_apiKey = apiKey.trimmed();
  return !m_apiKey.isEmpty();
}

void AdminTransport::setAccessToken(const QString &accessToken) {
  m_accessToken = accessToken.trimmed();
}

bool AdminTransport::isConfigured() const {
  return !m_gatewayUrl.isEmpty() && !m_apiKey.isEmpty();
}

AdminError AdminTransport::configurationError(const QString &operation) const {
  AdminError error;
  error.category = ErrorCategory::Configuration;
  error.serverCode = QStringLiteral("SDK_NOT_CONFIGURED");
  error.developerMessage =
      m_gatewayUrl.isEmpty()
          ? QStringLiteral(
                "Configure an HTTPS gateway URL before making an "
                "Admin API request (loopback HTTP is allowed for tests).")
          : QStringLiteral("Configure an Admin API key before making an Admin "
                           "API request.");
  error.operation = operation;
  return error;
}

AdminError AdminTransport::errorFor(const TransportResponse &response,
                                    const QString &operation) const {
  AdminError error;
  error.httpStatus = response.httpStatus;
  error.operation = operation;
  error.requestId = response.requestId;
  error.retryAfterSeconds = response.retryAfterSeconds;

  QJsonObject json;
  QJsonParseError parseError;
  const QJsonDocument document =
      QJsonDocument::fromJson(response.body, &parseError);
  if (!document.isNull() && document.isObject()) {
    json = document.object();
  }

  error.serverCode = responseCode(json);
  error.requestId = error.requestId.isEmpty()
                        ? json.value(QStringLiteral("request_id")).toString()
                        : error.requestId;
  error.developerMessage = responseMessage(json, error.serverCode);
  if (json.contains(QStringLiteral("details"))) {
    error.details = json.value(QStringLiteral("details"));
  } else if (json.contains(QStringLiteral("errors"))) {
    error.details = json.value(QStringLiteral("errors"));
  }
  if (error.retryAfterSeconds <= 0) {
    error.retryAfterSeconds =
        json.value(QStringLiteral("retry_after_seconds")).toInt();
  }

  // QNetworkReply reports many HTTP 4xx/5xx responses through its
  // NetworkError enum. Classify the HTTP contract first so 401/503 are not
  // accidentally exposed as generic transport failures.
  if (response.httpStatus >= 400 && response.httpStatus <= 599) {
    if (response.httpStatus == 503 &&
        error.serverCode == QStringLiteral("ADMIN_API_DISABLED")) {
      error.category = ErrorCategory::Maintenance;
      error.retryable = true;
      if (error.retryAfterSeconds <= 0) {
        error.retryAfterSeconds = 300;
      }
      return error;
    }

    switch (response.httpStatus) {
    case 400:
      error.category = ErrorCategory::Validation;
      break;
    case 401:
      error.category = ErrorCategory::Authentication;
      break;
    case 403:
      error.category = ErrorCategory::Authorization;
      break;
    case 409:
      error.category = ErrorCategory::Conflict;
      break;
    default:
      error.category = response.httpStatus >= 500 ? ErrorCategory::Server
                                                  : ErrorCategory::Unknown;
      break;
    }
    error.retryable = response.httpStatus == 429 || response.httpStatus >= 500;
    return error;
  }

  if (response.networkError != QNetworkReply::NoError) {
    error.category =
        response.timedOut ||
                response.networkError == QNetworkReply::TimeoutError
            ? ErrorCategory::Network
            : (response.networkError == QNetworkReply::OperationCanceledError
                   ? ErrorCategory::Canceled
                   : ErrorCategory::Network);
    error.retryable = error.category == ErrorCategory::Network;
    if (!response.networkErrorText.isEmpty()) {
      error.developerMessage = response.networkErrorText;
    } else if (error.category == ErrorCategory::Canceled) {
      error.developerMessage =
          QStringLiteral("Admin API request was canceled.");
    }
    return error;
  }

  error.category = ErrorCategory::Unknown;
  return error;
}

QUrl AdminTransport::endpoint(const QString &path) const {
  const QString base =
      withoutTrailingSlash(m_gatewayUrl.toString(QUrl::FullyEncoded));
  QString normalizedPath = path;
  if (!normalizedPath.startsWith(QLatin1Char('/'))) {
    normalizedPath.prepend(QLatin1Char('/'));
  }
  return QUrl(base + QStringLiteral("/api/admin/v1") + normalizedPath);
}

QNetworkRequest
AdminTransport::makeRequest(const TransportRequest &request, quint64 requestId,
                            const QString &requestIdHeader) const {
  QNetworkRequest networkRequest(endpoint(request.path));
  // Let Qt negotiate HTTP/2 over TLS with ALPN and transparently fall back
  // to HTTP/1.1 when the gateway or the connection does not support it. Do
  // not use Http2DirectAttribute: h2c/direct HTTP/2 is not the Admin API
  // transport contract and would break ordinary HTTP/1.1 gateways.
  networkRequest.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
  networkRequest.setRawHeader("Accept", "application/json");
  networkRequest.setRawHeader("X-API-Key", m_apiKey.toUtf8());
  networkRequest.setRawHeader("X-Request-ID", requestIdHeader.toUtf8());
  // Do not copy cookies or any other ambient browser credentials. The
  // QNetworkAccessManager uses a cookie-free jar configured by the transport.

  if (request.authentication == RequestAuth::Protected &&
      !m_accessToken.isEmpty()) {
    networkRequest.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") +
                                                     m_accessToken.toUtf8());
  }
  if (!request.idempotencyKey.trimmed().isEmpty()) {
    networkRequest.setRawHeader("Idempotency-Key",
                                request.idempotencyKey.trimmed().toUtf8());
  }
  if (!request.body.isEmpty()) {
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                             QStringLiteral("application/json"));
  }
  Q_UNUSED(requestId)
  return networkRequest;
}

quint64 AdminTransport::send(const TransportRequest &request) {
  if (!isConfigured()) {
    return 0;
  }

  const quint64 requestId = m_nextRequestId++;
  const QString requestIdHeader =
      QStringLiteral("req_") + QUuid::createUuid()
                                   .toString(QUuid::WithoutBraces)
                                   .remove(QLatin1Char('-'));
  const QNetworkRequest networkRequest =
      makeRequest(request, requestId, requestIdHeader);

  QNetworkReply *reply = nullptr;
  switch (request.method) {
  case QNetworkAccessManager::GetOperation:
    reply = m_manager->get(networkRequest);
    break;
  case QNetworkAccessManager::PostOperation:
    reply = m_manager->post(networkRequest, request.body);
    break;
  case QNetworkAccessManager::PutOperation:
    reply = m_manager->put(networkRequest, request.body);
    break;
  case QNetworkAccessManager::DeleteOperation:
    reply = m_manager->deleteResource(networkRequest);
    break;
  default:
    reply = m_manager->sendCustomRequest(
        networkRequest, QByteArrayLiteral("PATCH"), request.body);
    break;
  }

  ReplyState state;
  state.reply = reply;
  state.request = request;
  state.timer = new QTimer(this);
  state.timer->setSingleShot(true);
  m_replies.insert(requestId, state);

  connect(reply, &QNetworkReply::finished, this,
          [this, requestId]() { finish(requestId); });
  connect(state.timer, &QTimer::timeout, this,
          [this, requestId]() { abortForTimeout(requestId); });
  state.timer->start(qMax(1, request.timeoutMs));
  return requestId;
}

void AdminTransport::cancel(quint64 requestId) {
  const auto it = m_replies.find(requestId);
  if (it != m_replies.end() && it->reply) {
    it->reply->abort();
  }
}

void AdminTransport::cancelAll() {
  const QList<quint64> requestIds = m_replies.keys();
  for (const quint64 requestId : requestIds) {
    const auto it = m_replies.find(requestId);
    if (it == m_replies.end()) {
      continue;
    }
    const ReplyState state = it.value();
    m_replies.erase(it);
    state.reply->disconnect(this);
    state.reply->abort();
    state.timer->stop();
    state.timer->deleteLater();
    state.reply->deleteLater();
  }
  emit allRequestsCanceled();
}

void AdminTransport::abortForTimeout(quint64 requestId) {
  auto it = m_replies.find(requestId);
  if (it == m_replies.end() || !it->reply) {
    return;
  }
  it->timedOut = true;
  it->reply->abort();
}

void AdminTransport::finish(quint64 requestId) {
  const auto it = m_replies.find(requestId);
  if (it == m_replies.end()) {
    return;
  }
  const ReplyState state = it.value();
  m_replies.erase(it);

  TransportResponse response;
  response.operation = state.request.operation;
  response.httpStatus =
      state.reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  response.networkError = state.reply->error();
  if (response.networkError == QNetworkReply::NoError) {
    response.protocol =
        state.reply->attribute(QNetworkRequest::Http2WasUsedAttribute).toBool()
            ? HttpProtocol::Http2
            : HttpProtocol::Http1_1;
  }
  response.body = state.reply->readAll();
  response.requestId =
      QString::fromUtf8(state.reply->rawHeader("X-Request-ID"));
  response.retryAfterSeconds =
      state.reply->rawHeader("Retry-After").trimmed().toInt();
  response.timedOut = state.timedOut;
  response.networkErrorText = state.reply->errorString();

  state.timer->stop();
  state.timer->deleteLater();
  state.reply->deleteLater();

  const AdminError error = errorFor(response, state.request.operation);
  if (error.isMaintenance()) {
    MaintenanceInfo info;
    info.code = error.serverCode.isEmpty()
                    ? QStringLiteral("ADMIN_API_DISABLED")
                    : error.serverCode;
    info.requestId = error.requestId;
    info.retryAfterSeconds =
        error.retryAfterSeconds > 0 ? error.retryAfterSeconds : 300;
    emit maintenanceDetected(info);
  }
  emit finished(requestId, response);
}

} // namespace HubSight::Admin
