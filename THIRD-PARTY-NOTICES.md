# Third-party notices

MyOS itself is under the MIT License (see `LICENSE`). This file covers code
from other projects.

## Code adapted into this repository

Two files contain parts adapted from TinyUSB's `net_lwip_webserver` example
(https://github.com/hathach/tinyusb, `examples/device/net_lwip_webserver`):

- `arm/net.c`: the glue between TinyUSB's network class and lwIP
  (`linkoutput_fn`, `tud_network_recv_cb`, `tud_network_xmit_cb`, the netif
  set-up and the DHCP/DNS server configuration), adapted from `src/main.c`:

  > The MIT License (MIT)
  >
  > Copyright (c) 2020 Peter Lawrence
  >
  > influenced by lrndis https://github.com/fetisov/lrndis

- `arm/usb.c`: the Microsoft OS 2.0 descriptor set (`desc_ms_os_20`, the
  BOS descriptor and `tud_vendor_control_xfer_cb`) that makes Windows load
  its NCM driver, adapted from `src/usb_descriptors.c`:

  > The MIT License (MIT)
  >
  > Copyright (c) 2019 Ha Thach (tinyusb.org)

Both are under this licence:

> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in
> all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
> THE SOFTWARE.

## Projects MyOS builds with (not in this repository)

`tools/get-deps.ps1` downloads these into `third_party/` at pinned versions.
They keep their own licences; the MyOS builds and packages made from them
(kernel, `python.pkg`, `lua.pkg`, `node.pkg`, `sqlite3.pkg`, `busybox.pkg`, `python3.pkg`, `musl.pkg`)
fall under those licences too.

| Project | Used for | Licence |
|---|---|---|
| [MicroPython](https://github.com/micropython/micropython) | `python` package; its `lib/libm_dbl` (from musl) is the math library of `csdk/libc` | MIT |
| [TinyUSB](https://github.com/hathach/tinyusb) | USB serial + network on the Pi; `lib/networking` DHCP/DNS servers | MIT |
| [broadcom-peripherals](https://github.com/adafruit/broadcom-peripherals) | Pi boot code, MMU, interrupts, mailbox | MIT |
| [lwIP](https://github.com/lwip-tcpip/lwip) | TCP/IP | BSD 3-clause |
| [Monocypher](https://github.com/LoupVaillant/Monocypher) | SSH cryptography | BSD 2-clause or CC0 |
| [Lua](https://www.lua.org) | `lua` package | MIT |
| [QuickJS](https://bellard.org/quickjs/) | `node` package | MIT |
| [SQLite](https://www.sqlite.org) | `sqlite3` package | public domain |
| [BusyBox](https://busybox.net) (static binary from Alpine Linux) | `busybox` package | GPL-2.0; source: busybox.net and Alpine's aports |
| [CPython](https://www.python.org) 3.12, built by [python-build-standalone](https://github.com/astral-sh/python-build-standalone) (aarch64, musl) | `python3` package (program + standard library, unmodified) | PSF License; the build includes other libraries under their own licences (OpenSSL Apache-2.0, SQLite public domain, zlib, libffi MIT, bzip2, xz, ...): the full texts ship in the package as `/usr/lib/python3.12/LICENSE.txt` and are listed in the python-build-standalone release |
| [musl](https://musl.libc.org) 1.2.5 (Alpine Linux's `musl` package) | `musl` package: `/lib/ld-musl-aarch64.so.1`, the dynamic loader + C library of musl programs | MIT; its `COPYRIGHT` file ships in the package as `/usr/share/doc/musl/COPYRIGHT` |
| Raspberry Pi firmware (`bootcode.bin`, `start.elf`, ...) | USB boot only | Broadcom redistributable licence (`LICENCE.broadcom`) |
| Wi-Fi chip firmware (RPi-Distro `firmware-nonfree`) | Wi-Fi on the Pi | Cypress/Broadcom redistributable licence |
