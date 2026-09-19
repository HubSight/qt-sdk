## Summary

<!-- Describe what changed and why. Link related issues with `Fixes #123` when applicable. -->

## Scope

- [ ] Public API
- [ ] HTTP/Admin API contract
- [ ] Authentication or secure storage
- [ ] Realtime/WebSocket
- [ ] Uploads
- [ ] WebRTC foundation/signaling
- [ ] Build/CI/tooling
- [ ] Documentation only

## Validation

- [ ] `cmake --build build --parallel`
- [ ] `ctest --test-dir build --output-on-failure`
- [ ] `git diff --check`
- [ ] Platform-specific validation performed where relevant

## Security and compatibility

- [ ] No credentials, tokens, private URLs, or customer data are included.
- [ ] Backward compatibility impact is documented.
- [ ] New failure paths return structured diagnostics/errors.
- [ ] Documentation and changelog are updated if needed.

## Notes for reviewers

<!-- Mention design trade-offs, known limitations, migration steps, or follow-up work. -->
