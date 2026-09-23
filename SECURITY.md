# Security Policy

## Threat model

CompressCompare decodes image and video files, including files fetched from
arbitrary URLs supplied by the user. Untrusted input is therefore the normal
case, not the exception. The components that process it are:

- image and video decoding (OpenCV, `ffmpeg`)
- JPEG marker and quantisation-table parsing (`src/core/jpeg_info.cpp`)
- URL fetching (`src/core/net.cpp`)
- external process invocation (`src/core/process.cpp`)
- OpenCL kernels operating on attacker-controlled dimensions (`src/core/cl_kernels.h`)

Bugs in any of these reachable from a crafted input file or URL are in scope.

## Reporting a vulnerability

Please report security issues privately via GitHub's
[private vulnerability reporting](https://docs.github.com/en/code-security/security-advisories/guidance-on-reporting-and-writing-information-about-vulnerabilities/privately-reporting-a-security-vulnerability)
on this repository, rather than opening a public issue.

Please include a reproducer file or URL where possible, the build and macOS
version, and whether the GPU (OpenCL) or CPU path was active.

## Dependency posture

OpenCV is built and linked **statically** at a pinned commit, so operating
system security updates do not reach it. Dependency versions are advanced
deliberately in `scripts/build-deps.sh` and `cmake/Dependencies.cmake`.

Current pins:

| Dependency | Version | Linkage |
| --- | --- | --- |
| OpenCV | 4.12.0 | static |
| yaml-cpp | 0.8.0 | static |
| ffmpeg / ffprobe | system | external process |

`ffmpeg` and `ffprobe` are invoked as external processes and are supplied by
the user's system, so they receive OS updates independently.

## Hardening notes for contributors

- Never pass user-controlled paths or URLs through a shell. Use `posix_spawn`
  or `fork`/`execvp` with an argv array; never `system()` or `popen()`.
- Prefix user-supplied paths with `./` or separate them with `--` so a leading
  hyphen cannot be parsed as an `ffmpeg` option.
- Input validation must be real branching code, not a `contract.h` assertion —
  assertions may compile out under `NDEBUG` in release builds.
- Bounds-check every segment length read when parsing container or marker data.
- Guard OpenCL kernel indices against actual buffer dimensions, not against
  the logical image size.
