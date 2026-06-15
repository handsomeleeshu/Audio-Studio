#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Audio Studio platform build entry.

This script intentionally exposes platform selection only. The selected
platform owns its default Kconfig input and toolchain file.
"""

from __future__ import annotations

import argparse
import configparser
from pathlib import Path
import shutil
import subprocess
import sys


REPO_ROOT = Path(__file__).resolve().parents[1]


def load_platform(platform: str) -> dict[str, str]:
    cfg_path = REPO_ROOT / "platform" / platform / "config.ini"
    if not cfg_path.exists():
        raise SystemExit(f"unknown platform '{platform}': missing {cfg_path}")

    parser = configparser.ConfigParser()
    text = "[platform]\n" + cfg_path.read_text(encoding="utf-8")
    parser.read_string(text)
    section = parser["platform"]
    return {
        "name": section.get("name", platform),
        "default_config": section["default_config"],
        "toolchain": section.get("toolchain", ""),
    }


def run(cmd: list[str], cwd: Path | None = None) -> None:
    print("+", " ".join(cmd))
    subprocess.run(cmd, cwd=str(cwd or REPO_ROOT), check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", default="simulator_linux", help="platform profile under platform/")
    parser.add_argument("--build-type", default="Debug", choices=("Debug", "Release", "RelWithDebInfo", "MinSizeRel"))
    parser.add_argument("--build-dir", help="override build directory")
    parser.add_argument("--clean", action="store_true", help="remove build directory before configuring")
    parser.add_argument("--cmake-only", action="store_true", help="configure only")
    parser.add_argument("--target", action="append", default=[], help="optional CMake build target")
    parser.add_argument("--jobs", "-j", default="", help="parallel build jobs")
    args = parser.parse_args()

    platform = load_platform(args.platform)
    build_dir = Path(args.build_dir) if args.build_dir else REPO_ROOT / "build" / args.platform / args.build_type.lower()
    if args.clean and build_dir.exists():
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True, exist_ok=True)

    cmake_cmd = [
        "cmake",
        "-S", str(REPO_ROOT),
        "-B", str(build_dir),
        "-G", "Ninja",
        f"-DCMAKE_BUILD_TYPE={args.build_type}",
        f"-DPLATFORM={platform['name']}",
    ]
    toolchain = platform.get("toolchain", "")
    if toolchain:
        cmake_cmd.append(f"-DCMAKE_TOOLCHAIN_FILE={REPO_ROOT / toolchain}")
    run(cmake_cmd)

    if args.cmake_only:
        return 0

    build_cmd = ["cmake", "--build", str(build_dir)]
    if args.target:
        for target in args.target:
            build_cmd.extend(["--target", target])
    if args.jobs:
        build_cmd.extend(["--parallel", args.jobs])
    run(build_cmd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
