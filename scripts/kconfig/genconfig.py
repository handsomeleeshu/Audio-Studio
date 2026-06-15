#!/usr/bin/env python3
# SPDX-License-Identifier: ISC

import argparse
import kconfiglib

parser = argparse.ArgumentParser()
parser.add_argument("--header-path", default="config.h")
parser.add_argument("--config-out")
parser.add_argument("kconfig_filename", nargs="?", default="Kconfig")
args = parser.parse_args()

kconf = kconfiglib.Kconfig(args.kconfig_filename)
kconf.load_config(verbose=False)
kconf.write_autoconf(args.header_path)

if args.config_out:
    kconf.write_config(args.config_out, save_old=False)
