#!/usr/bin/env bash
set -euo pipefail

DEFAULT_OS=linux
DEFAULT_PROFILE=as_server_minimal
DEFAULT_PLATFORMS=(a2)
SUPPORTED_OSES=(linux macos windows)
SUPPORTED_PROFILES=(as_server_minimal as_config driver_interface_tests driver_interface_tests_macos gui_backend rpc_socket rpc_pipe)
SUPPORTED_PLATFORMS=(a2 simulator)

BUILD_TYPE=Debug
BUILD_JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
BUILD_VERBOSE=
MAKE_MENUCONFIG=no
CONFIGURE_ONLY=no
DRY_RUN=no
SKIP_MISSING_TOOLCHAIN=no
BUILD_ALL_PLATFORMS=no
BUILD_OS=
PROFILE="$DEFAULT_PROFILE"
PROFILE_EXECUTABLES=()
PLATFORMS=()

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

die() {
  printf '%s ERROR: ' "$0" >&2
  printf "$@" >&2
  exit 1
}

usage() {
  cat <<EOF
usage: $0 [options] <os> [platform(s)]

Options:
  -a                  Build all supported platforms (${SUPPORTED_PLATFORMS[*]})
  -d                  Use Debug build type (default)
  -r                  Use Release build type
  -c                  Run menuconfig after configure
  -v                  Verbose native build
  -j N                Build jobs
  --configure-only    Configure but do not build
  --dry-run           Print platform mapping without configuring
  --skip-missing-toolchain
                      Skip a platform when its compiler is unavailable
  --profile NAME      Build profile (${SUPPORTED_PROFILES[*]})

OS selects the PC system that Audio Studio Tool runs on and the toolchain.
Platform selects the target chip/profile Audio Studio talks to and feeds Kconfig.

Default OS: ${DEFAULT_OS}
Default profile: ${DEFAULT_PROFILE}
Default platform(s): ${DEFAULT_PLATFORMS[*]}
Supported OSes: ${SUPPORTED_OSES[*]}
Supported profiles: ${SUPPORTED_PROFILES[*]}
Supported platforms: ${SUPPORTED_PLATFORMS[*]}

Examples:
  $0 linux a2
  $0 windows a2
  $0 --dry-run windows a2
EOF
}

has_os() {
  local candidate="$1"
  local os
  for os in "${SUPPORTED_OSES[@]}"; do
    [[ "$os" == "$candidate" ]] && return 0
  done
  return 1
}

has_platform() {
  local candidate="$1"
  local platform
  for platform in "${SUPPORTED_PLATFORMS[@]}"; do
    [[ "$platform" == "$candidate" ]] && return 0
  done
  return 1
}

has_profile() {
  local candidate="$1"
  local profile
  for profile in "${SUPPORTED_PROFILES[@]}"; do
    [[ "$profile" == "$candidate" ]] && return 0
  done
  return 1
}

os_config() {
  local os="$1"
  case "$os" in
    linux)
      TOOLCHAIN_FILE="${ROOT}/scripts/cmake/toolchain/linux-gcc.cmake"
      REQUIRED_TOOLS=(c++)
      EXECUTABLE_SUFFIX=
      ;;
    macos)
      TOOLCHAIN_FILE="${ROOT}/scripts/cmake/toolchain/macos-clang.cmake"
      REQUIRED_TOOLS=(clang++)
      EXECUTABLE_SUFFIX=
      ;;
    windows)
      TOOLCHAIN_FILE="${ROOT}/scripts/cmake/toolchain/windows-mingw.cmake"
      REQUIRED_TOOLS=(x86_64-w64-mingw32-g++-posix)
      EXECUTABLE_SUFFIX=.exe
      ;;
    *)
      die 'unknown OS: %s\n' "$os"
      ;;
  esac
}

