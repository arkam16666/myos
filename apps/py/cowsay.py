# cowsay - a cow says what you type
#   cowsay Hello from MyOS
# @version 1.0
# @description A cow says your message
import sys

COW = r"""        \   ^__^
         \  (oo)\_______
            (__)\       )\/\
                ||----w |
                ||     ||"""


def wrap(text, width=38):
    lines, line = [], ""
    for w in text.split():
        if line and len(line) + 1 + len(w) > width:
            lines.append(line)
            line = w
        else:
            line = (line + " " + w).strip()
    if line:
        lines.append(line)
    return lines or [""]


def main():
    text = " ".join(sys.argv[1:]) or "Moo! Type: cowsay something"
    lines = wrap(text)
    w = max(len(l) for l in lines)
    print(" " + "_" * (w + 2))
    if len(lines) == 1:
        print("< " + lines[0] + " >")
    else:
        for i, l in enumerate(lines):
            left, right = ("/", "\\") if i == 0 else ("\\", "/") if i == len(lines) - 1 else ("|", "|")
            print(left + " " + l + " " * (w - len(l)) + " " + right)
    print(" " + "-" * (w + 2))
    print(COW)


main()
