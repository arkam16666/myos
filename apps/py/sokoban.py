# sokoban - push every box ($) onto a goal (.)
#   sokoban [LEVEL]
# Arrows (or WASD) walk and push, u undoes, r restarts the level,
# n / p go to the next / previous level, q quits. Boxes can't be pulled.
# @version 1.0
# @description Sokoban box-pushing puzzle, 9 levels
import sys
import myos

LEVELS = [
    ["#######",
     "#     #",
     "# .$@ #",
     "#     #",
     "#######"],
    ["########",
     "#      #",
     "# $  . #",
     "#  ##  #",
     "# .  $ #",
     "#  @   #",
     "########"],
    ["######",
     "#    #",
     "# #@ #",
     "# $* #",
     "# .* #",
     "#    #",
     "######"],
    ["  ######",
     "  #    #",
     "### $$ #",
     "#  .#. #",
     "# . $ ##",
     "#  .$ #",
     "### @ #",
     "  #####"],
    ["  ####",
     "###  #",
     "# $  #",
     "# .$.###",
     "##@ $  #",
     " #  .  #",
     " #######"],
    ["  #####",
     "###   #",
     "#  $# ##",
     "# #  . #",
     "#    # #",
     "## #   #",
     " #@  ###",
     " #####"],
    ["#######",
     "#.  . #",
     "# $$  #",
     "#  #$##",
     "## . @#",
     " #    #",
     " ######"],
    [" ########",
     " #      #",
     " # $  $ #",
     "## #..# #",
     "#  #..# #",
     "# $  $  #",
     "#   @   #",
     "#########"],
    ["########",
     "#  .   #",
     "# $##$ #",
     "#.  @ .#",
     "# $##$ #",
     "#   .  #",
     "########"],
]
MOVES = {"up": (0, -1), "down": (0, 1), "left": (-1, 0), "right": (1, 0),
         "w": (0, -1), "s": (0, 1), "a": (-1, 0), "d": (1, 0)}
SAVE = "sokoban.cfg"
WALL, BOX, GOAL, ME, OFF = "\x1b[34m", "\x1b[33;1m", "\x1b[32m", "\x1b[36;1m", "\x1b[0m"


def load(n):
    walls, goals, boxes, me = set(), set(), set(), (0, 0)
    for y, row in enumerate(LEVELS[n]):
        for x, ch in enumerate(row):
            if ch == "#":
                walls.add((x, y))
            if ch in ".*+":
                goals.add((x, y))
            if ch in "$*":
                boxes.add((x, y))
            if ch in "@+":
                me = (x, y)
    return walls, goals, boxes, me


def draw(n, walls, goals, boxes, me, moves, pushes, solved, message):
    myos.move(0, 0)
    print("SOKOBAN  level %d/%d   moves %d  pushes %d   solved %d/%d\x1b[K"
          % (n + 1, len(LEVELS), moves, pushes, len(solved), len(LEVELS)))
    print("\x1b[K")
    h = len(LEVELS[n])
    w = max(len(r) for r in LEVELS[n])
    for y in range(h):
        line = "   "
        for x in range(w):
            p = (x, y)
            if p in walls:
                line += WALL + "##" + OFF
            elif p == me:
                line += ME + ("@@" if p not in goals else "@+") + OFF
            elif p in boxes:
                line += BOX + ("[]" if p not in goals else "**") + OFF
            elif p in goals:
                line += GOAL + " ." + OFF
            else:
                line += "  "
        print(line + "\x1b[K")
    print("\x1b[K")
    print("  arrows/WASD move   u undo   r restart   n/p next/previous   q quit\x1b[K")
    print("  " + message + "\x1b[K")
    print("\x1b[J", end="")


def load_solved():
    try:
        with open(SAVE) as f:
            return set(int(x) for x in f.read().split())
    except (OSError, ValueError):
        return set()


def save_solved(solved):
    try:
        with open(SAVE, "w") as f:
            f.write(" ".join(str(x) for x in sorted(solved)))
    except OSError:
        pass


def main():
    solved = load_solved()
    n = 0
    if len(sys.argv) > 1:
        try:
            n = max(0, min(len(LEVELS) - 1, int(sys.argv[1]) - 1))
        except ValueError:
            pass
    else:
        while n in solved and n < len(LEVELS) - 1:     # carry on where you stopped
            n += 1
    myos.cursor(False)
    myos.clear()
    walls, goals, boxes, me = load(n)
    history = []
    pushes = 0
    message = "Push the boxes [] onto the goals . (you are @@)"
    try:
        while True:
            draw(n, walls, goals, boxes, me, len(history), pushes, solved, message)
            message = ""
            if boxes == goals:
                solved.add(n)
                save_solved(solved)
                myos.move(0, len(LEVELS[n]) + 5)
                if n == len(LEVELS) - 1:
                    print("  You solved the last level! Any key...\x1b[K")
                else:
                    print("  Level solved in %d moves! Any key for the next one...\x1b[K" % len(history))
                if myos.getkey() == "q":
                    break
                n = (n + 1) % len(LEVELS)
                walls, goals, boxes, me = load(n)
                history, pushes = [], 0
                myos.clear()
                continue
            k = myos.getkey()
            if k == "q":
                break
            if k in MOVES:
                dx, dy = MOVES[k]
                to = (me[0] + dx, me[1] + dy)
                if to in walls:
                    continue
                if to in boxes:
                    beyond = (to[0] + dx, to[1] + dy)
                    if beyond in walls or beyond in boxes:
                        continue
                    history.append((me, to, beyond))
                    boxes.remove(to)
                    boxes.add(beyond)
                    pushes += 1
                else:
                    history.append((me, None, None))
                me = to
            elif k == "u" and history:
                me, box_from, box_to = history.pop()
                if box_from:
                    boxes.remove(box_to)
                    boxes.add(box_from)
                    pushes -= 1
            elif k == "r":
                walls, goals, boxes, me = load(n)
                history, pushes = [], 0
            elif k in ("n", "p"):
                n = (n + (1 if k == "n" else -1)) % len(LEVELS)
                walls, goals, boxes, me = load(n)
                history, pushes = [], 0
                myos.clear()
    finally:
        myos.cursor(True)
        print()


main()
