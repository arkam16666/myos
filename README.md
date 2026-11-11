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
- `myfs.img` = ระบบไฟล์ของ MyOS สร้างด้วย `python tools\myfs.py myfs.img create 64` (MyFS v2, ตัวเลข = ขนาดเป็น MB ใหญ่ได้ถึงที่ว่างในไดรฟ์ `bootfs`)

**อัปเดตครั้งต่อไป (ไม่ต้องถอดการ์ด):** เสียบ Pi ทาง USB แล้ว
```powershell
powershell -ExecutionPolicy Bypass -File arm\update-pi.ps1
```
สคริปต์จะ build ส่ง kernel ใหม่ให้คำสั่ง `update` ของ MyOS (ตรวจ CRC32 และอ่านกลับตรวจทุก sector) แล้วสั่ง `reboot` ถ้าบูตไม่ขึ้น ให้ใช้ตัวอ่านการ์ดวาง `arm\build\myos8.img` ที่ใช้ได้กลับลงไป

**ใช้งาน:** เสียบสาย USB เข้าช่อง **USB** ของ Pi (ช่องกลาง) MyOS จะขึ้นเป็นพอร์ต COM ใหม่ เปิดด้วย PuTTY (Serial) ไฟ LED สีเขียวกะพริบ "ติ๊ก-ติ๊ก… เว้น" = MyOS รันอยู่, กะพริบรัวเร็ว = พัง (กด `R` ในหน้าต่าง COM port เพื่อรีสตาร์ทได้เลย ไม่ต้องถอดสาย)

คำสั่งของ MyOS Pi: `help`, `info`, `uptime`, `sd`, `hexdump`, `ls`, `cd`, `pwd`, `mkdir`, `rmdir`, `tree`, `cat`, `write`, `cp`, `mv`, `rm`, `df`, `fsck`, `fsupgrade`, `format`, `pkg`, `net`, `wifi`, `ping`, `passwd`, `exit`, `crash`, `update`, `recv`, `reboot` และชื่อโปรแกรมที่ติดตั้งไว้

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

**ติดตั้งแอปแบบ apt:** ถ้า Pi ต่อ Wi-Fi อยู่ `pkg update` ดึงคลังแอปจาก **GitHub** เอง (branch `packages` ของ repo นี้, ผ่าน HTTPS) ไม่ต้องเปิดอะไรบนคอม ถ้าไม่มีเน็ตจะลองดึงจากคอมผ่านสาย USB แทน ซึ่งต้องเปิดคลังแอปบนคอมทิ้งไว้:
```powershell
python tools\repo.py
```
แล้วบน Pi:
```
pkg update              ดึงรายการแอป (GitHub ผ่าน Wi-Fi หรือคอมผ่านสาย USB)
pkg search [คำ]          ดูว่ามีแอปอะไรบ้าง (* = ติดตั้งแล้ว)
pkg install snake 2048  ติดตั้ง (แอป Python จะติดตั้ง python ให้เองถ้ายังไม่มี)
pkg remove snake        ลบ
pkg upgrade             อัปเดตแอปที่มีเวอร์ชันใหม่
pkg list                แอปที่อยู่ในเครื่อง
apt install -y snake    ใช้ apt / apt-get แทน pkg ได้ (รับ -y, show, purge, full-upgrade)
```
แอปที่มีตอนนี้ (49 ตัว):
- **เกม:** snake, blocks, 2048, mines, maze, four, ttt, hangman, words, blackjack, rps, chess (หมากรุกกับคอม, เดินแบบ `e2e4`), sudoku, sokoban (เข็นกล่อง 9 ด่าน), invaders
- **เครื่องมือ:** edit (text editor แบบ nano เขียนโค้ดบน Pi ได้, Ctrl+R รัน), todo, units, stopwatch, primes, dice, banner, cowsay, bf, bc (เครื่องคิดเลขเลขยาวไม่จำกัด เหมือน `bc` ของ Linux, `bc -l` มี sin/cos/log), cal (ปฏิทิน มีปี พ.ศ.), sqlite3 (ฐานข้อมูล SQL)
- **ของเล่น:** life, mandel, matrix, hanoi, typetest, mathquiz, sl (รถไฟ เวลาพิมพ์ `ls` ผิด), neofetch (ข้อมูลเครื่องคู่โลโก้)
- **ระบบ:** htop (ดู CPU, อุณหภูมิ, หน่วยความจำ, โปรเซส; ลูกศรเลือก, `K` kill, `q` ออก), wifiman (จัดการ Wi-Fi)
- **โปรแกรม C เดิม:** calc, cdemo, countdown, crashme, fortune, guess, python

