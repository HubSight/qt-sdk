# Security Policy

## Supported versions

Security fixes are prioritized for the latest code on the default branch and for
the latest published release. Older releases may not receive fixes when the
underlying Qt, compiler, operating system, or dependency is no longer supported.

## Reporting a vulnerability

Please do **not** report security vulnerabilities in a public GitHub issue,
pull request, discussion, or chat message.

Use GitHub's private vulnerability reporting for this repository:

<https://github.com/HubSight/qt-sdk/security/advisories/new>

If private reporting is unavailable, contact the repository maintainers through
GitHub and clearly mark the message as a confidential security report.

Please include, when safe to share:

- affected version, commit, or build;
- operating system, architecture, Qt version, and compiler;
- a concise description of the impact;
- reproducible steps or a minimal proof of concept;
- whether credentials, tokens, URLs, uploaded files, or private data are exposed;
- any suggested mitigation.

Do not include real API keys, JWTs, refresh tokens, passwords, `.hscfg` PINs,
production `.hscfg` files, private endpoints, or customer data. Redact them and
use synthetic fixtures instead.

## Scope

Security reports are particularly important for:

- JWT and refresh-token handling;
- desktop secure storage integrations;
- `.hscfg` parsing, decryption, integrity verification, and memory handling;
- URL validation and credential leakage;
- API key and Authorization header handling;
- WebSocket authentication and reconnect behavior;
- multipart upload validation and path traversal;
- memory safety, denial of service, and dependency vulnerabilities.

The SDK deliberately does not store refresh tokens in plaintext application
settings and does not put credentials in URLs. Changes that weaken these
properties require explicit security review.

## Disclosure

Maintainers will coordinate investigation, remediation, release timing, and
public disclosure with the reporter. Please allow reasonable time for a fix
before publishing details.
