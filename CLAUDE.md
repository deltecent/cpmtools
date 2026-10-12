# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

cpmtools (v2.23) is a set of C command-line tools for accessing CP/M file system images/devices, analogous to mtools. It builds with autoconf + a plain Makefile; there is no git history or test framework in this checkout.

## Build

```
./configure            # generates Makefile, config.h and the *.1/*.5 man pages from *.in
make                   # builds cpmls cpmrm cpmcp cpmchmod cpmchattr mkfs.cpm fsck.cpm (+ fsed.cpm if curses found)
make fsck.test         # the only "test suite": runs fsck.cpm -n against each image in badfs/
make depend            # regenerate .depend (header deps; uses gcc -MM)
make install | clean | distclean
```

Useful `configure` options: `--with-diskdefs=PATH` (default `${datarootdir}/diskdefs`), `--with-defformat=NAME` (default `ibm-3740`), `--with-libdsk=PATH` (switches the device backend from `posix` to `libdsk`).

- `Makefile`, `config.h`, and the man pages `cpm.5`, `cpm*.1`, `diskdefs.5`, etc. are generated from `*.in` files — edit the `.in` versions (and `configure.in`; the committed `configure` is generated from it, version/date live in `configure.in`).
- GCC builds use a strict warning set (`-Wall -Wextra -Wshadow -Wmissing-prototypes -pedantic ...`, see `configure.in`); keep new code warning-free.
- There is no single-test runner. To exercise one case, run e.g. `./fsck.cpm -f ibm-3740 -n badfs/extno`. Note that `badfs/` images are used with `-n` (no repair) — don't run fsck on them without `-n` or the fixtures will be modified. Tools find `diskdefs` in the current directory first, then at the compiled-in `DISKDEFS` path, so running from the repo root uses the local file.

## Architecture

All tools share one library-like core, `cpmfs.c`/`cpmfs.h`, which implements a simulated Unix-like VFS (inodes, `cpmNamei`, `cpmOpen/Read/Write/Close`, `cpmCreat`, `cpmUnlink`, `cpmReaddir`, `cpmAttrGet/Set`, ...) over a CP/M directory. Each tool (`cpmls.c`, `cpmcp.c`, `cpmrm.c`, `cpmchmod.c`, `cpmchattr.c`, `mkfs.cpm.c`, `fsck.cpm.c`, `fsed.cpm.c`) is a thin `main()` that parses options with the bundled GNU `getopt*.c`, calls `cpmReadSuper`, then uses the `cpm*` API. Each tool defines `cmd` (its name, used in error messages) and reads the format from `-f` or the `CPMTOOLSFMT` env var, falling back to the compile-time `FORMAT`.

Key points that span files:

- **Disk geometry** comes from `diskdefs` (parsed by `diskdefReadSuper` in `cpmfs.c`; format documented in `diskdefs.5.in`). Each `diskdef` block sets seclen/tracks/sectrk/blocksize/maxdir/skew/boottrk/os etc.; the `os` value selects `CPMFS_*` feature flags in `cpmfs.h` (HI_USER, CPM3_DATES, DS_DATES, EXACT_SIZE...). `libdsk:format` entries are only used with the libdsk backend. A separate path (`amsReadSuper`) handles Amstrad PCW boot-sector-described discs.
- **Device layer**: `device.h` defines `struct Device` with sector-level `Device_open/readSector/writeSector/close`; the implementation is chosen at configure time via `DEVICE` (`device_posix.c` or `device_libdsk.c`). `device_win32.c` is referenced in `.depend` but was removed (Windows/Cygwin support dropped in 2.23). Sector addressing applies the diskdef skew table and boot-track offset.
- **Inodes are synthetic**: inode numbers map to the lowest-numbered extent of a file; extra inodes after `maxdir` are the root directory, the pseudo-file `[passwd]` and `[label]` (see the comments at the top of `cpmfs.c`). `struct PhysDirectoryEntry` in `cpmdir.h` is the on-disk 32-byte directory entry; `ISFILECHAR`/`EXTENT` macros live there.
- **Hard sector images** (`hardsector altair` in a diskdef, MITS 88-DCDD): `device_posix.c` maps the CP/M 128-byte sector to a 137-byte slot, applying the Altair skew (`(sector*17)%32` from track 6, tracks mod 128) and generating/verifying the header, stop byte and checksum; the system-track (0-5) and data-track layouts differ. `skewtab` in the diskdef is the CP/M BIOS translate table, not the physical order. `mkfs.cpm` writes a raw image first and then reformats it through the device. Unsupported with libdsk.
- **Allocation** is tracked in an in-memory allocation vector (`alv`); directory and datestamp changes are marked dirty and flushed in `cpmSync`/`cpmUmount`, so callers must call `cpmUmount` to persist writes.
- **User numbers** are specified as `user:file`; CP/M `/` in names is mapped to `,` on the host. Passwords are ignored (exposed read-only via `[passwd]`).
- `cpmfs.c` uses `/*{{{*/ ... /*}}}*/` fold markers around sections — preserve them when editing.
