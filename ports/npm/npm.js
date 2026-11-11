// npm.js - npm for MyOS, built into the 'npm' program (ports/node/node.c compiled
// with -DNODE_NPM: the same JavaScript engine and Node-like modules as 'node').
// Installs pure-JavaScript packages from https://registry.npmjs.org into
// ./node_modules, where node's require() finds them.
//
//   npm install [NAME[@VERSION|@RANGE|@TAG] | ./file.tgz ...] [-D] [--no-save] [-g]
//   npm uninstall NAME...      npm ls [--all]     npm view NAME[@VERSION] [FIELD]
//   npm init [-y]              npm run SCRIPT / npm start / npm test (scripts "node FILE ...")
//
// N = node.c's native helpers (fetch, gunzip, hash, text, jsonPick, evalModule ...).
// Called with test = true (tests/host/npm_host_test.py, on a PC) it returns its
// inner functions instead of running.
(function (N, test) {
"use strict";
const fs = require("fs"), path = require("path");
const REGISTRY = "https://registry.npmjs.org/";
const MB = 1 << 20;
const say = (s) => console.log(s);

class NpmError extends Error {}
function fail(msg) { throw new NpmError(msg); }
const has = (o, k) => !!o && Object.prototype.hasOwnProperty.call(o, k);

// ================================================================ semver
// versions: 1.2.3, 1.2.3-beta.1 (+build ignored); ranges: exact, ^, ~, x-ranges
// (1.x, 1.2.*, *), >, >=, <, <=, a - b, and sets joined with ||
function parseVersion(v) {
    const m = /^\s*v?=?\s*(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?\s*$/.exec(String(v));
    return m ? { major: +m[1], minor: +m[2], patch: +m[3], pre: m[4] ? m[4].split(".") : [] } : null;
}
function comparePre(a, b) {
    if (!a.length || !b.length) return b.length - a.length;    // no pre-release is higher
    for (let i = 0; i < a.length && i < b.length; i++) {
        const x = a[i], y = b[i];
        if (x === y) continue;
        const nx = /^\d+$/.test(x), ny = /^\d+$/.test(y);
        if (nx && ny) return +x - +y;
        if (nx || ny) return nx ? -1 : 1;
        return x < y ? -1 : 1;
    }
    return a.length - b.length;
}
function compare(a, b) {
    return a.major - b.major || a.minor - b.minor || a.patch - b.patch || comparePre(a.pre, b.pre);
}
const V = (major, minor, patch, pre = []) => ({ major, minor, patch, pre });

function partial(s) {
    const m = /^v?=?(\d+|[xX*])(?:\.(\d+|[xX*]))?(?:\.(\d+|[xX*]))?(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$/.exec(s);
    if (!m) fail("can't understand the version range '" + s + "'");
    let given = 0;
    for (const n of [m[1], m[2], m[3]]) { if (n === undefined || /[xX*]/.test(n)) break; given++; }
    return { v: V(given > 0 ? +m[1] : 0, given > 1 ? +m[2] : 0, given > 2 ? +m[3] : 0, given > 2 && m[4] ? m[4].split(".") : []), given };
}
// the first version past what a partial covers: 1 -> 2.0.0, 1.2 -> 1.3.0
const next = ({ v, given }) => (given === 1 ? V(v.major + 1, 0, 0) : V(v.major, v.minor + 1, 0));

function comparators(t) {
    const m = /^(\^|~>?|>=|<=|>|<|=)?(.*)$/.exec(t), op = m[1] || "", p = partial(m[2]), v = p.v, g = p.given;
    if (g === 0) return op === "<" || op === ">" ? [{ op: "<", v: V(0, 0, 0) }] : [];   // * : anything
    const ge = { op: ">=", v };
    switch (op) {
    case "^": {
        const up = v.major > 0 || g === 1 ? V(v.major + 1, 0, 0) : v.minor > 0 || g === 2 ? V(0, v.minor + 1, 0) : V(0, 0, v.patch + 1);
        return [ge, { op: "<", v: up }];
    }
    case "~": case "~>":
        return [ge, { op: "<", v: next({ v, given: g === 1 ? 1 : 2 }) }];
    case ">": return g === 3 ? [{ op: ">", v }] : [{ op: ">=", v: next(p) }];
    case ">=": return [ge];
    case "<": return [{ op: "<", v }];
    case "<=": return g === 3 ? [{ op: "<=", v }] : [{ op: "<", v: next(p) }];
    default: return g === 3 ? [{ op: "=", v }] : [ge, { op: "<", v: next(p) }];
    }
}
function parseRange(range) {
    return String(range).split("||").map((part) => {
        part = part.trim();
        const h = /^(\S+)\s+-\s+(\S+)$/.exec(part);
        if (h) {
            const lo = partial(h[1]), hi = partial(h[2]);
            return [{ op: ">=", v: lo.v }, ...(hi.given === 0 ? [] : hi.given === 3 ? [{ op: "<=", v: hi.v }] : [{ op: "<", v: next(hi) }])];
        }
        const set = [];
        for (const t of part.replace(/(>=|<=|~>|[<>=~^])\s+/g, "$1").split(/\s+/)) if (t && t !== "*") set.push(...comparators(t));
        return set;
    });
}
function satisfies(version, range) {
    const v = typeof version === "string" ? parseVersion(version) : version;
    if (!v) return false;
    if (isTag(range)) return true;
    return parseRange(range).some((set) => {
        // a pre-release only matches a set that names a pre-release of the same x.y.z
        if (v.pre.length && !set.some((c) => c.v.pre.length && c.v.major === v.major && c.v.minor === v.minor && c.v.patch === v.patch)) return false;
        return set.every(({ op, v: w }) => {
            const c = compare(v, w);
            return op === "<" ? c < 0 : op === "<=" ? c <= 0 : op === ">" ? c > 0 : op === ">=" ? c >= 0 : c === 0;
        });
    });
}
function maxSatisfying(versions, range) {
    let best = null, bv = null;
    for (const s of versions) {
        const v = parseVersion(s);
        if (v && satisfies(v, range) && (!bv || compare(v, bv) > 0)) { best = s; bv = v; }
    }
    return best;
}
// "latest", "next", "beta": a dist-tag rather than a range
const isTag = (r) => r === "" || /^[A-Za-z][\w.-]*$/.test(r) && !/^[vxX]([.\d]|$)/.test(r);

// ================================================================ specs
// "left-pad", "left-pad@1.3.0", "@scope/name@^2", "./file.tgz", "file:x.tgz"
function parseSpec(arg) {
    if (/^file:/.test(arg)) return { file: arg.slice(5) };
    if (/\.(tgz|tar\.gz)$/i.test(arg) || /^\.{0,2}\//.test(arg)) return { file: arg };
    if (/^(https?|git(\+\w+)?):|^github:|^[\w.-]+\/[\w.-]+(#.*)?$/.test(arg)) fail("'" + arg + "': only registry packages and .tgz files can be installed on MyOS");
    const at = arg.indexOf("@", 1);
    const name = at > 0 ? arg.slice(0, at) : arg, range = at > 0 ? arg.slice(at + 1).trim() : "latest";
    checkName(name);
    return { name, range: range || "latest" };
}
function checkName(name) {
    if (!/^(@[a-z0-9][\w.~-]*\/)?[a-z0-9~][\w.~-]*$/i.test(name) || name.length > 214) fail("'" + name + "' is not a package name");
}
// a dependency from package.json -> { name, range } or { name, file }
function depSpec(name, range, base) {
    range = String(range).trim();
    if (range.startsWith("npm:")) {                         // "alias": "npm:real@range"
        const s = parseSpec(range.slice(4));
        return { name, real: s.name, range: s.range };
    }
    if (range.startsWith("file:")) return { name, file: path.resolve(base, range.slice(5)) };
    if (/^(https?|git(\+\w+)?|github|link|workspace):/.test(range) || /^[\w.-]+\/[\w.-]+/.test(range))
        fail("dependency " + name + " is '" + range + "': git / URL / link dependencies are not supported on MyOS");
    return { name, range: range || "latest" };
}

// ================================================================ the registry
const regName = (name) => name.replace("/", "%2F");

function netError(code, url) {
    const host = (/^\w+:\/\/([^/]+)/.exec(url) || [])[1] || url;
    switch (code) {
    case -1: return "no network: Wi-Fi not connected? try 'wifi'";
    case -6: return "cannot find " + host + " (DNS): Wi-Fi not connected? try 'wifi'";
    case -2: return "cannot connect to " + host + ": Wi-Fi not connected? try 'wifi'";
    case -3: return "the download from " + host + " timed out (weak Wi-Fi?): try again";
    case -4: return "too big for MyOS's memory: " + url;
    case -7: return "the secure connection (TLS) to " + host + " failed";
    case -8: return "the address is too long for MyOS: " + url;
    default: return "download failed (MyOS error " + code + "): " + url;
    }
}
// GET -> ArrayBuffer, null for 404
function get(url, max) {
    const r = N.fetch(url, max);
    if (r.error) fail(netError(r.error, url));
    if (r.status === 404) return null;
    if (r.status !== 200 || !r.body) fail("the server answered HTTP " + r.status + " for " + url);
    return r.body;
}
function getJSON(url) {
    const b = get(url, 4 * MB);
    if (!b) return null;
    try { return JSON.parse(N.text(b)); } catch (e) { fail("bad JSON from " + url); }
}

const manifests = {};
// the registry's document for one version of a package (has dist.tarball, dist.integrity, dependencies)
function manifest(name, range) {
    const key = name + "@" + range;
    if (has(manifests, key)) return manifests[key];
    let man;
    if (parseVersion(range)) {
        const v = parseVersion(range);
        man = getJSON(REGISTRY + regName(name) + "/" + v.major + "." + v.minor + "." + v.patch + (v.pre.length ? "-" + v.pre.join(".") : ""));
        if (!man) fail(name + "@" + range + " is not in the npm registry");
    } else {
        const latest = getJSON(REGISTRY + regName(name) + "/latest");
        if (!latest) fail("package '" + name + "' is not in the npm registry");
        if (range === "latest" || (!isTag(range) && satisfies(latest.version, range))) {
            man = latest;
        } else {
            // every version: the "packument" (often MBs, so it isn't parsed as a whole)
            say("  reading the version list of " + name + " ...");
            const doc = get(REGISTRY + regName(name), 24 * MB);
            if (!doc) fail("package '" + name + "' is not in the npm registry");
            let v;
            if (isTag(range)) {
                const tags = JSON.parse(N.jsonPick(doc, ["dist-tags"]) || "{}");
                v = tags[range];
                if (!v) fail(name + " has no tag '" + range + "' (tags: " + Object.keys(tags).join(", ") + ")");
            } else {
                v = maxSatisfying(N.jsonPick(doc, ["versions"], true) || [], range);
                if (!v) fail("no version of " + name + " matches '" + range + "' (latest is " + latest.version + ")");
            }
            man = JSON.parse(N.jsonPick(doc, ["versions", v]));
        }
    }
    return (manifests[key] = man);
}

// ================================================================ tarballs
function b64hex(s) {
    const A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let bits = 0, val = 0, hex = "";
    for (const ch of s) {
        const i = A.indexOf(ch);
        if (i < 0) continue;
        val = (val << 6) | i;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            hex += ((val >> bits) & 255).toString(16).padStart(2, "0");
            val &= (1 << bits) - 1;
        }
    }
    return hex;
}
// check the download against the registry's sha512 (dist.integrity) or sha1 (dist.shasum)
function verify(bytes, dist, label) {
    const sri = String(dist.integrity || "").split(/\s+/).find((x) => x.startsWith("sha512-"));
    if (sri) {
        if (N.hash("sha512", bytes) !== b64hex(sri.slice(7))) fail(label + ": the download is damaged or changed (sha512 does not match)");
    } else if (dist.shasum) {
        if (N.hash("sha1", bytes) !== String(dist.shasum).toLowerCase()) fail(label + ": the download is damaged or changed (sha1 does not match)");
    } else {
        say("npm warn " + label + " has no checksum in the registry");
    }
}

// the entries of a tar file: { rel (without the first folder, "package/"), type, start, size }
function tarEntries(tar) {
    const list = [];
    let off = 0, longName = null;
    const str = (a, b) => { let e = a; while (e < b && tar[e]) e++; return N.text(tar, a, e); };
    while (off + 512 <= tar.length && tar[off]) {
        let name = str(off, off + 100);
        const size = parseInt(str(off + 124, off + 136).trim() || "0", 8) || 0;
        const type = tar[off + 156] ? String.fromCharCode(tar[off + 156]) : "0";
        if (str(off + 257, off + 262) === "ustar" && tar[off + 345]) name = str(off + 345, off + 500) + "/" + name;
        const start = off + 512;
        off = start + Math.ceil(size / 512) * 512;
        if (type === "L") { longName = str(start, start + size); continue; }            // GNU long name
        if (type === "x") {                                                              // pax: path=
            const m = /(?:^|\n)\d+ path=([^\n]*)\n/.exec(N.text(tar, start, start + size));
            if (m) longName = m[1];
            continue;
        }
        if (type === "g") continue;
        if (longName !== null) { name = longName; longName = null; }
        const parts = name.split("/").slice(1).filter((s) => s && s !== ".");
        if (!parts.length || parts.includes("..")) continue;
        list.push({ rel: parts.join("/"), type, start, size });
    }
    return list;
}
const tarText = (tar, entries, rel) => {
    const e = entries.find((x) => x.rel === rel && x.type !== "5");
    return e ? N.text(tar, e.start, e.start + e.size) : null;
};

function mkdirp(dir) { fs.mkdirSync(dir, { recursive: true }); }

// write the files into dir (MyFS: names up to 63 characters, printable ASCII, paths up to 255)
function extract(tar, entries, dir, label) {
    const skipped = [];
    let files = 0;
    for (const e of entries) {
        const full = path.join(dir, e.rel);
        const parts = e.rel.split("/");
        if (parts.some((p) => p.length > 63 || /[^\x21-\x7e]/.test(p)) || full.length > 250) {
            skipped.push(e.rel);
            continue;
        }
        if (e.type === "5") { mkdirp(full); continue; }
        if (e.type !== "0" && e.type !== "7") { skipped.push(e.rel + " (link)"); continue; }
        mkdirp(path.dirname(full));
        N.writeFile(full, tar.subarray(e.start, e.start + e.size), false);
        files++;
    }
    for (const s of skipped.slice(0, 5)) say("npm warn " + label + ": skipped " + s + " (MyFS can't store this name)");
    if (skipped.length > 5) say("npm warn " + label + ": skipped " + (skipped.length - 5) + " more files");
    return files;
}

// ================================================================ the project
let root = process.cwd();
const modulesDir = () => path.join(root, "node_modules");
function readPkg(dir) {
    try { return JSON.parse(fs.readFileSync(path.join(dir, "package.json"), "utf8")); } catch (e) { return null; }
}
function writePkg(pkg) {
    fs.writeFileSync(path.join(root, "package.json"), JSON.stringify(pkg, null, 2) + "\n");
}
const sortKeys = (o) => Object.fromEntries(Object.keys(o).sort().map((k) => [k, o[k]]));
// the package require() would find for 'name' from folder 'from' (looking no higher than root)
function findInstalled(name, from) {
    for (let d = from; ; d = path.dirname(d)) {
        if (path.basename(d) !== "node_modules") {
            const dir = path.join(d, "node_modules", name), pkg = readPkg(dir);
            if (pkg) return { dir, pkg, version: pkg.version };
        }
        if (d === root || path.dirname(d) === d || !d.startsWith(root)) return null;
    }
}
function nativeAddon(pkg, entries) {
    return pkg.gypfile === true || entries.some((e) => e.rel === "binding.gyp") ||
        /node-gyp|prebuild-install|node-pre-gyp/.test(["preinstall", "install", "postinstall"].map((k) => (pkg.scripts || {})[k] || "").join(" "));
}

// ================================================================ install
let added = 0;
const placed = new Set();       // folders installed or checked in this run
const queue = [];

function installOne(job) {
    const { name, from } = job;
    const realName = job.real || name;
    const have = name ? findInstalled(name, from) : null;
    if (!job.explicit && have && (job.file || isTag(job.range) || satisfies(have.version, job.range))) {
        if (!placed.has(have.dir)) { placed.add(have.dir); queueDeps(have.pkg, have.dir); }
        return { name, version: have.version, dir: have.dir };
    }
    let man = null, tgz, label;
    if (job.file) {
        label = path.basename(job.file);
        if (!fs.existsSync(job.file)) fail("no such file: " + job.file);
        tgz = fs.readFileSync(job.file);
    } else {
        man = manifest(realName, job.range);
        label = realName + "@" + man.version;
        const top = path.join(modulesDir(), name);
        if (job.explicit && have && have.dir === top && have.version === man.version) {
            say(label + " is already installed");
            placed.add(top);
            queueDeps(have.pkg, top);
            return { name, version: man.version, dir: top };
        }
        if (Array.isArray(man.os) && man.os.some((o) => o[0] !== "!") && !man.os.includes("linux"))
            fail(label + " only works on " + man.os.join(", "));
        if (nativeAddon(man, [])) fail(label + " needs a C++ build (node-gyp / binding.gyp): native addons are not supported on MyOS");
        if (!man.dist || !man.dist.tarball) fail(label + ": the registry gives no tarball");
        say("  downloading " + label + " ...");
        tgz = get(man.dist.tarball, 24 * MB);
        if (!tgz) fail(label + ": tarball not found (" + man.dist.tarball + ")");
        verify(tgz, man.dist, label);
    }
    let tar;
    try { tar = new Uint8Array(N.gunzip(tgz)); } catch (e) { fail(label + ": " + e.message); }
    tgz = null;
    const entries = tarEntries(tar);
    let pkg;
    try { pkg = JSON.parse(tarText(tar, entries, "package.json")); } catch (e) { fail(label + " has no valid package.json"); }
    const pkgName = job.file && job.explicit ? pkg.name : name;
    if (job.file) {
        if (!pkg.name || !pkg.version) fail(label + ": package.json needs a name and a version");
        checkName(pkg.name);
        label = pkg.name + "@" + pkg.version;
    }
    if (nativeAddon(pkg, entries)) fail(label + " needs a C++ build (node-gyp / binding.gyp): native addons are not supported on MyOS");
    // where it goes: the top node_modules if free, else under the package that needs it
    const top = path.join(modulesDir(), pkgName);
    const dir = job.explicit || from === root || !fs.existsSync(top) ? top : path.join(from, "node_modules", pkgName);
    if (placed.has(dir)) return { name: pkgName, version: pkg.version, dir };
    fs.rmSync(dir, { recursive: true, force: true });
    mkdirp(dir);
    extract(tar, entries, dir, label);
    placed.add(dir);
    added++;
    say("+ " + label);
    const scripts = pkg.scripts || {};
    if (scripts.preinstall || scripts.install || scripts.postinstall) say("npm warn " + label + " has an install script (not run on MyOS)");
    queueDeps(pkg, dir);
    return { name: pkgName, version: pkg.version, dir };
}

function queueDeps(pkg, dir) {
    const opt = pkg.optionalDependencies || {};
    const add = (deps, optional) => {
        for (const n of Object.keys(deps || {})) queue.push({ ...depSpec(n, deps[n], dir), from: dir, optional: optional || has(opt, n) });
    };
    add(pkg.dependencies, false);
    add(opt, true);
    add(pkg.peerDependencies, true);
}

function runQueue() {
    while (queue.length) {
        const job = queue.shift();
        try {
            installOne(job);
        } catch (e) {
            if (!(e instanceof NpmError) || !job.optional) throw e;
            say("npm warn skipped optional " + job.name + ": " + e.message);
        }
    }
}

function findRoot(global) {
    if (global) return "/";
    for (let d = process.cwd(); ; d = path.dirname(d)) {
        if (fs.existsSync(path.join(d, "package.json"))) return d;
        if (path.dirname(d) === d) return process.cwd();
    }
}

function install(args, flags) {
    const t0 = performance.now();
    const pkg = readPkg(root);
    const results = [];
    if (!args.length) {
        if (!pkg) fail("no package.json here: try 'npm init -y', or 'npm install NAME'");
        for (const field of ["dependencies", "devDependencies", "optionalDependencies"])
            for (const n of Object.keys(pkg[field] || {}))
                queue.push({ ...depSpec(n, pkg[field][n], root), from: root, optional: field === "optionalDependencies" });
        runQueue();
    } else {
        for (const a of args) {
            const s = parseSpec(a);
            const job = s.file ? { name: "", file: path.resolve(s.file), from: root, explicit: true }
                               : { name: s.name, range: s.range, from: root, explicit: true };
            const r = installOne(job);
            r.save = s.file ? "file:" + path.relative(root, job.file)
                   : isTag(s.range) ? "^" + r.version : s.range;
            results.push(r);
            runQueue();
        }
    }
    if (results.length && !flags.has("--no-save") && !flags.has("-g") && !flags.has("--global")) {
        const field = flags.has("-D") || flags.has("--save-dev") ? "devDependencies" : "dependencies";
        const p = pkg || {};
        p[field] = p[field] || {};
        for (const r of results) {
            for (const f of ["dependencies", "devDependencies"]) if (f !== field && p[f]) delete p[f][r.name];
            p[field][r.name] = r.save;
        }
        p[field] = sortKeys(p[field]);
        writePkg(p);
    }
    const secs = ((performance.now() - t0) / 1000).toFixed(1);
    say(added ? "added " + added + " package" + (added > 1 ? "s" : "") + " in " + secs + "s" : "up to date in " + secs + "s");
}

// ================================================================ ls / uninstall
// the packages in a node_modules folder: ["a", "@scope/b", ...]
function listModules(nm) {
    const out = [];
    let names = [];
    try { names = fs.readdirSync(nm); } catch (e) { return out; }
    for (const n of names) {
        if (n[0] === ".") continue;
        if (n[0] === "@") { try { for (const m of fs.readdirSync(nm + "/" + n)) out.push(n + "/" + m); } catch (e) {} } else out.push(n);
    }
    return out;
}
const depsOf = (pkg) => ({ ...(pkg.peerDependencies || {}), ...(pkg.optionalDependencies || {}), ...(pkg.dependencies || {}) });

// does an installed version fit what package.json asks for? (file:, npm: ... always do)
function fits(version, spec) {
    if (/^[\w+-]+:|\//.test(spec)) return true;
    try { return satisfies(version, spec); } catch (e) { return true; }
}

// what package.json asks for, directly
const wantedBy = (pkg) => (pkg ? { ...(pkg.devDependencies || {}), ...(pkg.optionalDependencies || {}), ...(pkg.dependencies || {}) } : {});
// the folders of every package that is needed (package.json's and theirs, all the way down)
function neededDirs(pkg) {
    const need = new Set(), todo = [[wantedBy(pkg), root]];
    while (todo.length) {
        const [deps, from] = todo.pop();
        for (const n of Object.keys(deps)) {
            const f = findInstalled(n, from);
            if (f && !need.has(f.dir)) { need.add(f.dir); todo.push([depsOf(f.pkg), f.dir]); }
        }
    }
    return need;
}
// installed at the top but needed by nothing
const extraneous = (pkg) => { const need = neededDirs(pkg); return listModules(modulesDir()).filter((n) => !need.has(path.join(modulesDir(), n))); };

function ls(flags) {
    const pkg = readPkg(root);
    const all = flags.has("--all") || flags.has("-a");
    say((pkg ? (pkg.name || "") + "@" + (pkg.version || "") + " " : "") + root);
    const wanted = wantedBy(pkg);
    const names = [...new Set([...Object.keys(wanted), ...extraneous(pkg)])].sort();
    const seen = new Set();
    const show = (names, want, from, prefix, top) => {
        names.forEach((n, i) => {
            const last = i === names.length - 1, f = findInstalled(n, from);
            const dup = !!f && !top && seen.has(f.dir);
            const kids = f && all && !dup ? Object.keys(depsOf(f.pkg)).sort() : [];
            let line = prefix + (last ? "└─" : "├─") + (kids.length ? "┬ " : "─ ");
            if (!f) line += (top && pkg && has(pkg.optionalDependencies, n) ? "UNMET OPTIONAL DEPENDENCY " : "UNMET DEPENDENCY ") + n + "@" + want[n];
            else {
                line += n + "@" + f.version;
                if (has(want, n) && !fits(f.version, String(want[n]))) line += " invalid: wanted " + want[n];
                if (top && !has(want, n)) line += " extraneous";
                if (dup) line += " deduped";
            }
            say(line);
            if (f) seen.add(f.dir);
            if (kids.length) {
                show(kids, depsOf(f.pkg), f.dir, prefix + (last ? "  " : "│ "), false);
            }
        });
    };
    if (!names.length) say("└── (empty)");
    show(names, wanted, root, "", true);
}

function rmPackage(dir) {
    fs.rmSync(dir, { recursive: true, force: true });
    const parent = path.dirname(dir);                 // an empty @scope folder too
    if (path.basename(parent)[0] === "@") { try { if (!fs.readdirSync(parent).length) fs.rmdirSync(parent); } catch (e) {} }
}

function uninstall(args, flags) {
    if (!args.length) fail("usage: npm uninstall NAME...");
    const pkg = readPkg(root);
    let removed = 0;
    for (const n of args) {
        const dir = path.join(modulesDir(), n);
        if (fs.existsSync(dir)) { rmPackage(dir); removed++; } else say("npm warn " + n + " is not installed");
        if (pkg) for (const f of ["dependencies", "devDependencies", "optionalDependencies"]) if (pkg[f]) delete pkg[f][n];
    }
    if (pkg && !flags.has("--no-save")) writePkg(pkg);
    // and what nothing needs any more (only with a package.json saying what is wanted)
    if (pkg) for (const n of extraneous(pkg)) { rmPackage(path.join(modulesDir(), n)); removed++; }
    say("removed " + removed + " package" + (removed === 1 ? "" : "s"));
}

// ================================================================ init / view / run
function init(flags) {
    const file = path.join(root, "package.json");
    if (fs.existsSync(file)) { say("package.json already exists:"); say(fs.readFileSync(file, "utf8")); return; }
    const base = path.basename(root) === "/" || !path.basename(root) ? "my-project" : path.basename(root).toLowerCase();
    const pkg = { name: base, version: "1.0.0", description: "", main: "index.js",
                  scripts: { start: "node index.js" }, keywords: [], author: "", license: "ISC" };
    if (!flags.has("-y") && !flags.has("--yes")) {
        for (const k of ["name", "version", "description", "main", "author", "license"]) {
            const a = prompt(k + ": (" + pkg[k] + ") ");
            if (a) pkg[k] = a;
        }
        pkg.scripts.start = "node " + pkg.main;
    }
    writePkg(pkg);
    say("Wrote to " + file + ":");
    say(JSON.stringify(pkg, null, 2));
}

function view(args) {
    if (!args.length) fail("usage: npm view NAME[@VERSION] [FIELD]");
    const s = parseSpec(args[0]);
    if (s.file) fail("npm view works on registry packages");
    const m = manifest(s.name, s.range);
    if (args[1]) {
        const v = args[1].split(".").reduce((o, k) => (o == null ? o : o[k]), m);
        say(typeof v === "string" ? v : JSON.stringify(v, null, 2));
        return;
    }
    const deps = Object.keys(m.dependencies || {});
    say(m.name + "@" + m.version + " | " + (typeof m.license === "string" ? m.license : "no licence") + " | deps: " + (deps.length || "none"));
    if (m.description) say(m.description);
    if (m.homepage) say(m.homepage);
    if (m.deprecated) say("DEPRECATED: " + m.deprecated);
    if (m.dist) say(NL() + "dist" + NL() + ".tarball: " + m.dist.tarball + NL() + ".shasum: " + m.dist.shasum +
                     (m.dist.integrity ? NL() + ".integrity: " + m.dist.integrity : "") +
                     (m.dist.unpackedSize ? NL() + ".unpackedSize: " + (m.dist.unpackedSize / 1024).toFixed(1) + " kB" : ""));
    if (deps.length) say(NL() + "dependencies:" + NL() + deps.map((d) => d + ": " + m.dependencies[d]).join(NL()));
    if (m.gypfile) say(NL() + "(a native addon: it can't be installed on MyOS)");
}
const NL = () => String.fromCharCode(10);

// scripts can't start other programs on MyOS, but "node FILE args" runs right here
function run(name) {
    const pkg = readPkg(root) || {};
    const script = (pkg.scripts || {})[name] || (name === "start" && fs.existsSync(path.join(root, "server.js")) ? "node server.js" : "");
    if (!script) fail("no script '" + name + "' in package.json" + (pkg.scripts ? " (there are: " + Object.keys(pkg.scripts).join(", ") + ")" : ""));
    say("> " + (pkg.name || "") + "@" + (pkg.version || "") + " " + name + NL() + "> " + script + NL());
    const m = /^node\s+(\S+)(.*)$/.exec(script.trim());
    if (!m || /[&|;<>]/.test(m[2])) fail("MyOS can only run scripts like 'node file.js ...'");
    let file = path.resolve(root, m[1]);
    if (!fs.existsSync(file) && fs.existsSync(file + ".js")) file += ".js";
    if (!fs.existsSync(file)) fail("no such file: " + m[1]);
    process.argv.splice(1, process.argv.length, file, ...m[2].trim().split(/\s+/).filter(Boolean));
    if (file.endsWith(".mjs") || N.detectModule(fs.readFileSync(file, "utf8"))) N.evalModule("import " + JSON.stringify(file) + ";", "<npm>");
    else globalThis.__myos_hooks.runMain(file);
}

// ================================================================ main
const HELP = [
    "npm for MyOS - packages from registry.npmjs.org (pure JavaScript)",
    "  npm install NAME[@VERSION]   add a package to ./node_modules (and package.json)",
    "  npm install ./file.tgz       install a package file",
    "  npm install                  everything package.json lists",
    "  npm uninstall NAME           remove one       npm ls [--all]   what's installed",
    "  npm view NAME [FIELD]        about a package  npm init [-y]    make package.json",
    "  npm run SCRIPT / start / test   (scripts of the form 'node file.js')",
    "Options: -D (devDependencies), --no-save, -g (into /node_modules: for every folder)",
];
const ALIASES = { i: "install", add: "install", in: "install", isntall: "install", ci: "install",
                  rm: "uninstall", r: "uninstall", un: "uninstall", remove: "uninstall", unlink: "uninstall",
                  list: "ls", la: "ls", ll: "ls", info: "view", show: "view", v: "view",
                  "run-script": "run", t: "test", tst: "test", create: "init" };

function main() {
    const argv = process.argv.slice(2);
    const flags = new Set(argv.filter((a) => a.startsWith("-")));
    const args = argv.filter((a) => !a.startsWith("-"));
    let cmd = args.shift();
    cmd = ALIASES[cmd] || cmd;
    if (!cmd) {
        say(flags.has("-v") || flags.has("--version") ? "10.0.0-myos" : HELP.join(NL()));
        return;
    }
    root = findRoot(flags.has("-g") || flags.has("--global"));
    try {
        if (cmd === "install") install(args, flags);
        else if (cmd === "uninstall") uninstall(args, flags);
        else if (cmd === "ls") ls(flags);
        else if (cmd === "init") { root = process.cwd(); init(flags); }
        else if (cmd === "view") view(args);
        else if (cmd === "run") args.length ? run(args[0]) : say("scripts: " + Object.keys((readPkg(root) || {}).scripts || {}).join(", "));
        else if (cmd === "start" || cmd === "test") run(cmd);
        else if (cmd === "help") say(HELP.join(NL()));
        else fail("unknown command '" + cmd + "' (try 'npm help')");
    } catch (e) {
        if (!(e instanceof NpmError) && !(e && /^E[A-Z]+:/.test(e.message))) throw e;
        say("npm error " + e.message);
        process.exitCode = 1;
    }
}

if (test) return { parseVersion, compare, satisfies, maxSatisfying, parseSpec, depSpec, isTag, b64hex, tarEntries, netError };
return main();
})
