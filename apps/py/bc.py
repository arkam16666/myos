# bc - the calculator language with numbers as big as you like
#   bc                interactive (type quit to leave)
#   bc -l             with the math library: s c a l e j, scale=20
#   bc FILE           run a program, then stay interactive
#   bc "2^200"        work it out and quit (anything that isn't a file)
# Like POSIX/GNU bc: scale, ibase, obase, last, variables, arrays,
# if/while/for, define f(x) { auto t; ... return (t) }, print, sqrt, length.
# @version 1.0
# @description bc calculator: huge numbers, scale, -l
import sys

# ----------------------------------------------------------------- numbers
# a number is (v, s): the value v / 10**s, v an exact Python integer
ZERO = (0, 0)
ONE = (1, 0)


class BcError(Exception):
    pass


class Incomplete(Exception):
    pass


def tdiv(a, b):
    """integer division rounding toward zero, like bc"""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def rescale(n, s):
    v, ns = n
    if s >= ns:
        return (v * 10 ** (s - ns), s)
    return (tdiv(v, 10 ** (ns - s)), s)


def align(a, b):
    s = max(a[1], b[1])
    return rescale(a, s)[0], rescale(b, s)[0], s


def n_add(a, b):
    x, y, s = align(a, b)
    return (x + y, s)


def n_sub(a, b):
    x, y, s = align(a, b)
    return (x - y, s)


def n_mul(a, b, scale):
    v, s = a[0] * b[0], a[1] + b[1]
    return rescale((v, s), min(s, max(scale, a[1], b[1])))


def n_div(a, b, scale):
    if b[0] == 0:
        raise BcError("divide by zero")
    return (tdiv(a[0] * 10 ** (b[1] + scale), b[0] * 10 ** a[1]), scale)


def n_mod(a, b, scale):
    if b[0] == 0:
        raise BcError("modulo by zero")
    q = n_div(a, b, scale)
    prod = (q[0] * b[0], q[1] + b[1])              # exact
    r = n_sub(a, prod)
    return rescale(r, max(scale + b[1], a[1]))


def n_int(a):
    return tdiv(a[0], 10 ** a[1])


def n_pow(a, e, scale):
    if e[1] and e[0] % 10 ** e[1]:
        raise BcError("non-zero scale in exponent")
    k = n_int(e)
    if k >= 0:
        v, s = a[0] ** k, a[1] * k
        return rescale((v, s), min(s, max(scale, a[1])))
    if a[0] == 0:
        raise BcError("divide by zero")
    p = (a[0] ** -k, a[1] * -k)
    return n_div(ONE, p, scale)


