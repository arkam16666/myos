# life - Conway's Game of Life: cells live, die and are born by simple rules
# @version 1.0
# @description Conway's Game of Life animation
import random
import time
import myos

W, H = 60, 20


def step(cells):
    counts = {}
    for x, y in cells:
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dx or dy:
                    p = ((x + dx) % W, (y + dy) % H)
                    counts[p] = counts.get(p, 0) + 1
    return {p for p, n in counts.items() if n == 3 or (n == 2 and p in cells)}


def seed():
    return {(random.randint(0, W - 1), random.randint(0, H - 1)) for _ in range(W * H // 5)}


def main():
    cells = seed()
    gen = 0
    myos.clear()
    myos.cursor(False)
    shown = set()
    try:
        while True:
            for x, y in shown - cells:          # only redraw changes
                myos.move(x, y + 1)
                print(" ", end="")
            for x, y in cells - shown:
                myos.move(x, y + 1)
                print("O", end="")
            shown = cells
            myos.move(0, 0)
            print("LIFE  generation %-5d cells %-5d   r = restart, q = quit" % (gen, len(cells)), end="")
            myos.move(0, H + 1)
            end = time.ticks_ms() + 150
            while time.ticks_ms() < end:
                k = myos.key()
                if k == "q":
                    return
                if k == "r":
                    cells, gen = seed(), 0
                time.sleep_ms(10)
            cells = step(cells)
            gen += 1
    finally:
        myos.cursor(True)
        myos.clear()


main()
