# Changelog

All notable changes to HubSight Admin SDK for Qt/C++ are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and
versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Generic request execution for all HTTP entries in the 136-entry Admin API
  catalog.
- Typed account, camera-management, identity, integration, member, and system
  operations clients.
- Multipart image upload and binary avatar upload helpers.
- Full-catalog contract tests, opt-in real-backend smoke tests, an independent
  Qt Core `HubSight::Preferences::PreferenceStore`, and open-source project
  governance documentation.

### Changed

- `AdminClient::api()` now performs real HTTP requests instead of returning a
  not-implemented placeholder error.
- Documentation now distinguishes the complete Admin REST surface from optional
  native WebRTC media and Socket.IO compatibility features.

### Security

- Generic endpoint requests preserve API-key/JWT handling and do not place
  credentials in URLs or query strings.
- Backend integration tests skip safely without credentials and document the
  secret/environment boundary for CI.

## [0.1.0]

The initial desktop SDK foundation release. See [`docs/PHASE1.md`](docs/PHASE1.md)
for the delivered REST, authentication, realtime, diagnostics, `.hscfg`, and
WebRTC foundation work.

[Unreleased]: https://github.com/HubSight/qt-sdk/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/HubSight/qt-sdk/releases/tag/v0.1.0
