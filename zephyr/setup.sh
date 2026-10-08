#!/usr/bin/env bash
# Richtet einen schlanken Zephyr-Workspace fuer das CY8CKIT-062S2-AI ein.
# NICHT automatisch ausgefuehrt. Laedt mehrere GB. Stand 2026-10-08, ungetestet.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"

echo "== 1. Python-venv mit west =="
python3 -m venv .venv
.venv/bin/pip install --upgrade pip west

echo "== 2. Schlanker Workspace (nur Zephyr + Infineon-HAL + CMSIS + tflite-micro) =="
# Manifest-Projekt: ein eigenes west.yml, das Zephyr importiert und die Modulliste einschraenkt.
mkdir -p manifest
cat > manifest/west.yml <<'YML'
manifest:
  remotes:
    - name: zephyrproject
      url-base: https://github.com/zephyrproject-rtos
  projects:
    - name: zephyr
      remote: zephyrproject
      revision: main
      import:
        name-allowlist:
          - cmsis
          - cmsis_6
          - hal_infineon
          - tflite-micro
          - picolibc
YML
.venv/bin/west init -l manifest
.venv/bin/west update
.venv/bin/west zephyr-export
.venv/bin/pip install -r zephyr/scripts/requirements.txt

echo "== 3. Zephyr SDK (nur ARM-Toolchain) =="
SDK_VER="$(.venv/bin/python - <<'PY'
import re,sys
print(re.search(r'(\d+\.\d+\.\d+)', open('zephyr/SDK_VERSION').read()).group(1))
PY
)"
echo "SDK-Version laut zephyr/SDK_VERSION: $SDK_VER"
mkdir -p sdk && cd sdk
curl -L -o sdk.tar.xz "https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${SDK_VER}/zephyr-sdk-${SDK_VER}_linux-x86_64_minimal.tar.xz"
tar -xJf sdk.tar.xz && rm sdk.tar.xz
cd "zephyr-sdk-${SDK_VER}" && ./setup.sh -t arm-zephyr-eabi -h -c
cd "$HERE"

echo "== 4. Binaer-Blob fuer den CM0+ =="
.venv/bin/west blobs fetch hal_infineon

echo "== 5. OpenOCD aus dem Projekt eintragen =="
.venv/bin/west config build.cmake-args -- "-DOPENOCD=$HERE/../tools/openocd/bin/openocd"

echo "Fertig. Testbuild:"
echo "  source $HERE/.venv/bin/activate && cd $HERE && west build -b cy8ckit_062s2_ai/cy8c624abzi_s2d44 zephyr/samples/hello_world"
