"""Check npm's pieces on the PC (no QEMU needed):
  - ports/node/npmlib.c (gunzip, SHA-512, SHA-1, JSON scan), built with the PC's C
    compiler (gcc or clang) and compared with Python's zlib / hashlib / json
  - ports/npm/npm.js's semver, spec and tar logic, run by the PC's node (if installed)

usage: python tests/host/npm_host_test.py
"""
import gzip
import hashlib
import io
import json
import os
import random
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

DRIVER = r"""
#include <stdio.h>
#include <stdlib.h>
#include "npmlib.c"
static unsigned char *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(*n + 1); fread(b, 1, *n, f); fclose(f); return b;
}
static int pr(const char *key, size_t len, size_t value, void *arg) {
    (void)value; (void)arg; printf("%.*s\n", (int)len, key); return 0;
}
int main(int argc, char **argv) {
    size_t n; unsigned char *in = slurp(argv[2], &n), d[64];
    if (argv[1][0] == 'z') {                        /* gunzip IN OUT */
        long size = npm_gzip_size(in, n);
        if (size < 0) { printf("notgz\n"); return 0; }
        unsigned char *out = malloc(size + 1);
        long r = npm_gunzip(in, n, out, (size_t)size);
        if (r < 0) { printf("err %ld\n", r); return 0; }
        FILE *f = fopen(argv[3], "wb"); fwrite(out, 1, (size_t)r, f); fclose(f);
        printf("ok %ld\n", r);
    } else if (argv[1][0] == 's') {                 /* sha512 / sha1 IN */
        int one = argv[1][3] == '1';
        if (one) npm_sha1(in, n, d); else npm_sha512(in, n, d);
        for (int i = 0; i < (one ? 20 : 64); i++) printf("%02x", d[i]);
        printf("\n");
    } else {                                        /* json IN key key ... (keys of the last if it starts with +) */
        size_t at = 0;
        int keys = 0;
        for (int i = 3; i < argc && at != JNONE; i++) {
            const char *k = argv[i];
            if (k[0] == '+') { keys = 1; k++; }
            at = npm_json_get((char *)in, n, at, k);
        }
        if (at == JNONE) { printf("NONE\n"); return 0; }
        if (keys) {
            npm_json_members((char *)in, n, at, pr, NULL);      /* the member names, one per line */
        } else {
            size_t end = npm_json_skip((char *)in, n, at);
            fwrite(in + at, 1, end - at, stdout);
            printf("\n");
        }
    }
    return 0;
}
"""

failed = 0


def check(name, ok, detail=""):
    global failed
    print(("PASS  " if ok else "FAIL  ") + name + ("" if ok else "   " + detail))
    if not ok:
        failed += 1


