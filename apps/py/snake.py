# snake - eat the food, don't hit the walls or yourself
# @version 1.0
# @description Snake game (arrow keys or WASD)
import json
import random
import time
import myos

W, H = 40, 18                       # playing field inside the walls
DIRS = {"up": (0, -1), "down": (0, 1), "left": (-1, 0), "right": (1, 0),
        "w": (0, -1), "s": (0, 1), "a": (-1, 0), "d": (1, 0)}


def load_best():
    try:
        with open("scores.json") as f:
            return json.loads(f.read()).get("snake", 0)
    except (OSError, ValueError):
        return 0


def save_best(score):
    try:
        with open("scores.json") as f:
            scores = json.loads(f.read())
    except (OSError, ValueError):
        scores = {}
    scores["snake"] = score
    with open("scores.json", "w") as f:
        f.write(json.dumps(scores))


def put(x, y, ch):
    myos.move(x + 1, y + 2)
    print(ch, end="")


def place_food(body):
    while True:
        p = (random.randint(0, W - 1), random.randint(0, H - 1))
        if p not in body:
            return p


def status(score, best):
    myos.move(0, 0)
    print("SNAKE   score %-5d best %-5d   q = quit" % (score, best), end="")


def game(best):
    myos.clear()
    print("\n+" + "-" * W + "+")
    for _ in range(H):
        print("|" + " " * W + "|")
    print("+" + "-" * W + "+", end="")
    body = [(W // 2 - i, H // 2) for i in range(3)]
    for x, y in body:
        put(x, y, "o")
    put(body[0][0], body[0][1], "@")
    food = place_food(body)
    put(food[0], food[1], "*")
    dx, dy = 1, 0
    score = 0
    delay = 150
    status(score, best)
    while True:
        end = time.ticks_ms() + delay
        while time.ticks_ms() < end:            # read keys until the next step
            k = myos.key()
            if k == "q":
                return score, True
            if k in DIRS:
                nx, ny = DIRS[k]
                if (nx, ny) != (-dx, -dy):      # no turning back into yourself
                    dx, dy = nx, ny
            time.sleep_ms(10)
        hx, hy = body[0][0] + dx, body[0][1] + dy
        if hx < 0 or hx >= W or hy < 0 or hy >= H or (hx, hy) in body[:-1]:
            return score, False
        put(body[0][0], body[0][1], "o")
        body.insert(0, (hx, hy))
        put(hx, hy, "@")
        if (hx, hy) == food:
            score += 10
            delay = max(60, delay - 4)
            food = place_food(body)
            put(food[0], food[1], "*")
            status(score, max(score, best))
        else:
            tx, ty = body.pop()
            put(tx, ty, " ")
        myos.move(0, H + 3)


def main():
    best = load_best()
    myos.cursor(False)
    try:
        while True:
            score, quit_now = game(best)
            if score > best:
                best = score
                save_best(best)
            if quit_now:
                break
            myos.move(W // 2 - 8, H // 2 + 1)
            print(" GAME OVER: %d " % score, end="")
            myos.move(W // 2 - 10, H // 2 + 2)
            print(" Enter = again, q = quit ", end="")
            while True:
                k = myos.getkey()
                if k in ("\n", "q"):
                    break
            if k == "q":
                break
    finally:
        myos.cursor(True)
        myos.clear()
    print("Best score:", best)


main()
