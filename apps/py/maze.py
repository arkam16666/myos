# maze - a new random maze every time; walk from S to E
# @version 1.0
# @description Random maze to walk through (arrows)
import random
import time
import myos

CW, CH = 19, 9                      # maze size in cells -> 39 x 19 characters


def generate():
    w, h = CW * 2 + 1, CH * 2 + 1
    grid = [["#"] * w for _ in range(h)]
    stack = [(0, 0)]
    seen = {(0, 0)}
    grid[1][1] = " "
    while stack:                    # depth-first search carves the passages
        x, y = stack[-1]
        options = [(x + dx, y + dy, dx, dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))
                   if 0 <= x + dx < CW and 0 <= y + dy < CH and (x + dx, y + dy) not in seen]
        if not options:
            stack.pop()
            continue
        nx, ny, dx, dy = random.choice(options)
        grid[2 * y + 1 + dy][2 * x + 1 + dx] = " "
        grid[2 * ny + 1][2 * nx + 1] = " "
        seen.add((nx, ny))
        stack.append((nx, ny))
    grid[h - 2][w - 2] = "E"
    return grid


def main():
    moves = {"up": (0, -1), "down": (0, 1), "left": (-1, 0), "right": (1, 0),
             "w": (0, -1), "s": (0, 1), "a": (-1, 0), "d": (1, 0)}
    level = 1
    myos.cursor(False)
    try:
        while True:
            grid = generate()
            myos.clear()
            print("MAZE level %d - reach E.  arrows/WASD move, q quits" % level)
            for row in grid:
                print("".join(row))
            x, y = 1, 1
            steps = 0
            start = time.time()
            while True:
                myos.move(x, y + 1)
                print("@", end="")
                myos.move(0, len(grid) + 1)
                k = myos.getkey()
                if k == "q":
                    return
                if k not in moves:
                    continue
                nx, ny = x + moves[k][0], y + moves[k][1]
                if grid[ny][nx] == "#":
                    continue
                myos.move(x, y + 1)
                print(".", end="")
                x, y = nx, ny
                steps += 1
                if grid[y][x] == "E":
                    break
            myos.move(0, len(grid) + 1)
            print("Solved in %d steps, %d seconds. Enter = next maze, q = quit"
                  % (steps, int(time.time() - start)))
            if myos.getkey() == "q":
                return
            level += 1
    finally:
        myos.cursor(True)


main()
