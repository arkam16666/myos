# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

MyOS is a hobby/educational operating system written from scratch, with two editions:
- **x86-64 (MyOS PC)**: assembly kernel (`boot.asm`, `kernel.asm`), runs in QEMU only.
- **ARM64 (MyOS Pi)**: C kernel in `arm/`, runs on the owner's Raspberry Pi Zero 2 W (and QEMU raspi3b/raspi4b). Current work is focused here; x86 is on hold (see `PLAN.md`).

The owner communicates in Thai. `README.md` (Thai) is the user guide; `PLAN.md` (Thai) is the roadmap with checkboxes; tick them off as items are completed.

## Commands (Windows, PowerShell)

```powershell
.\build.ps1 -NoRun                 # x86: NASM -> build\, disk.img
arm\build.ps1 -NoRun               # ARM: kernel8.img (USB/network build), kernel8-qemu.img (NO_USB),
                                   #      build\myos8.img (padded 1 MB), build\packages\*.pkg (C packages,
                                   #      ports like lua, MicroPython python.pkg)
arm\build.ps1 -NoRun -Pi4          # Pi 4 variant
powershell -ExecutionPolicy Bypass -File tests\run.ps1     # build everything + all QEMU tests
tools\get-deps.ps1                 # fetch pinned third_party\ sources (not in git)
```

Single test suites (run from the repo root; set `$env:PYTHONIOENCODING='utf-8'` or output with the Thai path crashes):
- `python tests\qemu_x86.py` – x86 (reads VGA memory via the QEMU monitor)
- `python tests\qemu_arm.py` – ARM shell, SD/MyFS, programs, pkg, Python, update protocol, panic
- `python tests\qemu_apps.py [APP ...]` – every `apps/py` app driven with scripted keys
- `python tests\qemu_langs.py [LANG ...]` – lua, basic, scheme, forth
- `python tests\qemu_multi.py` – multitasking (`&`, ps, kill, fg, Ctrl+C, memory isolation)

QEMU builds have no USB, so networking/SSH/telnet can only be tested on the real Pi.

Real Pi (plugged in by USB; it shows up as COM port VID 1209/PID 0001 plus a CDC-NCM network adapter):
- `arm\update-pi.ps1 [-NoBuild]` – send a new kernel over the COM port (CRC-checked, written over `myos8.img` on the SD card, reboots)
- `arm\push-pi.ps1 file...` / `-Packages` – send files
- `python tools\repo.py` – serve the package repository on 192.168.7.2:8080 for `pkg update` / `pkg install` on the Pi
- Pi = 192.168.7.1: `ssh myos@192.168.7.1` (password set on the Pi with `passwd`), telnet port 23
- The COM port can only be opened by one program: PuTTY must be closed before update/push scripts.
- Going back to Linux on the Pi: delete `config.txt` on the boot partition and rename `config.txt.linux-backup`.

## Environment gotchas