**ภาษาโปรแกรม** (`pkg search language`):
| แพ็กเกจ | ภาษา | ตัวอย่าง |
|---|---|---|
| `lua` | Lua 5.4.7 ตัวจริง (port จาก C) | `lua`, `lua -e "print(2^10)"`, `lua game.lua` |
| `node` | JavaScript (QuickJS 2026-06-04 ตัวจริง สั่งแบบ Node.js) | `node`, `node -e "console.log(2**10)"`, `node app.js` |
| `npm` | ลงแพ็กเกจ JavaScript จาก npm (ใช้กับ `node`) | `npm install left-pad`, `npm ls` |
| `python` | MicroPython (Python 3) | `python`, `python hello.py` |
| `basic` | BASIC แบบมีเลขบรรทัด | `10 PRINT "HI"` แล้ว `RUN`, `SAVE "a.bas"`, `basic a.bas` |
| `scheme` | Scheme (Lisp) | `(define (sq x) (* x x))` |
| `forth` | Forth (ภาษาแบบ stack) | `: square dup * ; 7 square .` |
| `bf` | Brainf*ck | `bf` |

ใช้ `edit` เขียนไฟล์โปรแกรมบน Pi ได้เลย เช่น `edit game.lua` แล้วรันด้วย `lua game.lua`

**JavaScript** (`pkg install node` หรือ `pkg install nodejs`): เครื่องยนต์ QuickJS ของจริง (ES2023+: class, async/await, BigInt, Map/Set, module) กับหน้าบ้านแบบ Node ที่เขียนเอง
- `node` เปิด REPL (พิมพ์หลายบรรทัดได้, `await` ได้, `.exit` ออก), `node -e "..."`, `node -p "1+2"`, `node app.js a b` (แบบ CommonJS), ไฟล์ `.mjs` หรือที่มี `import` รันเป็น ES module
- มี `console.log/error/table/time`, `print`, `prompt("ชื่อ? ")` (อ่านบรรทัดจากคีย์บอร์ด), `setTimeout/setInterval`, `process.argv/exit/env/cwd`, `require('fs')` (`readFileSync`, `writeFileSync`, `readdirSync`, `existsSync`, `mkdirSync`, `rmSync`, `statSync` ... ใช้โฟลเดอร์ได้บน MyFS v2), `path`, `os`, `util`, `events`, `readline`, `assert`, `tty`, `crypto` (แค่ `createHash` sha1/sha512), `require('./mylib')`, แพ็กเกจใน `node_modules` (หาแบบ Node: ไล่โฟลเดอร์ขึ้นไป, `main`/`exports` ใน package.json, `index.js`, `.json`) และ `import ... from 'fs'`; `require()` แพ็กเกจที่เป็น ES module ล้วนได้ (ได้ namespace แบบ Node 22)
- ไม่มี: เครือข่าย (`http`), `child_process`, `Buffer` (ใช้ `Uint8Array` แทน), `stream`, `crypto.randomBytes` (โปรแกรมยังไม่มีแหล่งเลขสุ่มที่ปลอดภัย), โมดูล `std`/`os` ของ qjs (ต้องใช้ Unix), Intl/Temporal; เรียกฟังก์ชันซ้อนลึกได้ราว 3,300 ชั้น
- โปรแกรมใหญ่ 750 KB (ใหญ่สุดที่ MyOS รับได้คือ 1 MB)

