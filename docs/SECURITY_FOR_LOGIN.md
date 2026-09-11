# Agent Guide: Session Logging, Device Fingerprinting & Session Management for HubSight CCTV Platform

## 1. System Background & Context

- HubSight is a centralized CCTV surveillance platform: client viewer applications (Flutter mobile/desktop, Web React) **do not connect directly to IP cameras**; instead, they route all connections through the central backend.
- The backend serves video streams to clients via **WebRTC** (ultra-low latency), coordinated by the Realtime SDK (WebRTC + Socket.IO).
- Authentication and business APIs (including sessions and playback segment feeds) belong to the **Core SDK** domain — client applications do not manage raw HTTP authentication lifecycles or build isolated HTTP clients from scratch.
- **Specific requirements**: Track **login sessions** (distinct from camera viewing logs). Account owners have the right to audit their own login history (device, timestamp, geolocation) and **revoke** any suspect session — revoking a session must take effect **in real time** (the revoked device is kicked immediately, without waiting for natural token expiration).

---

## 2. Scope of Work

The implementation covers three interrelated capability domains:

1. **Session Logging** — Records login session lifecycles, enabling users and administrators to audit authentication history.
2. **Device Fingerprinting** — Identifies viewer devices and detects unfamiliar hardware.
3. **Session Management** — Oversees token lifecycles and provides real-time revocation.

**Non-Goals for this Guide**:
- Core authentication mechanisms (password hashing, MFA/passkey enrollment flows), WebRTC stream encryption, and camera ACL access control belong to separate service modules.
- **Camera Access Logging (who viewed which camera and when) is out of scope for this guide.** This document tracks authentication sessions (`login_session`). Camera access auditing can be layered on later as compliance requirements emerge (`session_id` serves as a natural foreign key).

---

## 3. Data Model

### 3.1. `login_session` Table (Login Lifecycle)

```
login_session
├── id                  UUID, PK
├── user_id             FK → users
├── ip_address          inet
├── user_agent          text (raw)
├── device_fingerprint  text (hash, see section 4)
├── device_label        text (e.g. "Windows Desktop App", "iPhone 15 - Flutter")
├── client_type         enum: web | desktop_windows | desktop_mac | mobile_ios | mobile_android
├── geo_city / geo_country
├── created_at
├── last_active_at
├── expires_at
├── revoked_at          nullable
├── revoke_reason       enum: user_logout | admin_revoke | anomaly_detected | expired | concurrent_limit
```

### 3.2. Immutability Principles

- `login_session` records are never hard-deleted — only `revoked_at` is set (soft state), preserving original records for audit history.
- Use DB constraints or triggers to prevent direct `UPDATE`/`DELETE` queries against immutable audit columns (`created_at`, `ip_address`, `user_id`), permitting updates only to state columns (`revoked_at`, `last_active_at`).

---

## 4. Device Fingerprinting

### 4.1. Strategy by Client Type

| Client | Source Fingerprint |
|---|---|
| Web (React) | Passive: UA, Accept-Language + TLS/JA3 if available at edge. FingerprintJS can be added if higher precision is required. |
| Flutter Desktop (Windows/Mac) | OS-level stable hardware identifier (Windows: `MachineGuid` from registry; macOS: `IOPlatformUUID`) + locally stored installation UUID. Highly stable across reboots. |
| Flutter Mobile | `device_info_plus`: `identifierForVendor` (iOS) / `Android ID` (Android) combined with app installation ID stored in secure storage. |

**Key Architectural Decision**: Because native clients (Flutter desktop and mobile) represent the majority of clients, avoid brittle browser-style canvas/WebGL fingerprinting. Extract stable device identifiers supplied by the OS/SDK and SHA-256 hash them prior to transmission.

### 4.2. Execution Point

- The device fingerprint is computed by the **Core SDK** and attached automatically to login and refresh requests. Client application UI layers do not handle this logic manually.
- The server stores the SHA-256 hash and avoids retaining unnecessary raw hardware identifiers.

### 4.3. Unfamiliar Device Detection Logic

```
Upon successful login API invocation:
1. Server receives device_fingerprint from request.
2. Compares against registered fingerprints for this user_id in `known_devices`.
3. If NO match is found:
   → Create login_session with is_new_device = true
   → Dispatch security alert (Email / FCM Push): "New login detected from an unrecognized device"
   → For admin or security roles, optionally mandate step-up authentication before granting camera stream access.
4. If a match is found:
   → Grant session normally and update known_devices.last_seen_at.
```