- The project path contains Thai characters. `make` can't handle it, so MicroPython is built through the junction `%TEMP%\myos-src` (`arm\build.ps1` does this). Run make from PowerShell, not Git Bash (Bash resolves the junction back to the Thai path). Make also needs LLVM and Git's `usr\bin` on PATH.
- In the Bash tool, heredocs turn `\\` into `\`, which breaks C/Python escapes like `'\n'`. Use the Write/Edit tools for content with backslashes.
- Files are a mix of CRLF (git checkout) and LF (newly written). Scripted replacements must try both.
- MicroPython lacks some CPython APIs (e.g. `str.isalnum`, `bytearray * int`, stepped slice assignment). Test apps with `qemu_apps.py`, not only CPython.
- Commits: repo-local identity `arka <arka@localhost>`; end messages with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## ARM kernel architecture (`arm/`)

- **Boot/MMU**: broadcom-peripherals (`third_party/tinyusb/hw/mcu/broadcom`) supplies `boot8.s`, the flat MMU map (2 MB blocks), interrupts and mailbox. `kernel.c` `main()` initialises everything, then runs the serial shell (`shell_loop`, shared by all terminals).
- **Exceptions** (`vectors.S`): trap frame of 288 bytes; `el0_sync` → `user_sync` (syscalls via `svc #0`, number in x8). Also contains `proc_switch` (kernel-thread context switch incl. DAIF), `fp_save`/`fp_restore`, `enter_user`. The kernel is compiled with `-mgeneral-regs-only`; FP/SIMD registers belong to programs and are saved on every switch.
- **Processes** (`proc.c`): up to 16 slots (slot 0 = serial shell). Every user process has its own L1/L2 tables mapping VA `0x400000` (2 MB code/data/stack, args at `0x5FFF00`) and heap VA `0x02000000–0x04000000` to blocks from a physical pool from `0x04000000` up to the end of ARM RAM reported by the firmware mailbox at boot (capped at `0x3F000000`, fallback `0x14000000`; QEMU raspi3b gives 896 MB, a Pi Zero 2 W ~384 MB); heap blocks are mapped on first touch (data-abort handler) or by `proc_user_ensure` before a syscall touches user memory. Preemption only happens on timer IRQs taken from EL0 (`proc_irq_return`); kernel code switches only at `proc_yield()`, which `console_poll()` calls. That's why kernel code needs no locks as long as it doesn't yield mid-operation. Shells for network sessions are kernel threads (`proc_spawn_kernel`).
- **Terminals** (`console.c`): tty 0 = GPIO UART + USB CDC serial, tty 1 = telnet (`remote.c`), tty 2–4 = SSH sessions. Each tty has its own key queue and output device; a process writes to and reads from `proc->tty`. Only the tty's foreground process gets keys; Ctrl+C is intercepted in the key pump (`proc_interrupt`). Opening/closing a network session spawns/kills its shell and programs (`check_terminals`).
- **Syscalls** (`user.c`): numbering must match `csdk/myos.h` (and `apps/myos.inc` for x86). Waiting syscalls set `proc->waiting` so htop counts them as idle. System-info structs are shared in `csdk/myos_info.h`.
- **Storage**: `sd.c` (EMMC/SDHCI) → `fat.c` finds contiguous `myfs.img`/`myos8.img` on the FAT boot partition → `myfs.c` (flat file system, max 64 files, same format as x86 and `tools/myfs.py`).
- **Network** (USB only): TinyUSB CDC-NCM + lwIP (`net.c`, NO_SYS, polled; `net_lock` guards against re-entry from the TinyUSB send path), DHCP/DNS servers for the PC, telnet (`remote.c`), SSH-2 server (`ssh.c`: curve25519-sha256, ssh-ed25519, chacha20-poly1305, strict KEX, password auth, 3 sessions; Monocypher crypto, `sha256.c`, hardware RNG in `rng.c`), HTTP client for the repo (`http.c`).
- **Wi-Fi** (Pi Zero 2 W, USB build only for the network part): `sdio.c` drives the EMMC controller as an SDIO host (GPIO 34-39, WL_ON = GPIO 41, 25 MHz 4-bit); `wifi.c` loads firmware/NVRAM/CLM from MyFS into the CYW43436 (chip ID 0xA9A6), talks SDPCM/BCDC (ioctls, events, data), and adds a second lwIP netif with DHCP. Polled from `net_poll()`. Only clear host bits (0xF0) in the SDIO core's intstatus, or the chip misses its own frame interrupts. `wifi log` prints the firmware's console. Telnet and the USB DHCP/DNS servers stay bound to the USB netif.
- **Packages** (`pkg.c`, `tools/mkpkg.py`, `tools/repo.py`): 128-byte "MYPK" header; kind 1 = native program ("MYEX" header: u32 arch 1=x86/2=ARM64, u64 entry, linked at 0x400000 by `csdk/link.ld`), kind 2 = Python app installed as `NAME.py` and run by typing `NAME`. The repo index `pkg.idx` holds size and SHA-256 per package; `pkg.db` records installed versions. Python apps require python ≥ 1.3 (dependency check in `pkg.c`).

## User space

- `csdk/`: `crt0.c`, minimal `libc.c`, and with `-DMYOS_LIBC` a full C library (`csdk/include`, `csdk/libc`: stdio on MyFS with whole files kept in memory, malloc over both heap regions, printf with floats, strtod, setjmp, time, math from MicroPython's libm). There is no 128-bit `long double` runtime, so avoid `long double`.
- `packages/*.c`: small C programs built for both architectures.
- `ports/NAME/NAME.info` + `$ports` in `arm\build.ps1`: unmodified third-party C programs (Lua 5.4) built against the full libc.
- `python/`: MicroPython port (`make ARCH=arm64` → `build-arm64`); `modmyos.c` is the `myos` module (`key`, `getkey`, `clear`, `move`, `cursor`, `ticks`, `procs`, `sysinfo`, `kill`); `file.c` provides `open`/`os`. Bump `python/python.info` version when the port changes so `pkg upgrade` picks it up.
- `apps/py/*.py`: Python apps; metadata from `# @version` / `# @description` (≤ 43 ASCII chars) comment lines. New apps also need an entry in `tests/qemu_apps.py`.
