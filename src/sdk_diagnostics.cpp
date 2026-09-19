#include "../include/hubsight/admin/sdk_diagnostics.h"

namespace HubSight::Admin {

QString toString(DiagnosticSeverity severity) {
  switch (severity) {
  case DiagnosticSeverity::Debug:
    return QStringLiteral("debug");
  case DiagnosticSeverity::Info:
    return QStringLiteral("info");
  case DiagnosticSeverity::Warning:
    return QStringLiteral("warning");
  case DiagnosticSeverity::Error:
    return QStringLiteral("error");
  }
  return QStringLiteral("info");
}

QString toString(DiagnosticSource source) {
  switch (source) {
  case DiagnosticSource::Configuration:
    return QStringLiteral("configuration");
  case DiagnosticSource::Authentication:
    return QStringLiteral("authentication");
  case DiagnosticSource::Http:
    return QStringLiteral("http");
  case DiagnosticSource::StandardRelay:
    return QStringLiteral("standard_relay");
  case DiagnosticSource::SocketIo:
    return QStringLiteral("socket_io");
  case DiagnosticSource::WebRtc:
    return QStringLiteral("webrtc");
  case DiagnosticSource::Lifecycle:
    return QStringLiteral("lifecycle");
  case DiagnosticSource::Unknown:
    return QStringLiteral("unknown");
  }
  return QStringLiteral("unknown");
}

QJsonObject SdkDiagnostic::toJson() const {
  QJsonObject result{
      {QStringLiteral("timestamp"),
       timestamp.toUTC().toString(Qt::ISODateWithMs)},
      {QStringLiteral("severity"), toString(severity)},
      {QStringLiteral("source"), toString(source)},
      {QStringLiteral("code"), code},
      {QStringLiteral("operation"), operation},
      {QStringLiteral("message"), message},
      {QStringLiteral("retryable"), retryable},
  };
  if (!requestId.isEmpty()) {
    result.insert(QStringLiteral("request_id"), requestId);
  }
  if (!details.isEmpty()) {
    result.insert(QStringLiteral("details"), details);
  }
  return result;
}

} // namespace HubSight::Admin
