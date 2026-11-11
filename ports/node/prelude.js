// prelude.js - the JavaScript half of 'node' on MyOS (built into the program).
// node.c runs this once at start-up and calls the function with N, the native
// helpers: write, inspect, readFile, writeFile, fileSize, unlink, stat, readdir,
// mkdir, rmdir, chdir, rename, cwd, evalScript, evalModule, detectModule,
// readLine, setTimer, clearTimer, exit, ticks, sysinfo, heap, gc (and for npm:
// fetch, gunzip, hash, text, jsonPick).
(function (N) {
"use strict";
const g = globalThis;
const NL = String.fromCharCode(10);

function define(obj, name, value) {
    Object.defineProperty(obj, name, { value, writable: true, configurable: true, enumerable: false });
}

// ---------------------------------------------------------------- util
function inspect(v, opts) {
    return N.inspect(v, opts && typeof opts.depth === "number" ? opts.depth : undefined);
}

// console.log("%s has %d items", name, n) like Node's util.format
function format(...args) {
    const parts = [];
    let i = 0;
    if (typeof args[0] === "string") {
        const f = args[0];
        let s = "";
        i = 1;
        for (let k = 0; k < f.length; k++) {
            const c = f[k];
            const d = f[k + 1];
            if (c === "%" && d === "%") { s += "%"; k++; continue; }
            if (c === "%" && d !== undefined && "sdifjoOc".includes(d) && i < args.length) {
                const a = args[i++];
                k++;
                if (d === "s") s += typeof a === "string" ? a : typeof a === "object" && a !== null ? inspect(a, { depth: 1 }) : String(a);
                else if (d === "d") s += typeof a === "bigint" ? a + "n" : String(Number(a));
                else if (d === "i") s += typeof a === "bigint" ? a + "n" : String(parseInt(a));
                else if (d === "f") s += String(parseFloat(a));
                else if (d === "j") { try { s += JSON.stringify(a); } catch (e) { s += "[Circular]"; } }
                else if (d === "o" || d === "O") s += inspect(a);
                continue;                               // %c: CSS, nothing to show here
            }
            s += c;
        }
        parts.push(s);
    }
    for (; i < args.length; i++) parts.push(typeof args[i] === "string" ? args[i] : inspect(args[i]));
    return parts.join(" ");
}

const util = {
    format, inspect,
    isDeepStrictEqual: (a, b) => inspect(a, { depth: 20 }) === inspect(b, { depth: 20 }),
    promisify: (fn) => (...a) => new Promise((ok, bad) => fn(...a, (err, v) => (err ? bad(err) : ok(v)))),
    types: { isPromise: (v) => v instanceof Promise, isDate: (v) => v instanceof Date, isRegExp: (v) => v instanceof RegExp },
    // what npm packages often use
    inherits: (c, p) => { Object.setPrototypeOf(c.prototype, p.prototype); Object.setPrototypeOf(c, p); c.super_ = p; },
    deprecate: (fn) => fn,
    debuglog: () => () => {},
};

// ---------------------------------------------------------------- console
let indent = "";
const counts = {}, started = {};
const now = () => N.ticks() * 10;
function say(args) {
    let s = format(...args);
    if (indent) s = indent + s.split(NL).join(NL + indent);
    N.write(1, s + NL);
}
const console = {
    log: (...a) => say(a),
    info: (...a) => say(a),
    debug: (...a) => say(a),
    warn: (...a) => say(a),
    error: (...a) => say(a),
    dir: (v, o) => say([inspect(v, o)]),
    assert: (ok, ...a) => { if (!ok) say(["Assertion failed" + (a.length ? ": " + format(...a) : "")]); },
    trace: (...a) => say(["Trace" + (a.length ? ": " + format(...a) : "") + NL + String(new Error().stack).split(NL).slice(1).join(NL)]),
    group: (...a) => { if (a.length) say(a); indent += "  "; },
    groupEnd: () => { indent = indent.slice(2); },
    count: (l = "default") => { counts[l] = (counts[l] || 0) + 1; say([l + ": " + counts[l]]); },
    countReset: (l = "default") => { counts[l] = 0; },
    time: (l = "default") => { started[l] = now(); },
    timeLog: (l = "default", ...a) => say([l + ": " + (now() - started[l]) + "ms", ...a]),
    timeEnd: (l = "default") => { say([l + ": " + (now() - started[l]) + "ms"]); delete started[l]; },
    table: (data) => {
        if (typeof data !== "object" || data === null) return say([data]);
        const rows = Object.keys(data), cols = [];
        for (const r of rows) {
            const v = data[r];
            if (typeof v === "object" && v !== null) for (const c of Object.keys(v)) if (!cols.includes(c)) cols.push(c);
        }
        const hasValues = rows.some((r) => typeof data[r] !== "object" || data[r] === null);
        const head = ["(index)", ...cols, ...(hasValues ? ["Values"] : [])];
        const cell = (v) => (v === undefined ? "" : typeof v === "string" ? v : inspect(v, { depth: 0 }));
        const body = rows.map((r) => {
            const v = data[r], obj = typeof v === "object" && v !== null;
            return [r, ...cols.map((c) => (obj ? cell(v[c]) : "")), ...(hasValues ? [obj ? "" : cell(v)] : [])];
        });
        const w = head.map((h, i) => Math.max(h.length, ...body.map((b) => b[i].length)));
        const line = (l, m, r) => l + w.map((n) => "-".repeat(n + 2)).join(m) + r;
        const row = (cells) => "|" + cells.map((c, i) => " " + c.padEnd(w[i]) + " ").join("|") + "|";
        say([[line("+", "+", "+"), row(head), line("+", "+", "+"), ...body.map(row), line("+", "+", "+")].join(NL)]);
    },
};
define(g, "console", console);
define(g, "print", (...a) => N.write(1, a.map((v) => (typeof v === "string" ? v : inspect(v))).join(" ") + NL));

// prompt("Your name? ") reads a line from the keyboard (like a web browser's prompt)
define(g, "prompt", (msg = "", def) => {
    N.write(1, String(msg));
    const s = N.readLine();
    return s === null ? (def === undefined ? null : def) : s === "" && def !== undefined ? String(def) : s;
});

// ---------------------------------------------------------------- timers
define(g, "setTimeout", (fn, ms = 0, ...a) => N.setTimer(() => fn(...a), ms, false));
define(g, "setInterval", (fn, ms = 0, ...a) => N.setTimer(() => fn(...a), ms, true));
define(g, "setImmediate", (fn, ...a) => N.setTimer(() => fn(...a), 0, false));
define(g, "clearTimeout", (id) => { if (id !== undefined) N.clearTimer(id); });
define(g, "clearInterval", g.clearTimeout);
define(g, "clearImmediate", g.clearTimeout);
if (!g.queueMicrotask) define(g, "queueMicrotask", (fn) => { Promise.resolve().then(fn); });
if (!g.structuredClone) define(g, "structuredClone", (v) => JSON.parse(JSON.stringify(v)));
define(g, "performance", { now: () => now(), timeOrigin: 0 });

// ---------------------------------------------------------------- path (folders on MyFS v2)
const path = {
    sep: "/", delimiter: ":",
    basename(p, ext) {
        let b = String(p);
        while (b.length > 1 && b.endsWith("/")) b = b.slice(0, -1);
        b = b.slice(b.lastIndexOf("/") + 1);
        if (ext && b.endsWith(ext) && b !== ext) b = b.slice(0, -ext.length);
        return b;
    },
    dirname(p) {
        const i = String(p).lastIndexOf("/");
        return i < 0 ? "." : i === 0 ? "/" : p.slice(0, i);
    },
    extname(p) {
        const b = path.basename(p), i = b.lastIndexOf(".");
        return i <= 0 ? "" : b.slice(i);
    },
    normalize(p) {
        const abs = String(p).startsWith("/"), out = [];
        for (const s of String(p).split("/")) {
            if (s === "" || s === ".") continue;
            if (s === ".." && out.length && out[out.length - 1] !== "..") out.pop();
            else if (s !== ".." || !abs) out.push(s);
        }
        return (abs ? "/" : "") + out.join("/") || (abs ? "/" : ".");
    },
    join: (...a) => path.normalize(a.filter((x) => x !== "").join("/")),
    resolve: (...a) => {
        let p = N.cwd();
        for (const s of a) if (s !== "") p = String(s).startsWith("/") ? s : p + "/" + s;
        return path.normalize("/" + p);
    },
    isAbsolute: (p) => String(p).startsWith("/"),
    relative(from, to) {
        const f = path.resolve(from).split("/").filter(Boolean), t = path.resolve(to).split("/").filter(Boolean);
        let i = 0;
        while (i < f.length && i < t.length && f[i] === t[i]) i++;
        return [...f.slice(i).map(() => ".."), ...t.slice(i)].join("/");
    },
    parse(p) {
        const base = path.basename(p), ext = path.extname(p);
        return { root: p.startsWith("/") ? "/" : "", dir: path.dirname(p), base, ext, name: ext ? base.slice(0, -ext.length) : base };
    },
};
path.posix = path;

// ---------------------------------------------------------------- fs (MyFS; folders need MyFS v2)
function fsError(code, msg, name) {
    const e = new Error(code + ": " + msg + ", '" + name + "'");
    e.code = code;
    e.path = name;
    return e;
}
// MYOS_E* numbers from the folder calls -> Node error codes
const ERRS = { "-3": "ENOENT", "-4": "ENOSPC", "-5": "EINVAL", "-6": "EEXIST", "-7": "ENOTDIR", "-8": "EISDIR", "-9": "ENOTEMPTY" };
function check(r, what, name) {
    if (typeof r !== "number" || r >= 0) return r;
    if (r === -10) throw fsError("ENOSYS", "MyFS v1 has no folders (type 'fsupgrade' first), " + what, name);
    throw fsError(ERRS[r] || "EIO", "failed (MyOS error " + r + "), " + what, name);
}
function wantText(opt) {
    return typeof opt === "string" || (opt && typeof opt.encoding === "string");
}
class Stats {
    constructor(s) { this.size = s.size; this.mtime = new Date(0); this.mode = s.type === 2 ? 0o40755 : 0o100644; this._d = s.type === 2; }
    isFile() { return !this._d; }
    isDirectory() { return this._d; }
    isSymbolicLink() { return false; }
}
class Dirent {
    constructor(name, type) { this.name = name; this._d = type === 2; }
    isFile() { return !this._d; }
    isDirectory() { return this._d; }
    isSymbolicLink() { return false; }
}
const isDir = (p) => { const s = N.stat(String(p)); return !!s && s.type === 2; };
const isFile = (p) => { const s = N.stat(String(p)); return !!s && s.type === 1; };
const fs = {
    // text if an encoding is given; without one Node gives a Buffer - here a
    // Uint8Array that turns into the text with toString()
    readFileSync(name, opt) {
        if (isDir(name)) throw fsError("EISDIR", "illegal operation on a directory, read", name);
        if (N.fileSize(name) < 0) throw fsError("ENOENT", "no such file or directory, open", name);
        if (wantText(opt)) return N.readFile(name, false);
        const b = N.readFile(name, true);
        define(b, "toString", function (enc) { return enc === "hex" ? Array.from(this, (x) => x.toString(16).padStart(2, "0")).join("") : N.readFile(name, false); });
        return b;
    },
    writeFileSync(name, data) { N.writeFile(name, typeof data === "string" || ArrayBuffer.isView(data) || data instanceof ArrayBuffer ? data : String(data), false); },
    appendFileSync(name, data) { N.writeFile(name, typeof data === "string" || ArrayBuffer.isView(data) ? data : String(data), true); },
    existsSync: (name) => name === "." || name === "/" || !!N.stat(String(name)),
    unlinkSync(name) {
        if (N.fileSize(name) < 0) throw fsError(isDir(name) ? "EISDIR" : "ENOENT", "no such file, unlink", name);
        N.unlink(name);
    },
    rmSync(name, opt) {
        const s = N.stat(String(name));
        if (!s) { if (opt && opt.force) return; throw fsError("ENOENT", "no such file or directory, rm", name); }
        if (s.type === 1) return N.unlink(name);
        if (!(opt && opt.recursive)) throw fsError("EISDIR", "is a directory (use { recursive: true }), rm", name);
        for (const [n] of check(N.readdir(name), "rm", name)) fs.rmSync(name + "/" + n, opt);
        check(N.rmdir(name), "rm", name);
    },
    rmdirSync(name, opt) { if (opt && opt.recursive) fs.rmSync(name, opt); else check(N.rmdir(name), "rmdir", name); },
    renameSync(from, to) { check(N.rename(from, to), "rename", from); },
    copyFileSync(from, to) { N.writeFile(to, N.readFile(from, true), false); },
    readdirSync(name = ".", opt) {
        const l = check(N.readdir(name), "scandir", name).sort((a, b) => (a[0] < b[0] ? -1 : 1));
        return opt && opt.withFileTypes ? l.map((e) => new Dirent(e[0], e[1])) : l.map((e) => e[0]);
    },
    statSync(name, opt) {
        const s = name === "." || name === "/" ? { type: 2, size: 0 } : N.stat(String(name));
        if (!s) {
            if (opt && opt.throwIfNoEntry === false) return undefined;
            throw fsError("ENOENT", "no such file or directory, stat", name);
        }
        return new Stats(s);
    },
    mkdirSync(name, opt) {
        if (!(opt && opt.recursive)) return void check(N.mkdir(name), "mkdir", name);
        let p = String(name).startsWith("/") ? "" : ".";
        for (const part of String(name).split("/").filter(Boolean)) {
            p += "/" + part;
            if (!isDir(p)) check(N.mkdir(p), "mkdir", name);
        }
    },
};
fs.lstatSync = fs.statSync;
// the callback and promise versions on top of the ones above
const fsPromises = {};
for (const k of Object.keys(fs)) {
    if (!k.endsWith("Sync")) continue;
    const name = k.slice(0, -4), sync = fs[k];
    fs[name] = (...a) => {
        const cb = typeof a[a.length - 1] === "function" ? a.pop() : null;
        let r, err = null;
        try { r = sync(...a); } catch (e) { err = e; }
        if (cb) setImmediate(() => cb(err, r));
    };
    fsPromises[name] = (...a) => new Promise((ok, bad) => { try { ok(sync(...a)); } catch (e) { bad(e); } });
}
fs.promises = fsPromises;

// ---------------------------------------------------------------- os, process
const info = () => N.sysinfo();
const os = {
    EOL: NL,
    platform: () => "myos",
    type: () => "MyOS",
    arch: () => "arm64",
    release: () => "1.0",
    hostname: () => "myos",
    homedir: () => "/",
    tmpdir: () => "/",
    endianness: () => "LE",
    uptime: () => Math.floor(info().ticks / 100),
    totalmem: () => info().memTotalKB * 1024,
    freemem: () => info().memFreeKB * 1024,
    cpus: () => { const hz = info().armHz; return [0, 1, 2, 3].map(() => ({ model: "ARM Cortex-A53", speed: Math.round(hz / 1e6) })); },
    loadavg: () => [0, 0, 0],
    userInfo: () => ({ username: "myos", homedir: "/", shell: "myos" }),
};

const listeners = {};
const process = {
    argv: N.argv,
    argv0: "node",
    execPath: "node",
    env: { HOME: "/", USER: "myos", PATH: "/" },
    platform: "myos",
    arch: "arm64",
    pid: 1,
    version: "v" + N.version,
    versions: { quickjs: N.version, myos: "1.0" },
    release: { name: "quickjs" },
    exitCode: undefined,
    exit: (code) => { N.exit(code === undefined ? process.exitCode || 0 : code); },
    cwd: () => N.cwd(),
    chdir: (d) => { check(N.chdir(String(d)), "chdir", d); },
    uptime: () => N.ticks() / 100,
    hrtime: Object.assign((prev) => {
        const ms = now(), t = [Math.floor(ms / 1000), (ms % 1000) * 1e6];
        return prev ? [t[0] - prev[0], t[1] - prev[1]] : t;
    }, { bigint: () => BigInt(now()) * 1000000n }),
    memoryUsage: () => { const h = N.heap(); return { rss: h.heapUsed, heapTotal: h.heapUsed, heapUsed: h.heapUsed, external: 0 }; },
    nextTick: (fn, ...a) => { Promise.resolve().then(() => fn(...a)); },
    stdout: { write: (s) => { N.write(1, String(s)); return true; }, isTTY: true, columns: 80, rows: 25 },
    stderr: { write: (s) => { N.write(2, String(s)); return true; }, isTTY: true },
    stdin: { isTTY: true },
    on: (ev, fn) => { (listeners[ev] = listeners[ev] || []).push(fn); return process; },
    once: (ev, fn) => process.on(ev, fn),
    off: () => process,
    emitWarning: (w) => say(["Warning: " + w]),
};
define(g, "process", process);
if (g.gc === undefined) define(g, "gc", () => N.gc());

// ---------------------------------------------------------------- require
const events = (() => {
    class EventEmitter {
        constructor() { this._ev = {}; }
        on(e, f) { (this._ev[e] = this._ev[e] || []).push(f); return this; }
        addListener(e, f) { return this.on(e, f); }
        once(e, f) { const w = (...a) => { this.off(e, w); f(...a); }; return this.on(e, w); }
        off(e, f) { this._ev[e] = (this._ev[e] || []).filter((x) => x !== f); return this; }
        removeListener(e, f) { return this.off(e, f); }
        removeAllListeners(e) { if (e) delete this._ev[e]; else this._ev = {}; return this; }
        emit(e, ...a) { const l = this._ev[e]; if (!l || !l.length) return false; for (const f of [...l]) f.apply(this, a); return true; }
        listenerCount(e) { return (this._ev[e] || []).length; }
    }
    EventEmitter.EventEmitter = EventEmitter;
    return EventEmitter;
})();

// a synchronous readline: rl.question(q, cb) answers right away
const readline = {
    createInterface: () => {
        const rl = new events();
        rl.question = (q, cb) => { const a = g.prompt(q); if (cb) cb(a === null ? "" : a); else return Promise.resolve(a === null ? "" : a); };
        rl.close = () => rl.emit("close");
        rl.prompt = () => {};
        rl.setPrompt = () => {};
        return rl;
    },
};

const assert = (() => {
    function fail(msg, def) { const e = new Error(msg || def); e.name = "AssertionError"; e.code = "ERR_ASSERTION"; throw e; }
    const a = (v, m) => { if (!v) fail(m, "The expression evaluated to a falsy value"); };
    a.ok = a;
    a.equal = (x, y, m) => { if (x != y) fail(m, inspect(x) + " == " + inspect(y)); };
    a.notEqual = (x, y, m) => { if (x == y) fail(m, inspect(x) + " != " + inspect(y)); };
    a.strictEqual = (x, y, m) => { if (!Object.is(x, y)) fail(m, "Expected values to be strictly equal: " + inspect(x) + " !== " + inspect(y)); };
    a.notStrictEqual = (x, y, m) => { if (Object.is(x, y)) fail(m, "Expected values to differ: " + inspect(x)); };
    a.deepEqual = a.deepStrictEqual = (x, y, m) => { if (!util.isDeepStrictEqual(x, y)) fail(m, "Expected values to be deeply equal: " + inspect(x) + " vs " + inspect(y)); };
    a.throws = (fn, m) => { try { fn(); } catch (e) { return; } fail(typeof m === "string" ? m : null, "Missing expected exception."); };
    a.fail = (m) => fail(m, "Failed");
    a.strict = a;
    return a;
})();

const tty = { isatty: (fd) => fd <= 2 };
// crypto: SHA-1 / SHA-512 hashes (npm's); no random numbers (MyOS has no secure source for programs yet)
const crypto = {
    createHash(alg) {
        alg = String(alg).toLowerCase().replace("-", "");
        if (alg !== "sha1" && alg !== "sha512") throw new Error("crypto.createHash: MyOS has sha1 and sha512 only, not " + alg);
        const parts = [];
        const h = {
            update(d) { parts.push(typeof d === "string" ? N.bytes(d) : d instanceof ArrayBuffer ? new Uint8Array(d) : d); return h; },
            digest(enc) {
                const all = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
                parts.reduce((at, p) => (all.set(p, at), at + p.length), 0);
                const hex = N.hash(alg, all);
                if (enc === "hex") return hex;
                const raw = Uint8Array.from(hex.match(/../g), (x) => parseInt(x, 16));
                if (enc !== "base64") return raw;
                const A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                let s = "";
                for (let i = 0; i < raw.length; i += 3) {
                    const v = (raw[i] << 16) | ((raw[i + 1] || 0) << 8) | (raw[i + 2] || 0);
                    s += A[v >> 18] + A[(v >> 12) & 63] + (i + 1 < raw.length ? A[(v >> 6) & 63] : "=") + (i + 2 < raw.length ? A[v & 63] : "=");
                }
                return s;
            },
        };
        return h;
    },
    getHashes: () => ["sha1", "sha512"],
};
for (const f of ["randomBytes", "randomFillSync", "randomUUID", "getRandomValues", "randomInt"])
    crypto[f] = () => { throw new Error("crypto." + f + ": MyOS has no secure random numbers for programs yet"); };
const builtins = { fs, path, os, util, events, readline, assert, process, tty, crypto, "fs/promises": fsPromises, "assert/strict": assert, "readline/promises": readline };
const cache = {};
const has = (o, k) => Object.prototype.hasOwnProperty.call(o, k);
let mainModule;

function readJSON(p) {
    try { return JSON.parse(N.readFile(p, false)); } catch (e) { return null; }
}
function notFound(id, from) {
    const e = new Error("Cannot find module '" + id + "'" + (from ? " (from " + from + ")" : ""));
    e.code = "MODULE_NOT_FOUND";
    return e;
}

// Node's way to find a module: ./x as x, x.js, x.json, x.cjs, x.mjs or a folder
// (package.json "main", index.js); a package name in node_modules folders from
// the requiring file's folder up to /, honouring package.json "exports"
function asFile(p) {
    for (const e of ["", ".js", ".json", ".cjs", ".mjs"]) if (isFile(p + e)) return p + e;
    return null;
}
function asIndex(p) {
    for (const e of ["/index.js", "/index.json", "/index.cjs", "/index.mjs"]) if (isFile(p + e)) return p + e;
    return null;
}
function asDir(p) {
    if (!isDir(p)) return null;
    const pj = readJSON(p + "/package.json");
    if (pj && typeof pj.main === "string" && pj.main) {
        const m = path.join(p, pj.main), f = asFile(m) || asIndex(m);
        if (f) return f;
    }
    return asIndex(p);
}
function target(t, conds) {                     // an "exports" value -> a path, by conditions
    if (typeof t === "string") return t;
    if (Array.isArray(t)) {
        for (const x of t) { const r = target(x, conds); if (r) return r; }
    } else if (t && typeof t === "object") {
        for (const k of Object.keys(t)) {
            if (k !== "default" && !conds.includes(k)) continue;
            const r = target(t[k], conds);
            if (r) return r;
        }
    }
    return null;
}
function viaExports(dir, ex, sub, conds) {
    if (typeof ex === "string" || Array.isArray(ex) || !Object.keys(ex).some((k) => k.startsWith("."))) ex = { ".": ex };
    if (has(ex, sub)) {
        const t = target(ex[sub], conds);
        return t ? path.join(dir, t) : null;
    }
    for (const k of Object.keys(ex)) {          // "./*": "./lib/*.js"
        const star = k.indexOf("*"), pre = k.slice(0, star), post = k.slice(star + 1);
        if (star < 0 || sub.length < k.length - 1 || !sub.startsWith(pre) || !sub.endsWith(post)) continue;
        const t = target(ex[k], conds);
        return t ? path.join(dir, t.split("*").join(sub.slice(pre.length, sub.length - post.length))) : null;
    }
    return null;
}
const REQ = ["require", "node", "default"], IMP = ["import", "node", "default"];
function fromNodeModules(id, dir, condsList) {
    const parts = id.split("/"), n = id.startsWith("@") ? 2 : 1;
    const name = parts.slice(0, n).join("/"), sub = parts.slice(n).join("/");
    for (let d = dir; ; d = path.dirname(d)) {
        const pkg = path.join(d, "node_modules", name);
        if (path.basename(d) !== "node_modules" && isDir(pkg)) {
            const pj = readJSON(pkg + "/package.json");
            if (pj && pj.exports && (typeof pj.exports === "object" || typeof pj.exports === "string")) {
                for (const conds of condsList) {
                    const f = viaExports(pkg, pj.exports, sub ? "./" + sub : ".", conds);
                    if (f && isFile(f)) return f;
                }
            }
            const p = sub ? path.join(pkg, sub) : pkg;
            return asFile(p) || asDir(p);
        }
        if (d === "/") return null;
    }
}
// -> a builtin's name ("fs") or a file's full path ("/app/node_modules/x/index.js")
function resolve(id, dir, esm, from) {
    const bare = id.startsWith("node:") ? id.slice(5) : id;
    if (has(builtins, bare)) return bare;
    let f;
    if (/^\.{0,2}\//.test(id) || id === "." || id === "..") {
        const p = path.resolve(dir, id);
        f = asFile(p) || asDir(p);
    } else {
        f = fromNodeModules(id, path.resolve(dir), esm ? [IMP, REQ] : [REQ, IMP]) ||
            asFile(path.resolve(dir, id));      // (MyOS: require('lib') also finds ./lib.js)
    }
    if (!f) throw notFound(id, from);
    if (f.endsWith(".node")) throw new Error("'" + id + "' is a native addon (C++): not supported on MyOS");
    return f;
}

// ES module or CommonJS? .mjs/.cjs, then the nearest package.json "type", then the code itself
function isESM(file, src) {
    if (file.endsWith(".mjs")) return true;
    if (file.endsWith(".cjs") || file.endsWith(".json")) return false;
    for (let d = path.dirname(file); ; d = path.dirname(d)) {
        const pj = isFile(d + "/package.json") && readJSON(d + "/package.json");
        if (pj && pj.type === "module") return true;
        if (pj && pj.type === "commonjs") return false;
        if (pj || d === "/") break;
    }
    return N.detectModule(src);
}

function load(file, parent) {
    if (cache[file]) return cache[file].exports;
    const module = { id: file, filename: file, exports: {}, loaded: false, children: [], paths: [], parent };
    cache[file] = module;
    try {
        if (file.endsWith(".json")) module.exports = JSON.parse(N.readFile(file, false));
        else {
            const src = N.readFile(file, false);
            if (isESM(file, src)) {             // require() of an ES module (like Node 22): its namespace
                N.evalModule("import * as ns from " + JSON.stringify(file) + "; __myos_hooks.ns = ns;", "<require>");
                module.exports = hooks.ns;
                hooks.ns = undefined;
            } else {
                runCommonJS(module, src);
            }
        }
    } catch (e) {
        delete cache[file];
        throw e;
    }
    module.loaded = true;
    return module.exports;
}

function makeRequire(parent) {
    const dir = () => (parent ? path.dirname(parent.filename) : N.cwd());
    const req = (id) => {
        const f = resolve(String(id), dir(), false, parent && parent.filename);
        return f[0] === "/" ? load(f, parent) : builtins[f];
    };
    req.cache = cache;
    req.resolve = (id) => resolve(String(id), dir(), false, parent && parent.filename);
    req.main = mainModule;
    return req;
}

// Node's module wrapper; kept on the first line so line numbers stay right
function runCommonJS(module, src) {
    if (src === undefined) src = N.readFile(module.filename, false);
    if (src.startsWith("#!")) src = "//" + src;
    const fn = N.evalScript("(function (exports, require, module, __filename, __dirname) {" + src + NL + "})", module.filename);
    const req = makeRequire(module);
    module.require = req;
    fn.call(module.exports, module.exports, req, module, module.filename, path.dirname(module.filename));
}

// import of a builtin or a CommonJS file: an ES module with its keys as named exports
const RESERVED = new Set(("break case catch class const continue debugger default delete do else enum export extends false " +
    "finally for function if import in instanceof new null return super switch this throw true try typeof var void while " +
    "with yield let static implements interface package private protected public await arguments eval").split(" "));
function wrapperSource(expr, m) {
    const names = m && (typeof m === "object" || typeof m === "function")
        ? Object.keys(m).filter((k) => /^[A-Za-z_$][A-Za-z0-9_$]*$/.test(k) && !RESERVED.has(k)) : [];
    return "const m = " + expr + "; export default m;" + (names.length ? " export const {" + names.join(", ") + "} = m;" : "");
}

define(g, "require", makeRequire(null));
define(g, "global", g);
const hooks = {
    runMain(name) {
        const file = path.resolve(name);
        const module = { id: ".", filename: file, exports: {}, loaded: false, children: [], paths: [] };
        cache[file] = module;
        mainModule = g.require.main = module;
        runCommonJS(module);
        module.loaded = true;
    },
    // import x from 'id' in module 'base' -> the name module_loader gets
    resolveImport(id, base) {
        const dir = base[0] === "<" || base[0] === "[" ? N.cwd() : path.dirname(path.resolve(base));
        return resolve(id, dir, true, base);
    },
    moduleSource(name) {
        if (name[0] !== "/") return wrapperSource("require(" + JSON.stringify(name) + ")", builtins[name]);
        if (name.endsWith(".json")) return "export default " + N.readFile(name, false) + ";";
        const src = N.readFile(name, false);
        return isESM(name, src) ? src : wrapperSource("require(" + JSON.stringify(name) + ")", load(name, null));
    },
};
define(g, "__myos_hooks", hooks);
})
