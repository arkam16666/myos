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

**อัปเดตครั้งต่อไป:** ถอด SD card มาเสียบคอม คัดลอก `arm\build\kernel8.img` ไปทับ `myos8.img` ในไดรฟ์ `bootfs`

**ใช้งาน:** เสียบสาย USB เข้าช่อง **USB** ของ Pi (ช่องกลาง) MyOS จะขึ้นเป็นพอร์ต COM ใหม่ เปิดด้วย PuTTY (Serial) ไฟ LED สีเขียวกะพริบ "ติ๊ก-ติ๊ก… เว้น" = MyOS รันอยู่, กะพริบรัวเร็ว = พัง

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
