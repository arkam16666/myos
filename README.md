# MyOS

ระบบปฏิบัติการเล็ก ๆ ที่เขียนเองตั้งแต่ศูนย์ มีสองรุ่น

| | MyOS PC | MyOS Pi |
|---|---|---|
| CPU | x86-64 (Intel/AMD) | ARM64 (Raspberry Pi Zero 2 W, Pi 3, Pi 4) |
| โฟลเดอร์ | `.` (root) | `arm\` |
| Kernel | Assembly (`kernel.asm`) | C |
| รันบน | QEMU | Pi ตัวจริง หรือ QEMU |

ความสามารถ ความคืบหน้า และงานที่เหลือ ดูใน [PLAN.md](PLAN.md)

## MyOS PC ทำอะไรได้

- บูตเอง 16 → 32 → 64-bit, interrupt, timer, driver ดิสก์ ATA
- ระบบไฟล์ของตัวเอง (MyFS)
- โปรแกรมรันใน ring 3 แยกจาก kernel โปรแกรมพังแล้ว OS ไม่ล่ม
- System call 13 บริการ, ระบบแพ็กเกจ `pkg`
- โปรแกรมภาษา C และ Python (MicroPython) อ่านเขียนไฟล์ได้

## เครื่องมือที่ต้องมี (Windows)

```powershell
winget install NASM.NASM                          # assembler
winget install SoftwareFreedomConservancy.QEMU    # เครื่องจำลอง
winget install LLVM.LLVM                          # clang: โปรแกรม C, MyOS Pi
winget install ezwinports.make                    # build MicroPython
```

ต้องมี **Python 3** และ **git** ด้วย จากนั้นดาวน์โหลดซอร์สโค้ดภายนอก (MicroPython, TinyUSB) เวอร์ชันที่ทดสอบแล้ว:

```powershell
powershell -ExecutionPolicy Bypass -File tools\get-deps.ps1
```

## Build และรัน

**MyOS PC**
```powershell
powershell -ExecutionPolicy Bypass -File build.ps1          # build + เปิด QEMU
powershell -ExecutionPolicy Bypass -File build.ps1 -NoRun   # build อย่างเดียว
```
`build.ps1` สร้างทุกอย่าง: kernel, ดิสก์ `disk.img` (32 MB, สร้างครั้งแรกครั้งเดียว), แพ็กเกจ และ MicroPython

**MyOS Pi ใน QEMU**
```powershell
powershell -ExecutionPolicy Bypass -File arm\build.ps1      # เปิดหน้าต่าง serial console
```
ออกจาก QEMU ด้วย Ctrl+A แล้ว X

**ทดสอบอัตโนมัติ**
```powershell
powershell -ExecutionPolicy Bypass -File tests\run.ps1
```

## ลง MyOS บน Raspberry Pi Zero 2 W

**ครั้งแรก (Pi ยังรัน Linux อยู่):** `arm\install-pi.ps1` คัดลอก MyOS ลง SD card ผ่าน SSH, สำรอง `config.txt` เดิม แล้วรีบูต
```powershell
powershell -ExecutionPolicy Bypass -File arm\install-pi.ps1 -User <ชื่อผู้ใช้> -PiHost <ชื่อเครื่อง>.local
```

**ไฟล์ที่ต้องมีในไดรฟ์ `bootfs`** (วางด้วยตัวอ่านการ์ดครั้งแรกครั้งเดียว):
- `myos8.img` = `arm\build\myos8.img` (kernel ขนาดตายตัว 1 MB เพื่อให้อัปเดตทับที่เดิมได้)
- `myfs.img` = ระบบไฟล์ของ MyOS สร้างด้วย `python tools\myfs.py myfs.img create 16`

**อัปเดตครั้งต่อไป (ไม่ต้องถอดการ์ด):** เสียบ Pi ทาง USB แล้ว
```powershell
powershell -ExecutionPolicy Bypass -File arm\update-pi.ps1
```
สคริปต์จะ build ส่ง kernel ใหม่ให้คำสั่ง `update` ของ MyOS (ตรวจ CRC32 และอ่านกลับตรวจทุก sector) แล้วสั่ง `reboot` ถ้าบูตไม่ขึ้น ให้ใช้ตัวอ่านการ์ดวาง `arm\build\myos8.img` ที่ใช้ได้กลับลงไป

**ใช้งาน:** เสียบสาย USB เข้าช่อง **USB** ของ Pi (ช่องกลาง) MyOS จะขึ้นเป็นพอร์ต COM ใหม่ เปิดด้วย PuTTY (Serial) ไฟ LED สีเขียวกะพริบ "ติ๊ก-ติ๊ก… เว้น" = MyOS รันอยู่, กะพริบรัวเร็ว = พัง

คำสั่งของ MyOS Pi: `help`, `info`, `uptime`, `sd`, `hexdump`, `ls`, `cat`, `write`, `rm`, `format`, `pkg`, `net`, `passwd`, `exit`, `crash`, `update`, `recv`, `reboot` และชื่อโปรแกรมที่ติดตั้งไว้

**เครือข่ายผ่านสาย USB:** นอกจากพอร์ต COM แล้ว Pi ยังขึ้นเป็นการ์ดเครือข่าย "MyOS Network" บน Windows (Windows 10 ขึ้นไปโหลด driver เอง) Pi คือ `192.168.7.1` (ชื่อ `myos.usb`) คอมจะได้ `192.168.7.2` อัตโนมัติ
- `ping 192.168.7.1`
- เข้า shell ผ่านเครือข่าย: PuTTY → Connection type **Telnet** → Host `192.168.7.1` (ครั้งละ 1 คน, ยังไม่เข้ารหัสและไม่มีรหัสผ่าน แต่เข้าได้เฉพาะจากคอมที่ต่อสาย USB อยู่)
- ดูสถานะ: คำสั่ง `net` บน MyOS

**SSH:** ตั้งรหัสผ่านก่อนครั้งแรก พิมพ์ `passwd` ใน MyOS (ผ่าน COM port หรือ telnet) แล้วบน Windows:
```powershell
ssh myos@192.168.7.1
```
- ครั้งแรก ssh จะถามว่าเชื่อ host key มั้ย ให้เทียบกับบรรทัด `Host key: ED25519 SHA256:...` ของคำสั่ง `net` ถ้าตรงกันพิมพ์ `yes`
- ออกด้วย `exit`
- ใช้ชื่อผู้ใช้อะไรก็ได้ มีรหัสผ่านเดียว ผิด 5 ครั้งตัดการเชื่อมต่อ
- อัลกอริทึม: curve25519-sha256, ssh-ed25519, chacha20-poly1305@openssh.com (+ strict KEX) จาก [Monocypher](https://monocypher.org)
- OpenSSH 10 ขึ้นไปจะเตือนว่า "not using a post-quantum key exchange" เป็นแค่คำเตือน ใช้งานได้ปกติ
- host key อยู่ในไฟล์ `ssh_host_ed25519.key` ส่วนรหัสผ่านเก็บเป็น hash ใน `ssh_password` บน MyFS (ถ้า `format` ใหม่ host key จะเปลี่ยน ssh จะเตือน ให้ลบบรรทัด 192.168.7.1 ออกจาก `%USERPROFILE%\.ssh\known_hosts`)

**ติดตั้งแอปแบบ apt** (ผ่าน USB network): บนคอมเปิดคลังแอปทิ้งไว้
```powershell
python tools\repo.py
```
แล้วบน Pi:
```
pkg update              ดึงรายการแอปจากคอม
pkg search [คำ]          ดูว่ามีแอปอะไรบ้าง (* = ติดตั้งแล้ว)
pkg install snake 2048  ติดตั้ง (แอป Python จะติดตั้ง python ให้เองถ้ายังไม่มี)
pkg remove snake        ลบ
pkg upgrade             อัปเดตแอปที่มีเวอร์ชันใหม่
pkg list                แอปที่อยู่ในเครื่อง
```
แอปที่มีตอนนี้ (38 ตัว):
- **เกม:** snake, blocks, 2048, mines, maze, four, ttt, hangman, words, blackjack, rps
- **เครื่องมือ:** edit (text editor แบบ nano เขียนโค้ดบน Pi ได้, Ctrl+R รัน), todo, units, stopwatch, primes, dice, banner, cowsay, bf
- **ของเล่น:** life, mandel, matrix, hanoi, typetest, mathquiz
- **ระบบ:** htop (ดู CPU, อุณหภูมิ, หน่วยความจำ, โปรเซส; ลูกศรเลือก, `K` kill, `q` ออก)
- **โปรแกรม C เดิม:** calc, cdemo, countdown, crashme, fortune, guess, python

**ภาษาโปรแกรม** (`pkg search language`):
| แพ็กเกจ | ภาษา | ตัวอย่าง |
|---|---|---|
| `lua` | Lua 5.4.7 ตัวจริง (port จาก C) | `lua`, `lua -e "print(2^10)"`, `lua game.lua` |
| `python` | MicroPython (Python 3) | `python`, `python hello.py` |
| `basic` | BASIC แบบมีเลขบรรทัด | `10 PRINT "HI"` แล้ว `RUN`, `SAVE "a.bas"`, `basic a.bas` |
| `scheme` | Scheme (Lisp) | `(define (sq x) (* x x))` |
| `forth` | Forth (ภาษาแบบ stack) | `: square dup * ; 7 square .` |
| `bf` | Brainf*ck | `bf` |

ใช้ `edit` เขียนไฟล์โปรแกรมบน Pi ได้เลย เช่น `edit game.lua` แล้วรันด้วย `lua game.lua`

**C library ของ MyOS** (`csdk\include` + `csdk\libc`): `stdio` (ไฟล์บน MyFS), `malloc`, `printf`/`snprintf` (รวมทศนิยม), `strtod`, `string`, `ctype`, `math`, `setjmp`, `time` ใช้ port โปรแกรม C มาตรฐานมาลง MyOS ได้ (Lua port มาด้วยวิธีนี้ ไม่ต้องแก้ซอร์สของ Lua เลย) โปรแกรมได้หน่วยความจำ 2 MB + heap อีก 32 MB เพิ่ม port ใหม่ใน `$ports` ของ `arm\build.ps1`

ข้อจำกัด: Pi ไม่มีนาฬิกา `os.time()` / `time()` นับจาก 1 ม.ค. 2000 ตอนเปิดเครื่อง, ยังรันคำสั่ง shell จากในโปรแกรมไม่ได้ (`os.execute`)

เขียนแอปเพิ่มเองได้: วางไฟล์ `.py` ใน `apps\py\` ใส่บรรทัด `# @version 1.0` และ `# @description ...` แล้วเปิด `tools\repo.py` ใหม่ แอปใช้โมดูล `myos` ได้: `myos.key()` (อ่านปุ่มไม่ต้องรอ Enter), `myos.getkey()`, `myos.clear()`, `myos.move(x, y)`, `myos.cursor(on)`, `myos.ticks()`, `myos.procs()`, `myos.sysinfo()`, `myos.kill(pid)` ทดสอบแอปทั้งหมดใน QEMU ด้วย `python tests\qemu_apps.py`

