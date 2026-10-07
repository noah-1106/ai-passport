#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir

    python3 tools/check_repo.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_demo_navigation.c main/demo_navigation.c \
        -o "${test_dir}/test_demo_navigation"
    "${test_dir}/test_demo_navigation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_display_rounding.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_display_rounding"
    "${test_dir}/test_bsp_display_rounding"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_es8311_sleep_check.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_es8311_sleep_check"
    "${test_dir}/test_bsp_es8311_sleep_check"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_button.c -o "${test_dir}/test_bsp_button"
    "${test_dir}/test_bsp_button"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_lvgl_init"
    "${test_dir}/test_bsp_lvgl_init"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/audio_stubs -Icomponents/bsp/include -Icomponents/bsp/src \
        tests/test_bsp_audio_recovery.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_audio_recovery"
    "${test_dir}/test_bsp_audio_recovery"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_jianlu_store.c main/jianlu_store.c \
        -o "${test_dir}/test_jianlu_store"
    "${test_dir}/test_jianlu_store"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_jianlu_voice.c main/jianlu_voice.c \
        -o "${test_dir}/test_jianlu_voice"
    "${test_dir}/test_jianlu_voice"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_jianlu_config.c main/jianlu_config.c \
        -o "${test_dir}/test_jianlu_config"
    "${test_dir}/test_jianlu_config"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_jianlu_netflow.c main/jianlu_netflow.c main/jianlu_store.c \
        -o "${test_dir}/test_jianlu_netflow"
    "${test_dir}/test_jianlu_netflow"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_jianlu_dns.c main/jianlu_dns.c \
        -o "${test_dir}/test_jianlu_dns"
    "${test_dir}/test_jianlu_dns"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_jianlu_level.c main/jianlu_level.c \
        -o "${test_dir}/test_jianlu_level"
    "${test_dir}/test_jianlu_level"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_jianlu_snapshot.c main/jianlu_snapshot.c main/jianlu_store.c \
        -o "${test_dir}/test_jianlu_snapshot"
    "${test_dir}/test_jianlu_snapshot"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_jianlu_powersave.c main/jianlu_powersave.c \
        -o "${test_dir}/test_jianlu_powersave"
    "${test_dir}/test_jianlu_powersave"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_jianlu_glyph.c main/jianlu_glyph.c \
        -o "${test_dir}/test_jianlu_glyph"
    "${test_dir}/test_jianlu_glyph"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_jianlu_layout.c main/jianlu_layout.c \
        -o "${test_dir}/test_jianlu_layout"
    "${test_dir}/test_jianlu_layout"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_jianlu_pager.c main/jianlu_pager.c \
        -o "${test_dir}/test_jianlu_pager"
    "${test_dir}/test_jianlu_pager"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_jianlu_voiceq.c main/jianlu_voiceq.c \
        -o "${test_dir}/test_jianlu_voiceq"
    "${test_dir}/test_jianlu_voiceq"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_jianlu_timefmt.c main/jianlu_timefmt.c \
        -o "${test_dir}/test_jianlu_timefmt"
    "${test_dir}/test_jianlu_timefmt"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_jianlu_modal.c main/jianlu_modal.c \
        -o "${test_dir}/test_jianlu_modal"
    "${test_dir}/test_jianlu_modal"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_jianlu_nav.c main/jianlu_nav.c \
        -o "${test_dir}/test_jianlu_nav"
    "${test_dir}/test_jianlu_nav"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_jianlu_theme.c main/jianlu_theme.c \
        -o "${test_dir}/test_jianlu_theme"
    "${test_dir}/test_jianlu_theme"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_jianlu_dlink_auto.c main/jianlu_dlink_auto.c \
        -o "${test_dir}/test_jianlu_dlink_auto"
    "${test_dir}/test_jianlu_dlink_auto"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_jianlu_home.c main/jianlu_home.c main/jianlu_store.c main/jianlu_timefmt.c \
        -o "${test_dir}/test_jianlu_home"
    "${test_dir}/test_jianlu_home"
    # JSON 解析测试直接编 ESP-IDF 源码树里的 cJSON;未激活 IDF 时跳过。
    if [[ -n "${IDF_PATH:-}" && -f "${IDF_PATH}/components/json/cJSON/cJSON.c" ]]; then
        "${CC:-cc}" -std=c11 -Wall -Wextra \
            -Imain -I"${IDF_PATH}/components/json/cJSON" \
            tests/test_jianlu_json.c main/jianlu_json.c main/jianlu_store.c \
            "${IDF_PATH}/components/json/cJSON/cJSON.c" \
            -o "${test_dir}/test_jianlu_json"
        "${test_dir}/test_jianlu_json"
    else
        echo "test_jianlu_json: SKIP (IDF cJSON source not found)"
    fi
    # Linux ld 用 --gc-sections;macOS ld 对应 -dead_strip。
    local gc_flag="--gc-sections"
    if [[ "$(uname -s)" == "Darwin" ]]; then
        gc_flag="-dead_strip"
    fi
    for demo in audio low_power ble wifi; do
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
            -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
            "tests/test_demo_${demo}_runtime.c" -Wl,${gc_flag} \
            -o "${test_dir}/test_demo_${demo}_runtime"
        "${test_dir}/test_demo_${demo}_runtime"
    done
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_deep_sleep_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_check_repo.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_verify_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_archive_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_install_passport_skills.py
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local validation_build_dir

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    PYTHONDONTWRITEBYTECODE=1 python3 tools/archive_firmware.py create \
        "${validation_build_dir}" --archive-root "${repo_root}/build/firmware"
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