**npm** (`pkg install npm`, ใช้คู่กับ `node`): ลงแพ็กเกจ JavaScript จาก registry.npmjs.org ตัวจริงผ่าน Wi-Fi ลงใน `./node_modules` แล้ว `require()` ได้เลย ต้องเป็น MyFS v2 (มีโฟลเดอร์: พิมพ์ `fsupgrade` ถ้ายังเป็น v1)
```
npm init -y                  สร้าง package.json
npm install left-pad         ลงแพ็กเกจ (กับแพ็กเกจที่มันต้องใช้) และจดลง package.json
npm install lodash@^4 ms@2.1.3   ระบุเวอร์ชันได้: ตรงตัว, ^, ~, 1.x, >=, a - b, ||, latest หรือ tag อื่น
npm install ./my-lib-1.0.0.tgz   ลงจากไฟล์ .tgz (แบบที่ npm pack ทำ)
npm install                  ลงทุกอย่างที่ package.json ระบุ
npm ls [--all]               ดูว่าลงอะไรไว้      npm uninstall left-pad   ลบ (และของที่ไม่มีใครใช้แล้ว)
npm view express             ดูข้อมูลแพ็กเกจ    npm start / npm test / npm run X  (สคริปต์แบบ "node file.js")
node -e "console.log(require('left-pad')('7', 3, '0'))"
```
- ใช้ได้: แพ็กเกจ JavaScript ล้วน (เช่น lodash, chalk, dayjs, ms, left-pad) ทั้งแบบ CommonJS และ ES module, scoped (`@scope/name`), `-D` ลงเป็น devDependencies, `--no-save`, `-g` ลงที่ `/node_modules` (ใช้ได้ทุกโฟลเดอร์)
- ใช้ไม่ได้: native addon ที่ต้องคอมไพล์ C++ (`binding.gyp`, node-gyp เช่น bcrypt, sqlite3 ของ npm) จะขึ้นว่า "needs a C++ build"; แพ็กเกจที่ต้องใช้ built-in ของ Node ที่ MyOS ไม่มี (`http`, `net`, `stream`, `child_process`, `Buffer` ...) ลงได้แต่รันไม่ได้; สคริปต์ติดตั้ง (`postinstall`) ไม่ถูกรัน; dependency แบบ git/URL; ไฟล์ที่ชื่อยาวเกิน 63 ตัวอักษรหรือมีอักขระนอก ASCII ถูกข้าม (มีคำเตือน)
- ไม่มีเน็ต: "Wi-Fi not connected? try 'wifi'" แพ็กเกจที่มีรายชื่อเวอร์ชันใหญ่มาก (เกิน ~24 MB เช่น typescript) อาจเกินหน่วยความจำ
- ความปลอดภัย: MyOS ยังไม่ตรวจใบรับรอง HTTPS (certificate) เลย npm ตรวจไฟล์ .tgz ทุกไฟล์ด้วย sha512 (`dist.integrity`, หรือ sha1 ถ้าไม่มี) แต่ค่า sha512 นั้นก็มาจาก registry ผ่าน HTTPS ที่ไม่ได้ตรวจใบรับรองเหมือนกัน จึงกันไฟล์เสียระหว่างทางได้ แต่กันคนที่ดักกลางทาง (เช่น Wi-Fi สาธารณะ) ไม่ได้; โค้ดจาก npm รันด้วยสิทธิ์เต็มของโปรแกรม ลงเฉพาะแพ็กเกจที่ไว้ใจ
- โปรแกรม `npm` คือ `node` ตัวเดียวกันที่มี npm (เขียนด้วย JavaScript, `ports/npm/npm.js`) ฝังอยู่ ขนาด 778 KB; gunzip/SHA-512/SHA-1 เขียนเองใน `ports/node/npmlib.c`

**SQLite** (`pkg install sqlite3`): SQLite 3.54.1 ตัวจริง ไฟล์ฐานข้อมูลเก็บบน MyFS
```
sqlite3 shop.db                              เปิด (หรือสร้าง) แล้วพิมพ์ SQL จบด้วย ;
sqlite3 shop.db "SELECT * FROM fruit"        รันคำสั่งเดียวแล้วออก
sqlite3 -box shop.db "SELECT ..."            ตารางสวย (หรือ -csv -json -line -column -table)
```
คำสั่งจุด: `.tables`, `.schema`, `.mode box`, `.headers on`, `.import data.csv t`, `.dump`, `.read file.sql`, `.output file`, `.once file`, `.open file`, `.timer on`, `.help`, `.quit` ข้อจำกัด: ฐานข้อมูลอยู่ในหน่วยความจำทั้งก้อนและเขียนลงดิสก์ทุกครั้งที่ commit (ไม่มี WAL, ไม่มี lock)

**C library ของ MyOS** (`csdk\include` + `csdk\libc`): `stdio` (ไฟล์บน MyFS), โฟลเดอร์ (`opendir`/`readdir`, `mkdir`, `stat`, `chdir`, `getcwd`, `rename`), `malloc`, `printf`/`snprintf` (รวมทศนิยม), `strtod`, `string`, `ctype`, `math`, `setjmp`, `time` (+ `clock_gettime`, `gettimeofday`), `inttypes.h`, `fenv.h`, `alloca` ใช้ port โปรแกรม C มาตรฐานมาลง MyOS ได้ (Lua, QuickJS และ SQLite port มาด้วยวิธีนี้ ไม่ต้องแก้ซอร์สของเขาเลย) โปรแกรมได้หน่วยความจำ 2 MB + heap อีก 32 MB เพิ่ม port ใหม่ใน `$ports` ของ `arm\build.ps1`

ข้อจำกัด: Pi ไม่มีนาฬิกา `os.time()` / `time()` นับจาก 1 ม.ค. 2000 ตอนเปิดเครื่อง, ยังรันคำสั่ง shell จากในโปรแกรมไม่ได้ (`os.execute`)

