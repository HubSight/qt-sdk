# Support

## Documentation first

Start with:

- [`README.md`](README.md) for installation, architecture, and application usage;
- [`docs/BUILD_CROSS_PLATFORM.md`](docs/BUILD_CROSS_PLATFORM.md) for supported
  Windows, Linux, and macOS builds;
- [`docs/HSCFG_IMPORT.md`](docs/HSCFG_IMPORT.md) for `.hscfg` configuration;
- [`docs/RELAY_FOUNDATION.md`](docs/RELAY_FOUNDATION.md) for Standard JSON relay;
- [`docs/WEBRTC_FOUNDATION.md`](docs/WEBRTC_FOUNDATION.md) for the WebRTC boundary;
- [`CONTRIBUTING.md`](CONTRIBUTING.md) for development and pull requests.

## Questions and bug reports

For a reproducible bug, open a GitHub issue and include:

- SDK version or commit;
- operating system and architecture;
- Qt and compiler versions;
- exact CMake configuration;
- sanitized diagnostic output;
- minimal reproduction steps.

Do not include API keys, JWTs, refresh tokens, passwords, `.hscfg` PINs, private
URLs, or customer data. Use synthetic values and redacted logs.

For feature requests, explain the use case, affected platform(s), API contract,
and whether the change is backward-compatible.

Security vulnerabilities must be reported privately as described in
[`SECURITY.md`](SECURITY.md), not through a public issue.