ข้อจำกัด: MyFS ตอนนี้เก็บได้สูงสุด 64 ไฟล์ (จะแก้ในข้อ 3.4 MyFS v2)

**รันหลายโปรแกรมพร้อมกัน** (multitasking):
```
lua game.lua &        ใส่ & ท้ายคำสั่ง = รันเบื้องหลัง แล้วใช้ shell ต่อได้เลย
ps                    ดูโปรแกรมที่รันอยู่ (PID, หน่วยความจำ, เวลา)
kill 2                หยุดโปรแกรม PID 2
fg 2                  ดึงโปรแกรม PID 2 กลับมาข้างหน้า (ให้รับคีย์บอร์ดได้)
Ctrl+C                หยุดโปรแกรมที่อยู่ข้างหน้า (หรือยกเลิกบรรทัดที่พิมพ์ค้าง)
```
- รันได้พร้อมกันสูงสุด 7 โปรแกรม สลับกันทุก 10 ms (preemptive)
- แต่ละโปรแกรมมีหน่วยความจำแยกกัน: เห็นตัวเองอยู่ที่ `0x400000` เหมือนเดิม แต่ MMU พาไปคนละที่ใน RAM ทำให้โปรแกรมหนึ่งพังหรือเขียนผิดที่ ไม่กระทบตัวอื่น
- heap (สูงสุด 32 MB ต่อโปรแกรม) ได้ทีละ 2 MB เมื่อใช้จริงเท่านั้น จากคลังรวม 256 MB
- คีย์บอร์ดไปที่โปรแกรมข้างหน้าเท่านั้น โปรแกรมเบื้องหลังพิมพ์ออกจอได้แต่อ่านคีย์ไม่ได้
- ข้อจำกัด: Ctrl+C ใน Python จะปิด Python ทั้งตัว

