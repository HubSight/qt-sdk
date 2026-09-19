# Backend integration testing

The SDK includes an opt-in Qt Test target for running smoke tests against a real
HubSight Admin backend. It is disabled by default so normal builds and public CI
do not require credentials.

## Environment variables

Required for the public status smoke test:

```sh
export HUBSIGHT_ADMIN_INTEGRATION_GATEWAY_URL="https://admin.example.com"
export HUBSIGHT_ADMIN_INTEGRATION_API_KEY="redacted-test-api-key"
```

Optional variables enable the JWT authentication smoke test:

```sh
export HUBSIGHT_ADMIN_INTEGRATION_USERNAME="admin-test-user"
export HUBSIGHT_ADMIN_INTEGRATION_PASSWORD="redacted-test-password"
```

Use a dedicated non-production test account. Do not put these values in source
files, shell history committed to the repository, CI logs, or issue reports.

## Configure and run

```sh
cmake -S . -B build-integration \
  -DHUBSIGHT_ADMIN_BUILD_TESTS=ON \
  -DHUBSIGHT_ADMIN_BUILD_INTEGRATION_TESTS=ON \
  -DHUBSIGHT_ADMIN_ENABLE_HSCFG_IMPORT=OFF \
  -DHUBSIGHT_ADMIN_ENABLE_DESKTOP_SECURE_STORAGE=OFF
cmake --build build-integration --parallel
ctest --test-dir build-integration --output-on-failure
```

Without the environment variables, the integration tests report `SKIP` rather
than failing. This makes the target safe to include in developer workflows while
keeping credentials outside the repository.

## What is verified

`hubsight-admin-sdk-integration-tests` currently verifies:

- the configured gateway accepts the SDK's Admin URL and API-key contract;
- `GET /api/admin/v1/system/status` returns a valid Admin response;
- the SDK maps backend errors to structured `AdminError` values;
- optional username/password authentication produces a valid JWT session or an
  explicit 2FA challenge;
- the app facade can own the authentication flow without exposing native
  transport objects.

The test intentionally does not enable automatic realtime or WebRTC media. Those
flows need a backend environment with the desired relay, STUN/TURN, and media
services and should be added as separately provisioned integration suites.

## CI usage

A protected CI environment can run this target by injecting the four variables as
secrets/environment variables. Keep the integration job separate from untrusted
pull requests so secrets are never exposed to forked code.
