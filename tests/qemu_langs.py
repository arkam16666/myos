"""Test the programming-language packages in QEMU (Raspberry Pi 3B).

Installs each language from its package and runs small programs in it.

usage: python tests/qemu_langs.py [LANG ...]
"""
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import mkpkg  # noqa: E402
import mksdimg  # noqa: E402

QEMU = (shutil.which("qemu-system-aarch64")
        or r"C:\Program Files\qemu\qemu-system-aarch64.exe")
def _free_port():           # a port nobody uses, so several test runs can go at once
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


PORT = int(os.environ.get("MYOS_TEST_PORT", 0)) or _free_port()

# language -> list of (command, [lines typed after it], [text expected])
TESTS = {
    "lua": [
        ('lua -e "print(1/3, 0.1, 2^53, 10//3, 7 % 3, math.pi)"', [],
         ["0.33333333333333\t0.1\t9.007199254741e+15\t3\t1\t3.1415926535898"]),
        ("lua -e \"print(string.format('%5.2f|%g|%e|%d|%x|%s', 3.14159, 1e20, 12345.678, 42, 255, 'hi'))\"", [],
         [" 3.14|1e+20|1.234568e+04|42|ff|hi"]),
        ("lua -e \"print(tonumber('0x10'), tonumber('1e3'), tonumber('  12.5  '), 1e300*1e10, -0.0)\"", [],
         ["16\t1000.0\t12.5\tinf\t-0.0"]),
        ("lua -e \"local t={} for i=1,10 do t[i]=i*i end print(#t, table.concat(t,','))\"", [],
         ["10\t1,4,9,16,25,36,49,64,81,100"]),
        ("lua -e \"local function fib(n) if n<2 then return n end return fib(n-1)+fib(n-2) end print(fib(20))\"", [],
         ["6765"]),
        ("lua -e \"print(('hello world'):upper(), ('abc'):rep(3,'-'), ('x=1,y=2'):gsub('%w+=(%w+)','%1'))\"", [],
         ["HELLO WORLD\tabc-abc-abc\t1,2\t2"]),
        ("lua -e \"local f=io.open('t.lua','w') f:write('print(\\'from file\\', ...)') f:close()\"", [], []),
        ("lua t.lua a b", [], ["from file\ta\tb"]),
        ("lua -e \"for l in io.lines('t.lua') do print(#l) end print(os.time() > 946684800, type(os.clock()))\"", [],
         ["23", "true\tnumber"]),
        ('lua -e "error(\'boom\')"', [], ["boom", "stack traceback"]),
        ("lua -e \"local co=coroutine.wrap(function() for i=1,3 do coroutine.yield(i) end end) print(co(),co(),co())\"", [],
         ["1\t2\t3"]),
        ("lua -e \"local t={} for i=1,200000 do t[i]=tostring(i) end collectgarbage() print(#t, t[123456])\"", [],
         ["200000\t123456"]),
        ("lua", ["x = 6 * 7", "print(x)", "= x + 1", "os.exit()"], ["Lua 5.4.7", "42", "43"]),
    ],
    "basic": [
        ("basic", ["10 FOR I = 1 TO 3", "20 PRINT I * I;", "30 NEXT I", "40 PRINT", "50 GOSUB 100",
                   "60 END", "100 PRINT LEFT$(\"HELLO\", 2); LEN(\"ABC\")", "110 RETURN", "RUN",
                   "SAVE \"sq.bas\"", "BYE"], [" 1  4  9", "HE 3", "Saved sq.bas"], b"]"),
        ("basic sq.bas", [], [" 1  4  9", "HE 3"]),
        ("basic", ["PRINT 10 / 0", "PRINT 2 ^ 10, INT(-2.5)", "BYE"],
         ["division by zero", " 1024", "-3"], b"]"),
    ],
    "scheme": [
        ("scheme", ["(define (fact n) (if (= n 0) 1 (* n (fact (- n 1)))))", "(fact 25)",
                    "(define (loop i acc) (if (= i 0) acc (loop (- i 1) (+ acc i))))", "(loop 20000 0)",
                    "(map (lambda (x) (* x x)) '(1 2 3))", "(car '(a b c))", "(undefined-thing)", "(exit)"],
         ["15511210043330985984000000", "200010000", "(1 4 9)", "a", "unbound variable"], b">"),
    ],
    "forth": [
        ("forth", ["2 3 + .", ": square dup * ;  7 square .", ": count 5 0 do i . loop ; count",
                   "variable x 10 x ! 5 x +! x @ .", "foo", "bye"],
         ["5  ok", "49  ok", "0 1 2 3 4  ok", "15  ok", "unknown word: foo"], b"ok"),
    ],
    "node": [
        ("node -v", [], ["(QuickJS)"]),
        ("node -e \"console.log('hello', 1+2, {a:1, b:[1,2,3]}, [1,'x'], {}, 0.1+0.2)\"", [],
         ['hello 3 { a: 1, b: [ 1, 2, 3 ] } [ 1, "x" ] {} 0.30000000000000004']),
        ('node -p "[Math.max(3, 9, 2), Math.PI.toFixed(3), 2 ** 0.5, 7 % 3, Math.round(-2.5)]"', [],
         ['[ 9, "3.142", 1.4142135623730951, 1, -2 ]']),
        ("node -e \"class A { #x = 5; get x() { return this.#x } } class B extends A { toString() { return 'B' + this.x } }"
         " console.log(String(new B()), JSON.stringify({q: [1, {r: 2}]}), Object.entries({k: 1}))\"", [],
         ['B5 {"q":[1,{"r":2}]} [ [ "k", 1 ] ]']),
        ("node -e \"const o = []; setTimeout(() => { o.push('C'); console.log(o.join('')) }, 50);"
         " Promise.resolve().then(() => o.push('B')); o.push('A')\"", [], ["ABC"]),
        ("node -e \"(async () => { await new Promise(r => setTimeout(r, 30)); console.log('async', 2n ** 70n) })()\"", [],
         ["async 1180591620717411303424n"]),
        ("node -e \"require('fs').writeFileSync('t.js', 'const l = require(`./lib`); console.log(l.hi(process.argv[2]), process.argv.length)')\"",
         [], []),
        ("node -e \"require('fs').writeFileSync('lib.js', 'exports.hi = n => `hi ${n}`')\"", [], []),
        ("node t.js bob x", [], ["hi bob 4"]),
        ("node -e \"const q = JSON.stringify; require('fs').writeFileSync('m.mjs', 'import { sq } from ' + q('./sq.mjs') +"
         " '; import { existsSync } from ' + q('node:fs') + '; console.log(sq(9), existsSync(`m.mjs`)); await null; console.log(`tla`)')\"",
         [], []),
        ("node -e \"require('fs').writeFileSync('sq.mjs', 'export const sq = x => x * x')\"", [], []),
        ("node m.mjs", [], ["81 true", "tla"]),
        ("node -e \"function r(n) { return n ? r(n - 1) + 1 : 0 } console.log(r(2000)); try { r(1e6) } catch (e) { console.log(e.message) }\"",
         [], ["2000", "stack overflow"]),
        ("node -e \"let a = []; for (let i = 0; i < 100000; i++) a.push({i}); console.log(a.length, a[99999].i)\"", [],
         ["100000 99999"]),
        ("node -e \"null.x\"", [], ["Uncaught TypeError", "Program exited with code 1"]),
        ("node -e \"console.log('Hi ' + prompt('Name? '))\"", ["Arka"], ["Hi Arka"], b"?"),
        ("node", ["let x = 6 * 7", "x + 1", "[1, 2].map(n => n * x)", "await new Promise(r => setTimeout(() => r('done'), 20))",
                  "nope()", ".exit"],
         ["Welcome to JavaScript", "43", "[ 42, 84 ]", '"done"', "Uncaught ReferenceError"]),
    ],
}


