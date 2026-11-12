# MyOS packages

The package repository of [MyOS](https://github.com/arkam16666/myos): `pkg update` on the Pi downloads `index.txt` (signed: `index.sig`) and the `.pkg` files from here. Made by `python tools/repo.py publish`; this branch is replaced each time.

Licences: see THIRD-PARTY-NOTICES.md on the main branch. `busybox.pkg` (if present) is BusyBox 1.36.1 from Alpine Linux's busybox-static package, GPL-2.0; its source: https://busybox.net/downloads/busybox-1.36.1.tar.bz2 and https://gitlab.alpinelinux.org/alpine/aports/-/tree/3.20-stable/main/busybox
