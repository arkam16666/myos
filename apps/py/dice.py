# dice - roll dice
#   dice          one six-sided die
#   dice 3        three dice
#   dice 2d20     two twenty-sided dice (board-game style)
# @version 1.0
# @description Roll dice (2d6, 1d20, ...)
import random
import sys

FACES = {
    1: ["     ", "  o  ", "     "], 2: ["o    ", "     ", "    o"],
    3: ["o    ", "  o  ", "    o"], 4: ["o   o", "     ", "o   o"],
    5: ["o   o", "  o  ", "o   o"], 6: ["o   o", "o   o", "o   o"],
}


def main():
    arg = sys.argv[1].lower() if len(sys.argv) > 1 else "1"
    if "d" in arg:
        count, _, sides = arg.partition("d")
        count = int(count) if count.isdigit() else 1
        sides = int(sides) if sides.isdigit() else 6
    else:
        count, sides = (int(arg) if arg.isdigit() else 1), 6
    count = max(1, min(count, 12))
    sides = max(2, min(sides, 1000))
    rolls = [random.randint(1, sides) for _ in range(count)]
    if sides == 6 and count <= 8:
        print("  ".join("+-----+" for _ in rolls))
        for row in range(3):
            print("  ".join("|" + FACES[r][row] + "|" for r in rolls))
        print("  ".join("+-----+" for _ in rolls))
    else:
        print("  ".join("[%d]" % r for r in rolls))
    if count > 1:
        print("Total:", sum(rolls))


main()