**คลังแอปบน GitHub และลายเซ็น:**
- `python tools/repo.py publish` สร้างคลังแอป เซ็นรายการ แล้ว push ขึ้น branch `packages` (แทนที่ของเดิมทุกครั้ง) Pi เห็นของใหม่ภายในราว 5 นาที (GitHub cache)
- รายการแอป `index.txt` ถูกเซ็นด้วย Ed25519 (`index.sig`) MyOS มี public key ฝังอยู่ (`arm/repo_key.h`) และ**ไม่ยอมใช้รายการจากอินเทอร์เน็ตที่ลายเซ็นไม่ถูก** ส่วนแต่ละแพ็กเกจตรวจด้วย SHA-256 ที่อยู่ในรายการ (หลักเดียวกับ apt)
- key ลับอยู่**นอก repo** ที่ `~/.myos/repo-signing-key` (สร้างครั้งเดียวด้วย `python tools/repo.py keygen`) ใครจะ publish ต้องมีไฟล์นี้ ถ้าหายต้องสร้างใหม่แล้ว build MyOS ใหม่ให้ Pi รู้จัก key ใหม่
- HTTPS ของ MyOS เป็น TLS 1.3 ที่เขียนเอง (`arm/tls.c`: X25519 + ChaCha20-Poly1305) ไม่ตรวจใบรับรองของ server (ความปลอดภัยมาจากลายเซ็นข้างบน)
- เปลี่ยนแหล่งได้ด้วยไฟล์ `pkg.src` บน MyFS: `pc` = ใช้คอมผ่านสาย USB เสมอ, หรือ URL อื่นที่ลงท้ายด้วย `/`

เขียนแอปเพิ่มเองได้: วางไฟล์ `.py` ใน `apps\py\` ใส่บรรทัด `# @version 1.0` และ `# @description ...` แล้วเปิด `tools\repo.py` ใหม่ แอปใช้โมดูล `myos` ได้: `myos.key()` (อ่านปุ่มไม่ต้องรอ Enter), `myos.getkey()`, `myos.clear()`, `myos.move(x, y)`, `myos.cursor(on)`, `myos.ticks()`, `myos.procs()`, `myos.sysinfo()`, `myos.kill(pid)` ทดสอบแอปทั้งหมดใน QEMU ด้วย `python tests\qemu_apps.py`

**ไฟล์และโฟลเดอร์ (MyFS v2):**
```
ls [โฟลเดอร์]          ดูไฟล์ (โฟลเดอร์มี / ต่อท้าย)
cd apps                เข้าโฟลเดอร์ (cd .. ถอยขึ้น, cd เฉยๆ กลับไปที่ /)
pwd                    อยู่โฟลเดอร์ไหน (prompt ก็บอก เช่น myos:/apps>)
mkdir games            สร้างโฟลเดอร์ (mkdir -p a/b/c สร้างทั้งสาย)
rmdir games            ลบโฟลเดอร์ว่าง
cp a.txt /backup       คัดลอกไฟล์ (ปลายทางเป็นโฟลเดอร์ = ใส่ไว้ในนั้น)
mv a.txt b.txt         เปลี่ยนชื่อ หรือย้ายไฟล์/โฟลเดอร์ (mv a.txt /backup)
rm -r games            ลบโฟลเดอร์พร้อมทุกอย่างข้างใน
tree                   ดูทุกอย่างในโฟลเดอร์แบบต้นไม้
cat apps/x.py          ทุกคำสั่งใช้ path ได้: /apps/x.py, ../x.txt, ./x.txt
df                     พื้นที่ทั้งหมด / ที่ใช้ / ที่เหลือ
fsck                   ตรวจระบบไฟล์ (fsck fix = ลบ entry ที่เสีย)
```
- ชื่อยาวได้ 63 ตัวอักษร (ห้ามมีช่องว่าง), เก็บได้หลายพันไฟล์, ไฟล์ใหญ่ได้เท่าที่ว่าง, ลบหรือเขียนไฟล์ให้เล็กลงแล้วได้พื้นที่คืนทันที
- COM, telnet และ SSH แต่ละ session มีโฟลเดอร์ปัจจุบันของตัวเอง โปรแกรมที่รันจะเริ่มที่โฟลเดอร์นั้น (เช่น `cd games` แล้ว `python snake.py` เปิด `/games/snake.py`)
- พิมพ์ชื่อโปรแกรมเฉยๆ MyOS หาที่ `/` (ที่ `pkg` ติดตั้ง) ก่อน แล้วค่อยหาในโฟลเดอร์ปัจจุบัน แอป Python จาก `pkg` รันได้จากทุกโฟลเดอร์
- ไฟดับระหว่างเขียน: เสียอย่างมากแค่ไฟล์ที่กำลังเขียน (ส่วนใหญ่ได้ไฟล์เดิมคืน) เพราะเขียนข้อมูลลงที่ว่างก่อน แล้วค่อยเปลี่ยน entry ใน sector เดียว ตอนบูต MyOS ตรวจทุกโฟลเดอร์และคืนพื้นที่ที่ค้างไว้ให้เอง
- Python 1.4 ขึ้นไป (`pkg upgrade`): `os.listdir('/apps')`, `os.mkdir`, `os.rmdir`, `os.rename`, `os.stat`, `os.chdir`, `os.getcwd`, `open('apps/x.txt')`

