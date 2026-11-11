# แผนงาน MyOS

อัปเดตล่าสุด: 2026-11-11

## สถานะปัจจุบัน

| | MyOS PC (x86-64) | MyOS Pi (ARM64) |
|---|---|---|
| เวอร์ชัน | v1.0 | v0.5 |
| รันบน | QEMU | Raspberry Pi Zero 2 W ตัวจริง + QEMU |
| ภาษาของ kernel | Assembly (`kernel.asm`) | C (`arm\`) |
| Shell | ✅ | ✅ ผ่าน USB serial (COM) และ GPIO serial |
| Interrupt | ✅ | ✅ timer + USB |
| ระบบไฟล์ MyFS | ✅ v1 (แบน, 64 ไฟล์) | ✅ v2 ใน `myfs.img` บน SD card (โฟลเดอร์, ไม่จำกัดจำนวนไฟล์, คืนพื้นที่) |
| อัปเดต OS / ส่งไฟล์โดยไม่ถอดการ์ด | — | ✅ ผ่าน USB (`update-pi.ps1`, `push-pi.ps1`) |
| แยกสิทธิ์ kernel / โปรแกรม | ✅ ring 0 / ring 3 | ✅ EL1 / EL0 |
| System call + ตัวโหลดโปรแกรม | ✅ 13 บริการ | ✅ 13 บริการ (เลขเดียวกัน) |
| ระบบแพ็กเกจ `pkg` | ✅ | ✅ |
| โปรแกรมภาษา C | ✅ | ✅ (โค้ดชุดเดียวกัน) |
| Python (MicroPython) | ✅ | ✅ |
| หน้า panic | ✅ | ✅ |
| รันโปรแกรม Linux (ELF static ARM64) | — | ✅ BusyBox (`pkg install busybox`) |

## สิ่งที่ยังขาด

**ทั้งสองสาย**
- Multitasking (รันได้ทีละโปรแกรม)
- Network (ไม่มี driver การ์ดเครือข่าย, TCP/IP, SSH)
- Text editor ใน OS
- Ctrl+C, ปุ่มลูกศร, ประวัติคำสั่ง
- MyFS (x86): ไม่มีโฟลเดอร์, จำกัด 64 ไฟล์, ลบไฟล์แล้วไม่ได้พื้นที่คืน (Pi ใช้ MyFS v2 แล้ว)

**เฉพาะ x86**
- บูตบนเครื่องจริง (ตอนนี้เป็น BIOS + ฟลอปปี้ ทดสอบแค่ใน QEMU)

**เฉพาะ Pi**
- แสดงผลออก HDMI และคีย์บอร์ด USB
- SD card ของ Pi 4 (ตัวควบคุม EMMC2 คนละแบบ)

**ความเรียบร้อยของโปรเจกต์**
- โค้ด kernel ซ้ำสองชุด (assembly กับ C)

---

## เฟส 0: เก็บงานให้ปลอดภัย

ประมาณ 1 รอบ

- [x] **0.1** `git init` + `.gitignore` (ไม่เก็บ `build\`, `disk.img`, `third_party\`) + commit แรก
  - เสร็จเมื่อ: ย้อนเวอร์ชันได้
- [x] **0.2** README: วิธีติดตั้งเครื่องมือ, build, รัน, โครงสร้างโฟลเดอร์
  - เสร็จเมื่อ: คนอื่นอ่านแล้ว build ตามได้
  - เพิ่ม `tools\get-deps.ps1` ดาวน์โหลดซอร์สภายนอกตามเลข commit ที่ทดสอบแล้ว
- [x] **0.3** ย้ายสคริปต์ทดสอบเข้า `tests\` ให้รันได้ด้วยคำสั่งเดียว
  - เสร็จเมื่อ: `tests\run.ps1` ผ่านทั้ง x86 และ ARM (QEMU)
  - ตอนนี้ 45 ข้อ: x86 17 ข้อ (อ่านข้อความจากหน่วยความจำจอ, ใช้สำเนาของ `disk.img`), Pi 28 ข้อ (รวม SD card และระบบอัปเดต)
- [x] **0.4** เก็บของค้าง
  - [x] ลงตัวแก้โลโก้ซ้ำบน Pi (ยืนยันบนเครื่องจริงแล้ว)
  - [x] ทดสอบคำสั่ง `reboot` ของ x86 (อยู่ในชุดทดสอบแล้ว)
  - [x] ทดสอบ `game.py` ตอนทายถูกและบันทึก `best.txt` (ชุดทดสอบเล่นชนะเองด้วย binary search)

## เฟส 1: MyOS-ARM มีระบบไฟล์

ประมาณ 2–3 รอบ

- [x] **1.1** Driver SD card (EMMC) อ่าน/เขียน sector
  - เสร็จเมื่อ: คำสั่ง `hexdump` บน Pi อ่าน sector ได้
  - ยืนยันบนเครื่องจริงแล้ว (การ์ด SDHC 64 GB) และใน QEMU (การ์ด SDSC)
- [x] **1.2** MyFS บน SD card
  - เสร็จเมื่อ: `ls`, `cat`, `write`, `rm` ใช้ได้บน Pi
  - เลือกเก็บเป็นไฟล์ `myfs.img` ใน `bootfs` MyOS อ่าน FAT เพื่อหาตำแหน่งไฟล์ (อ่านอย่างเดียว) และเขียนได้เฉพาะในขอบเขตของไฟล์นี้ พาร์ทิชัน Linux ไม่ถูกแตะ
- [x] **1.3** ปรับ `tools\myfs.py` ให้เขียนลงการ์ดได้
  - เสร็จเมื่อ: ส่งไฟล์จาก Windows เข้า Pi ได้
  - `myfs.py` ใช้กับ `myfs.img` บนการ์ดได้ทันที เพิ่มคำสั่ง `create` และ `tools\mksdimg.py` สร้าง SD card จำลองสำหรับทดสอบ
- [x] **1.4** (เพิ่มเติม) อัปเดต MyOS ผ่าน USB โดยไม่ต้องถอดการ์ด
  - `arm\update-pi.ps1` ส่ง kernel ใหม่ให้คำสั่ง `update` ตรวจ CRC32 เขียนทับ `myos8.img` (ขนาดตายตัว 1 MB) อ่านกลับตรวจทุก sector แล้ว `reboot`
  - ยืนยันบนเครื่องจริงแล้ว

ผลพลอยได้: ชุดทดสอบเจอบั๊กที่เกิดเป็นครั้งคราวใน driver ดิสก์ของ x86 (จับเวลารอดิสก์ด้วยการนับรอบลูป ถ้าเครื่องทำงานหนักจะคิดว่าดิสก์เสีย) แก้ให้ใช้นาฬิกาจริงแล้ว

## เฟส 2: MyOS-ARM รันโปรแกรมได้

ประมาณ 3–4 รอบ จบเฟสนี้ MyOS บน Pi จะทำได้เท่ากับเวอร์ชัน PC

- [x] **2.1** Exception handler เต็มรูปแบบ: หน้า panic บอกสาเหตุและ register
  - เสร็จเมื่อ: `crash` แสดงข้อมูลผ่าน USB
  - `arm\vectors.S` ตาราง exception ของ MyOS เอง หลัง panic ขับ USB แบบวนเช็ค (interrupt ปิดอยู่) ข้อความจึงยังส่งออกได้ ทดสอบใน QEMU (บนเครื่องจริงจะทำให้ Pi หยุดจนต้องถอดไฟ)
- [x] **2.2** Timer interrupt + page table ที่แยกพื้นที่โปรแกรม
  - เสร็จเมื่อ: `uptime` นับจาก interrupt
  - system timer 100 ครั้งต่อวินาที, โปรแกรมแตะได้เฉพาะ `0x400000–0x5FFFFF` (ที่อยู่เดียวกับ MyOS PC)
- [x] **2.3** โหมดผู้ใช้ EL0 + system call ผ่าน `svc` (เลขเดิม 13 ตัว)
  - เสร็จเมื่อ: โปรแกรม C รันใน EL0 ได้ และโปรแกรมพังแล้ว OS ไม่ล่ม
  - ยืนยันบนเครื่องจริง: `crashme` ถูก CPU หยุด (Data Abort) แล้ว shell ทำงานต่อ
- [x] **2.4** ตัวโหลดโปรแกรม + `pkg` + ปรับ `csdk\` ให้ build ได้สองสถาปัตยกรรม
  - เสร็จเมื่อ: `pkg install calc` บน Pi
  - header ของโปรแกรมบอกสถาปัตยกรรม (1 = x86, 2 = ARM) ทั้งสอง kernel ปฏิเสธโปรแกรมของอีกฝั่ง
  - เพิ่มคำสั่ง `recv` + `arm\push-pi.ps1` ส่งไฟล์เข้า Pi ผ่าน USB
- [x] **2.5** Build MicroPython สำหรับ ARM
  - เสร็จเมื่อ: `python game.py` บน Pi
  - ยืนยันบนเครื่องจริง บน ARM ใช้การแปลงเลขทศนิยมแบบ APPROX เพราะ `long double` ของ ARM64 ต้องใช้ไลบรารีที่ไม่มี

## เฟส 3: ใช้งานสะดวกขึ้น

ประมาณ 3–4 รอบ ทำกับทั้งสองสาย แทรกได้ทุกเมื่อ เพราะทดสอบบน x86 ใน QEMU ได้โดยไม่ต้องถอดการ์ด

- [ ] **3.1** Ctrl+C หยุดโปรแกรม
  - เสร็จเมื่อ: ลูป `while True` ใน Python หยุดได้
- [x] **3.2** ปุ่มลูกศร + ประวัติคำสั่ง + system call ย้ายเคอร์เซอร์
  - เสร็จเมื่อ: กดลูกศรขึ้นเรียกคำสั่งเก่าได้ ✅ (shell: ↑/↓ ประวัติ 32 บรรทัดต่อ terminal, ←/→ Home End Delete, Ctrl+A/E/U; ทดสอบบน Pi จริง)
- [ ] **3.3** Text editor เต็มจอ (เป็นแพ็กเกจ)
  - เสร็จเมื่อ: แก้ไฟล์ `.py` ใน MyOS แล้วรันได้
- [x] **3.4** MyFS v2: โฟลเดอร์, คืนพื้นที่เมื่อลบไฟล์ (Pi)
  - เสร็จเมื่อ: `mkdir`, `cd` ใช้ได้ ✅ (tests/qemu_fs.py)
  - โฟลเดอร์ซ้อนกันได้, ไฟล์กี่พันไฟล์ก็ได้, ชื่อยาว 63 ตัว, ไฟล์ใหญ่ได้เท่าที่ว่าง, bitmap พื้นที่ว่าง (ลบแล้วได้คืนทันที)
  - เขียนแบบ copy-on-write: ข้อมูลก่อน แล้วค่อยเขียน entry ใน sector เดียว ไฟดับเสียอย่างมากแค่ไฟล์ที่กำลังเขียน ตอน mount ตรวจทุกโฟลเดอร์และคืนพื้นที่ที่ค้าง
  - shell: `cd`, `pwd`, `mkdir`, `rmdir`, `cp`, `mv`, `rm -r`, `tree`, `fsck`, `fsupgrade` (โฟลเดอร์ปัจจุบันแยกต่อ terminal)
  - MyFS v1 เดิมยังใช้ได้ `fsupgrade` แปลงเป็น v2 ในที่เดิมโดยไม่ย้ายข้อมูล (ไฟล์ครบทุกไฟล์), `tools/myfs.py` อ่าน/เขียนได้ทั้งสองแบบ
  - x86 ยังใช้ v1 อยู่
  - `fsexpand`: ย้าย MyFS จาก `myfs.img` (16 MB) ไปใช้ partition 2 ทั้งหมดของ SD card (แทน Linux เดิม) ดิสก์ใหญ่ได้ถึง 128 GB (bitmap ใน RAM จาก pool, เขียน bitmap บนดิสก์เฉพาะส่วนที่ใช้) ✅ Pi จริง: 60 GB, 48 ไฟล์ครบ, fsck ผ่าน; QEMU: tests/qemu_fs.py F (256 MB), G (8 GB)

## เฟส 4: งานใหญ่

เลือกตามความสนใจ

| # | งาน | ขนาด | หมายเหตุ |
|---|---|---|---|
| 4.1 | รวม kernel เป็นภาษา C ชุดเดียว (shell, MyFS, pkg) ใช้ทั้ง x86 และ ARM | ใหญ่ | ลดงานซ้ำระยะยาว ควรทำก่อน 4.2–4.4 |
| 4.2 | Multitasking + ตัวแบ่งจอแบบ tmux | ใหญ่ | ✅ multitasking เสร็จ (Pi) ส่วนแบ่งจอยังไม่ได้ทำ |
| 4.3 | Network: driver + IP + `ping` → TCP → ดาวน์โหลดแพ็กเกจ | ใหญ่มาก | บน Pi Zero 2 W ทำเป็น USB network ได้ เพราะมี TinyUSB แล้ว |
| 4.4 | HDMI + คีย์บอร์ด USB บน Pi | ใหญ่ | ให้ Pi ใช้งานได้โดยไม่ต้องต่อคอม |
| 4.5 | บูต x86 บนเครื่องจริงผ่าน UEFI | กลาง | |
| 4.7 | Wi-Fi บน Pi Zero 2 W (ชิป CYW43436 ผ่าน SDIO) | ใหญ่ | ดูหัวข้อ 4.7 ด้านล่าง |
| 4.8 | รันโปรแกรม Linux (เป้าหมาย: แอปจาก `apt`) | ใหญ่มาก | ✅ ขั้นแรก: โปรแกรม static เช่น BusyBox |

ตอนนี้ทำเฉพาะ ARM (Pi) ก่อน ส่วน 4.1 และ 4.5 ของ x86 พักไว้ ลำดับ: 4.3 → 4.4 → 4.2

### 4.3 Network บน Pi (ผ่านสาย USB)
- [x] **4.3.1** USB network: Pi ขึ้นเป็นการ์ดเครือข่าย (CDC-NCM) คู่กับพอร์ต COM, Windows โหลด driver เอง
- [x] **4.3.2** TCP/IP (lwIP), DHCP แจก 192.168.7.2 ให้คอม, DNS `myos.usb`
  - เสร็จเมื่อ: `ping 192.168.7.1` ได้ ✅ (ทดสอบบน Pi จริง 1472 ไบต์ ไม่หาย)
- [x] **4.3.3** Remote shell ผ่าน TCP (telnet พอร์ต 23, ยังไม่เข้ารหัส)
  - เสร็จเมื่อ: PuTTY แบบ Telnet เข้า shell ได้ ✅ (โปรแกรม, Python, ข้อความ 3000 บรรทัดไม่หาย)
- [x] **4.3.4** SSH (การเข้ารหัสจาก Monocypher)
  - เสร็จเมื่อ: `ssh` จาก Windows เข้า MyOS ได้ ✅ (OpenSSH 9.5 และ 10.5, รหัสผิดถูกปฏิเสธ, ข้อความ 3000 บรรทัดไม่หาย)

### 4.2 Multitasking บน Pi
- [x] **4.2.1** โปรเซส: ตาราง MMU แยกต่อโปรเซส, kernel stack แยก, scheduler แบบ preemptive ทุก 10 ms
- [x] **4.2.2** heap แบบ demand paging (block 2 MB เมื่อแตะครั้งแรก), เก็บ/คืน register ทศนิยมตอนสลับ
- [x] **4.2.3** shell: `&`, `ps`, `kill`, `fg`, Ctrl+C
  - เสร็จเมื่อ: รันหลายโปรแกรมพร้อมกันบน Pi จริงได้ ✅ (tests/qemu_multi.py 13 ข้อ)
- [x] **4.2.4** หน้าจอแยกต่อ session: terminal (tty) ต่อช่องทาง, shell เป็น kernel thread, SSH พร้อมกัน 3 session
  - เสร็จเมื่อ: SSH 2 หน้าต่างพร้อมกันเห็นแค่ของตัวเอง ✅ (ทดสอบบน Pi จริง)
- [ ] **4.2.5** แบ่งจอแบบ tmux ในหน้าต่างเดียว

### 4.7 Wi-Fi บน Pi Zero 2 W
- [x] **4.7.1** SDIO driver บน EMMC (GPIO 34-39 ALT3, WL_ON = GPIO 41): CMD5/3/7, CMD52/53, bus 4 บิต 25 MHz (SD card ย้ายไป SDHOST แล้ว)
  - เสร็จเมื่อ: Pi จริงเจอชิป ✅ (chip ID 0xA9A6 rev 2 = CYW43436)
- [x] **4.7.2** ปลุกชิป: backplane, หยุด/รัน ARM ในชิป, โหลด firmware + NVRAM + CLM blob จาก MyFS (ไฟล์ดาวน์โหลดด้วย `tools/get-deps.ps1`)
  - เสร็จเมื่อ: firmware รัน ✅ (`wifi log`: RTE (SDIO-CDC) 9.88.4.77 on BCM43430 r2)
- [x] **4.7.3** ช่องคำสั่ง BCDC/ioctl: เวอร์ชัน firmware, MAC, `wifi scan` (ชื่อ/สัญญาณ/ช่อง/ความปลอดภัย)
  - เสร็จเมื่อ: Pi จริงสแกนเจอเครือข่ายรอบบ้าน ✅ (5-6 เครือข่าย, WPA2, ช่อง 1-11)
- [x] **4.7.4** เข้า WPA2-PSK ด้วย supplicant ในชิป, frame ข้อมูลเป็น netif ที่สองของ lwIP, DHCP client, `ping`
  - เสร็จเมื่อ: Pi จริงต่อ WPA2 บ้านได้ ✅ (DHCP ได้ IP, ping router 2 ms, ping 8.8.8.8 9 ms, SSH จากคอมผ่าน Wi-Fi ได้)
- [x] **4.7.5** คำสั่ง `wifi` ครบ (scan/connect/status/disconnect/forget), จำเครือข่ายใน `wifi.cfg`, ต่อเองตอนบูต, แอป `wifiman` เต็มจอ (ทดสอบบน Pi จริงแล้ว: scan, connect, forget, ต่อเองตอนบูต, wifiman)
- [ ] **4.7.6** SDIO 50 MHz (high speed: ตอนนี้เขียนแล้ว CRC ผิด จึงใช้ 25 MHz), Wi-Fi บน Pi 4

### 4.6 แอปและภาษาเพิ่ม (ให้รู้สึกเหมือน Ubuntu)
- [x] **4.6.1** JavaScript: QuickJS เป็นแพ็กเกจ `node` (`pkg install nodejs` ก็ได้) REPL, `node -e/-p`, `node app.js`, ES module, `require('fs'/'path'/'os'/...)`
  - เสร็จเมื่อ: `node -e "console.log(1+2)"` ได้ ✅ (tests/qemu_langs.py 17 ข้อ)
- [x] **4.6.2** `sqlite3`: SQLite 3.54.1 + VFS บน MyFS + shell (`.tables .schema .mode box .import .dump ...`)
  - เสร็จเมื่อ: สร้างตาราง ปิด เปิดใหม่ ข้อมูลยังอยู่ ✅
- [x] **4.6.3** `chess` (C): หมากรุกกับคอม (alpha-beta, ตรวจ move generator ด้วย perft)
- [x] **4.6.4** แอป Python: `bc`, `neofetch`, `cal`, `sl`, `sudoku`, `sokoban`, `invaders`
- [x] **4.6.5** C library เพิ่ม: `inttypes.h`, `sys/time.h`, `fenv.h`, `clock_gettime`, `alloca`, หาร 128 บิต
- [ ] **4.6.6** ภาษาเพิ่ม: Tcl (Jim Tcl), Scheme ตัว C, ...
- [ ] **4.6.7** C compiler บน Pi (TinyCC มี backend ARM64) ให้เขียน C แล้วคอมไพล์บน MyOS ได้
- [ ] **4.6.8** วันที่จริง: เอาเวลาจากคอมผ่าน USB network (HTTP `Date` / NTP) ให้ `cal`, `date`, `Date()` ถูก
- [x] **4.6.9** pkg จากอินเทอร์เน็ต: DNS (lwIP), HTTPS (TLS 1.3 เขียนเอง: X25519 + ChaCha20-Poly1305), คลังแอปบน GitHub (branch packages) เซ็นด้วย Ed25519
  - เสร็จเมื่อ: Pi ต่อ Wi-Fi แล้ว pkg update / pkg install ดึงจาก GitHub ได้โดยไม่ต้องเปิดคอม ✅ (ลายเซ็นผ่าน, node 728 KB)
- [x] **4.6.10** `pip` (และ `mip`): ไลบรารี Python จาก micropython-lib ลง `/lib` (ตาม dependency, ตรวจ SHA-256 ทุกไฟล์), `list`/`show`/`uninstall`/`search`, python 1.7 มี `hashlib`/`binascii`/`re`/`time.localtime` และค้น `/lib`
  - เสร็จเมื่อ: `pip install datetime` บน Pi จริงแล้ว `import datetime` ได้ (QEMU ✅ ด้วยเว็บจำลอง, `tests/host/pip_host.py` กับ micropython.org จริง ✅, Pi จริง ✅ 2026-11-12)
- [x] **4.6.11** `npm`: ลงแพ็กเกจ JavaScript ล้วนจาก registry.npmjs.org ลง `node_modules` (semver ^ ~ x >= ||, dependency ซ้อน, ตรวจ sha512, gunzip + tar เขียนเอง, `npm install/ls/uninstall/view/init/run`), `require()` หา `node_modules` แบบ Node (`main`/`exports`, ES module ด้วย)
  - เสร็จเมื่อ: `npm install left-pad` แล้ว `require('left-pad')` ได้ (QEMU: ไฟล์ .tgz ✅ tests/qemu_langs.py npm 24 ข้อ; registry จริงผ่าน TLS 1.2 บน Pi ✅ left-pad, chalk@4 + 5 dependency)

### 4.8 โปรแกรม Linux บน Pi (เป้าหมาย: แอปจาก apt)
แอปใน `.deb` คือโปรแกรม Linux จึงให้ MyOS รันไฟล์ ELF ของ Linux ได้โดยจำลอง system call ของ Linux (`arm\linux.c`)
- [x] **4.8.1** ตัวโหลด ELF64 (ET_EXEC และ static PIE), stack เริ่มต้นแบบ Linux (argc/argv/envp/auxv), แผนผังหน่วยความจำแบบ Linux (โปรแกรม + brk ที่ 0x400000, mmap + stack ใน GB ที่สอง), เก็บ TPIDR_EL0 ต่อโปรเซส
- [x] **4.8.2** system call ของ Linux ราว 110 ตัว: ไฟล์บน MyFS, mmap/brk, เวลา, uname, getrandom ... ตัวที่ไม่มีตอบ -ENOSYS, `strace PROG` แสดงทุก system call
- [x] **4.8.3** fork/vfork/execve/wait4, pipe, signal handler จริง (signal frame + rt_sigreturn), termios, ppoll
  - เสร็จเมื่อ: `busybox sh` ใช้ pipe, redirect, `$(...)`, `trap`, Ctrl+C ได้ และ `vi` แก้ไฟล์ได้ ✅
- [x] **4.8.4** `/proc` จำลอง (meminfo, stat, uptime, PID/stat ...) และ statfs: `ps`, `top`, `free`, `df` ใช้ได้
- [x] **4.8.5** แพ็กเกจชนิด linux (`pkg.c`, `mkpkg.py`, `repo.py`), BusyBox 1.36.1 จาก Alpine (`get-deps.ps1` ตรวจ SHA-256), พิมพ์ชื่อคำสั่งของ BusyBox ได้เลย (`vi` = `busybox vi`)
  - เสร็จเมื่อ: `pkg install busybox` แล้วใช้คำสั่งแบบ Ubuntu ได้ ✅ (tests/qemu_linux.py 28 ข้อ ใน QEMU)
- [ ] **4.8.6** ทดสอบบน Pi จริง (build แบบ USB, ผ่าน SSH/telnet)
- [ ] **4.8.7** ตัวแปลง `.deb` → แพ็กเกจ MyOS (แตก `data.tar` เลือกไฟล์ ELF static) เช่น `tools\deb2pkg.py`
- [ ] **4.8.8** โปรแกรม dynamic (ส่วนใหญ่ใน apt): โหลด `ld-linux-aarch64.so.1` + glibc `.so` ต้องมี mmap ไฟล์จริงแบบแชร์หน้า และไฟล์ใน `/lib`, `/usr/lib`, `/etc` (MyFS v2 มีโฟลเดอร์แล้ว)
- [ ] **4.8.9** thread (clone แบบ CLONE_VM/CLONE_THREAD, futex จริง) สำหรับโปรแกรมที่ใช้ pthread
- [ ] **4.8.10** socket สำหรับโปรแกรม Linux (ผ่าน lwIP) เช่น `wget`, `nc`

---

## ลำดับที่แนะนำ

1. เฟส 0 → 1 → 2 ตามลำดับ เพราะแต่ละเฟสพึ่งเฟสก่อนหน้า
2. เฟส 3 แทรกได้ทุกเมื่อ
3. เฟส 4 เลือกทำหลังเฟส 2

### จุดที่ต้องตัดสินใจ: จะทำ 4.1 (รวม kernel เป็น C) เมื่อไร

- **ทำก่อนเฟส 2:** เขียน shell, MyFS และ `pkg` ครั้งเดียวใช้ได้สองสาย ประหยัดงานเฟส 2 ราวครึ่งหนึ่ง แต่ต้องเขียน kernel x86 ใหม่เป็น C ซึ่งเสี่ยงทำของที่ใช้ได้อยู่แล้วพัง
- **ข้อเสนอ:** ทำเฉพาะส่วนที่ใช้ร่วมกันได้ง่ายตอนถึงเฟส 1–2 (MyFS, shell, pkg เขียนเป็น C สำหรับ ARM ก่อน) และคง `kernel.asm` ของ x86 ไว้ ค่อยย้าย x86 มาใช้โค้ดชุดเดียวกันทีหลัง

---

## ข้อมูลอ้างอิงสำหรับทำงานต่อ

**อัปเดต MyOS บน Pi**
- ปกติ: เสียบ Pi ทาง USB แล้วรัน `arm\update-pi.ps1` (ไม่ต้องถอดการ์ด)
- กู้คืนถ้าบูตไม่ขึ้น: ถอดการ์ดเสียบตัวอ่าน คัดลอก `arm\build\myos8.img` (ขนาด 1 MB) ไปทับ `myos8.img` ในไดรฟ์ `bootfs`
- ถ้า Windows ไม่ให้อักษรไดรฟ์กับ `bootfs` ยังเข้าถึงได้ทาง path ของ volume (`Get-Volume` → `Path`)

**กลับไปใช้ Linux บน Pi**
ในไดรฟ์ `bootfs` ลบ `config.txt` แล้วเปลี่ยนชื่อ `config.txt.linux-backup` เป็น `config.txt`

**ข้อจำกัดที่รู้แล้ว**
- rpiboot (บูต Pi ผ่าน USB) ใช้กับคอมเครื่องนี้ไม่ได้ ช่วงที่สองของการส่งไฟล์หมดเวลาทุกครั้ง
- `make` อ่านชื่อโฟลเดอร์ภาษาไทยไม่ได้ `build.ps1` จึง build MicroPython ผ่าน junction `%TEMP%\myos-src`
- เวอร์ชัน Pi 4 ทดสอบแค่ใน QEMU ยังไม่เคยลงเครื่องจริง
