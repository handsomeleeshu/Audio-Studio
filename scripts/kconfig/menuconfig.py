#!/usr/bin/env python3
# SPDX-License-Identifier: ISC

import kconfiglib

def main():
    kconf = kconfiglib.standard_kconfig()
    kconfiglib.menuconfig(kconf)

if __name__ == "__main__":
    main()
