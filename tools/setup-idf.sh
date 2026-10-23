#!/usr/bin/env bash
# Installs ESP-IDF v4.4.8 for this project without root and writes an env file.
#
#   tools/setup-idf.sh            # then:  . ~/esp/idf-env.sh
#
# Overridable: IDF_VERSION, IDF_DIR (default ~/esp/esp-idf), ENV_FILE (default ~/esp/idf-env.sh)
#
# ESP-IDF 4.4 does not support Python 3.12 or newer. If the system python3 is too
# new, a private Python 3.10 is fetched with uv (installed into a venv, nothing
# system-wide is touched). Safe to run again, finished steps are skipped.
set -euo pipefail

IDF_VERSION=${IDF_VERSION:-v4.4.8}
IDF_DIR=${IDF_DIR:-$HOME/esp/esp-idf}
ENV_FILE=${ENV_FILE:-$HOME/esp/idf-env.sh}

for tool in git python3 curl; do
  command -v "$tool" >/dev/null || { echo "missing: $tool" >&2; exit 1; }
done

py_bin_dir=""
if ! python3 -c 'import sys; sys.exit(0 if sys.version_info < (3, 12) else 1)'; then
  echo "== system python is too new for ESP-IDF 4.4, getting Python 3.10"
  uv=$(command -v uv || true)
  if [ -z "$uv" ]; then
    python3 -m venv "$HOME/.local/uv-venv"
    "$HOME/.local/uv-venv/bin/pip" install --quiet uv
    uv="$HOME/.local/uv-venv/bin/uv"
  fi
  "$uv" python install 3.10
  py_bin_dir=$(dirname "$("$uv" python find 3.10)")
fi

mkdir -p "$(dirname "$IDF_DIR")" "$(dirname "$ENV_FILE")"
if [ ! -d "$IDF_DIR/.git" ]; then
  echo "== cloning ESP-IDF $IDF_VERSION (about 350 MB)"
  git clone -b "$IDF_VERSION" --depth 1 --recursive --shallow-submodules --jobs 8 \
      https://github.com/espressif/esp-idf.git "$IDF_DIR"
fi

cat > "$ENV_FILE" <<ENV
# Source this file:  . $ENV_FILE
# Activates ESP-IDF $IDF_VERSION$( [ -n "$py_bin_dir" ] && echo " with the private Python 3.10 it was installed with" ).
${py_bin_dir:+export PATH="$py_bin_dir:\$PATH"}
${py_bin_dir:+export PIP_BREAK_SYSTEM_PACKAGES=1}
. "$IDF_DIR/export.sh"
ENV

echo "== installing the ESP32 and ESP32-S3 toolchains and the Python environment"
(
  [ -n "$py_bin_dir" ] && export PATH="$py_bin_dir:$PATH" PIP_BREAK_SYSTEM_PACKAGES=1
  "$IDF_DIR/install.sh" esp32,esp32s3
  # CMake and Ninja come from the OS package manager on Linux, fetch them instead (no root)
  . "$IDF_DIR/export.sh" >/dev/null
  python "$IDF_DIR/tools/idf_tools.py" install cmake ninja
)

echo
echo "Done. Activate it in every new shell with:  . $ENV_FILE"
