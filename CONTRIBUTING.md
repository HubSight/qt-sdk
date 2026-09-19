# Contributing to HubSight Admin SDK for Qt/C++

Thank you for contributing to the HubSight Admin SDK. This repository contains a
cross-platform C++20/Qt desktop SDK for Windows, macOS, and Linux.

## Before opening an issue or pull request

1. Search existing issues and pull requests.
2. Read the relevant documentation under [`docs/`](docs/).
3. For API changes, check the Admin API catalog and the typed resource DTOs.
4. Never include API keys, JWTs, refresh tokens, passwords, `.hscfg` PINs, or
   production endpoint data in issues, logs, fixtures, or commits.

Security vulnerabilities must not be reported in a public issue. Follow
[`SECURITY.md`](SECURITY.md) instead.

## Development requirements

- Qt 6.6 or newer:
  - Qt Core;
  - Qt Network;
  - Qt WebSockets;
  - Qt Test for the test suite.
- C++20 compiler.
- CMake 3.21 or newer.
- A supported desktop host: Windows, macOS, or Linux.

See [`docs/BUILD_CROSS_PLATFORM.md`](docs/BUILD_CROSS_PLATFORM.md) for the
supported architecture matrix.

## Configure, build, and test

```sh
cmake -S . -B build \
  -DHUBSIGHT_ADMIN_BUILD_TESTS=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Before submitting a change, also run:

```sh
git diff --check
```

The `.hscfg` importer and desktop secure storage integrations are optional at
configure time. A local build may disable them when their native dependencies
are not installed, but changes to those areas should be tested with the relevant
platform dependencies in CI or a native development environment.

## Project conventions

- Keep the public API under `include/hubsight/admin/` and implementation under
  `src/`.
- Prefer existing Qt and CMake facilities over introducing new dependencies.
- Keep normal application code independent from `QNetworkReply`, JWT lifecycle,
  Socket.IO framing, and native WebRTC objects.
- Use typed resource clients for stable Admin API domains and keep
  `AdminEndpointClient` generic behavior forward-compatible.
- Preserve the `/api/admin/v1` namespace and the documented realtime boundaries.
- Return structured `AdminError` values with stable error codes.
- Do not log or serialize credentials or sensitive payloads.
- Add or update contract tests whenever behavior, endpoint mapping, authentication,
  diagnostics, or DTO parsing changes.
- Keep changes focused. Unrelated refactors should be proposed separately.

There is no requirement to use a particular commit-message tool. Clear, small
commits are preferred, and commit messages should explain the user-visible
change.

## Pull request checklist

Before requesting review, confirm that:

- [ ] the change is scoped to the stated problem;
- [ ] public API changes are documented in `README.md` or `docs/`;
- [ ] tests cover new behavior and failure paths;
- [ ] `cmake --build build --parallel` succeeds;
- [ ] `ctest --test-dir build --output-on-failure` succeeds;
- [ ] `git diff --check` succeeds;
- [ ] no credentials, private URLs, generated build output, or machine-specific
      paths are included;
- [ ] cross-platform implications were considered;
- [ ] breaking changes and migration notes are called out clearly.

Maintainers may request a backend contract test or a platform-specific build for
changes affecting authentication, `.hscfg`, secure storage, HTTP negotiation,
realtime, uploads, or WebRTC signaling.