def isqrt(n):
    if n < 2:
        return n
    x = 10 ** ((len(str(n)) + 1) // 2)          # a bit more than the root
    while True:
        y = (x + n // x) // 2
        if y >= x:
            return x
        x = y


def n_sqrt(a, scale):
    if a[0] < 0:
        raise BcError("square root of a negative number")
    rs = max(scale, a[1])
    return (isqrt(a[0] * 10 ** (2 * rs - a[1])), rs)


def n_cmp(a, b):
    x, y, _ = align(a, b)
    return (x > y) - (x < y)


def n_bool(a):
    return a[0] != 0


def n_length(a):
    v, s = a
    digits = len(str(abs(v))) if v else 1
    if v == 0:
        return s if s else 1
    return max(digits, s)


DIGITS = "0123456789ABCDEF"


def n_parse(text, ibase):
    """a number as typed (digits 0-9 A-F, maybe a point) in base ibase"""
    if "." in text:
        ip, fp = text.split(".", 1)
    else:
        ip, fp = text, ""
    v = 0
    for ch in ip:
        v = v * ibase + DIGITS.index(ch)
    if not fp:
        return (v, 0)
    if ibase == 10:
        return (v * 10 ** len(fp) + int(fp), len(fp))
    num, den = 0, 1
    for ch in fp:
        num = num * ibase + DIGITS.index(ch)
        den *= ibase
    s = len(fp)
    return (v * 10 ** s + num * 10 ** s // den, s)


def zpad(text, width):
    return "0" * (width - len(text)) + text


def n_str(a, obase=10):
    v, s = a
    if v == 0:
        return "0"
    sign = "-" if v < 0 else ""
    v = abs(v)
    ip, fp = divmod(v, 10 ** s)
    if obase == 10:
        out = str(ip) if (ip or not s) else ""
        if s:
            out += "." + zpad(str(fp), s)
    else:
        wide = obase > 16                       # big bases: each digit as decimal groups
        width = len(str(obase - 1))
        dig = []
        n = ip
        while n:
            n, d = divmod(n, obase)
            dig.append(" " + zpad(str(d), width) if wide else DIGITS[d])
        out = "".join(reversed(dig)) if dig else ("" if s else ("0" if not wide else " " + "0" * width))
        if s:
            out += "."
            prec, f, den = 1, fp, 10 ** s
            while prec < den:                   # enough digits to show 10^-s
                f *= obase
                d, f = divmod(f, den)
                out += " " + zpad(str(d), width) if wide else DIGITS[d]
                prec *= obase
    out = sign + out
    lines = []
    while len(out) > 69:                        # bc splits long numbers with a backslash
        lines.append(out[:69] + "\\")
        out = out[69:]
    lines.append(out)
    return "\n".join(lines)


def n_from_int(i):
    return (i, 0)


# ----------------------------------------------------------------- the math library (bc -l)
# fixed-point numbers: an integer F standing for F / 10^p; every division
# rounds toward zero (tdiv) so the series end when the terms reach 0
def fixed(a, p):
    return rescale(a, p)[0]


def fmul(a, b, one):
    return tdiv(a * b, one)


def series_exp(x, p):
    """e^x"""
    one = 10 ** p
    k = 0
    while abs(x) > one // 2:                    # make it small, square back afterwards
        x = tdiv(x, 2)
        k += 1
    total, term, n = one, one, 1
    while term:
        term = tdiv(fmul(term, x, one), n)
        total += term
        n += 1
    for _ in range(k):
        total = fmul(total, total, one)
    return total


def series_atan(x, p, hyperbolic):
    """atan(x) or atanh(x) for a small x"""
    one = 10 ** p
    total, power, n, x2, sign = x, x, 3, fmul(x, x, one), 1 if hyperbolic else -1
    while power:
        power = fmul(power, x2, one)
        total += tdiv(power, n) if hyperbolic else sign * tdiv(power, n)
        sign, n = -sign, n + 2
    return total


def fixed_pi(p):
    """Machin: pi = 16 atan(1/5) - 4 atan(1/239)"""
    one = 10 ** p
    return 16 * series_atan(one // 5, p, False) - 4 * series_atan(one // 239, p, False)


def fixed_ln(x, p):
    if x <= 0:
        raise BcError("logarithm of a number <= 0")
    one = 10 ** p
    k = 0
    while x > 2 * one:
        x //= 2
        k += 1
    while x < one // 2:
        x *= 2
        k -= 1
    ln2 = 2 * series_atan(one // 3, p, True)
    return 2 * series_atan(tdiv((x - one) * one, x + one), p, True) + k * ln2


def fixed_atan(x, p):
    one = 10 ** p
    neg = x < 0
    x = abs(x)
    inv = x > one
    if inv:
        x = one * one // x
    halvings = 0
    while x > one // 10:                        # atan(x) = 2 atan(x / (1 + sqrt(1 + x^2)))
        x = x * one // (one + isqrt(one * one + x * x))
        halvings += 1
    total = series_atan(x, p, False) * (1 << halvings)
    if inv:
        total = fixed_pi(p) // 2 - total
    return -total if neg else total


def fixed_sin(x, p):
    one = 10 ** p
    two_pi = 2 * fixed_pi(p)
    x = x % two_pi
    if x > two_pi // 2:
        x -= two_pi
    total, term, n, x2 = x, x, 1, fmul(x, x, one)
    while term:
        term = tdiv(-fmul(term, x2, one), (n + 1) * (n + 2))
        total += term
        n += 2
    return total


def math_fn(name, args, scale):
    p = scale + 10                              # work with spare digits
    one = 10 ** p
    if name == "j":                             # Bessel J_n(x)
        n = abs(n_int(args[0]))
        half = tdiv(fixed(args[1], p), 2)
        term = one
        for k in range(1, n + 1):
            term = tdiv(fmul(term, half, one), k)
        total, k = term, 0
        while term:
            k += 1
            term = tdiv(-fmul(fmul(term, half, one), half, one), k * (n + k))
            total += term
        return rescale((total, p), scale)
    x = fixed(args[0], p)
    if name == "s":
        r = fixed_sin(x, p)
    elif name == "c":
        r = fixed_sin(x + fixed_pi(p) // 2, p)
    elif name == "a":
        r = fixed_atan(x, p)
    elif name == "l":
        r = fixed_ln(x, p)
    else:                                       # e
        r = series_exp(x, p)
    return rescale((r, p), scale)


# ----------------------------------------------------------------- reading the program
KEYWORDS = ("if", "else", "while", "for", "break", "continue", "return", "define", "auto",
            "print", "quit", "halt", "length", "sqrt", "scale", "ibase", "obase", "last", "read", "limits")


def tokenize(src):
    toks = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == "\\" and i + 1 < n and src[i + 1] == "\n":   # line continues
            i += 2
            continue
        if c in " \t\r":
            i += 1
        elif c == "\n":
            toks.append(("nl", "\n"))
            i += 1
        elif c == "#":
            while i < n and src[i] != "\n":
                i += 1
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            if j < 0:
                raise Incomplete()
            if "\n" in src[i:j]:
                toks.append(("nl", "\n"))
            i = j + 2
        elif c == '"':
            j = src.find('"', i + 1)
            if j < 0:
                raise Incomplete()
            toks.append(("str", src[i + 1:j]))
            i = j + 1
        elif c.isdigit() or ("A" <= c <= "F") or (c == "." and i + 1 < n and (src[i + 1].isdigit() or "A" <= src[i + 1] <= "F")):
            j = i
            while j < n and (src[j].isdigit() or "A" <= src[j] <= "F" or src[j] == "."):
                j += 1
            toks.append(("num", src[i:j]))
            i = j
        elif c.isalpha() or c == "_":
            j = i
            while j < n and (src[j].isalpha() or src[j].isdigit() or src[j] == "_"):
                j += 1
            w = src[i:j]
            toks.append(("kw" if w in KEYWORDS else "name", w))
            i = j
        else:
            for op in ("++", "--", "+=", "-=", "*=", "/=", "%=", "^=", "==", "<=", ">=", "!=", "&&", "||"):
                if src.startswith(op, i):
                    toks.append(("op", op))
                    i += 2
                    break
            else:
                if c in "+-*/%^=<>!()[]{},;.":
                    toks.append(("op", c))
                    i += 1
                else:
                    raise BcError("illegal character: %s" % c)
    toks.append(("eof", ""))
    return toks


class Parser:
    def __init__(self, toks):
        self.t = toks
        self.i = 0

    def peek(self, k=0):
        return self.t[min(self.i + k, len(self.t) - 1)]

    def next(self):
        tok = self.t[self.i]
        if tok[0] == "eof":
            raise Incomplete()
        self.i += 1
        return tok

    def accept(self, val):
        if self.peek()[1] == val and self.peek()[0] in ("op", "kw"):
            self.i += 1
            return True
        return False

    def expect(self, val):
        if self.peek()[0] == "eof":
            raise Incomplete()
        if not self.accept(val):
            raise BcError("syntax error: expected '%s' near '%s'" % (val, self.peek()[1]))

    def skip_nl(self):
        while self.peek()[0] == "nl" or self.peek()[1] == ";":
            self.i += 1

    def program(self):
        stmts = []
        while True:
            self.skip_nl()
            if self.peek()[0] == "eof":
                return stmts
            stmts.append(self.statement())
            if self.peek()[0] not in ("nl", "eof") and self.peek()[1] not in (";", "}"):
                raise BcError("syntax error near '%s'" % self.peek()[1])

    def block_body(self):
        stmts = []
        while True:
            self.skip_nl()
            if self.peek()[0] == "eof":
                raise Incomplete()
            if self.accept("}"):
                return stmts
            stmts.append(self.statement())

    def statement(self):
        tok = self.peek()
        if tok[0] == "str":
            self.i += 1
            return ("str", tok[1])
        if tok[1] == "{" and tok[0] == "op":
            self.i += 1
            return ("block", self.block_body())
        if tok[0] == "kw":
            w = tok[1]
            if w == "if":
                self.i += 1
                self.expect("(")
                cond = self.expr()
                self.expect(")")
                then = self.body()
                save = self.i
                self.skip_nl()
                if self.accept("else"):
                    return ("if", cond, then, self.body())
                self.i = save
                return ("if", cond, then, None)
            if w == "while":
                self.i += 1
                self.expect("(")
                cond = self.expr()
                self.expect(")")
                return ("while", cond, self.body())
            if w == "for":
                self.i += 1
                self.expect("(")
                init = None if self.peek()[1] == ";" else self.expr()
                self.expect(";")
                cond = None if self.peek()[1] == ";" else self.expr()
                self.expect(";")
                step = None if self.peek()[1] == ")" else self.expr()
                self.expect(")")
                return ("for", init, cond, step, self.body())
            if w in ("break", "continue", "quit", "halt", "limits"):
                self.i += 1
                return (w,)
            if w == "return":
                self.i += 1
                if self.peek()[0] in ("nl", "eof") or self.peek()[1] in (";", "}"):
                    return ("return", None)
                return ("return", self.expr())
            if w == "print":
                self.i += 1
                items = []
                while True:
                    if self.peek()[0] == "str":
                        items.append(("str", self.next()[1]))
                    else:
                        items.append(self.expr())
                    if not self.accept(","):
                        return ("print", items)
            if w == "define":
                return self.define()
        return ("expr", self.expr())

    def body(self):
        while self.peek()[0] == "nl":
            self.i += 1
        if self.peek()[0] == "eof":
            raise Incomplete()
        return self.statement()

    def define(self):
        self.i += 1
        name = self.next()
        if name[0] != "name" and name[1] not in ("s", "c", "a", "l", "e", "j"):
            raise BcError("bad function name")
        self.expect("(")
        params = []
        while not self.accept(")"):
            p = self.next()[1]
            if self.accept("["):
                self.expect("]")
                p += "[]"
            params.append(p)
            self.accept(",")
        self.skip_nl_only()
        self.expect("{")
        self.skip_nl()
        autos = []
        if self.accept("auto"):
            while True:
                a = self.next()[1]
                if self.accept("["):
                    self.expect("]")
                    a += "[]"
                autos.append(a)
                if not self.accept(","):
                    break
        return ("define", name[1], params, autos, self.block_body())

    def skip_nl_only(self):
        while self.peek()[0] == "nl":
            self.i += 1

    # expressions, lowest precedence first (POSIX bc's odd order: relational
    # is below assignment)
    def expr(self):
        left = self.and_()
        while self.accept("||"):
            left = ("or", left, self.and_())
        return left

    def and_(self):
        left = self.not_()
        while self.accept("&&"):
            left = ("and", left, self.not_())
        return left

    def not_(self):
        if self.accept("!"):
            return ("not", self.not_())
        return self.rel()

    def rel(self):
        left = self.assign()
        while self.peek()[0] == "op" and self.peek()[1] in ("<", "<=", ">", ">=", "==", "!="):
            op = self.next()[1]
            left = ("rel", op, left, self.assign())
        return left

    def assign(self):
        left = self.add()
        if self.peek()[0] == "op" and self.peek()[1] in ("=", "+=", "-=", "*=", "/=", "%=", "^="):
            if left[0] not in ("var", "elem", "special"):
                raise BcError("can only assign to a variable")
            op = self.next()[1]
            return ("assign", op, left, self.assign())
        return left

    def add(self):
        left = self.mul()
        while self.peek()[0] == "op" and self.peek()[1] in ("+", "-"):
            op = self.next()[1]
            left = ("bin", op, left, self.mul())
        return left

    def mul(self):
        left = self.power()
        while self.peek()[0] == "op" and self.peek()[1] in ("*", "/", "%"):
            op = self.next()[1]
            left = ("bin", op, left, self.power())
        return left

    def power(self):
        base = self.unary()
        if self.accept("^"):
            return ("bin", "^", base, self.power())
        return base

    def unary(self):
        if self.accept("-"):
            return ("neg", self.unary())
        if self.accept("+"):
            return self.unary()
        if self.peek()[1] in ("++", "--") and self.peek()[0] == "op":
            op = self.next()[1]
            target = self.unary()
            if target[0] not in ("var", "elem", "special"):
                raise BcError("++/-- needs a variable")
            return ("pre", op, target)
        return self.postfix()

    def postfix(self):
        e = self.primary()
        if self.peek()[1] in ("++", "--") and self.peek()[0] == "op" and e[0] in ("var", "elem", "special"):
            return ("post", self.next()[1], e)
        return e

    def primary(self):
        tok = self.next()
        kind, val = tok
        if kind == "num":
            return ("num", val)
        if kind == "op" and val == "(":
            e = self.expr()
            self.expect(")")
            return e
        if kind == "op" and val == ".":
            return ("special", "last")
        if kind == "kw":
            if val in ("scale", "ibase", "obase", "last"):
                if val == "scale" and self.accept("("):
                    e = self.expr()
                    self.expect(")")
                    return ("scale_of", e)
                return ("special", val)
            if val in ("length", "sqrt"):
                self.expect("(")
                e = self.expr()
                self.expect(")")
                return (val, e)
            if val == "read":
                self.expect("(")
                self.expect(")")
                return ("read",)
        if kind == "name":
            if self.accept("("):
                args = []
                while not self.accept(")"):
                    if self.peek()[0] == "name" and self.peek(1)[1] == "[" and self.peek(2)[1] == "]":
                        args.append(("array", self.next()[1]))
                        self.i += 2
                    else:
                        args.append(self.expr())
                    self.accept(",")
                return ("call", val, args)
            if self.accept("["):
                idx = self.expr()
                self.expect("]")
                return ("elem", val, idx)
            return ("var", val)
        if kind == "eof":
            raise Incomplete()
        raise BcError("syntax error near '%s'" % val)


# ----------------------------------------------------------------- running it
class Break(Exception):
    pass


class Continue(Exception):
    pass


class Return(Exception):
    def __init__(self, value):
        self.value = value


class Quit(Exception):
    pass


class Bc:
    def __init__(self, mathlib):
        self.scale, self.ibase, self.obase = 0, 10, 10
        self.last = ZERO
        self.globals = {}
        self.arrays = {}
        self.funcs = {}
        self.frames = []
        self.mathlib = mathlib
        if mathlib:
            self.scale = 20

    # variables: a function's parameters and autos hide the globals
    def lookup(self, name, arrays=False):
        for frame in reversed(self.frames):
            table = frame[1] if arrays else frame[0]
            if name in table:
                return table
            break                               # bc has dynamic scope, but one level is enough here
        return self.arrays if arrays else self.globals

    def get_var(self, name):
        return self.lookup(name).get(name, ZERO)

    def get_elem(self, name, idx):
        arr = self.lookup(name, True).setdefault(name, {})
        return arr.get(idx, ZERO)

    def store(self, target, value):
        if target[0] == "var":
            self.lookup(target[1])[target[1]] = value
        elif target[0] == "elem":
            idx = n_int(self.eval(target[2]))
            if idx < 0 or idx > 65535:
                raise BcError("array index out of range")
            self.lookup(target[1], True).setdefault(target[1], {})[idx] = value
        else:
            name = target[1]
            if name == "scale":
                s = n_int(value)
                if s < 0 or s > 1000:
                    raise BcError("scale must be 0 to 1000")
                self.scale = s
            elif name in ("ibase", "obase"):
                b = n_int(value)
                lo, hi = (2, 16) if name == "ibase" else (2, 1000)
                if not lo <= b <= hi:
                    raise BcError("%s must be %d to %d" % (name, lo, hi))
                setattr(self, name, b)
            else:
                self.last = value

    def load(self, target):
        if target[0] == "var":
            return self.get_var(target[1])
        if target[0] == "elem":
            return self.get_elem(target[1], n_int(self.eval(target[2])))
        name = target[1]
        if name == "last":
            return self.last
        return n_from_int(getattr(self, name))

    def binop(self, op, a, b):
        if op == "+":
            return n_add(a, b)
        if op == "-":
            return n_sub(a, b)
        if op == "*":
            return n_mul(a, b, self.scale)
        if op == "/":
            return n_div(a, b, self.scale)
        if op == "%":
            return n_mod(a, b, self.scale)
        return n_pow(a, b, self.scale)

    def eval(self, e):
        k = e[0]
        if k == "num":
            return n_parse(e[1], self.ibase)
        if k in ("var", "elem", "special"):
            return self.load(e)
        if k == "bin":
            return self.binop(e[1], self.eval(e[2]), self.eval(e[3]))
        if k == "neg":
            v = self.eval(e[1])
            return (-v[0], v[1])
        if k == "assign":
            op, target = e[1], e[2]
            value = self.eval(e[3])
            if op != "=":
                value = self.binop(op[0], self.load(target), value)
            self.store(target, value)
            return value
        if k in ("pre", "post"):
            old = self.load(e[2])
            new = n_add(old, ONE) if e[1] == "++" else n_sub(old, ONE)
            self.store(e[2], new)
            return new if k == "pre" else old
        if k == "rel":
            c = n_cmp(self.eval(e[2]), self.eval(e[3]))
            r = {"<": c < 0, "<=": c <= 0, ">": c > 0, ">=": c >= 0, "==": c == 0, "!=": c != 0}[e[1]]
            return ONE if r else ZERO
        if k == "and":
            return ONE if n_bool(self.eval(e[1])) and n_bool(self.eval(e[2])) else ZERO
        if k == "or":
            return ONE if n_bool(self.eval(e[1])) or n_bool(self.eval(e[2])) else ZERO
        if k == "not":
            return ZERO if n_bool(self.eval(e[1])) else ONE
        if k == "sqrt":
            return n_sqrt(self.eval(e[1]), self.scale)
        if k == "length":
            return n_from_int(n_length(self.eval(e[1])))
        if k == "scale_of":
            return n_from_int(self.eval(e[1])[1])
        if k == "read":
            try:
                text = input()
            except EOFError:
                text = "0"
            return self.eval(Parser(tokenize(text)).expr())
        if k == "call":
            return self.call(e[1], e[2])
        raise BcError("cannot work out %s" % k)

    def call(self, name, args):
        if name not in self.funcs:
            if self.mathlib and name in ("s", "c", "a", "l", "e", "j"):
                vals = [self.eval(a) for a in args]
                if len(vals) != (2 if name == "j" else 1):
                    raise BcError("%s() takes %d argument(s)" % (name, 2 if name == "j" else 1))
                return math_fn(name, vals, self.scale)
            raise BcError("function %s() is not defined" % name)
        params, autos, body = self.funcs[name]
        if len(args) != len(params):
            raise BcError("%s() takes %d argument(s)" % (name, len(params)))
        if len(self.frames) > 150:
            raise BcError("too much recursion")
        vars_, arrs = {}, {}
        for p, a in zip(params, args):
            if p.endswith("[]"):
                if a[0] != "array":
                    raise BcError("%s() wants an array" % name)
                src = self.lookup(a[1], True).get(a[1], {})
                arrs[p[:-2]] = dict(src)         # arrays are passed by value
            else:
                vars_[p] = self.eval(a)
        for v in autos:
            if v.endswith("[]"):
                arrs[v[:-2]] = {}
            else:
                vars_[v] = ZERO
        self.frames.append((vars_, arrs))
        try:
            self.run_list(body)
            return ZERO
        except Return as r:
            return r.value
        finally:
            self.frames.pop()

    def show(self, value):
        print(n_str(value, self.obase))

    def run(self, s):
        k = s[0]
        if k == "expr":
            e = s[1]
            v = self.eval(e)
            if e[0] != "assign":
                self.show(v)
                self.last = v
        elif k == "str":
            print(s[1], end="")
        elif k == "print":
            for item in s[1]:
                if item[0] == "str":
                    print(unescape(item[1]), end="")
                else:
                    v = self.eval(item)
                    self.last = v
                    print(n_str(v, self.obase), end="")
        elif k == "block":
            self.run_list(s[1])
        elif k == "if":
            if n_bool(self.eval(s[1])):
                self.run(s[2])
            elif s[3]:
                self.run(s[3])
        elif k == "while":
            while n_bool(self.eval(s[1])):
                try:
                    self.run(s[2])
                except Break:
                    break
                except Continue:
                    pass
        elif k == "for":
            if s[1]:
                self.eval(s[1])
            while s[2] is None or n_bool(self.eval(s[2])):
                try:
                    self.run(s[4])
                except Break:
                    break
                except Continue:
                    pass
                if s[3]:
                    self.eval(s[3])
        elif k == "break":
            raise Break()
        elif k == "continue":
            raise Continue()
        elif k == "return":
            raise Return(self.eval(s[1]) if s[1] else ZERO)
        elif k == "define":
            self.funcs[s[1]] = (s[2], s[3], s[4])
        elif k in ("quit", "halt"):
            raise Quit()
        elif k == "limits":
            print("BC_BASE_MAX     = 1000\nBC_SCALE_MAX    = 1000\nBC_DIM_MAX      = 65535\n"
                  "Numbers have no size limit (only memory).")

    def run_list(self, stmts):
        for s in stmts:
            self.run(s)

    def execute(self, src):
        """run source text; raises Incomplete if it needs more lines"""
        prog = Parser(tokenize(src)).program()
        for s in prog:
            if s[0] in ("break", "continue", "return") and not self.frames:
                raise BcError("%s outside a loop or function" % s[0])
            try:
                self.run(s)
            except (Break, Continue):
                pass
            except Return:
                raise BcError("return outside a function")


def unescape(s):
    out, i = "", 0
    table = {"n": "\n", "t": "\t", "\\": "\\", '"': '"', "a": "\a", "b": "\b", "f": "\f", "r": "\r", "q": '"', "e": "\\"}
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s):
            out += table.get(s[i + 1], s[i + 1])
            i += 2
        else:
            out += s[i]
            i += 1
    return out


def run_text(bc, src, interactive):
    try:
        bc.execute(src)
    except Incomplete:
        if not interactive:
            print("Parse error: the program ends too soon")
    except BcError as e:
        print("Runtime error: %s" % e)
    except RuntimeError:                        # Python's own recursion limit
        print("Runtime error: too much recursion")


def main():
    args = sys.argv[1:]
    mathlib = False
    quiet = False
    while args and args[0].startswith("-") and len(args[0]) > 1 and not args[0][1].isdigit():
        a = args.pop(0)
        if "l" in a:
            mathlib = True
        if "q" in a:
            quiet = True
        if a in ("-h", "--help"):
            print("usage: bc [-l] [-q] [FILE | \"EXPRESSION\"]")
            return
    bc = Bc(mathlib)
    try:
        for i, a in enumerate(args):
            try:
                with open(a) as f:
                    src = f.read()
            except OSError:
                # not a file: the rest of the line is bc code (bc "2^100"); the
                # quotes may still be there, as Python on MyOS splits at spaces
                src = " ".join(args[i:])
                if len(src) > 1 and src[0] == src[-1] and src[0] in "\"'":
                    src = src[1:-1]
                run_text(bc, src + "\n", False)
                return
            run_text(bc, src + "\n", False)
        if not quiet:
            print("bc for MyOS (numbers of any size)%s. Type quit to leave." % (" with -l math" if mathlib else ""))
        pending = ""
        while True:
            try:
                line = input("" if not pending else "")
            except EOFError:
                break
            pending += line + "\n"
            try:
                bc.execute(pending)
                pending = ""
            except Incomplete:
                continue
            except BcError as e:
                print("Runtime error: %s" % e)
                pending = ""
            except RuntimeError:
                print("Runtime error: too much recursion")
                pending = ""
            except KeyboardInterrupt:
                pending = ""
    except Quit:
        pass


main()
