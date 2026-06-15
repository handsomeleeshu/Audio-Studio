# Audio Studio platforms

Each platform directory owns its default Kconfig input and toolchain selection. `scripts/build_all.py` selects a platform only; the platform profile then decides the host environment and toolchain file.
