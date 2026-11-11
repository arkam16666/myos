# matrix - green digital rain (any key stops it)
# @version 1.0
# @description Digital rain screen saver
import random
import time
import myos

W, H = 79, 23
GLYPHS = "0123456789ABCDEFabcdef@#$%&*+=<>"


def main():
    drops = [random.randint(-H, 0) for _ in range(W)]
    myos.clear()
    myos.cursor(False)
    print("\x1b[32m", end="")
    try:
        while not myos.key():
            out = []
            for x in range(0, W, 2):
                y = drops[x]
                if 0 <= y < H:
                    out.append("\x1b[%d;%dH\x1b[1;37m%s" % (y + 1, x + 1, random.choice(GLYPHS)))
                if 0 <= y - 1 < H:
                    out.append("\x1b[%d;%dH\x1b[0;32m%s" % (y, x + 1, random.choice(GLYPHS)))
                if 0 <= y - 12 < H:
                    out.append("\x1b[%d;%dH " % (y - 11, x + 1))
                drops[x] = y + 1 if y < H + 12 else random.randint(-10, 0)
            print("".join(out), end="")
            time.sleep_ms(60)
    finally:
        print("\x1b[0m", end="")
        myos.cursor(True)
        myos.clear()


main()
