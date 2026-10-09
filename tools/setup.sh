#!/usr/bin/env bash
# Laedt die Werkzeuge fuer das CY8CKIT-062S2-AI unter Linux, auf feste Versionen gepinnt und per SHA-256 geprueft:
#   tools/openocd/      Infineon-OpenOCD 5.20.0 (Flashen, Debuggen ueber KitProg3)
#   tools/fw-loader/    Infineon fw-loader 3.14.0 (KitProg3-Firmware lesen und aktualisieren)
#   tools/kp-firmware/  KitProg3-Firmware 2.90.1804 (liegt im fw-loader-Paket)
#   tools/arduino-cli/  arduino-cli 1.5.1
# Kein Infineon-Konto noetig, alles kommt von GitHub-Releases. Aufruf: tools/setup.sh
# Danach einmalig die udev-Regeln installieren (sudo, siehe SETUP-PROTOKOLL.md Abschnitt 13):
#   sudo cp tools/fw-loader/udev_rules/57-cypress_programmer.rules tools/58-psoc6-ai-kit-tty.rules /etc/udev/rules.d/
#   sudo udevadm control --reload-rules && sudo udevadm trigger
set -euo pipefail
cd "$(dirname "$0")"

fetch() {   # fetch <datei> <url> <sha256>
	[ -f "$1" ] || curl -fL --progress-bar -o "$1" "$2"
	echo "$3  $1" | sha256sum -c - >/dev/null || { echo "Pruefsumme falsch: $1"; rm -f "$1"; exit 1; }
}

if [ ! -x openocd/bin/openocd ]; then
	fetch openocd.tar.gz https://github.com/Infineon/openocd/releases/download/release-v5.20.0/openocd-5.20.0.5177-linux.tar.gz \
	      fbb381523284d3c9fb37afbfa43d17ebbe6d4ce2da88fe15b2aa084d41641255
	tar -xzf openocd.tar.gz && rm openocd.tar.gz
fi
if [ ! -x fw-loader/bin/fw-loader ]; then
	fetch fw-loader.zip https://github.com/Infineon/Firmware-loader/releases/download/3.14.0/fw-loader-3.14.0.2771-kitprog3-package-2.90.0.1804-linux.zip \
	      3159b2073d0bb7e7333c0715f72d9d936d9fe2cc80d183e4391810f580c73e36
	unzip -q -o fw-loader.zip 'fw-loader/*' 'kp-firmware/*' && rm fw-loader.zip
fi
if [ ! -x arduino-cli/arduino-cli ]; then
	fetch arduino-cli.tar.gz https://github.com/arduino/arduino-cli/releases/download/v1.5.1/arduino-cli_1.5.1_Linux_64bit.tar.gz \
	      28a8e119c498a25607821c36cb2dc49e8463941b261a0d99091baa7bc692dd2b
	mkdir -p arduino-cli && tar -xzf arduino-cli.tar.gz -C arduino-cli && rm arduino-cli.tar.gz
fi
echo "openocd:     $(openocd/bin/openocd --version 2>&1 | head -1)"
echo "fw-loader:   $(fw-loader/bin/fw-loader --version 2>&1 | head -1 || true)"
echo "KitProg3:    $(cat kp-firmware/kitprog3.version)"
echo "arduino-cli: $(arduino-cli/arduino-cli version | head -1)"
