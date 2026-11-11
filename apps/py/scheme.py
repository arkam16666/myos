# scheme - a small Scheme (Lisp) interpreter
#   scheme             interactive: (define (sq x) (* x x))  (sq 12)
#   scheme prog.scm    run a file
# Special forms: define lambda if cond let let* begin set! quote ' and or when unless
# Built-ins: + - * / quotient remainder modulo = < > <= >= abs min max sqrt expt
#   car cdr cons list length append reverse map filter apply null? pair? list?
#   number? symbol? string? procedure? eq? equal? not display newline
#   string-append number->string string->number string-length exit
# Tail calls don't grow the stack, so loops written as recursion work.
# @version 1.0
# @description Scheme (Lisp) language
import math
import sys


class Symbol(str):
    pass


class SchemeError(Exception):
    pass


NIL = []


def sym(s, table={}):
    if s not in table:
        table[s] = Symbol(s)
    return table[s]


QUOTE, IF, DEFINE, LAMBDA, BEGIN, SET, LET, LETSTAR, COND, ELSE, AND, OR, WHEN, UNLESS = map(
    sym, ["quote", "if", "define", "lambda", "begin", "set!", "let", "let*", "cond", "else",
          "and", "or", "when", "unless"])


def tokenize(text):
    toks, i = [], 0
    while i < len(text):
        c = text[i]
        if c in " \t\r\n":
            i += 1
        elif c == ";":
            while i < len(text) and text[i] != "\n":
                i += 1
        elif c in "()'":
            toks.append(c)
            i += 1
        elif c == '"':
            j, s = i + 1, ""
            while j < len(text) and text[j] != '"':
                if text[j] == "\\" and j + 1 < len(text):
                    j += 1
                    s += {"n": "\n", "t": "\t"}.get(text[j], text[j])
                else:
                    s += text[j]
                j += 1
            toks.append(("str", s))
            i = j + 1
        else:
            j = i
            while j < len(text) and text[j] not in " \t\r\n()';\"":
                j += 1
            toks.append(text[i:j])
            i = j
    return toks


def atom(t):
    if isinstance(t, tuple):
        return t[1]
    if t == "#t":
        return True
    if t == "#f":
        return False
    try:
        return int(t)
    except ValueError:
        try:
            return float(t)
        except ValueError:
            return sym(t)


def read(toks):
    if not toks:
        raise SchemeError("unexpected end of input")
    t = toks.pop(0)
    if t == "(":
        out = []
        while toks and toks[0] != ")":
            out.append(read(toks))
        if not toks:
            raise SchemeError("missing )")
        toks.pop(0)
        return out
    if t == ")":
        raise SchemeError("unexpected )")
    if t == "'":
        return [QUOTE, read(toks)]
    return atom(t)


def show(x):
    if x is True:
        return "#t"
    if x is False:
        return "#f"
    if isinstance(x, Symbol):
        return x
    if isinstance(x, str):
        return '"%s"' % x
    if isinstance(x, list):
        return "(" + " ".join(show(e) for e in x) + ")"
    if isinstance(x, Procedure):
        return "#<procedure>"
    if callable(x):
        return "#<builtin>"
    if isinstance(x, float) and x == int(x) and abs(x) < 1e15:
        return str(int(x)) + "."
    return str(x)


def display(x):
    print(x if isinstance(x, str) and not isinstance(x, Symbol) else show(x), end="")


class Env(dict):
    def __init__(self, names=(), values=(), outer=None):
        super().__init__()
        self.outer = outer
        if isinstance(names, Symbol):           # (lambda args ...) takes all
            self[names] = list(values)
            return
        names = list(names)
        if len(names) != len(values):
            raise SchemeError("expected %d arguments, got %d" % (len(names), len(values)))
        for n, v in zip(names, values):
            self[n] = v

    def find(self, name):
        e = self
        while e is not None:
            if name in e:
                return e
            e = e.outer
        raise SchemeError("unbound variable: %s" % name)


class Procedure:
    def __init__(self, params, body, env):
        self.params, self.body, self.env = params, body, env


def truthy(x):
    return x is not False


def arith(op, start):
    def f(*args):
        if not args:
            return start
        acc = args[0]
        if len(args) == 1 and op in ("-", "/"):
            return -acc if op == "-" else 1 / acc
        for a in args[1:]:
            if op == "+":
                acc += a
            elif op == "-":
                acc -= a
            elif op == "*":
                acc *= a
            else:
                if a == 0:
                    raise SchemeError("division by zero")
                acc = acc / a
                if acc == int(acc) and isinstance(args[0], int) and isinstance(a, int):
                    acc = int(acc)
        return acc
    return f


def chain(test):
    return lambda *a: all(test(a[i], a[i + 1]) for i in range(len(a) - 1))


