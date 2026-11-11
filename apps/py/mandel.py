# mandel - the Mandelbrot set in text; move and zoom with the keyboard
# @version 1.0
# @description Mandelbrot fractal explorer (zoom in/out)
import myos

W, H = 78, 22
CHARS = " .:-=+*#%@"


def draw(cx, cy, scale, limit):
    myos.move(0, 0)
    rows = []
    for j in range(H):
        y0 = cy + (j - H / 2) * scale * 2
        row = []
        for i in range(W):
            x0 = cx + (i - W / 2) * scale
            x = y = 0.0
            n = 0
            while x * x + y * y <= 4 and n < limit:
                x, y = x * x - y * y + x0, 2 * x * y + y0
                n += 1
            row.append(" " if n == limit else CHARS[n % (len(CHARS) - 1) + 1])
        rows.append("".join(row))
    print("\n".join(rows))
    print("arrows move  + zoom in  - zoom out  r reset  q quit   (%.5f, %.5f) x%d   "
          % (cx, cy, int(0.04 / scale)), end="")


def main():
    cx, cy, scale = -0.6, 0.0, 0.04
    myos.clear()
    myos.cursor(False)
    try:
        while True:
            limit = min(200, 30 + int(10 * (0.04 / scale) ** 0.3))
            draw(cx, cy, scale, limit)
            k = myos.getkey()
            if k == "q":
                break
            if k in ("+", "="):
                scale /= 2
            elif k == "-":
                scale *= 2
            elif k == "left":
                cx -= scale * 10
            elif k == "right":
                cx += scale * 10
            elif k == "up":
                cy -= scale * 10
            elif k == "down":
                cy += scale * 10
            elif k == "r":
                cx, cy, scale = -0.6, 0.0, 0.04
    finally:
        myos.cursor(True)
        myos.clear()


main()
