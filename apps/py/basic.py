# basic - a classic line-numbered BASIC
#   basic              type a program: 10 PRINT "HELLO", then RUN
#   basic prog.bas     run a program file
# Commands: RUN LIST NEW SAVE "f" LOAD "f" BYE.  Statements: PRINT LET INPUT IF..THEN
# GOTO GOSUB RETURN FOR..TO..STEP NEXT END REM DIM, several per line with ':'.
# Functions: RND INT ABS SQR SIN COS LEN VAL ASC STR$ CHR$ LEFT$ RIGHT$ MID$
# @version 1.0
# @description BASIC language (line numbers, GOTO...)
import math
import random
import sys


class BasicError(Exception):
    pass


def tokenize(s):
    toks, i = [], 0
    while i < len(s):
        c = s[i]
        if c in " \t":
            i += 1
        elif c == '"':
            j = s.find('"', i + 1)
            if j < 0:
                j = len(s)
            toks.append(("str", s[i + 1:j]))
            i = j + 1
        elif c.isdigit() or (c == "." and i + 1 < len(s) and s[i + 1].isdigit()):
            j = i
            while j < len(s) and (s[j].isdigit() or s[j] == "."):
                j += 1
            if j < len(s) and s[j] in "eE" and j + 1 < len(s) and (s[j + 1].isdigit() or s[j + 1] in "+-"):
                j += 2
                while j < len(s) and s[j].isdigit():
                    j += 1
            toks.append(("num", float(s[i:j])))
            i = j
        elif c.isalpha():
            j = i
            while j < len(s) and (s[j].isalpha() or s[j].isdigit() or s[j] == "_"):   # (no isalnum in MicroPython)
                j += 1
            if j < len(s) and s[j] == "$":
                j += 1
            word = s[i:j].upper()
            toks.append(("id", word))
            i = j
            if word == "REM":
                toks.append(("str", s[i:]))
                break
        elif s[i:i + 2] in ("<=", ">=", "<>"):
            toks.append(("op", s[i:i + 2]))
            i += 2
        else:
            toks.append(("op", c))
            i += 1
    return toks


FUNCS = ("RND", "INT", "ABS", "SQR", "SIN", "COS", "TAN", "ATN", "EXP", "LOG", "SGN", "LEN", "VAL",
         "ASC", "STR$", "CHR$", "LEFT$", "RIGHT$", "MID$")


