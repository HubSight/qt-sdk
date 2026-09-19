#pragma once

#include "admin_export.h"

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QVector>

namespace HubSight::Admin {

// The complete Admin API v1 surface. The registry is intentionally broader
// than the currently implemented Phase 1 clients; deferred methods are kept in
// the public SDK so later phases do not need to redesign the client surface.
#define HUBSIGHT_ADMIN_ENDPOINTS(X)                                            \
  X(SystemStatus, systemStatus, "GET", "/system/status", false, "", "Phase 1") \
  X(AuthLogin, authLogin, "POST", "/auth/login", false, "", "Phase 1")         \
  X(AuthTwoFactorVerify, authTwoFactorVerify, "POST", "/auth/2fa/verify",      \
    false, "", "Phase 1")                                                      \
  X(AuthPasskeysLoginOptions, authPasskeysLoginOptions, "POST",                \
    "/auth/passkeys/login/options", false, "", "Deferred")                     \
  X(AuthPasskeysLoginVerify, authPasskeysLoginVerify, "POST",                  \
    "/auth/passkeys/login/verify", false, "", "Deferred")                      \
  X(AuthRefresh, authRefresh, "POST", "/auth/refresh", false, "", "Phase 1")   \
  X(AuthLogout, authLogout, "POST", "/auth/logout", true, "Authenticated",     \
    "Phase 1")                                                                 \
  X(AuthMe, authMe, "GET", "/auth/me", true, "Authenticated", "Phase 1")       \
  X(AuthVerifyPassword, authVerifyPassword, "POST", "/auth/verify-password",   \
    true, "Authenticated", "Deferred")                                         \
  X(AuthPasswordUpdate, authPasswordUpdate, "PUT", "/auth/password", true,     \
    "Authenticated", "Deferred")                                               \
  X(ProfileUpdate, profileUpdate, "PUT", "/profile", true, "Authenticated",    \
    "Deferred")                                                                \
  X(ProfileSessions, profileSessions, "GET", "/profile/sessions", true,        \
    "Authenticated", "Deferred")                                               \
  X(ProfileSessionRevoke, profileSessionRevoke, "DELETE",                      \
    "/profile/sessions/{session_id}", true, "Authenticated", "Deferred")       \
  X(ProfileSessionsRevokeOthers, profileSessionsRevokeOthers, "POST",          \
    "/profile/sessions:revoke-others", true, "Authenticated", "Deferred")      \
  X(AuthTwoFactorSetup, authTwoFactorSetup, "POST", "/auth/2fa/setup", true,   \
    "Authenticated", "Deferred")                                               \
  X(AuthTwoFactorEnable, authTwoFactorEnable, "POST", "/auth/2fa/enable",      \
    true, "Authenticated", "Deferred")                                         \
  X(AuthTwoFactorDisable, authTwoFactorDisable, "POST", "/auth/2fa/disable",   \
    true, "Authenticated", "Deferred")                                         \
  X(AuthTwoFactorRecoveryCodesRegenerate,                                      \
    authTwoFactorRecoveryCodesRegenerate, "POST",                              \
    "/auth/2fa/recovery-codes:regenerate", true, "Authenticated", "Deferred")  \
  X(AuthPasskeysList, authPasskeysList, "GET", "/auth/passkeys", true,         \
    "Authenticated", "Deferred")                                               \
  X(AuthPasskeysRegistrationOptions, authPasskeysRegistrationOptions, "POST",  \
    "/auth/passkeys/registration/options", true, "Authenticated", "Deferred")  \
  X(AuthPasskeysRegistrationVerify, authPasskeysRegistrationVerify, "POST",    \
    "/auth/passkeys/registration/verify", true, "Authenticated", "Deferred")   \
  X(AuthPasskeyRename, authPasskeyRename, "PATCH",                             \
    "/auth/passkeys/{passkey_id}", true, "Authenticated", "Deferred")          \
  X(AuthPasskeyDelete, authPasskeyDelete, "DELETE",                            \
    "/auth/passkeys/{passkey_id}", true, "Authenticated", "Deferred")          \
  X(DashboardSummary, dashboardSummary, "GET", "/dashboard/summary", true,     \
    "system:monitor", "Deferred")                                              \
  X(DashboardActivity, dashboardActivity, "GET", "/dashboard/activity", true,  \
    "system:monitor", "Deferred")                                              \
  X(SystemHealth, systemHealth, "GET", "/system/health", true,                 \
    "system:monitor", "Deferred")                                              \
  X(SystemCapabilities, systemCapabilities, "GET", "/system/capabilities",     \
    true, "system:monitor", "Phase 1")                                         \
  X(SystemSettingsGet, systemSettingsGet, "GET", "/system/settings", true,     \
    "system:settings", "Phase 1")                                              \
  X(SystemSettingsPatch, systemSettingsPatch, "PATCH", "/system/settings",     \
    true, "system:settings", "Deferred")                                       \
  X(SystemStorageCleanup, systemStorageCleanup, "POST",                        \
    "/system/storage:cleanup", true, "system:settings", "Deferred")            \
  X(SystemAuditEvents, systemAuditEvents, "GET", "/system/audit-events", true, \
    "system:monitor", "Deferred")                                              \
  X(CamerasList, camerasList, "GET", "/cameras", true, "cameras:view",         \
    "Phase 1")                                                                 \
  X(CamerasCreate, camerasCreate, "POST", "/cameras", true, "cameras:manage",  \
    "Deferred")                                                                \
  X(CameraGet, cameraGet, "GET", "/cameras/{camera_id}", true, "cameras:view", \
    "Phase 1")                                                                 \
  X(CameraPatch, cameraPatch, "PATCH", "/cameras/{camera_id}", true,           \
    "cameras:manage", "Deferred")                                              \
  X(CameraDelete, cameraDelete, "DELETE", "/cameras/{camera_id}", true,        \
    "cameras:manage", "Deferred")                                              \
  X(CameraStart, cameraStart, "POST", "/cameras/{camera_id}:start", true,      \
    "cameras:manage", "Deferred")                                              \
  X(CameraStop, cameraStop, "POST", "/cameras/{camera_id}:stop", true,         \
    "cameras:manage", "Deferred")                                              \
  X(CameraRestart, cameraRestart, "POST", "/cameras/{camera_id}:restart",      \
    true, "cameras:manage", "Deferred")                                        \
  X(CameraThumbnail, cameraThumbnail, "GET", "/cameras/{camera_id}/thumbnail", \
    true, "cameras:view", "Deferred")                                          \
  X(CameraSnapshot, cameraSnapshot, "GET", "/cameras/{camera_id}/snapshot",    \
    true, "cameras:view", "Deferred")                                          \
  X(CameraHomographyPatch, cameraHomographyPatch, "PATCH",                     \
    "/cameras/{camera_id}/homography", true, "cameras:manage", "Deferred")     \
  X(CameraPtzMove, cameraPtzMove, "POST", "/cameras/{camera_id}/ptz:move",     \
    true, "cameras:manage", "Deferred")                                        \
  X(CameraPresetsList, cameraPresetsList, "GET",                               \
    "/cameras/{camera_id}/presets", true, "cameras:view", "Deferred")          \
  X(CameraPresetsCreate, cameraPresetsCreate, "POST",                          \
    "/cameras/{camera_id}/presets", true, "cameras:manage", "Deferred")        \
  X(CameraDiscoveryScan, cameraDiscoveryScan, "POST",                          \
    "/camera-discovery:scan", true, "cameras:manage", "Deferred")              \
  X(CameraDiscoveryJobGet, cameraDiscoveryJobGet, "GET",                       \
    "/camera-discovery/jobs/{job_id}", true, "cameras:manage", "Deferred")     \
  X(CameraDiscoveryJobCancel, cameraDiscoveryJobCancel, "POST",                \
    "/camera-discovery/jobs/{job_id}:cancel", true, "cameras:manage",          \
    "Deferred")                                                                \
  X(CamerasOnvifProbe, camerasOnvifProbe, "POST", "/cameras:onvif-probe",      \
    true, "cameras:manage", "Deferred")                                        \
  X(CameraRecognitionLogsList, cameraRecognitionLogsList, "GET",               \
    "/cameras/{camera_id}/recognition-logs", true, "members:view", "Deferred") \
  X(CameraRecognitionLogsClear, cameraRecognitionLogsClear, "DELETE",          \
    "/cameras/{camera_id}/recognition-logs", true, "members:manage",           \
    "Deferred")                                                                \
  X(LiveCapabilities, liveCapabilities, "GET", "/live/capabilities", true,     \
    "cameras:view", "Phase 2")                                                 \
  X(LiveCameras, liveCameras, "GET", "/live/cameras", true, "cameras:view",    \
    "Phase 2")                                                                 \
  X(LiveSessionsNegotiate, liveSessionsNegotiate, "POST",                      \
    "/live/sessions:negotiate", true, "cameras:view", "Phase 2")               \
  X(LiveSessionsHeartbeat, liveSessionsHeartbeat, "POST",                      \
    "/live/sessions:heartbeat", true, "cameras:view", "Phase 2")               \
  X(LiveSessionsRelease, liveSessionsRelease, "POST",                          \
    "/live/sessions:release", true, "cameras:view", "Phase 2")                 \
  X(LiveSessionsChangeProfile, liveSessionsChangeProfile, "POST",              \
    "/live/sessions:change-profile", true, "cameras:view", "Phase 2")          \
  X(LiveSessionsStats, liveSessionsStats, "GET", "/live/sessions:stats", true, \
    "cameras:view", "Phase 2")                                                 \
  X(LiveSessionsQoe, liveSessionsQoe, "POST", "/live/sessions:qoe", true,      \
    "cameras:view", "Phase 2")                                                 \
  X(LiveCameraStatus, liveCameraStatus, "GET",                                 \
    "/live/cameras/{camera_id}/status", true, "cameras:view", "Phase 2")       \
  X(ArchiveTimeline, archiveTimeline, "GET", "/archive/timeline", true,        \
    "recordings:view", "Phase 2")                                              \
  X(ArchiveAvailableDays, archiveAvailableDays, "GET",                         \
    "/archive/cameras/{camera_id}/available-days", true, "recordings:view",    \
    "Phase 2")                                                                 \
  X(ArchiveRecordingGet, archiveRecordingGet, "GET",                           \
    "/archive/recordings/{recording_id}", true, "recordings:view", "Phase 2")  \
  X(ArchiveRecordingPlaybackUrl, archiveRecordingPlaybackUrl, "POST",          \
    "/archive/recordings/{recording_id}:playback-url", true,                   \
    "recordings:view", "Phase 2")                                              \
  X(ArchiveRecordingDownloadUrl, archiveRecordingDownloadUrl, "POST",          \
    "/archive/recordings/{recording_id}:download-url", true,                   \
    "recordings:view", "Phase 2")                                              \
  X(ArchiveRecordingThumbnailUrl, archiveRecordingThumbnailUrl, "POST",        \
    "/archive/recordings/{recording_id}:thumbnail-url", true,                  \
    "recordings:view", "Phase 2")                                              \
  X(MembersList, membersList, "GET", "/members", true, "members:view",         \
    "Deferred")                                                                \
  X(MembersCreate, membersCreate, "POST", "/members", true, "members:manage",  \
    "Deferred")                                                                \
  X(MemberGet, memberGet, "GET", "/members/{member_id}", true, "members:view", \
    "Deferred")                                                                \
  X(MemberPatch, memberPatch, "PATCH", "/members/{member_id}", true,           \
    "members:manage", "Deferred")                                              \
  X(MemberDelete, memberDelete, "DELETE", "/members/{member_id}", true,        \
    "members:manage", "Deferred")                                              \
  X(MemberAvatarPut, memberAvatarPut, "PUT", "/members/{member_id}/avatar",    \
    true, "members:manage", "Deferred")                                        \
  X(MemberAvatarDelete, memberAvatarDelete, "DELETE",                          \
    "/members/{member_id}/avatar", true, "members:manage", "Deferred")         \
  X(MemberFacesList, memberFacesList, "GET", "/members/{member_id}/faces",     \
    true, "members:view", "Deferred")                                          \
  X(MemberFacesEnroll, memberFacesEnroll, "POST",                              \
    "/members/{member_id}/faces:enroll", true, "members:manage", "Deferred")   \
  X(MemberFaceDelete, memberFaceDelete, "DELETE",                              \
    "/members/{member_id}/faces/{face_id}", true, "members:manage",            \
    "Deferred")                                                                \
  X(MemberFacesBatchDelete, memberFacesBatchDelete, "POST",                    \
    "/members/{member_id}/faces:batch-delete", true, "members:manage",         \
    "Deferred")                                                                \
  X(UploadImages, uploadImages, "POST", "/uploads/images", true,               \
    "members:manage", "Deferred")                                              \
  X(UploadImagesPresign, uploadImagesPresign, "POST",                          \
    "/uploads/images:presign", true, "members:manage", "Deferred")             \
  X(NotificationsList, notificationsList, "GET", "/notifications", true,       \
    "Authenticated", "Phase 2")                                                \
  X(NotificationGet, notificationGet, "GET",                                   \
    "/notifications/{notification_id}", true, "Authenticated", "Phase 2")      \
  X(NotificationPatch, notificationPatch, "PATCH",                             \
    "/notifications/{notification_id}", true, "Authenticated", "Phase 2")      \
  X(NotificationsReadAll, notificationsReadAll, "POST",                        \
    "/notifications:read-all", true, "Authenticated", "Phase 2")               \
  X(NotificationDelete, notificationDelete, "DELETE",                          \
    "/notifications/{notification_id}", true, "Authenticated", "Phase 2")      \
  X(NotificationsBatchDelete, notificationsBatchDelete, "POST",                \
    "/notifications:batch-delete", true, "Authenticated", "Phase 2")           \
  X(NotificationsClear, notificationsClear, "POST", "/notifications:clear",    \
    true, "Authenticated", "Phase 2")                                          \
  X(NotificationsTest, notificationsTest, "POST", "/notifications:test", true, \
    "system:monitor", "Phase 2")                                               \
  X(NotificationPushConfig, notificationPushConfig, "GET",                     \
    "/notifications/push-config", true, "Authenticated", "Phase 2")            \
  X(NotificationPushSubscriptionPut, notificationPushSubscriptionPut, "PUT",   \
    "/notifications/push-subscriptions/current", true, "Authenticated",        \
    "Phase 2")                                                                 \
  X(NotificationPushSubscriptionDelete, notificationPushSubscriptionDelete,    \
    "DELETE", "/notifications/push-subscriptions/current", true,               \
    "Authenticated", "Phase 2")                                                \
  X(NvrStatus, nvrStatus, "GET", "/nvr/status", true, "system:monitor",        \
    "Deferred")                                                                \
  X(PoolStatus, poolStatus, "GET", "/pool/status", true, "system:monitor",     \
    "Deferred")                                                                \
  X(PoolSync, poolSync, "POST", "/pool:sync", true, "system:monitor",          \
    "Deferred")                                                                \
  X(PermissionsList, permissionsList, "GET", "/permissions", true,             \
    "users:view", "Deferred")                                                  \
  X(RolesList, rolesList, "GET", "/roles", true, "users:view", "Deferred")     \
  X(RolesCreate, rolesCreate, "POST", "/roles", true, "roles:manage",          \
    "Deferred")                                                                \
  X(RoleGet, roleGet, "GET", "/roles/{role_id}", true, "users:view",           \
    "Deferred")                                                                \
  X(RolePatch, rolePatch, "PATCH", "/roles/{role_id}", true, "roles:manage",   \
    "Deferred")                                                                \
  X(RoleDelete, roleDelete, "DELETE", "/roles/{role_id}", true,                \
    "roles:manage", "Deferred")                                                \
  X(UsersList, usersList, "GET", "/users", true, "users:view", "Deferred")     \
  X(UsersCreate, usersCreate, "POST", "/users", true, "users:manage",          \
    "Deferred")                                                                \
  X(UserGet, userGet, "GET", "/users/{user_id}", true, "users:view",           \
    "Deferred")                                                                \
  X(UserPatch, userPatch, "PATCH", "/users/{user_id}", true, "users:manage",   \
    "Deferred")                                                                \
  X(UserResetPassword, userResetPassword, "POST",                              \
    "/users/{user_id}:reset-password", true, "users:manage", "Deferred")       \
  X(UserBlock, userBlock, "POST", "/users/{user_id}:block", true,              \
    "users:manage", "Deferred")                                                \
  X(UserUnblock, userUnblock, "POST", "/users/{user_id}:unblock", true,        \
    "users:manage", "Deferred")                                                \
  X(UserDelete, userDelete, "DELETE", "/users/{user_id}", true,                \
    "users:manage", "Deferred")                                                \
  X(UserSessionsList, userSessionsList, "GET", "/users/{user_id}/sessions",    \
    true, "users:manage", "Deferred")                                          \
  X(UserSessionDelete, userSessionDelete, "DELETE",                            \
    "/users/{user_id}/sessions/{session_id}", true, "users:manage",            \
    "Deferred")                                                                \
  X(UserSessionsRevokeAll, userSessionsRevokeAll, "POST",                      \
    "/users/{user_id}/sessions:revoke-all", true, "users:manage", "Deferred")  \
  X(ClientsList, clientsList, "GET", "/clients", true, "clients:manage",       \
    "Deferred")                                                                \
  X(ClientsCreate, clientsCreate, "POST", "/clients", true, "clients:manage",  \
    "Deferred")                                                                \
  X(ClientGet, clientGet, "GET", "/clients/{client_id}", true,                 \
    "clients:manage", "Deferred")                                              \
  X(ClientPatch, clientPatch, "PATCH", "/clients/{client_id}", true,           \
    "clients:manage", "Deferred")                                              \
  X(ClientEnable, clientEnable, "POST", "/clients/{client_id}:enable", true,   \
    "clients:manage", "Deferred")                                              \
  X(ClientDisable, clientDisable, "POST", "/clients/{client_id}:disable",      \
    true, "clients:manage", "Deferred")                                        \
  X(ClientRotateKey, clientRotateKey, "POST",                                  \
    "/clients/{client_id}:rotate-key", true, "clients:manage", "Deferred")     \
  X(ClientsVerify, clientsVerify, "POST", "/clients:verify", true,             \
    "clients:manage", "Deferred")                                              \
  X(ClientDelete, clientDelete, "DELETE", "/clients/{client_id}", true,        \
    "clients:manage", "Deferred")                                              \
  X(GoogleServiceAccountsList, googleServiceAccountsList, "GET",               \
    "/google-service-accounts", true, "service_accounts:manage", "Deferred")   \
  X(GoogleServiceAccountImport, googleServiceAccountImport, "POST",            \
    "/google-service-accounts:import", true, "service_accounts:manage",        \
    "Deferred")                                                                \
  X(GoogleServiceAccountGet, googleServiceAccountGet, "GET",                   \
    "/google-service-accounts/{account_id}", true, "service_accounts:manage",  \
    "Deferred")                                                                \
  X(GoogleServiceAccountActivate, googleServiceAccountActivate, "POST",        \
    "/google-service-accounts/{account_id}:activate", true,                    \
    "service_accounts:manage", "Deferred")                                     \
  X(GoogleServiceAccountTest, googleServiceAccountTest, "POST",                \
    "/google-service-accounts/{account_id}:test", true,                        \
    "service_accounts:manage", "Deferred")                                     \
  X(GoogleServiceAccountFirebasePreflight,                                     \
    googleServiceAccountFirebasePreflight, "GET",                              \
    "/google-service-accounts/{account_id}/firebase-preflight", true,          \
    "service_accounts:manage", "Deferred")                                     \
  X(GoogleServiceAccountDelete, googleServiceAccountDelete, "DELETE",          \
    "/google-service-accounts/{account_id}", true, "service_accounts:manage",  \
    "Deferred")                                                                \
  X(AppConfigsList, appConfigsList, "GET", "/app-configs", true,               \
    "app_configs:manage", "Deferred")                                          \
  X(AppConfigCreate, appConfigCreate, "POST", "/app-configs", true,            \
    "app_configs:manage", "Deferred")                                          \
  X(AppConfigGet, appConfigGet, "GET", "/app-configs/{config_id}", true,       \
    "app_configs:manage", "Deferred")                                          \
  X(AppConfigDownloadUrl, appConfigDownloadUrl, "POST",                        \
    "/app-configs/{config_id}/download-url", true, "app_configs:manage",       \
    "Deferred")                                                                \
  X(AppConfigQr, appConfigQr, "GET", "/app-configs/{config_id}/qr", true,      \
    "app_configs:manage", "Deferred")                                          \
  X(AppConfigRevoke, appConfigRevoke, "POST",                                  \
    "/app-configs/{config_id}:revoke", true, "app_configs:manage", "Deferred") \
  X(AppConfigDelete, appConfigDelete, "DELETE", "/app-configs/{config_id}",    \
    true, "app_configs:manage", "Deferred")                                    \
  X(OperationGet, operationGet, "GET", "/operations/{operation_id}", true,     \
    "Originating permission", "Deferred")                                      \
  X(OperationCancel, operationCancel, "POST",                                  \
    "/operations/{operation_id}:cancel", true, "Originating permission",       \
    "Deferred")                                                                \
  X(RealtimeRelay, realtimeRelay, "WEBSOCKET", "/relay/admin/v1", true,        \
    "Realtime auth", "Deferred")

enum class AdminEndpoint {
#define HUBSIGHT_ADMIN_ENDPOINT_ENUM(id, method, httpMethod, path, auth,       \
                                     permission, phase)                        \
  id,
  HUBSIGHT_ADMIN_ENDPOINTS(HUBSIGHT_ADMIN_ENDPOINT_ENUM)
#undef HUBSIGHT_ADMIN_ENDPOINT_ENUM
};

struct HUBSIGHT_ADMIN_EXPORT AdminEndpointDefinition {
  AdminEndpoint endpoint;
  QByteArray method;
  QString pathTemplate;
  QString operation;
  bool requiresAuthentication = true;
  QString permission;
  QString phase;
};

HUBSIGHT_ADMIN_EXPORT QVector<AdminEndpointDefinition> adminEndpointCatalog();
HUBSIGHT_ADMIN_EXPORT const AdminEndpointDefinition &
adminEndpointDefinition(AdminEndpoint endpoint);

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::AdminEndpoint)
Q_DECLARE_METATYPE(HubSight::Admin::AdminEndpointDefinition)