def main():
    only = sys.argv[1:]
    tmp = tempfile.mkdtemp(prefix="myos-langtest-")
    langs = [n for n in TESTS if not only or n in only]
    files = []
    for n in langs:
        port = os.path.join(ROOT, "arm", "build", "packages", n + ".pkg")
        if os.path.exists(port):
            files.append(port)
        else:                                       # a language written in Python
            src = os.path.join(ROOT, "apps", "py", n + ".py")
            out = os.path.join(tmp, n + ".pkg")
            with open(out, "wb") as f:
                f.write(mkpkg.make_package(src, src))
            files.append(out)
            if os.path.join(ROOT, "arm", "build", "packages", "python.pkg") not in files:
                files.append(os.path.join(ROOT, "arm", "build", "packages", "python.pkg"))
    card = os.path.join(tmp, "sd.img")
    image = "arm/build/kernel8-qemu.img"
    mksdimg.build(card, files, kernel=os.path.join(ROOT, image))

    qemu = subprocess.Popen([QEMU, "-M", "raspi3b", "-kernel", image, "-display", "none",
                             "-monitor", "none", "-drive", f"file={card},if=sd,format=raw",
                             "-serial", f"tcp:127.0.0.1:{PORT},server=on,wait=on"],
                            cwd=ROOT, stderr=subprocess.DEVNULL)
    for _ in range(50):
        try:
            s = socket.create_connection(("127.0.0.1", PORT))
            break
        except OSError:
            time.sleep(0.2)
    s.settimeout(0.1)

    def read_until_prompt(seconds, prompt=b"myos>"):
        data = b""
        end = time.time() + seconds
        while time.time() < end:
            try:
                data += s.recv(65536)
            except socket.timeout:
                if data.rstrip().endswith(prompt):
                    break
        return data.decode("utf-8", "replace")

    def send_line(text):
        for ch in text:
            s.sendall(ch.encode())
            time.sleep(0.01)
        s.sendall(b"\r")

    failed = 0
    try:
        read_until_prompt(8)
        for lang in langs:
            send_line("pkg install " + lang)
            out = read_until_prompt(60)
            if "Done." not in out:
                print(f"FAIL  {lang}  install   got: {out[-300:]!r}")
                failed += 1
                continue
            for i, test in enumerate(TESTS[lang], 1):
                cmd, typed, expect = test[:3]
                prompt = test[3] if len(test) > 3 else b">"
                send_line(cmd)
                out = ""
                for t in typed:
                    out += read_until_prompt(10, prompt)
                    send_line(t)
                out += read_until_prompt(60)
                missing = [e for e in expect if e not in out]
                back = out.rstrip().endswith("myos>")
                if missing or not back:
                    failed += 1
                    print(f"FAIL  {lang} #{i}  {cmd[:50]}\n      missing={missing} back={back}\n"
                          f"      got: {out[-600:]!r}")
                else:
                    print(f"PASS  {lang} #{i}  {cmd[:60]}")
    finally:
        s.close()
        qemu.kill()
        qemu.wait()
    print("ALL LANGUAGE TESTS PASSED" if not failed else f"{failed} language test(s) failed")
    return failed


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
