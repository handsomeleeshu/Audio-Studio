#!/usr/bin/env python3
# SPDX-License-Identifier: ISC

import kconfiglib

def main():
    kconf = kconfiglib.standard_kconfig()
    kconf.write_config()

if __name__ == "__main__":
    main()