class Basic:
    def __init__(self):
        self.lines = {}
        self.vars = {}
        self.arrays = {}
        self.mid_line = False

    # ---------------------------------------------------------- expressions
    def peek(self):
        return self.toks[self.pos] if self.pos < len(self.toks) else (None, None)

    def take(self, val=None):
        t = self.peek()
        if val is not None and t[1] != val:
            raise BasicError("expected %s" % val)
        self.pos += 1
        return t

    def expr(self):
        a = self.and_expr()
        while self.peek()[1] == "OR":
            self.take()
            b = self.and_expr()
            a = -1.0 if (a or b) else 0.0
        return a

    def and_expr(self):
        a = self.not_expr()
        while self.peek()[1] == "AND":
            self.take()
            b = self.not_expr()
            a = -1.0 if (a and b) else 0.0
        return a

    def not_expr(self):
        if self.peek()[1] == "NOT":
            self.take()
            return 0.0 if self.not_expr() else -1.0
        return self.compare()

    def compare(self):
        a = self.add()
        while self.peek()[1] in ("=", "<", ">", "<=", ">=", "<>"):
            op = self.take()[1]
            b = self.add()
            if type(a) != type(b):
                raise BasicError("type mismatch")
            r = {"=": a == b, "<": a < b, ">": a > b, "<=": a <= b, ">=": a >= b, "<>": a != b}[op]
            a = -1.0 if r else 0.0
        return a

    def add(self):
        a = self.mul()
        while self.peek()[1] in ("+", "-"):
            op = self.take()[1]
            b = self.mul()
            if op == "+":
                if type(a) != type(b):
                    raise BasicError("type mismatch")
                a = a + b
            else:
                a = self.num(a) - self.num(b)
        return a

    def mul(self):
        a = self.power()
        while self.peek()[1] in ("*", "/", "MOD"):
            op = self.take()[1]
            b = self.num(self.power())
            a = self.num(a)
            if op != "*" and b == 0:
                raise BasicError("division by zero")
            a = a * b if op == "*" else a / b if op == "/" else a % b
        return a

    def power(self):
        a = self.unary()
        if self.peek()[1] == "^":
            self.take()
            a = self.num(a) ** self.num(self.power())
        return a

    def unary(self):
        if self.peek()[1] == "-":
            self.take()
            return -self.num(self.unary())
        if self.peek()[1] == "+":
            self.take()
        return self.atom()

    def num(self, v):
        if isinstance(v, str):
            raise BasicError("type mismatch")
        return v

    def atom(self):
        kind, val = self.take()
        if kind in ("num", "str"):
            return val
        if val == "(":
            v = self.expr()
            self.take(")")
            return v
        if kind == "id" and val in FUNCS:
            self.take("(")
            args = [self.expr()]
            while self.peek()[1] == ",":
                self.take()
                args.append(self.expr())
            self.take(")")
            return self.call(val, args)
        if kind == "id":
            if self.peek()[1] == "(":
                return self.arrays.get(val, {}).get(tuple(self.indices()), "" if val.endswith("$") else 0.0)
            return self.vars.get(val, "" if val.endswith("$") else 0.0)
        raise BasicError("syntax error")

    def indices(self):
        self.take("(")
        idx = [int(self.num(self.expr()))]
        while self.peek()[1] == ",":
            self.take()
            idx.append(int(self.num(self.expr())))
        self.take(")")
        return idx

    def call(self, f, a):
        x = a[0]
        if f == "RND":
            return random.random() if x <= 1 else float(random.randint(1, int(x)))
        if f == "INT":
            return float(math.floor(x))
        if f == "ABS":
            return abs(x)
        if f == "SQR":
            return math.sqrt(x)
        if f in ("SIN", "COS", "TAN", "EXP", "LOG"):
            return getattr(math, f.lower())(x)
        if f == "ATN":
            return math.atan(x)
        if f == "SGN":
            return float((x > 0) - (x < 0))
        if f == "LEN":
            return float(len(x))
        if f == "VAL":
            try:
                return float(x)
            except ValueError:
                return 0.0
        if f == "ASC":
            return float(ord(x[0])) if x else 0.0
        if f == "STR$":
            return fmt(x)
        if f == "CHR$":
            return chr(int(x))
        if f == "LEFT$":
            return x[:int(a[1])]
        if f == "RIGHT$":
            return x[len(x) - int(a[1]):] if int(a[1]) > 0 else ""
        if f == "MID$":
            start = int(a[1]) - 1
            return x[start:start + int(a[2])] if len(a) > 2 else x[start:]

    # ---------------------------------------------------------- statements
    def assign(self, name):
        if self.peek()[1] == "(":
            idx = tuple(self.indices())
            self.take("=")
            v = self.expr()
            self.arrays.setdefault(name, {})[idx] = v
        else:
            self.take("=")
            v = self.expr()
            if name.endswith("$") != isinstance(v, str):
                raise BasicError("type mismatch")
            self.vars[name] = v

    def statement(self):
        kind, word = self.take()
        if kind != "id":
            raise BasicError("syntax error")
        if word == "REM":
            self.pos = len(self.toks)
        elif word == "PRINT" or word == "?":
            out, newline = "", True
            while self.peek()[0] is not None and self.peek()[1] != ":":
                if self.peek()[1] == ";":
                    self.take()
                    newline = False
                    continue
                if self.peek()[1] == ",":
                    self.take()
                    out += " " * (14 - len(out) % 14)
                    newline = False
                    continue
                v = self.expr()                 # numbers get a space for the sign and after
                out += v if isinstance(v, str) else (" " if v >= 0 else "") + fmt(v) + " "
                newline = True
            print(out, end="\n" if newline else "")
            self.mid_line = not newline
        elif word == "LET":
            self.assign(self.take()[1])
        elif word == "INPUT":
            prompt = "? "
            if self.peek()[0] == "str":
                prompt = self.take()[1]
                if self.peek()[1] in (";", ","):
                    prompt += "? " if self.take()[1] == ";" else ""
            names = [self.take()[1]]
            while self.peek()[1] == ",":
                self.take()
                names.append(self.take()[1])
            for name in names:
                text = input(prompt)
                prompt = "?? "
                if name.endswith("$"):
                    self.vars[name] = text
                else:
                    try:
                        self.vars[name] = float(text)
                    except ValueError:
                        self.vars[name] = 0.0
        elif word == "IF":
            cond = self.expr()
            self.take("THEN")
            if not cond:
                self.pos = len(self.toks)       # skip the rest of the line
            elif self.peek()[0] == "num":
                self.jump(int(self.take()[1]))
                return
            return                              # the statements after THEN run next
        elif word == "GOTO":
            self.jump(int(self.num(self.expr())))
            return
        elif word == "GOSUB":
            target = int(self.num(self.expr()))
            self.stack.append(("gosub", self.line_i, self.pos))
            self.jump(target)
            return
        elif word == "RETURN":
            while self.stack and self.stack[-1][0] != "gosub":
                self.stack.pop()
            if not self.stack:
                raise BasicError("RETURN without GOSUB")
            _, line_i, pos = self.stack.pop()
            self.go(line_i, pos)
            return
        elif word == "FOR":
            var = self.take()[1]
            self.take("=")
            self.vars[var] = self.num(self.expr())
            self.take("TO")
            limit = self.num(self.expr())
            step = 1.0
            if self.peek()[1] == "STEP":
                self.take()
                step = self.num(self.expr())
            self.stack.append(("for", var, limit, step, self.line_i, self.pos))
        elif word == "NEXT":
            var = self.take()[1] if self.peek()[0] == "id" else None
            while self.stack and self.stack[-1][0] == "for" and var and self.stack[-1][1] != var:
                self.stack.pop()
            if not self.stack or self.stack[-1][0] != "for":
                raise BasicError("NEXT without FOR")
            _, v, limit, step, li, pos = self.stack[-1]
            self.vars[v] += step
            if (step >= 0 and self.vars[v] <= limit) or (step < 0 and self.vars[v] >= limit):
                self.go(li, pos)                # back to just after the FOR
                return
            self.stack.pop()
        elif word == "DIM":
            while True:
                name = self.take()[1]
                self.indices()
                self.arrays.setdefault(name, {})
                if self.peek()[1] != ",":
                    break
                self.take()
        elif word in ("END", "STOP"):
            self.go(len(self.code), 0)
            return
        elif word == "CLS":
            print("\x1b[2J\x1b[H", end="")
        else:
            self.assign(word)                   # A = 5 without LET
        if self.peek()[0] is not None and self.peek()[1] != ":":
            raise BasicError("syntax error")

    def go(self, line_i, pos):
        """continue at line index line_i, token pos (GOTO, NEXT, RETURN, END)"""
        self.line_i, self.pos = line_i, pos
        self.transferred = True

    def jump(self, number):
        for i, (n, _) in enumerate(self.code):
            if n == number:
                self.go(i, 0)
                return
        raise BasicError("no line %d" % number)

    def exec_line(self):
        """run the current line from self.pos until its end or a jump"""
        self.transferred = False
        self.toks = self.code[self.line_i][1]
        while self.pos < len(self.toks):
            if self.peek()[1] == ":":
                self.take()
                continue
            self.statement()                    # (each statement checks its own end)
            if self.transferred:
                return

    def run(self):
        self.code = [(n, tokenize(self.lines[n])) for n in sorted(self.lines)]
        self.vars, self.arrays, self.stack = {}, {}, []
        self.line_i, self.pos = 0, 0
        try:
            while self.line_i < len(self.code):
                self.exec_line()
                if not self.transferred:
                    self.line_i, self.pos = self.line_i + 1, 0
        except BasicError as e:
            n = self.code[self.line_i][0] if self.line_i < len(self.code) else "?"
            print("Error in line %s: %s" % (n, e))
        except KeyboardInterrupt:
            print("Break")
        if self.mid_line:                       # the program ended with PRINT ...;
            print()
            self.mid_line = False

    # ---------------------------------------------------------- the editor
    def command(self, text):
        text = text.strip()
        if not text:
            return True
        if text[0].isdigit():
            num, _, rest = text.partition(" ")
            if not num.isdigit():
                print("Bad line number")
            elif rest.strip():
                self.lines[int(num)] = rest.strip()
            else:
                self.lines.pop(int(num), None)
            return True
        word = text.split()[0].upper()
        if word == "RUN":
            self.run()
        elif word == "LIST":
            for n in sorted(self.lines):
                print(n, self.lines[n])
        elif word == "NEW":
            self.lines = {}
        elif word in ("SAVE", "LOAD"):
            name = text[4:].strip().strip('"')
            if not name:
                print("Usage: %s \"file.bas\"" % word)
            elif word == "SAVE":
                with open(name, "w") as f:
                    f.write("\n".join("%d %s" % (n, self.lines[n]) for n in sorted(self.lines)) + "\n")
                print("Saved", name)
            else:
                self.load(name)
        elif word in ("BYE", "EXIT", "QUIT"):
            return False
        else:                                   # immediate mode: PRINT 2+2
            self.code = [(0, tokenize(text))]
            self.line_i, self.pos, self.stack = 0, 0, []
            try:
                self.exec_line()
            except BasicError as e:
                print("Error:", e)
            if self.mid_line:
                print()
                self.mid_line = False
        return True

    def load(self, name):
        try:
            with open(name) as f:
                text = f.read()
        except OSError:
            print("Can't open", name)
            return False
        self.lines = {}
        for line in text.split("\n"):
            line = line.strip()
            if line:
                self.command(line)
        return True


def fmt(v):
    if v == int(v) and abs(v) < 1e15:
        return str(int(v))
    return "%g" % v


def main():
    b = Basic()
    if len(sys.argv) > 1:
        if b.load(sys.argv[1]):
            b.run()
        return
    print("MyOS BASIC. Type lines like:  10 PRINT \"HELLO\"   then RUN.  BYE to leave.")
    while True:
        try:
            if not b.command(input("] ")):
                break
        except KeyboardInterrupt:
            print()


main()
