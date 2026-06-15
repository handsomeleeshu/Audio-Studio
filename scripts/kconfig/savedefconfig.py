#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause

import sys
import kconfiglib

def main():
    if len(sys.argv) < 3:
        sys.exit("usage: savedefconfig.py KCONFIG_FILENAME DEFCONFIG_OUTPUT_FILE")
    kconf = kconfiglib.Kconfig(sys.argv[1])
    kconf.load_config()
    kconf.write_min_config(sys.argv[2])

if __name__ == "__main__":
    main()