**หน้าจอแยกต่อ session:** COM port, telnet และ SSH แต่ละ session เป็น terminal ของตัวเอง มี shell, คีย์บอร์ด และหน้าจอแยกกัน
- SSH เข้าได้พร้อมกัน 3 session (เปิดหลายหน้าต่าง `ssh myos@192.168.7.1` ได้เลย) อันที่ 4 จะถูกปฏิเสธ
- โปรแกรมพิมพ์ออกเฉพาะ session ที่รันมัน, Ctrl+C หยุดเฉพาะโปรแกรมของ session นั้น, `fg` ดึงได้เฉพาะงานของ session ตัวเอง
- `ps` เห็นทุกโปรแกรมของทุก session (คอลัมน์ TTY: serial / telnet / ssh1-3, `*` = session นี้)
- ปิดหน้าต่าง SSH หรือ `exit` แล้ว shell และโปรแกรมทั้งหมดของ session นั้นถูกปิดตาม

**ส่งไฟล์เข้า Pi ผ่าน USB** (แพ็กเกจ, ไฟล์ `.py` ฯลฯ):
```powershell
powershell -ExecutionPolicy Bypass -File arm\push-pi.ps1 -Packages            # แพ็กเกจทั้งหมดสำหรับ Pi
powershell -ExecutionPolicy Bypass -File arm\push-pi.ps1 files\game.py        # ไฟล์ใดก็ได้
```
แล้วใน MyOS: `pkg list`, `pkg install python`, `python game.py` แพ็กเกจสำหรับ Pi build จาก `packages\*.c` ชุดเดียวกับ MyOS PC ไว้ที่ `arm\build\packages\`

ตอนการ์ดอยู่ในตัวอ่าน จัดการไฟล์ใน `myfs.img` จาก Windows ได้ด้วย `tools\myfs.py` เหมือนกัน

**กลับไปใช้ Linux:** ในไดรฟ์ `bootfs` ลบ `config.txt` แล้วเปลี่ยนชื่อ `config.txt.linux-backup` เป็น `config.txt`

## เขียนโปรแกรมให้ MyOS PC

| ภาษา | วางไฟล์ที่ | ใช้งานใน MyOS |
|---|---|---|
| Python | `files\ชื่อ.py` | `python ชื่อ.py` |
| C (แพ็กเกจ) | `packages\ชื่อ.c` | `pkg install ชื่อ` แล้ว `ชื่อ` |
| Assembly | `apps\ชื่อ.asm` (ติดตั้งมาให้เลย) | `ชื่อ` |

แล้วรัน `build.ps1` ไฟล์ใน `files\` ที่ยังไม่มีบนดิสก์จะถูกคัดลอกเข้าไป (ไม่เขียนทับไฟล์ที่มีอยู่แล้ว) แพ็กเกจ C ใส่ข้อมูลไว้ในคอมเมนต์บรรทัดแรก ๆ:
```c
// @version 1.0
// @description บอกว่าทำอะไร (ไม่เกิน 43 ตัวอักษร)
#include "myos.h"
int main(const char *args) { sys_print("hello\n"); return 0; }
```
System call ทั้งหมดดูใน `csdk\myos.h` (C) หรือ `apps\myos.inc` (assembly)

จัดการดิสก์จาก Windows:
```powershell
python tools\myfs.py disk.img ls
python tools\myfs.py disk.img put files\ชื่อ.py
python tools\myfs.py disk.img get note.txt
```

## โครงสร้างโฟลเดอร์

```
myos\
├── boot.asm, kernel.asm   MyOS PC: bootloader และ kernel
├── build.ps1              build MyOS PC + ดิสก์ + แพ็กเกจ + Python
├── apps\                  โปรแกรม assembly ที่ติดตั้งมาให้ + myos.inc
├── packages\              แพ็กเกจ (C) ในคลัง pkg
├── files\                 ไฟล์ที่ส่งเข้าดิสก์ (ตัวอย่าง .py)
├── csdk\                  SDK ภาษา C สำหรับโปรแกรมบน MyOS
├── python\                MicroPython port สำหรับ MyOS
├── tools\                 myfs.py, mkpkg.py, get-deps.ps1
├── arm\                   MyOS Pi: kernel C, driver, USB, สคริปต์ลง Pi
├── tests\                 ทดสอบอัตโนมัติใน QEMU
├── old\                   เวอร์ชันเก่า (16-bit, 64-bit text, 64-bit GUI)
└── third_party\           ซอร์สภายนอก (ไม่อยู่ใน git, โหลดด้วย get-deps.ps1)
```

## ข้อจำกัดที่รู้แล้ว

- `make` อ่านชื่อโฟลเดอร์ภาษาไทยไม่ได้ `build.ps1` จึง build MicroPython ผ่าน junction `%TEMP%\myos-src`
- rpiboot (บูต Pi ผ่าน USB โดยไม่ใช้ SD card) ใช้กับคอมที่ใช้พัฒนาไม่ได้ ช่วงที่สองของการส่งไฟล์หมดเวลา
- MyOS PC ทดสอบแค่ใน QEMU

## ซอร์สภายนอกที่ใช้

| | ใช้ทำ | สัญญาอนุญาต |
|---|---|---|
| [MicroPython](https://github.com/micropython/micropython) | Python บน MyOS | MIT |
| [TinyUSB](https://github.com/hathach/tinyusb) | USB serial บน MyOS Pi | MIT |
| [broadcom-peripherals](https://github.com/adafruit/broadcom-peripherals) | โค้ดบูต, MMU, interrupt ของ Pi | MIT |
| Raspberry Pi firmware | ไฟล์บูตของ Pi | Broadcom (แจกจ่ายได้) |
