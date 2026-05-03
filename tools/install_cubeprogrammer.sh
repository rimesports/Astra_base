#!/usr/bin/env bash
# Install STM32CubeProgrammer from ST's official Linux ZIP.
#
# Usage:
#   tools/install_cubeprogrammer.sh ~/Downloads/en.stm32cubeprg-lin-v*.zip
#
# ST gates the official download behind its website/export form, so this script
# intentionally does not fetch third-party mirrors. Download the Linux ZIP from:
#   https://www.st.com/en/development-tools/stm32cubeprog.html
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 /path/to/en.stm32cubeprg-lin-v*.zip" >&2
  exit 2
fi

ZIP_PATH="$1"
if [ ! -f "$ZIP_PATH" ]; then
  echo "Installer ZIP not found: $ZIP_PATH" >&2
  exit 1
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

unzip -q "$ZIP_PATH" -d "$WORK_DIR"
SETUP="$(find "$WORK_DIR" -maxdepth 3 -type f -name 'SetupSTM32CubeProgrammer*.linux' | head -n 1)"

if [ -z "$SETUP" ]; then
  echo "Could not find SetupSTM32CubeProgrammer*.linux inside $ZIP_PATH" >&2
  exit 1
fi

chmod +x "$SETUP"
"$SETUP" -console <<'EOF'
1
1
1

1


Y
Y
1
Y
N
N
EOF

CLI="$HOME/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI"
if [ ! -x "$CLI" ]; then
  CLI="$(find "$HOME/STMicroelectronics" -type f -name STM32_Programmer_CLI 2>/dev/null | head -n 1)"
fi

if [ -z "${CLI:-}" ] || [ ! -x "$CLI" ]; then
  echo "Install finished, but STM32_Programmer_CLI was not found under ~/STMicroelectronics." >&2
  exit 1
fi

mkdir -p "$HOME/.local/bin"
ln -sf "$CLI" "$HOME/.local/bin/STM32_Programmer_CLI"

echo "Installed STM32CubeProgrammer CLI:"
"$HOME/.local/bin/STM32_Programmer_CLI" --version || true
echo
echo "If needed, add this to your shell PATH:"
echo '  export PATH="$HOME/.local/bin:$PATH"'
