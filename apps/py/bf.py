# bf - interpreter for the tiny Brainf*ck language (8 commands: + - < > [ ] . ,)
#   bf                run the built-in hello-world demo
#   bf FILE.bf        run a program from the disk
# @version 1.0
# @description Brainf*ck language interpreter
import sys

HELLO = ("++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++."
         ">>.<-.<.+++.------.--------.>>+.>++.")


def run(code):
    code = [c for c in code if c in "+-<>[].,"]
    jump, stack = {}, []
    for i, c in enumerate(code):                # match the brackets first
        if c == "[":
            stack.append(i)
        elif c == "]":
            if not stack:
                print("Error: ] without [")
                return
            j = stack.pop()
            jump[i], jump[j] = j, i
    if stack:
        print("Error: [ without ]")
        return
    tape = bytearray(30000)
    ptr = pc = steps = 0
    out = []
    while pc < len(code):
        c = code[pc]
        if c == "+":
            tape[ptr] = (tape[ptr] + 1) & 255
        elif c == "-":
            tape[ptr] = (tape[ptr] - 1) & 255
        elif c == ">":
            ptr = (ptr + 1) % 30000
        elif c == "<":
            ptr = (ptr - 1) % 30000
        elif c == ".":
            out.append(chr(tape[ptr]))
            if tape[ptr] == 10 or len(out) > 60:
                print("".join(out), end="")
                out = []
        elif c == ",":
            print("".join(out), end="")
            out = []
            s = input()
            tape[ptr] = ord(s[0]) if s else 10
        elif c == "[" and tape[ptr] == 0:
            pc = jump[pc]
        elif c == "]" and tape[ptr] != 0:
            pc = jump[pc]
        pc += 1
        steps += 1
    print("".join(out), end="")
    print("\n(%d steps)" % steps)


if len(sys.argv) > 1:
    try:
        run(open(sys.argv[1]).read())
    except OSError:
        print("Can't open", sys.argv[1])
else:
    print("Running the hello-world demo:\n" + HELLO + "\n")
    run(HELLO)