### 4.4. Client Device Metadata Contract

To provide detailed session logging (`login_session` & `known_devices`), both Web SPAs and Mobile/Desktop Apps (Flutter, React Native, Go) **must transmit complete device metadata** during authentication calls (`POST /api/auth/login`, `POST /api/auth/2fa/verify`).

#### 4.4.1. `device_info` JSON Body Payload Structure

```json
{
  "username": "admin",
  "password": "...",
  "device_info": {
    "fingerprint": "a3f89b91c49b6d41829e18b1...",
    "device_label": "Apple iPhone 15 Pro (iOS 17.5.1) • App v1.2.0",
    "client_type": "mobile_ios",
    "platform": "iOS",
    "os_version": "17.5.1",
    "model": "iPhone 15 Pro",
    "manufacturer": "Apple",
    "app_version": "1.2.0",
    "screen_resolution": "1179x2556",
    "language": "en-US",
    "timezone": "UTC"
  }
}
```

| Field | Type | Description | Example |
|---|---|---|---|
| `fingerprint` | string | Unique, stable SHA-256 device identifier | SHA-256 hash |
| `device_label` | string | Human-readable device label displayed in UI | `Apple iPhone 15 Pro (iOS 17.5.1) • App v1.2.0` |
| `client_type` | enum | Client classification (`web`, `mobile_ios`, `mobile_android`, `desktop_windows`, `desktop_mac`, `desktop_linux`, `desktop_app`, `third_party`) | `mobile_ios` |
| `platform` | string | Operating system name | `iOS`, `Android`, `Windows`, `macOS`, `Linux` |
| `os_version` | string | Operating system version | `17.5.1`, `14.0`, `11` |
| `browser_name` | string | (Web only) Browser name | `Chrome`, `Firefox`, `Safari`, `Edge` |
| `browser_version`| string | (Web only) Browser version | `128.0` |
| `app_version` | string | Application release version | `1.2.0` |
| `model` | string | Hardware model identifier | `iPhone 15 Pro`, `SM-S928B`, `ThinkPad X1` |
| `manufacturer` | string | Hardware manufacturer | `Apple`, `Samsung`, `Lenovo`, `Dell` |
| `screen_resolution` | string | Screen display resolution | `1179x2556`, `1920x1080` |
| `language` | string | System/client locale | `en-US`, `vi-VN` |
| `timezone` | string | IANA timezone string | `UTC`, `Asia/Ho_Chi_Minh` |

#### 4.4.2. Dual HTTP Header Injection

Alongside the JSON body, clients attach matching HTTP headers so gateways and logging reverse proxies can inspect device identity on requests that lack bodies:
- `X-Device-Fingerprint`: Unique device hash
- `X-Device-Label`: Human-readable device label
- `X-Client-Type`: Client type identifier (`mobile_ios`, `mobile_android`, `desktop_windows`, `web`)
- `X-Screen-Resolution`: Screen resolution string

#### 4.4.3. Flutter Integration Implementation

Using `device_info_plus` and `package_info_plus`:

```dart
import 'dart:io';
import 'dart:convert';
import 'package:device_info_plus/device_info_plus.dart';
import 'package:package_info_plus/package_info_plus.dart';
import 'package:crypto/crypto.dart';

Future<Map<String, dynamic>> collectDeviceInfo() async {
  final deviceInfoPlugin = DeviceInfoPlugin();
  final packageInfo = await PackageInfo.fromPlatform();
  
  String fingerprint = '';
  String model = '';
  String manufacturer = '';
  String osVersion = '';
  String platform = '';
  String clientType = 'mobile_ios';

  if (Platform.isIOS) {
    final ios = await deviceInfoPlugin.iosInfo;
    platform = 'iOS';
    clientType = 'mobile_ios';
    model = ios.utsname.machine; // e.g. iPhone15,2
    manufacturer = 'Apple';
    osVersion = ios.systemVersion;
    fingerprint = sha256.convert(utf8.encode(ios.identifierForVendor ?? 'ios_device')).toString();
  } else if (Platform.isAndroid) {
    final android = await deviceInfoPlugin.androidInfo;
    platform = 'Android';
    clientType = 'mobile_android';
    model = android.model;
    manufacturer = android.manufacturer;
    osVersion = android.version.release;
    fingerprint = sha256.convert(utf8.encode('${android.id}_${android.hardware}')).toString();
  } else if (Platform.isWindows) {
    final win = await deviceInfoPlugin.windowsInfo;
    platform = 'Windows';
    clientType = 'desktop_windows';
    model = win.computerName;
    manufacturer = 'PC';
    fingerprint = sha256.convert(utf8.encode(win.deviceId)).toString();
  }

  final deviceLabel = '$manufacturer $model ($platform $osVersion) • App v${packageInfo.version}';

  return {
    'fingerprint': fingerprint,
    'device_label': deviceLabel,
    'client_type': clientType,
    'platform': platform,
    'os_version': osVersion,
    'model': model,
    'manufacturer': manufacturer,
    'app_version': packageInfo.version,
    'language': Platform.localeName,
  };
}
```

