#!/usr/bin/env bash
# One-command install of the Core language from the hosted apt repository:
#
#   curl -fsSL http://94.24.39.227/setup-apt.sh | sudo bash
#
# Adds the repo key + source, then installs the `core` package.
set -euo pipefail
REPO="http://94.24.39.227"

if [ "$(id -u)" -ne 0 ]; then
  echo "error: run as root (pipe into 'sudo bash')" >&2
  exit 1
fi
command -v curl >/dev/null 2>&1 || { apt-get update && apt-get install -y curl; }
command -v gpg >/dev/null 2>&1 || { apt-get update && apt-get install -y gnupg; }

curl -fsSL "$REPO/key.gpg" | gpg --dearmor -o /usr/share/keyrings/core.gpg
echo "deb [signed-by=/usr/share/keyrings/core.gpg] $REPO stable main" \
  > /etc/apt/sources.list.d/core.list
apt-get update
apt-get install -y core
echo "installed: $(core version)"