def standard_env():
    env = Env()
    env.update({
        "+": arith("+", 0), "-": arith("-", 0), "*": arith("*", 1), "/": arith("/", 1),
        "quotient": lambda a, b: a // b, "remainder": lambda a, b: a - b * int(a / b),
        "modulo": lambda a, b: a % b,
        "=": chain(lambda a, b: a == b), "<": chain(lambda a, b: a < b), ">": chain(lambda a, b: a > b),
        "<=": chain(lambda a, b: a <= b), ">=": chain(lambda a, b: a >= b),
        "abs": abs, "min": min, "max": max, "sqrt": math.sqrt, "expt": lambda a, b: a ** b,
        "floor": math.floor, "round": round, "exact->inexact": float,
        "car": lambda x: x[0], "cdr": lambda x: x[1:], "cons": lambda a, b: [a] + (b if isinstance(b, list) else [b]),
        "list": lambda *x: list(x), "length": len, "append": lambda *ls: [e for l in ls for e in l],
        "reverse": lambda l: l[::-1], "list-ref": lambda l, i: l[i],
        "null?": lambda x: x == [], "pair?": lambda x: isinstance(x, list) and x != [],
        "list?": lambda x: isinstance(x, list), "number?": lambda x: isinstance(x, (int, float)) and not isinstance(x, bool),
        "symbol?": lambda x: isinstance(x, Symbol), "string?": lambda x: isinstance(x, str) and not isinstance(x, Symbol),
        "procedure?": lambda x: callable(x) or isinstance(x, Procedure),
        "zero?": lambda x: x == 0, "even?": lambda x: x % 2 == 0, "odd?": lambda x: x % 2 == 1,
        "eq?": lambda a, b: a is b or (isinstance(a, (int, Symbol)) and a == b), "equal?": lambda a, b: a == b,
        "not": lambda x: x is False,
        "display": lambda x: display(x), "newline": lambda: print(),
        "string-append": lambda *s: "".join(s), "number->string": lambda n: show(n),
        "string->number": lambda s: atom(s) if not isinstance(atom(s), Symbol) else False,
        "string-length": len, "symbol->string": str, "string->symbol": sym,
        "exit": lambda *a: sys.exit(),
    })
    env["map"] = lambda f, *ls: [apply_proc(f, list(args)) for args in zip(*ls)]
    env["filter"] = lambda f, l: [x for x in l if truthy(apply_proc(f, [x]))]
    env["apply"] = lambda f, args: apply_proc(f, args)
    return env


def apply_proc(f, args):
    if isinstance(f, Procedure):
        return evaluate([BEGIN] + f.body, Env(f.params, args, f.env))
    if callable(f):
        return f(*args)
    raise SchemeError("not a procedure: %s" % show(f))


def evaluate(x, env):
    while True:                                 # loop = tail calls without a deeper stack
        if isinstance(x, Symbol):
            return env.find(x)[x]
        if not isinstance(x, list):
            return x
        if not x:
            raise SchemeError("empty combination ()")
        op = x[0]
        if op is QUOTE:
            return x[1]
        if op is IF:
            x = x[2] if truthy(evaluate(x[1], env)) else (x[3] if len(x) > 3 else False)
            continue
        if op is DEFINE:
            if isinstance(x[1], list):          # (define (f a b) body...)
                env[x[1][0]] = Procedure(x[1][1:] if not (len(x[1]) == 3 and x[1][1] == ".") else x[1][2],
                                         x[2:], env)
            else:
                env[x[1]] = evaluate(x[2], env)
            return None
        if op is SET:
            env.find(x[1])[x[1]] = evaluate(x[2], env)
            return None
        if op is LAMBDA:
            return Procedure(x[1], x[2:], env)
        if op is BEGIN:
            if len(x) == 1:
                return None
            for e in x[1:-1]:
                evaluate(e, env)
            x = x[-1]
            continue
        if op is LET or op is LETSTAR:
            new = Env(outer=env)
            for name, val in x[1]:
                new[name] = evaluate(val, new if op is LETSTAR else env)
            env = new
            x = [BEGIN] + x[2:]
            continue
        if op is COND:
            for clause in x[1:]:
                if clause[0] is ELSE or truthy(evaluate(clause[0], env)):
                    x = [BEGIN] + clause[1:]
                    break
            else:
                return None
            continue
        if op is AND or op is OR:
            val = op is AND
            for e in x[1:]:
                val = evaluate(e, env)
                if truthy(val) != (op is AND):
                    return val
            return val
        if op is WHEN or op is UNLESS:
            if truthy(evaluate(x[1], env)) == (op is WHEN):
                x = [BEGIN] + x[2:]
                continue
            return None
        f = evaluate(op, env)
        args = [evaluate(a, env) for a in x[1:]]
        if isinstance(f, Procedure):            # tail call: reuse this loop
            env = Env(f.params, args, f.env)
            x = [BEGIN] + f.body
            continue
        if callable(f):
            return f(*args)
        raise SchemeError("not a procedure: %s" % show(f))


def run_text(text, env, echo):
    toks = tokenize(text)
    while toks:
        expr = read(toks)
        val = evaluate(expr, env)
        if echo and val is not None:
            print(show(val))


def main():
    env = standard_env()
    if len(sys.argv) > 1:
        try:
            run_text(open(sys.argv[1]).read(), env, False)
        except OSError:
            print("Can't open", sys.argv[1])
        except (SchemeError, ZeroDivisionError, TypeError, IndexError, ValueError) as e:
            print("Error:", e)
        return
    print("MyOS Scheme. Try: (define (sq x) (* x x))  (sq 12)   Leave with (exit)")
    while True:
        text = input("scheme> ")
        while text.count("(") > text.count(")"):    # keep reading until the parens close
            text += "\n" + input("   ... ")
        try:
            run_text(text, env, True)
        except (SchemeError, ZeroDivisionError, TypeError, IndexError, ValueError) as e:
            print("Error:", e)
        except RuntimeError:
            print("Error: too deep (recursion)")


main()