**อัปเกรด MyFS เดิม (v1) เป็น v2** (การ์ดที่สร้างก่อนมี v2): MyFS v1 (สูงสุด 64 ไฟล์ ไม่มีโฟลเดอร์) ยังใช้ได้เหมือนเดิม ตอนบูตจะขึ้นว่า `old MyFS v1` เมื่อพร้อมแล้ว
1. (แนะนำ) สำรองไว้ก่อน: ถอดการ์ดใส่คอม คัดลอก `myfs.img` จากไดรฟ์ `bootfs` เก็บไว้ แล้วใส่การ์ดกลับ
2. อัปเดต MyOS เป็นรุ่นที่มี MyFS v2 (`arm\update-pi.ps1`) แล้วดูว่า `ls` เห็นไฟล์ครบ
3. พิมพ์ `fsupgrade` จะขึ้น `Done: N file(s) kept`
4. `ls` ดูว่าไฟล์ครบ และ `fsck` ต้องขึ้น `No problems found.` ต่อจากนี้ใช้ `mkdir`, `cd` ได้เลย
- ไม่มีการย้ายข้อมูลของไฟล์: MyOS เขียนตารางโฟลเดอร์กับ bitmap ใหม่ลงที่ว่าง แล้วเปลี่ยน sector แรกเป็นขั้นสุดท้าย ถ้าไฟดับก่อนขั้นนั้นก็ยังเป็น v1 เหมือนเดิม ถ้าที่ว่างไม่พอ (ไม่ถึง 20 KB) จะไม่แตะอะไรเลย ให้ลบไฟล์ออกสักไฟล์แล้วลองใหม่
- พื้นที่ที่ v1 ไม่เคยคืน (ไฟล์ที่ลบไปแล้ว) ได้คืนหมดหลังอัปเกรด
- หลังอัปเกรด MyOS PC (x86) และ kernel ของ Pi รุ่นก่อน v2 อ่าน `myfs.img` นี้ไม่ได้แล้ว ถ้าต้องย้อนกลับ ให้วาง `myfs.img` ที่สำรองไว้กลับลงการ์ด
- ทำบนคอมแทนก็ได้: `python tools\myfs.py myfs.img upgrade`

**ขยาย `myfs.img` ให้ใหญ่ขึ้น** (เช่น 256 MB): สร้างอันใหม่แล้วคัดลอกทุกอย่างจากอันเดิม (อันเดิมเป็น v1 หรือ v2 ก็ได้)
```powershell
python tools\myfs.py myfs-new.img create 256
python tools\myfs.py myfs-new.img import myfs.img
```
แล้ววางทับ `myfs.img` ในไดรฟ์ `bootfs` (ไฟล์ต้องเรียงต่อกันบนการ์ด ถ้าบูตแล้วขึ้น `myfs.img is in pieces` ให้ลบไฟล์อื่นที่ไม่ใช้หรือคัดลอกใหม่) ใหญ่ได้ถึง 4 GB แต่ไม่เกินที่ว่างของ partition `bootfs`

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
- heap (สูงสุด 32 MB ต่อโปรแกรม) ได้ทีละ 2 MB เมื่อใช้จริงเท่านั้น จากคลังรวมที่ใช้ RAM ทั้งหมดที่ firmware ให้ ARM (Pi Zero 2 W ประมาณ 384 MB, QEMU 896 MB)
- คีย์บอร์ดไปที่โปรแกรมข้างหน้าเท่านั้น โปรแกรมเบื้องหลังพิมพ์ออกจอได้แต่อ่านคีย์ไม่ได้
- ข้อจำกัด: Ctrl+C ใน Python จะปิด Python ทั้งตัว

**หน้าจอแยกต่อ session:** COM port, telnet และ SSH แต่ละ session เป็น terminal ของตัวเอง มี shell, คีย์บอร์ด และหน้าจอแยกกัน
- SSH เข้าได้พร้อมกัน 3 session (เปิดหลายหน้าต่าง `ssh myos@192.168.7.1` ได้เลย) อันที่ 4 จะถูกปฏิเสธ
- โปรแกรมพิมพ์ออกเฉพาะ session ที่รันมัน, Ctrl+C หยุดเฉพาะโปรแกรมของ session นั้น, `fg` ดึงได้เฉพาะงานของ session ตัวเอง
- `ps` เห็นทุกโปรแกรมของทุก session (คอลัมน์ TTY: serial / telnet / ssh1-3, `*` = session นี้)
- ปิดหน้าต่าง SSH หรือ `exit` แล้ว shell และโปรแกรมทั้งหมดของ session นั้นถูกปิดตาม

