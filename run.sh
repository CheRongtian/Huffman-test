#!/usr/bin/env bash

set -euo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
QT_PREFIX=""

if ! command -v cmake >/dev/null 2>&1; then
    printf '%s\n' \
        "CMake was not found." \
        "Install CMake, then run ./run.sh again."
    exit 1
fi

if command -v brew >/dev/null 2>&1; then
    if QT_PREFIX="$(brew --prefix qt 2>/dev/null)"; then
        :
    else
        QT_PREFIX=""
    fi
fi

if [[ -z "${QT_PREFIX}" ]] && command -v qmake6 >/dev/null 2>&1; then
    QT_PREFIX="$(qmake6 -query QT_INSTALL_PREFIX 2>/dev/null || true)"
fi

if [[ -z "${QT_PREFIX}" ]] && command -v qtpaths6 >/dev/null 2>&1; then
    QT_PREFIX="$(qtpaths6 --query QT_INSTALL_PREFIX 2>/dev/null || true)"
fi

CMAKE_ARGS=(
    -S "${PROJECT_DIR}"
    -B "${BUILD_DIR}"
    -DMGZ_BUILD_GUI=ON
)

if [[ -n "${QT_PREFIX}" ]]; then
    CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${QT_PREFIX}")
fi

printf 'Configuring MGZ Compressor...\n'
cmake "${CMAKE_ARGS[@]}"

if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]] &&
   grep -q '^Qt6_DIR:PATH=Qt6_DIR-NOTFOUND$' "${BUILD_DIR}/CMakeCache.txt"; then
    printf '%s\n' \
        "Qt 6 Widgets was not found, so the desktop application cannot be built." \
        "On macOS with Homebrew, install it with: brew install qt" \
        "Then run ./run.sh again."
    exit 1
fi

printf 'Building MGZ Compressor...\n'
cmake --build "${BUILD_DIR}" --target mgz_gui --parallel

case "$(uname -s)" in
    Darwin)
        APP_PATH=""
        for CANDIDATE in \
            "${BUILD_DIR}/MGZ Compressor.app" \
            "${BUILD_DIR}/Debug/MGZ Compressor.app" \
            "${BUILD_DIR}/Release/MGZ Compressor.app"; do
            if [[ -d "${CANDIDATE}" ]]; then
                APP_PATH="${CANDIDATE}"
                break
            fi
        done

        if [[ -z "${APP_PATH}" ]]; then
            printf '%s\n' "Build completed, but the application bundle was not found in ${BUILD_DIR}."
            exit 1
        fi

        printf 'Launching MGZ Compressor...\n'
        open "${APP_PATH}"
        ;;
    Linux)
        APP_PATH=""
        for CANDIDATE in \
            "${BUILD_DIR}/MGZ Compressor" \
            "${BUILD_DIR}/Debug/MGZ Compressor" \
            "${BUILD_DIR}/Release/MGZ Compressor"; do
            if [[ -x "${CANDIDATE}" ]]; then
                APP_PATH="${CANDIDATE}"
                break
            fi
        done

        if [[ -z "${APP_PATH}" ]]; then
            printf '%s\n' "Build completed, but the application executable was not found in ${BUILD_DIR}."
            exit 1
        fi

        printf 'Launching MGZ Compressor...\n'
        "${APP_PATH}" &
        ;;
    *)
        printf '%s\n' \
            "The application was built successfully." \
            "Automatic launching is supported on macOS and Linux."
        ;;
esac