profile_config() {
  case "$PROFILE" in
    as_server_minimal)
      PROFILE_EXECUTABLES=("as_server${EXECUTABLE_SUFFIX}")
      ;;
    as_config)
      if [[ "$BUILD_OS" == windows ]]; then
        die 'profile as_config currently supports linux/macos host OS; Windows host config drivers are not implemented yet\n'
      fi
      PROFILE_EXECUTABLES=(
        "as_server${EXECUTABLE_SUFFIX}"
        "as_config${EXECUTABLE_SUFFIX}"
      )
      ;;
    driver_interface_tests)
      PROFILE_EXECUTABLES=(
        "as_server${EXECUTABLE_SUFFIX}"
        "audio_studio_server_tests${EXECUTABLE_SUFFIX}"
        "audio_studio_driver_interface_tests${EXECUTABLE_SUFFIX}"
      )
      ;;
    driver_interface_tests_macos)
      PROFILE_EXECUTABLES=(
        "as_server${EXECUTABLE_SUFFIX}"
        "audio_studio_driver_interface_tests${EXECUTABLE_SUFFIX}"
      )
      ;;
    gui_backend)
      if [[ "$BUILD_OS" != linux ]]; then
        die 'profile gui_backend currently supports linux OS only; GUI/backend uses POSIX sockets today\n'
      fi
      PROFILE_EXECUTABLES=(audio_studio_server audio_studio_backend_tests)
      ;;
    rpc_socket|rpc_pipe)
      PROFILE_EXECUTABLES=(
        "as_server${EXECUTABLE_SUFFIX}"
        "as_control${EXECUTABLE_SUFFIX}"
        "as_play${EXECUTABLE_SUFFIX}"
        "as_record${EXECUTABLE_SUFFIX}"
        "as_log${EXECUTABLE_SUFFIX}"
        "as_dump${EXECUTABLE_SUFFIX}"
      )
      ;;
    *)
      die 'unknown profile: %s\n' "$PROFILE"
      ;;
  esac
}

platform_config() {
  local platform="$1"
  case "$platform" in
    a2)
      PLATFORM_CONFIG="${ROOT}/configs/platform/a2_defconfig"
      ;;
    simulator)
      PLATFORM_CONFIG="${ROOT}/configs/platform/simulator_defconfig"
      ;;
    *)
      die 'unknown platform: %s\n' "$platform"
      ;;
  esac
}

check_tools() {
  local tool
  for tool in "${REQUIRED_TOOLS[@]}"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
      if [[ "$SKIP_MISSING_TOOLCHAIN" == yes ]]; then
        printf '[skip] %s: missing tool %s\n' "$PLATFORM" "$tool"
        return 1
      fi
      die '%s platform requires tool %s\n' "$PLATFORM" "$tool"
    fi
  done
  return 0
}

create_initial_config() {
  local output="$1"
  local profile_config="${ROOT}/configs/profile/${PROFILE}_defconfig"

  {
    printf '# Generated by scripts/build_all.sh\n'
    printf '# Profile: %s\n' "$PROFILE"
    if [[ "$BUILD_OS" == windows ]]; then
      grep -v -E '^(CONFIG_FRAMEWORK_CONFIG|CONFIG_DRIVER_OS|CONFIG_DRIVER_OS_LINUX_HOST|CONFIG_DRIVER_FILESYSTEM|CONFIG_DRIVER_FILESYSTEM_LINUX_HOST|CONFIG_DRIVER_DYNLIB|CONFIG_DRIVER_DYNLIB_LINUX_HOST|CONFIG_DRIVER_SOCKET_LINUX_HOST|CONFIG_DRIVER_AUDIO_ALSA|CONFIG_DRIVER_AUDIO_PULSE)=y$' "$profile_config"
    else
      cat "$profile_config"
    fi
    printf '\n# Target platform\n'
    cat "$PLATFORM_CONFIG"
    printf '\n# Host OS implementation overrides\n'
    if [[ "$BUILD_OS" == windows ]]; then
      if grep -q '^CONFIG_DRIVER_SOCKET=y' "$profile_config"; then
        printf 'CONFIG_DRIVER_SOCKET_WINDOWS_HOST=y\n'
      fi
      if grep -q '^CONFIG_DRIVER_AUDIO=y' "$profile_config"; then
        printf 'CONFIG_DRIVER_AUDIO_WASAPI=y\n'
      fi
    fi
  } > "$output"
}