def test_c(tmp):
    cc = shutil.which("gcc") or shutil.which("clang")
    if not cc:
        print("SKIP  npmlib.c (no gcc/clang on PATH)")
        return
    drv = os.path.join(tmp, "drv.c")
    open(drv, "w").write(DRIVER)
    exe = os.path.join(tmp, "drv.exe")
    subprocess.check_call([cc, "-O1", "-I" + os.path.join(ROOT, "ports", "node"), drv, "-o", exe])

    def run(*a):
        return subprocess.run([exe, *a], capture_output=True, text=True).stdout

    rnd = random.Random(7)
    samples = [b"", b"a", b"abc", bytes(range(256)) * 40, os.urandom(100000),
               ("".join(rnd.choice("ab cd\n{}") for _ in range(300000))).encode(),
               open(os.path.join(ROOT, "ports", "node", "prelude.js"), "rb").read()]
    for i, data in enumerate(samples):
        src = os.path.join(tmp, "s%d" % i)
        open(src, "wb").write(data)
        check("sha512 #%d (%d bytes)" % (i, len(data)), run("sha512", src).strip() == hashlib.sha512(data).hexdigest())
        check("sha1   #%d" % i, run("sha1", src).strip() == hashlib.sha1(data).hexdigest())
        for level in (0, 1, 6, 9):
            gz = os.path.join(tmp, "g%d_%d.gz" % (i, level))
            raw = zlib.compressobj(level, zlib.DEFLATED, 31)          # gzip wrapper
            open(gz, "wb").write(raw.compress(data) + raw.flush())
            outp = gz + ".out"
            r = run("z", gz, outp).strip()
            got = open(outp, "rb").read() if r.startswith("ok") else None
            check("gunzip #%d level %d" % (i, level), got == data, r)
    # gzip.compress with a file name in the header (FNAME), and a damaged one
    gz = os.path.join(tmp, "named.gz")
    buf = io.BytesIO()
    with gzip.GzipFile("package.tar", "wb", fileobj=buf) as g:
        g.write(samples[6])
    open(gz, "wb").write(buf.getvalue())
    check("gunzip with FNAME", run("z", gz, gz + ".out").startswith("ok") and open(gz + ".out", "rb").read() == samples[6])
    bad = bytearray(buf.getvalue())
    bad[len(bad) // 2] ^= 0x55
    open(gz, "wb").write(bad)
    check("gunzip damaged -> error", run("z", gz, gz + ".out").startswith("err"))
    # JSON scan on a packument-like document
    doc = {"_id": "x", "dist-tags": {"latest": "1.2.0"}, "name": "x\"q",
           "versions": {"1.0.0": {"v": "1.0.0", "s": "a}b{c\"]", "n": [1, {"z": None}]}, "1.2.0": {"dist": {"tarball": "t"}}},
           "time": {"1.0.0": "2020"}}
    js = os.path.join(tmp, "doc.json")
    open(js, "w").write(json.dumps(doc, indent=1))
    check("json keys", run("j", js, "+versions").split() == ["1.0.0", "1.2.0"])
    check("json pick", json.loads(run("j", js, "versions", "1.0.0")) == doc["versions"]["1.0.0"])
    check("json pick 2", json.loads(run("j", js, "dist-tags")) == {"latest": "1.2.0"})
    check("json missing", run("j", js, "versions", "9.9.9").strip() == "NONE")


def test_js(tmp):
    node = shutil.which("node")
    if not node:
        print("SKIP  npm.js (no node on PATH)")
        return
    # a small tarball for tarEntries
    tgz = os.path.join(tmp, "p.tar")
    with tarfile.open(tgz, "w", format=tarfile.GNU_FORMAT) as t:
        for name, data in (("package/package.json", b'{"name":"p"}'), ("package/lib/" + "x" * 120 + ".js", b"1")):
            ti = tarfile.TarInfo(name)
            ti.size = len(data)
            t.addfile(ti, io.BytesIO(data))
    script = r"""
const fs = require('fs');
const N = { text: (b, s, e) => Buffer.from(b.buffer, b.byteOffset, b.length).toString('utf8', s, e) };
const npm = eval(fs.readFileSync(process.argv[2], 'utf8'))(N, true);
const cases = JSON.parse(fs.readFileSync(process.argv[3], 'utf8'));
const out = cases.map(([v, r]) => npm.satisfies(v, r));
const versions = ['0.9.0', '1.0.0', '1.2.3', '1.2.4-beta.1', '1.3.0', '1.10.2', '2.0.0-rc.1', '2.0.0', '2.1.0'];
const maxes = ['^1.2.0', '~1.2.0', '1.x', '*', '>=2.0.0-0', '<1.2.3', '^3', '1.2.3 - 1.3', '>1.2 <2', '^2.0.0-rc.0', 'latest'].map(r => npm.maxSatisfying(versions, r));
const tar = new Uint8Array(fs.readFileSync(process.argv[4]));
const ents = npm.tarEntries(tar).map(e => e.rel);
const specs = ['left-pad', 'left-pad@1.3.0', '@babel/core@^7', './x.tgz', 'file:a/b.tgz'].map(s => npm.parseSpec(s));
console.log(JSON.stringify({ out, maxes, ents, specs, hex: npm.b64hex('XI5MPzVNApjAyhQ=') }));
"""
    cases = [["1.2.3", "^1.2.0", True], ["2.0.0", "^1.2.0", False], ["0.2.5", "^0.2.3", True], ["0.3.0", "^0.2.3", False],
             ["0.0.4", "^0.0.3", False], ["0.0.3", "^0.0.3", True], ["0.9.0", "^0.x", True], ["1.2.9", "~1.2.3", True],
             ["1.3.0", "~1.2.3", False], ["1.9.0", "~1", True], ["1.2.3", "1.2.3", True], ["1.2.4", "1.2.3", False],
             ["1.2.4", "1.2.x", True], ["1.3.0", "1.2.*", False], ["5.0.0", "*", True], ["5.0.0", "", True],
             ["1.5.0", ">=1.2.0 <2.0.0", True], ["2.0.0", ">=1.2.0 <2.0.0", False], ["1.5.0", "1.2 - 1.6", True],
             ["1.6.9", "1.2 - 1.6", True], ["1.7.0", "1.2 - 1.6", False], ["3.0.0", "^1.0.0 || ^3.0.0", True],
             ["2.5.0", "^1.0.0 || ^3.0.0", False], ["1.2.4-beta", "^1.2.0", False], ["1.2.4-beta.2", ">=1.2.4-beta.1", True],
             ["1.3.0", ">1.2", True], ["1.2.9", ">1.2", False], ["1.2.9", "<=1.2", True], ["1.3.0", "<=1.2", False],
             ["1.0.0", "v1.0.0", True], ["1.0.0", "= 1.0.0", True], ["2.3.4", ">= 2.0.0", True], ["1.0.0", "latest", True],
             ["1.0.0-rc.1", "1.0.0-rc.1", True], ["1.0.0", "1.0.0-rc.1", False]]
    cf = os.path.join(tmp, "cases.json")
    json.dump([c[:2] for c in cases], open(cf, "w"))
    sf = os.path.join(tmp, "t.js")
    open(sf, "w").write(script)
    r = subprocess.run([node, sf, os.path.join(ROOT, "ports", "npm", "npm.js"), cf, tgz], capture_output=True, text=True)
    if r.returncode:
        check("npm.js runs on the PC", False, r.stderr[-800:])
        return
    res = json.loads(r.stdout)
    for (v, rng, want), got in zip(cases, res["out"]):
        check("satisfies(%s, %r) = %s" % (v, rng, want), got == want)
    want_max = ["1.10.2", "1.2.3", "1.10.2", "2.1.0", "2.1.0", "1.0.0", None, "1.3.0", "1.10.2", "2.1.0", "2.1.0"]
    check("maxSatisfying", res["maxes"] == want_max, str(res["maxes"]))
    check("tar entries (GNU long name)", res["ents"] == ["package.json", "lib/" + "x" * 120 + ".js"], str(res["ents"]))
    check("specs", res["specs"] == [{"name": "left-pad", "range": "latest"}, {"name": "left-pad", "range": "1.3.0"},
                                    {"name": "@babel/core", "range": "^7"}, {"file": "./x.tgz"}, {"file": "a/b.tgz"}], str(res["specs"]))
    check("base64 -> hex", res["hex"] == "5c8e4c3f354d0298c0ca14")


NET_SCRIPT = r"""
// npm.js on the PC's node against the real registry: N's helpers done with curl, zlib, crypto
const fs = require('fs'), zlib = require('zlib'), crypto = require('crypto'), cp = require('child_process');
const N = {
    fetch(url, max) {
        const r = cp.spawnSync('curl', ['-s', '-L', '-w', '\n%{http_code}', url], { maxBuffer: 64 << 20 });
        if (r.status) return { status: 0, error: -2 };
        const b = r.stdout, nl = b.lastIndexOf(10);
        const body = b.subarray(0, nl);
        if (body.length > max) return { status: 0, error: -4 };
        return { status: +b.subarray(nl + 1).toString(), error: 0, body: new Uint8Array(body).buffer };
    },
    gunzip: (b) => new Uint8Array(zlib.gunzipSync(Buffer.from(b instanceof ArrayBuffer ? new Uint8Array(b) : b))).buffer,
    hash: (alg, b) => crypto.createHash(alg).update(b instanceof ArrayBuffer ? new Uint8Array(b) : b).digest('hex'),
    text: (b, s, e) => Buffer.from(b instanceof ArrayBuffer ? new Uint8Array(b) : b).toString('utf8', s, e),
    jsonPick(b, keys, names) {
        let v = JSON.parse(N.text(b));
        for (const k of keys) { if (v == null || !(k in v)) return undefined; v = v[k]; }
        return names ? Object.keys(v) : JSON.stringify(v);
    },
    writeFile: (name, data) => fs.writeFileSync(name, data),
    detectModule: () => false,
};
globalThis.performance = performance;
process.chdir(process.argv[3]);
const run = eval(fs.readFileSync(process.argv[2], 'utf8'));
process.argv = ['node', 'npm', ...process.argv.slice(4)];
run(N);
"""


def test_net(tmp):
    """npm.js with the real registry (on the PC: needs node, curl and the internet)"""
    node = shutil.which("node")
    if not node or not shutil.which("curl"):
        print("SKIP  --net (needs node and curl)")
        return
    sf = os.path.join(tmp, "net.js")
    open(sf, "w").write(NET_SCRIPT)
    proj = os.path.join(tmp, "proj")
    os.mkdir(proj)
    npmjs = os.path.join(ROOT, "ports", "npm", "npm.js")

    def npm(*a):
        r = subprocess.run([node, sf, npmjs, proj, *a], capture_output=True, text=True, encoding="utf-8")
        return r.stdout + r.stderr

    npm("init", "-y")
    out = npm("install", "left-pad", "ms@^2.0.0", "debug@~4.3.0", "chalk@^4")
    print(out)
    pkg = json.load(open(os.path.join(proj, "package.json")))
    check("net: install wrote package.json", set(pkg["dependencies"]) == {"left-pad", "ms", "debug", "chalk"}, str(pkg))
    ver = lambda n: json.load(open(os.path.join(proj, "node_modules", n, "package.json")))["version"]
    check("net: ms ^2", ver("ms").startswith("2."), ver("ms"))
    check("net: debug ~4.3", ver("debug").startswith("4.3."), ver("debug"))
    check("net: chalk's deps", os.path.exists(os.path.join(proj, "node_modules", "supports-color", "package.json")))
    r = subprocess.run([node, "-e", "console.log(require('left-pad')('x', 3), require('chalk').red.name || 'ok')"],
                       cwd=proj, capture_output=True, text=True)
    check("net: the PC's node can require them", r.stdout.startswith("  x"), r.stdout + r.stderr)
    out = npm("view", "express@4")
    check("net: view express@4", "express@4." in out and "dependencies:" in out, out[-300:])
    out = npm("ls", "--all")
    print(out)
    check("net: ls --all", "chalk@4." in out and "has-flag@" in out, out)
    out = npm("uninstall", "chalk")
    check("net: uninstall prunes", "removed" in out and not os.path.exists(os.path.join(proj, "node_modules", "has-flag")), out)
    out = npm("install", "bcrypt")
    check("net: native addon refused", "needs a C++ build" in out, out)


def main():
    tmp = tempfile.mkdtemp(prefix="myos-npmhost-")
    try:
        test_c(tmp)
        test_js(tmp)
        if "--net" in sys.argv:
            test_net(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("ALL NPM HOST TESTS PASSED" if not failed else "%d npm host test(s) failed" % failed)
    return failed


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
