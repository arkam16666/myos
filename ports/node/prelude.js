// prelude.js - the JavaScript half of 'node' on MyOS (built into the program).
// node.c runs this once at start-up and calls the function with N, the native
// helpers: write, inspect, readFile, writeFile, fileSize, unlink, list,
// evalScript, readLine, setTimer, clearTimer, exit, ticks, sysinfo, heap, gc.
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

// ---------------------------------------------------------------- path (MyFS has no folders)
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
        let p = "";
        for (const s of a) p = String(s).startsWith("/") ? s : p + "/" + s;
        return path.normalize("/" + p);
    },
    isAbsolute: (p) => String(p).startsWith("/"),
    relative: (from, to) => path.normalize(to).replace(/^\//, ""),
    parse(p) {
        const base = path.basename(p), ext = path.extname(p);
        return { root: p.startsWith("/") ? "/" : "", dir: path.dirname(p), base, ext, name: ext ? base.slice(0, -ext.length) : base };
    },
};
path.posix = path;

// ---------------------------------------------------------------- fs (MyFS: one flat folder)
function fsError(code, msg, name) {
    const e = new Error(code + ": " + msg + ", '" + name + "'");
    e.code = code;
    e.path = name;
    return e;
}
function wantText(opt) {
    return typeof opt === "string" || (opt && typeof opt.encoding === "string");
}
class Stats {
    constructor(size) { this.size = size; this.mtime = new Date(0); this.mode = 0o100644; }
    isFile() { return true; }
    isDirectory() { return false; }
    isSymbolicLink() { return false; }
}
const fs = {
    // text if an encoding is given; without one Node gives a Buffer - here a
    // Uint8Array that turns into the text with toString()
    readFileSync(name, opt) {
        if (N.fileSize(name) < 0) throw fsError("ENOENT", "no such file or directory, open", name);
        if (wantText(opt)) return N.readFile(name, false);
        const b = N.readFile(name, true);
        define(b, "toString", function (enc) { return enc === "hex" ? Array.from(this, (x) => x.toString(16).padStart(2, "0")).join("") : N.readFile(name, false); });
        return b;
    },
    writeFileSync(name, data) { N.writeFile(name, typeof data === "string" || ArrayBuffer.isView(data) || data instanceof ArrayBuffer ? data : String(data), false); },
    appendFileSync(name, data) { N.writeFile(name, typeof data === "string" || ArrayBuffer.isView(data) ? data : String(data), true); },
    existsSync: (name) => name === "." || name === "/" || N.fileSize(name) >= 0,
    unlinkSync(name) {
        if (N.fileSize(name) < 0) throw fsError("ENOENT", "no such file or directory, unlink", name);
        N.unlink(name);
    },
    rmSync(name, opt) {
        if (N.fileSize(name) >= 0) N.unlink(name);
        else if (!(opt && opt.force)) throw fsError("ENOENT", "no such file or directory, rm", name);
    },
    renameSync(from, to) {
        const data = N.readFile(from, true);
        N.writeFile(to, data, false);
        N.unlink(from);
    },
    copyFileSync(from, to) { N.writeFile(to, N.readFile(from, true), false); },
    readdirSync: () => N.list().sort(),
    statSync(name) {
        if (name === "." || name === "/") { const s = new Stats(0); s.isFile = () => false; s.isDirectory = () => true; return s; }
        const n = N.fileSize(name);
        if (n < 0) throw fsError("ENOENT", "no such file or directory, stat", name);
        return new Stats(n);
    },
    mkdirSync() { throw fsError("ENOSYS", "MyFS has no folders", "mkdir"); },
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
    cwd: () => "/",
    chdir: () => {},
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

const builtins = { fs, path, os, util, events, readline, assert, process, "fs/promises": fsPromises, "assert/strict": assert, "readline/promises": readline };
const cache = {};

function loadFile(name) {
    for (const n of [name, name + ".js", name + ".json"]) {
        if (N.fileSize(n) >= 0) return n;
    }
    return null;
}

function makeRequire(from) {
    const req = (id) => {
        id = String(id);
        const bare = id.startsWith("node:") ? id.slice(5) : id;
        if (Object.prototype.hasOwnProperty.call(builtins, bare)) return builtins[bare];
        const file = loadFile(path.normalize(id).replace(/^\//, ""));
        if (!file) {
            const e = new Error("Cannot find module '" + id + "'" + (from ? " (from " + from + ")" : ""));
            e.code = "MODULE_NOT_FOUND";
            throw e;
        }
        if (cache[file]) return cache[file].exports;
        const module = { id: file, filename: file, exports: {}, loaded: false, children: [], paths: [] };
        cache[file] = module;
        try {
            if (file.endsWith(".json")) module.exports = JSON.parse(N.readFile(file, false));
            else runCommonJS(module);
        } catch (e) {
            delete cache[file];
            throw e;
        }
        module.loaded = true;
        return module.exports;
    };
    req.cache = cache;
    req.resolve = (id) => loadFile(id) || id;
    req.main = undefined;
    return req;
}

// Node's module wrapper; kept on the first line so line numbers stay right
function runCommonJS(module) {
    let src = N.readFile(module.filename, false);
    if (src.startsWith("#!")) src = "//" + src;
    const fn = N.evalScript("(function (exports, require, module, __filename, __dirname) {" + src + NL + "})", module.filename);
    const req = makeRequire(module.filename);
    module.require = req;
    fn.call(module.exports, module.exports, req, module, module.filename, "/");
}

define(g, "require", makeRequire(null));
define(g, "global", g);
define(g, "__myos_hooks", {
    runMain(name) {
        const module = { id: ".", filename: name, exports: {}, loaded: false, children: [], paths: [] };
        cache[name] = module;
        g.require.main = module;
        runCommonJS(module);
        module.loaded = true;
    },
    // import fs from 'fs' / import { readFileSync } from 'node:fs'
    builtinModuleSource(id) {
        const bare = id.startsWith("node:") ? id.slice(5) : id;
        if (!Object.prototype.hasOwnProperty.call(builtins, bare)) return undefined;
        const m = builtins[bare];
        const names = Object.keys(m).filter((k) => /^[A-Za-z_$][A-Za-z0-9_$]*$/.test(k) && k !== "default");
        return "const m = require(" + JSON.stringify(bare) + "); export default m;" +
               (names.length ? " export const {" + names.join(", ") + "} = m;" : "");
    },
    jsonModuleSource: (v) => "export default " + JSON.stringify(v) + ";",
});
})
