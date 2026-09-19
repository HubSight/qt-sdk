#pragma once

#include "admin_export.h"

#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>

namespace HubSight::Admin {

enum class DiagnosticSeverity {
  Debug,
  Info,
  Warning,
  Error,
};

HUBSIGHT_ADMIN_EXPORT QString toString(DiagnosticSeverity severity);

enum class DiagnosticSource {
  Configuration,
  Authentication,
  Http,
  StandardRelay,
  SocketIo,
  WebRtc,
  Lifecycle,
  Unknown,
};

HUBSIGHT_ADMIN_EXPORT QString toString(DiagnosticSource source);

// Application-safe SDK diagnostic. It intentionally contains operation and
// correlation metadata but never access tokens, refresh tokens, passwords, or
// WebSocket URL credentials.
struct HUBSIGHT_ADMIN_EXPORT SdkDiagnostic {
  QDateTime timestamp;
  DiagnosticSeverity severity = DiagnosticSeverity::Info;
  DiagnosticSource source = DiagnosticSource::Unknown;
  QString code;
  QString operation;
  QString message;
  QString requestId;
  QJsonObject details;
  bool retryable = false;

  QJsonObject toJson() const;
};

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::DiagnosticSeverity)
Q_DECLARE_METATYPE(HubSight::Admin::DiagnosticSource)
Q_DECLARE_METATYPE(HubSight::Admin::SdkDiagnostic)
