#!/usr/bin/env bash
# Pre-publication check. Run from the repository root before the first push,
# and before any push that adds files.
#
#   scripts/preflight.sh
#
# Exits non-zero if anything that should not be public is staged or tracked.

set -euo pipefail

fail=0
note() { printf '  %s\n' "$*"; }
problem() { printf '\n[FAIL] %s\n' "$*"; fail=1; }
ok() { printf '[ ok ] %s\n' "$*"; }

if ! git rev-parse --git-dir >/dev/null 2>&1; then
  echo "Not a git repository. Run 'git init' first."
  exit 1
fi

# Files git would actually publish. Empty on a fresh repo with nothing added.
tracked() { git ls-files -z; }

# ---- 1. Large files --------------------------------------------------------
# GitHub warns above 50MB and refuses above 100MB. Anything above 5MB in a
# source repo deserves a conscious decision.
big=$(tracked | xargs -0 -I{} sh -c '[ -f "{}" ] && [ "$(wc -c <"{}")" -gt 5242880 ] && echo "{}"' 2>/dev/null || true)
if [ -n "$big" ]; then
  problem "Tracked files larger than 5MB:"
  echo "$big" | while read -r f; do note "$f ($(du -h "$f" | cut -f1))"; done
  note "Binaries in git history are permanent. Use Releases or Git LFS."
else
  ok "No tracked files over 5MB"
fi

# ---- 2. Build output and IDE state ----------------------------------------
junk=$(tracked | tr '\0' '\n' | grep -E '(^|/)(build|cmake-build-[^/]*|dist|deps|\.idea|\.vscode)/|(^|/)\.DS_Store$' || true)
if [ -n "$junk" ]; then
  problem "Build output, IDE state or OS metadata is tracked:"
  echo "$junk" | head -20 | while read -r f; do note "$f"; done
  note "These leak local absolute paths. Add to .gitignore and 'git rm --cached'."
else
  ok "No build output or IDE state tracked"
fi

# ---- 3. Local absolute paths ----------------------------------------------
# CMake caches and IDE files embed /Users/<name>/, publishing your username.
leaks=$(tracked | xargs -0 grep -lI -E '/(Users|home)/[a-zA-Z0-9._-]+/' 2>/dev/null || true)
if [ -n "$leaks" ]; then
  problem "Tracked files contain local absolute paths:"
  echo "$leaks" | head -20 | while read -r f; do note "$f"; done
else
  ok "No local absolute paths in tracked files"
fi

# ---- 4. Signing material and credentials -----------------------------------
creds=$(tracked | tr '\0' '\n' | grep -E '\.(p12|pem|key|cer|mobileprovision|certSigningRequest)$|(^|/)\.env$' || true)
if [ -n "$creds" ]; then
  problem "Possible credential or signing material tracked:"
  echo "$creds" | while read -r f; do note "$f"; done
else
  ok "No credential or signing files tracked"
fi

# ---- 5. Secret scan --------------------------------------------------------
if command -v gitleaks >/dev/null 2>&1; then
  if gitleaks detect --no-banner --redact -v >/dev/null 2>&1; then
    ok "gitleaks found no secrets"
  else
    problem "gitleaks reported findings. Run 'gitleaks detect -v' for detail."
  fi
else
  note "gitleaks not installed; skipping secret scan (brew install gitleaks)"
fi

echo
if [ "$fail" -ne 0 ]; then
  echo "Preflight FAILED. Resolve the above before pushing."
  exit 1
fi
echo "Preflight passed."
