# libvterm 0.3.3 (vendored)

Paul Evans's terminal-emulator library, unmodified (MIT, see `LICENSE`), copied from the 0.3.3 release tarball.
nebula links the system libvterm when pkg-config finds one; this copy is built where there is none (Windows),
or when configuring with `-DNEBULA_BUNDLED_VTERM=ON`.