---

## 5. Session Management

### 5.1. Token Strategy

- **Short-Lived Access Tokens (JWT)**: 10-15 minute lifespan. Given the sensitive nature of surveillance feeds, long-lived access tokens are strictly prohibited.
- **Opaque Refresh Tokens**: Stored in the database (not JWTs), allowing instant revocation.
- **Refresh Token Rotation**: Each refresh cycle issues a new token pair and invalidates the previous refresh token. Replay of an invalidated refresh token immediately invalidates the entire session lineage and triggers security alerts.

### 5.2. Concurrent Session Limits

Do not impose arbitrary single-device limits. Security guards frequently monitor streams on control room workstations while simultaneously using patrol mobile devices.

Instead:
- Configure concurrent session limits **per role** (e.g., `viewer` capped at 2 sessions, while `security_admin` is unconstrained).
- Prefer **anomaly alerts** over rigid rejections (e.g. 5+ concurrent logins across geographically disparate IPs within 10 minutes warrants an immediate administrative alert).

### 5.3. Real-Time Revocation

When an account owner or administrator revokes a session, it must be invalidated **immediately**:

Combine two complementary enforcement mechanisms:
1. **API Gateway / Middleware Verification**: Every request with a Bearer token checks `session_id` state in Redis (`session:{id} → revoked | active`), rather than relying solely on unexpired JWT signatures.
2. **Real-Time Signal Broadcast via Socket.IO**: Server emits `session:revoked` directly to the active socket connection of the target device. Client apps listen for this event and immediately:
   - Terminate active WebRTC streams.
   - Clear credentials from secure storage.
   - Redirect the user to the login screen.

Using both ensures live video streams are terminated instantaneously (via Socket.IO) and subsequent HTTP requests are blocked even if socket connectivity is momentarily lost (via Redis).

### 5.4. Timeouts

- **Live View Idle Timeout**: Shorter than standard applications (e.g., 20 minutes without UI interaction requires re-authentication).
- **Absolute Session Timeout**: Configurable by organizational policy (e.g., mandatory re-authentication per work shift).

---

## 6. Anomaly Detection Baseline

Implement the following deterministic rules:

1. **Impossible Travel**: Consecutive logins separated by geographic distances that could not physically be traversed in the elapsed interval.
2. **Off-Hours Logins**: Unusual login times outside established work shifts or operational patterns are flagged in audit logs.
3. **Repeated Failed Authentication Attempts**: Consecutive failures from the same IP or account trigger rate limits and credential stuffing alerts.

---

## 7. Implementation Acceptance Criteria

- [ ] Every authentication creates a `login_session` record populated with IP, User-Agent, fingerprint, and geolocation.
- [ ] Users can query their own active and historical sessions via API and UI.
- [ ] Account owners can revoke their own sessions; administrators can revoke any user's sessions.
- [ ] Revocation takes effect in **real time**: subsequent HTTP requests are rejected at the Gateway (via Redis), AND active socket sessions are kicked immediately via `session:revoked`.
- [ ] Refresh token rotation is active; token reuse invalidates the entire token chain.
- [ ] Fingerprint mismatches trigger notifications and step-up authentication when configured.
- [ ] Concurrent session thresholds are configurable by role.
- [ ] Historical audit columns on `login_session` cannot be modified via standard application updates.