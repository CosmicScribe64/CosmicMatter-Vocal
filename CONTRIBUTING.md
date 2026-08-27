# Contributing

Thanks for helping improve Cosmic Matter: Vocal. Bug reports, reproducible
voicebank compatibility cases, documentation corrections, and focused pull
requests are welcome.

## Before opening an issue

1. Check the manual's troubleshooting section and search existing issues.
2. Reproduce the problem with the latest `main` branch or released package.
3. Record the Rack version, operating system/architecture, Vocal version,
   singer/phonemizer, import format, and exact steps.
4. For audio defects, include the smallest score or public-domain fixture that
   demonstrates the problem. Do not upload a voicebank unless its license
   permits redistribution.

Crash logs and project files can contain local file paths. Review them before
attaching them to a public issue.

## Build and test

The reference SDK is VCV Rack SDK 2.6.6.

```sh
export RACK_DIR=/path/to/Rack-SDK
make
make test
make release
```

For the host-independent core and offline renderer checks, Docker is preferred:

```sh
make docker-test
```

Run `sh scripts/validate_release.sh` before submitting a pull request. Changes
to rendering, phonemization, import/export, transport, or persistence should
include a focused automated regression. UI changes should include visual and
mouse/keyboard verification in real VCV Rack.

## Pull requests

- Keep changes scoped and explain user-visible behavior.
- Preserve real-time audio-thread safety and the immutable render-snapshot
  boundary documented in `ARCHITECTURE.md`.
- Do not commit Rack SDKs, OpenUtau checkouts, generated packages, local runtime
  directories, or third-party assets without clear redistribution permission.
- Retain third-party notices and provenance when modifying bundled data.

Project-authored contributions are accepted under the repository's MIT License.