while (($#)); do
  case "$1" in
    -a)
      BUILD_ALL_PLATFORMS=yes
      shift
      ;;
    -d)
      BUILD_TYPE=Debug
      shift
      ;;
    -r)
      BUILD_TYPE=Release
      shift
      ;;
    -c)
      MAKE_MENUCONFIG=yes
      shift
      ;;
    -v)
      BUILD_VERBOSE='--verbose'
      shift
      ;;
    -j)
      shift
      [[ $# -gt 0 ]] || die '-j requires a job count\n'
      BUILD_JOBS="$1"
      shift
      ;;
    --configure-only)
      CONFIGURE_ONLY=yes
      shift
      ;;
    --dry-run)
      DRY_RUN=yes
      shift
      ;;
    --skip-missing-toolchain)
      SKIP_MISSING_TOOLCHAIN=yes
      shift
      ;;
    --profile)
      shift
      [[ $# -gt 0 ]] || die '--profile requires a profile name\n'
      has_profile "$1" || die 'unknown profile: %s. Supported profiles: %s\n' "$1" "${SUPPORTED_PROFILES[*]}"
      PROFILE="$1"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    -*)
      die 'unknown option: %s\n' "$1"
      ;;
    *)
      if [[ -z "$BUILD_OS" ]] && has_os "$1"; then
        BUILD_OS="$1"
      elif has_platform "$1"; then
        PLATFORMS+=("$1")
      elif [[ -z "$BUILD_OS" ]]; then
        die 'unknown OS: %s. Supported OSes: %s\n' "$1" "${SUPPORTED_OSES[*]}"
      else
        die 'unknown platform: %s. Supported platforms: %s\n' "$1" "${SUPPORTED_PLATFORMS[*]}"
      fi
      shift
      ;;
  esac
done

if [[ -z "$BUILD_OS" ]]; then
  BUILD_OS="$DEFAULT_OS"
fi

if [[ "$BUILD_ALL_PLATFORMS" == yes ]]; then
  PLATFORMS=("${SUPPORTED_PLATFORMS[@]}")
fi

if [[ ${#PLATFORMS[@]} -eq 0 ]]; then
  PLATFORMS=("${DEFAULT_PLATFORMS[@]}")
fi

os_config "$BUILD_OS"
profile_config

for PLATFORM in "${PLATFORMS[@]}"; do
  platform_config "$PLATFORM"
  BUILD_DIR="${ROOT}/out/${BUILD_OS}/${PLATFORM}/${PROFILE}/${BUILD_TYPE}"
  INIT_CONFIG="${BUILD_DIR}/initial.config"

  printf '\n   ------\n   %s / %s\n   ------\n' "$BUILD_OS" "$PLATFORM"
  printf 'OS=%s\n' "$BUILD_OS"
  printf 'PROFILE=%s\n' "$PROFILE"
  printf 'PLATFORM=%s\n' "$PLATFORM"
  printf 'PLATFORM_CONFIG=%s\n' "$PLATFORM_CONFIG"
  printf 'INIT_CONFIG=%s\n' "$INIT_CONFIG"
  printf 'TOOLCHAIN_FILE=%s\n' "$TOOLCHAIN_FILE"
  printf 'BUILD_DIR=%s\n' "$BUILD_DIR"
  for executable in "${PROFILE_EXECUTABLES[@]}"; do
    printf 'EXECUTABLE=%s\n' "${BUILD_DIR}/${executable}"
  done

  if [[ "$DRY_RUN" == yes ]]; then
    continue
  fi

  check_tools || continue

  rm -rf "$BUILD_DIR"
  mkdir -p "$BUILD_DIR"
  create_initial_config "$INIT_CONFIG"

  (
    set -x
    cmake -S "$ROOT" \
      -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
      -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
      -DINIT_CONFIG="$INIT_CONFIG"
  )

  if [[ "$MAKE_MENUCONFIG" == yes ]]; then
    cmake --build "$BUILD_DIR" -- menuconfig
  fi

  if [[ "$CONFIGURE_ONLY" == no ]]; then
    cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS" -- ${BUILD_VERBOSE}
  fi
done