**Wi-Fi** (Pi Zero 2 W, ชิป CYW43436): ต้องมีไฟล์ firmware ใน MyFS ก่อน ดาวน์โหลดด้วย `tools\get-deps.ps1` (ไปอยู่ที่ `third_party\wifi-firmware\` ไม่เก็บใน git เพราะเป็นของ Broadcom/Cypress) แล้วส่งเข้า Pi:
```
python3 tools/pi.py push third_party/wifi-firmware/brcmfmac43436-sdio.bin third_party/wifi-firmware/brcmfmac43436-sdio.txt third_party/wifi-firmware/brcmfmac43436-sdio.clm_blob
```
(บน Windows ใช้ `arm\push-pi.ps1 ไฟล์...` ได้เหมือนกัน; บอร์ดบางตัวเป็นชิป rev 1 ต้องใช้ `brcmfmac43436s-sdio.bin` + `.txt` แทน `wifi -v on` จะบอกว่าขาดไฟล์ไหน)

คำสั่งบน MyOS:
```
wifi                         สถานะ: ชิป, firmware, MAC, เครือข่าย, IP, เครือข่ายที่จำไว้
wifi scan                    เครือข่ายรอบตัว (ชื่อ, ความแรงสัญญาณ, ช่อง, ระบบรักษาความปลอดภัย)
wifi connect ชื่อ รหัสผ่าน    เข้าเครือข่าย (ชื่อมีเว้นวรรคใส่ "ชื่อ มี เว้นวรรค") ต่อได้แล้วจำไว้ให้
wifi connect ชื่อ             เครือข่ายที่จำไว้แล้ว หรือเครือข่ายเปิด
wifi disconnect              ออกจากเครือข่าย
wifi forget ชื่อ              ลืมเครือข่ายที่จำไว้
wifi log                     log ของ firmware ในชิป Wi-Fi (ไว้หาสาเหตุเวลามีปัญหา)
wifi -v ...                  แสดงทุกขั้นตอน (-vv แสดงทุก frame)
ping 192.168.1.1             ส่ง ping 4 ครั้ง (ใช้ได้ทั้งทาง Wi-Fi และสาย USB)
wifiman                      แอปจัดการ Wi-Fi เต็มจอ (pkg install wifiman): ลูกศรเลือก, Enter เข้า, r สแกน, d ออก, f ลืม, q ปิด
```
- เครือข่ายที่จำไว้อยู่ในไฟล์ `wifi.cfg` บน MyFS (รหัสผ่านเก็บเป็นข้อความธรรมดา) เปิดเครื่องแล้ว MyOS จะต่อเครือข่ายที่จำไว้ซึ่งสัญญาณแรงที่สุดให้เองเบื้องหลัง ถ้า router ตัดการเชื่อมต่อ (รีสตาร์ท router, สัญญาณอ่อน) MyOS จะต่อเครือข่ายเดิมกลับเอง: ลองทุก 10 วินาที, หลังครั้งที่ 5 ทุก 1 นาที จนกว่าจะได้ หรือจนกว่าจะสั่ง wifi disconnect / connect / forget เอง (ทดสอบได้ด้วย wifi drop)
- รองรับ: เครือข่ายเปิด, WPA2-PSK, WPA-PSK (firmware ในชิปทำ 4-way handshake เอง) ยังไม่รองรับ WEP, WPA3 อย่างเดียว, 5 GHz (ชิปมีแค่ 2.4 GHz)
- ได้ IP จาก router ด้วย DHCP แล้ว Wi-Fi เป็นเส้นทางหลักออกเน็ต ส่วน 192.168.7.x ยังไปทางสาย USB เหมือนเดิม
- SSH เข้าทาง Wi-Fi ได้ (`ssh myos@<IP ของ Pi>`) แต่ telnet (ไม่มีรหัสผ่าน) และ DHCP/DNS server ของ MyOS ทำงานเฉพาะทางสาย USB
- แอป Python ใช้ `myos.wifi_status()`, `myos.wifi_scan()`, `myos.wifi_networks()`, `myos.wifi_connect(ชื่อ, รหัส)`, `myos.wifi_disconnect()`, `myos.wifi_forget(ชื่อ)`, `myos.wifi_saved()` ได้ (ต้องใช้ python 1.4 ขึ้นไป: `pkg upgrade`)
- ใน QEMU ไม่มีชิป Wi-Fi คำสั่ง `wifi` จะบอกว่า "No Wi-Fi chip found"

**pip: ไลบรารี Python จากอินเทอร์เน็ต** (`pkg install pip`, ต่อ Wi-Fi ก่อน): ดึงไลบรารีจาก **micropython-lib** (คลังเดียวกับที่คำสั่ง `mip` ของ MicroPython ใช้ ที่ micropython.org) มาไว้ในโฟลเดอร์ `/lib` แล้ว `import` ได้เลย
```
pip install datetime          ติดตั้ง (พร้อมตัวที่มันต้องใช้) ลงใน /lib
pip install datetime==4.0.0   เลือกรุ่น
pip list                      ที่ติดตั้งไว้
pip show datetime             รุ่น และไฟล์ที่ลงไว้
pip uninstall datetime        ลบ (ไฟล์ที่แพ็กเกจอื่นใช้ร่วมอยู่จะไม่ถูกลบ)
pip search date               ค้นในคลัง (pip search เฉยๆ = ทั้งหมด)
python                        แล้ว >>> import datetime
```
- `mip` (`pkg install mip`) คือชื่อเดียวกันแบบ MicroPython: `mip install logging` = `pip install logging`
- ใช้ได้: ไลบรารี Python ล้วนของ micropython-lib เช่น `datetime`, `logging`, `base64`, `fnmatch`, `functools`, `itertools`, `collections-defaultdict`, `unittest`, `copy`, `bisect`, `string` (ทดสอบใน QEMU แล้ว) ติดตั้งแบบซอร์ส `.py` (ไม่ใช่ `.mpy`)
- ใช้ไม่ได้: แพ็กเกจจาก PyPI ที่ทำมาสำหรับ CPython เช่น `numpy`, `pandas`, `flask` (หลายตัวมีส่วนที่เขียนด้วย C) เพราะ MyOS รัน **MicroPython** ไม่ใช่ CPython, pip จะบอกให้ ไลบรารีที่ต้องใช้ `socket` (เช่น `requests`, `umqtt.simple`) หรือฮาร์ดแวร์ของบอร์ดไมโครคอนโทรลเลอร์ (`machine`, `bluetooth`) ติดตั้งได้แต่ยังใช้ไม่ได้ (pip ขึ้น Note บอก) ส่วน `textwrap` ติด regex ที่ซับซ้อนเกินสำหรับ `re` ของ MicroPython
- ต้องใช้ python 1.7 ขึ้นไป (`pkg upgrade`): เพิ่ม `hashlib` (SHA-256), `binascii`, `re`, `time.gmtime/localtime/mktime`, `sys.stdout/stderr` และค้น `import` ใน `/lib` ด้วย ดิสก์ต้องเป็น MyFS v2 (มีโฟลเดอร์) ถ้ายังเป็น v1 pip จะบอกให้พิมพ์ `fsupgrade`
- MyOS ยังไม่มีนาฬิกาจริง: `time.localtime()` / `datetime.now()` นับจาก 1 ม.ค. 1970 ตอนเปิดเครื่อง (ดู 4.6.8 ใน PLAN)
- ความปลอดภัย: HTTPS ของ MyOS **ไม่ตรวจใบรับรอง** ของเซิร์ฟเวอร์ pip ตรวจ SHA-256 ของทุกไฟล์กับค่าในรายการของแพ็กเกจ (ไฟล์เสียหรือถูกแก้ระหว่างทางจะถูกปฏิเสธ) แต่ตัวรายการเองก็มาทาง HTTPS ที่ไม่ตรวจใบรับรองเหมือนกัน คนที่ดักกลางทางได้ (เช่น Wi-Fi สาธารณะ) จึงยังปลอมทั้งรายการและไฟล์ได้ ใช้บนเครือข่ายที่ไว้ใจ
- รายชื่อที่ติดตั้งเก็บใน `/lib/.pip.json`, ในโปรแกรมดาวน์โหลดเองได้ด้วย `myos.fetch(url)` → `(status, bytes)` และ `myos.download(url, path)`

**รันโปรแกรม Linux (BusyBox):** MyOS Pi รันโปรแกรม Linux ที่เป็นไฟล์ ELF แบบ static สำหรับ ARM64 (AArch64) ได้ โดยจำลอง system call ของ Linux (`arm\linux.c`) ตัวแรกที่ใช้ได้คือ BusyBox ซึ่งรวมคำสั่งแบบ Ubuntu กว่า 300 คำสั่งไว้ในไฟล์เดียว
```
pkg install busybox          ติดตั้ง (หรือ push-pi.ps1 -Packages แล้ว pkg install busybox)
vi note.txt                  พิมพ์ชื่อคำสั่งของ BusyBox ได้เลย = busybox vi note.txt
grep -n main prog.c          grep, sed, awk, sort, uniq, head, tail, wc, md5sum, find, tar ...
sh                           shell แบบ Linux (ash): pipe |, > ไฟล์, $(...), for/while, Ctrl+C
sh -c 'ls | wc -l'           รันคำสั่ง shell บรรทัดเดียวจาก shell ของ MyOS
busybox ls -l                คำสั่งที่ชื่อซ้ำกับคำสั่งของ MyOS (ls, cat, echo, rm, ps, kill ...) ต้องนำหน้าด้วย busybox
top / free / df / busybox ps ดูโปรเซส หน่วยความจำ ดิสก์ (อ่านจาก /proc ที่ MyOS จำลองให้)
strace PROG ...              รันโปรแกรม Linux แล้วแสดง system call ทุกตัว (ไว้หาว่าขาดอะไร)
```
- ไฟล์ BusyBox มาจากแพ็กเกจ `busybox-static` ของ Alpine Linux: `tools\get-deps.ps1` ดาวน์โหลดและตรวจ SHA-256 ให้ แล้ว `arm\build.ps1` ทำเป็น `busybox.pkg` (แพ็กเกจชนิดใหม่ "linux") ไฟล์ ELF อื่นทำแพ็กเกจได้ด้วย `python tools\mkpkg.py โปรแกรม ไฟล์.info ชื่อ.pkg`
- โปรแกรม Linux ใช้โฟลเดอร์ของ MyFS v2 ได้ (`mkdir`, `cd`, `mv`, `rm -r`, `find` ...) และเริ่มในโฟลเดอร์ปัจจุบันของ shell; `/bin/ls`, `/usr/bin/ls` ที่ไม่มีอยู่จริงหมายถึง `/ls` (ที่ pkg ติดตั้ง) หรือ BusyBox, `/tmp` ถูกสร้างให้เมื่อใช้ครั้งแรก, `/dev/null`, `/dev/tty`, `/proc` มีให้
- โปรแกรมที่รันล่าสุด (เช่น busybox) ถูกเก็บสำเนาไว้ใน RAM จึงเริ่มเร็ว (ตรวจกับไฟล์ทุกครั้ง)
- ไฟล์ที่โปรแกรมเปิดเขียนจะอยู่ใน RAM ก่อน แล้วบันทึกลง MyFS ตอนปิดไฟล์ (สูงสุด 32 MB ต่อไฟล์)
- ข้อจำกัด: รันได้เฉพาะโปรแกรม static (ไฟล์จาก `apt` ส่วนใหญ่เป็น dynamic ใช้ glibc + ไลบรารี `.so` ยังรันไม่ได้), ไม่มี thread, ไม่มี socket/network สำหรับโปรแกรม Linux, ผู้ใช้เป็น root (uid 0) คนเดียว, นาฬิกาเริ่มที่ปี 1970 (ตั้งด้วย `date -s` ได้)
- ทดสอบใน QEMU: `python tests\qemu_linux.py` (ตัวโหลด ELF, system call ด้วย `tests\linux\lxtest.c`, BusyBox, sh, vi, signal, /proc)

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
`tools\myfs.py` ใช้กับ `myfs.img` ของ Pi ได้ด้วย (อ่านเขียนได้ทั้ง v1 และ v2): `ls /apps`, `tree`, `mkdir /apps`, `put game.py /apps/`, `mv`, `rm`, `rmdir`, `info`, `fsck`, `upgrade`, `import อันเดิม.img` (MyOS PC ยังใช้ MyFS v1 `format` ของ disk.img เปล่าจึงได้ v1 เสมอ)

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
| [TinyUSB](https://github.com/hathach/tinyusb) | USB serial และ USB network บน MyOS Pi | MIT |
| [broadcom-peripherals](https://github.com/adafruit/broadcom-peripherals) | โค้ดบูต, MMU, interrupt ของ Pi | MIT |
| [lwIP](https://github.com/lwip-tcpip/lwip) | TCP/IP (ping, telnet, SSH, `pkg`) | BSD 3-clause |
| [Monocypher](https://github.com/LoupVaillant/Monocypher) | การเข้ารหัสของ SSH | BSD 2-clause หรือ CC0 |
| Raspberry Pi firmware | ไฟล์บูตของ Pi | Broadcom (แจกจ่ายได้) |
| Wi-Fi firmware (RPi-Distro `firmware-nonfree`) | firmware ของชิป Wi-Fi | Cypress/Broadcom (แจกจ่ายได้) |
| [Lua](https://github.com/lua/lua) | แพ็กเกจ `lua` | MIT |
| [QuickJS](https://github.com/bellard/quickjs) | แพ็กเกจ `node` (JavaScript) | MIT |
| [SQLite](https://www.sqlite.org) | แพ็กเกจ `sqlite3` | Public domain |
| [BusyBox](https://busybox.net/) (`busybox-static` จาก Alpine Linux) | คำสั่งแบบ Linux บน MyOS Pi | GPL-2.0 |

ซอร์สเหล่านี้ไม่ได้อยู่ใน repo นี้ (`tools/get-deps.ps1` ดาวน์โหลดให้) แต่ละตัวใช้สัญญาอนุญาตของตัวเอง และแพ็กเกจที่ build จากมัน เช่น `busybox.pkg` (GPL-2.0) ก็อยู่ใต้สัญญาอนุญาตนั้นด้วย

## สัญญาอนุญาต

MyOS ใช้สัญญาอนุญาต **MIT** (ดู [`LICENSE`](LICENSE)): นำไปใช้ แก้ไข หรือแจกต่อได้ รวมถึงใช้ในงานเชิงพาณิชย์ ขอแค่คงข้อความลิขสิทธิ์และสัญญาอนุญาตไว้

โค้ดบางส่วนใน `arm/net.c` และ `arm/usb.c` ดัดแปลงจากตัวอย่างของ TinyUSB (MIT) รายละเอียดและข้อความลิขสิทธิ์เดิมอยู่ใน [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md)
