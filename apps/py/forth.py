# forth - a small Forth: a language where everything works on a stack
#   forth              interactive:  2 3 + .   : square dup * ;  7 square .
#   forth prog.fs      run a file
# Words: + - * / mod /mod negate abs min max = < > 0= and or invert
#   dup drop swap over rot nip tuck 2dup . .s emit cr space spaces ." text"
#   : name ... ;   if else then   do loop +loop i j   begin until   begin while repeat
#   variable x   x !   x @   +!   constant   words   bye
# @version 1.0
# @description Forth language (stack based)
import sys


class ForthError(Exception):
    pass


class Forth:
    def __init__(self):
        self.stack = []
        self.rstack = []                    # loop counters
        self.words = {}                     # user words: name -> compiled list
        self.mem = []                       # variables
        self.consts = {}
        self.out_col = 0

    def pop(self):
        if not self.stack:
            raise ForthError("stack empty")
        return self.stack.pop()

    def emit(self, s):
        print(s, end="")

    # ------------------------------------------------- built-in words
    def builtin(self, w):
        s = self.stack
        p = self.pop
        if w == "+": b, a = p(), p(); s.append(a + b)
        elif w == "-": b, a = p(), p(); s.append(a - b)
        elif w == "*": b, a = p(), p(); s.append(a * b)
        elif w in ("/", "mod", "/mod"):
            b, a = p(), p()
            if b == 0:
                raise ForthError("division by zero")
            q = abs(a) // abs(b) * (1 if (a < 0) == (b < 0) else -1)
            r = a - q * b
            if w == "/": s.append(q)
            elif w == "mod": s.append(r)
            else: s.extend([r, q])
        elif w == "negate": s.append(-p())
        elif w == "abs": s.append(abs(p()))
        elif w == "min": b, a = p(), p(); s.append(min(a, b))
        elif w == "max": b, a = p(), p(); s.append(max(a, b))
        elif w == "=": b, a = p(), p(); s.append(-1 if a == b else 0)
        elif w == "<": b, a = p(), p(); s.append(-1 if a < b else 0)
        elif w == ">": b, a = p(), p(); s.append(-1 if a > b else 0)
        elif w == "0=": s.append(-1 if p() == 0 else 0)
        elif w == "and": b, a = p(), p(); s.append(a & b)
        elif w == "or": b, a = p(), p(); s.append(a | b)
        elif w == "invert": s.append(~p())
        elif w == "dup": a = p(); s.extend([a, a])
        elif w == "drop": p()
        elif w == "swap": b, a = p(), p(); s.extend([b, a])
        elif w == "over": b, a = p(), p(); s.extend([a, b, a])
        elif w == "rot": c, b, a = p(), p(), p(); s.extend([b, c, a])
        elif w == "nip": b, a = p(), p(); s.append(b)
        elif w == "tuck": b, a = p(), p(); s.extend([b, a, b])
        elif w == "2dup": b, a = p(), p(); s.extend([a, b, a, b])
        elif w == ".": self.emit("%d " % p())
        elif w == ".s": self.emit("<%d> %s " % (len(s), " ".join(str(v) for v in s)))
        elif w == "emit": self.emit(chr(p()))
        elif w == "cr": self.emit("\n")
        elif w == "space": self.emit(" ")
        elif w == "spaces": self.emit(" " * max(0, p()))
        elif w == "@": s.append(self.mem[p()])
        elif w == "!": addr = p(); self.mem[addr] = p()
        elif w == "+!": addr = p(); self.mem[addr] += p()
        elif w == "i": s.append(self.rstack[-1][0])
        elif w == "j": s.append(self.rstack[-2][0])
        elif w == "words":
            self.emit(" ".join(sorted(self.words)) + " (+ the built-in words)\n")
        elif w == "bye":
            raise SystemExit
        else:
            return False
        return True

    # ------------------------------------------------- compiling
    def compile(self, toks):
        """turn words into a list of ops with jumps resolved"""
        code, ctrl = [], []
        i = 0
        while i < len(toks):
            t = toks[i]
            lw = t.lower()
            if lw == '."':
                j = i + 1
                text = []
                while j < len(toks) and not toks[j].endswith('"'):
                    text.append(toks[j])
                    j += 1
                if j < len(toks):
                    text.append(toks[j][:-1])
                code.append(("str", " ".join(text)))
                i = j + 1
                continue
            if lw == "(":
                while i < len(toks) and not toks[i].endswith(")"):
                    i += 1
                i += 1
                continue
            if lw == "\\":
                break
            if lw == "if":
                ctrl.append(("if", len(code)))
                code.append(["jz", None])
            elif lw == "else":
                kind, at = ctrl.pop()
                ctrl.append(("if", len(code)))
                code.append(["jmp", None])
                code[at][1] = len(code)
            elif lw == "then":
                kind, at = ctrl.pop()
                code[at][1] = len(code)
            elif lw == "do":
                code.append(("do",))
                ctrl.append(("do", len(code)))
            elif lw in ("loop", "+loop"):
                kind, at = ctrl.pop()
                code.append(["loop" if lw == "loop" else "+loop", at])
            elif lw == "begin":
                ctrl.append(("begin", len(code)))
            elif lw == "until":
                kind, at = ctrl.pop()
                code.append(["jz", at])
            elif lw == "while":
                ctrl.append(("while", len(code)))
                code.append(["jz", None])
            elif lw == "repeat":
                _, w_at = ctrl.pop()
                _, b_at = ctrl.pop()
                code.append(["jmp", b_at])
                code[w_at][1] = len(code)
            else:
                code.append(("word", t))
            i += 1
        if ctrl:
            raise ForthError("unfinished %s" % ctrl[-1][0])
        return code

    def execute(self, code):
        pc = 0
        while pc < len(code):
            op = code[pc]
            kind = op[0]
            pc += 1
            if kind == "word":
                self.do_word(op[1])
            elif kind == "str":
                self.emit(op[1])
            elif kind == "jz":
                if self.pop() == 0:
                    pc = op[1]
            elif kind == "jmp":
                pc = op[1]
            elif kind == "do":
                start, limit = self.pop(), self.pop()
                self.rstack.append([start, limit])
            elif kind in ("loop", "+loop"):
                step = 1 if kind == "loop" else self.pop()
                top = self.rstack[-1]
                top[0] += step
                if (step > 0 and top[0] < top[1]) or (step < 0 and top[0] >= top[1]):
                    pc = op[1]
                else:
                    self.rstack.pop()

    def do_word(self, t):
        lw = t.lower()
        if lw in self.words:
            self.execute(self.words[lw])
        elif lw in self.consts:
            self.stack.append(self.consts[lw])
        elif not self.builtin(lw):
            try:
                self.stack.append(int(t, 0) if not t.startswith("0") or t == "0" else int(t))
            except ValueError:
                raise ForthError("unknown word: %s" % t)

    # ------------------------------------------------- the outer interpreter
    def interpret(self, text):
        toks = text.split()
        i = 0
        top = []
        while i < len(toks):
            t = toks[i].lower()
            if t == ":":                        # : name ... ;
                name = toks[i + 1].lower()
                j = i + 2
                while j < len(toks) and toks[j] != ";":
                    j += 1
                if j >= len(toks):
                    raise ForthError("missing ;")
                self.words[name] = self.compile(toks[i + 2:j])
                i = j + 1
            elif t == "variable":
                self.mem.append(0)
                self.consts[toks[i + 1].lower()] = len(self.mem) - 1
                i += 2
            elif t == "constant":
                self.consts[toks[i + 1].lower()] = self.pop()
                i += 2
            else:
                j = i                           # run everything up to the next definition
                while j < len(toks) and toks[j].lower() not in (":", "variable", "constant"):
                    j += 1
                self.execute(self.compile(toks[i:j]))
                i = j
        return top


def main():
    f = Forth()
    if len(sys.argv) > 1:
        try:
            f.interpret(open(sys.argv[1]).read())
        except OSError:
            print("Can't open", sys.argv[1])
        except ForthError as e:
            print("\nError:", e)
        print()
        return
    print("MyOS Forth. Try:  2 3 + .   : square dup * ;  7 square .   (bye leaves)\n ok")
    while True:
        line = input("")
        try:
            f.interpret(line)
            print(" ok")
        except ForthError as e:
            print(" Error:", e)
            f.stack.clear()
            f.rstack.clear()


main()
