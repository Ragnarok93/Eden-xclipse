# AI-assisted development policy - Ragnarok93/Eden-xclipse

## Scope

The maintainer authorizes AI-assisted and Codex-driven research, implementation,
code review, tests, documentation, debugging, and automated optimization **within
this fork**. [AGENTS.md](../../AGENTS.md) contains the binding engineering and
automation requirements. This fork-specific policy supersedes the inherited
prohibition for `Ragnarok93/Eden-xclipse` only, not upstream Eden or its
communities. Never interpret it as permission to submit AI-assisted work to an
upstream project that prohibits it.

## Engineering standards

- Preserve the architecture and correctness of the Switch emulator. Focus on
  Exynos 2400 / Xclipse 940 stability, Vulkan 1.3 compatibility, accurate
  rendering, and measured performance.
- Validate device features and formats by live Vulkan queries and, as needed,
  execution probes. A historical capability report is evidence, not authority.
- Keep changes small, testable, reversible, and license-compliant. Preserve
  existing SPDX attribution and follow the coding standards.
- Write concise commits that describe observed changes, not conjectured gains.
- Distinguish hypotheses, repository facts, CI results, and physical-device
  measurements. Never invent successful builds, hashes, logs, test results,
  screenshots, or performance figures.

## Automation safety

- Use explicit ADB device selection and an exclusive session lease. Verify
  application ID, signing certificate, version code, source and binary identity.
- Installing or upgrading an APK or changing device-wide settings requires
  authorization in the current session. Never uninstall, clear production app
  data, or interrupt an active game to solve a benchmark issue.
- Keep benchmark saves, NAND, shader caches and settings isolated. Journal
  mutations, verify exact readback/restoration, and block new writes on pending
  recovery.
- Never commit firmware, keys, proprietary games, user saves, credentials,
  signing secrets, or sensitive device artifacts.

## Evidence and acceptance

A passing CI build is not proof of correct graphics, stability, or higher FPS.
Use focused policy tests, runtime configuration attestation, matching cold/warm
game workloads, repeated measurements, thermal context, and independently
attributed emulated-game, renderer, and display metrics. Missing or partial
evidence is explicitly unavailable, not a pass. Do not automatically promote,
merge to main, or publish a release without the required review.

## Upstream relationship

This policy applies to the fork only. Follow upstream Eden's independent
contribution rules when contacting upstream maintainers or contributing there.
