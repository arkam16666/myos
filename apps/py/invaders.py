# invaders - stop the aliens before they land
# Left/right (or a/d) move, space fires, p pauses, q quits.
# @version 1.0
# @description Space invaders: shoot the aliens (arrows)
import json
import random
import time
import myos

W, H = 44, 20
ALIEN_ROWS = [("/^\\", 30), ("{o}", 20), ("<->", 10)]
GREEN, RED, YELLOW, CYAN, BOLD, OFF = "\x1b[32m", "\x1b[31m", "\x1b[33m", "\x1b[36m", "\x1b[1m", "\x1b[0m"


def load_best():
    try:
        with open("scores.json") as f:
            return json.loads(f.read()).get("invaders", 0)
    except (OSError, ValueError):
        return 0


def save_best(score):
    try:
        with open("scores.json") as f:
            scores = json.loads(f.read())
    except (OSError, ValueError):
        scores = {}
    scores["invaders"] = score
    with open("scores.json", "w") as f:
        f.write(json.dumps(scores))


def new_wave(wave):
    aliens = []
    for r in range(5):
        kind = 0 if r == 0 else 1 if r < 3 else 2
        for c in range(8):
            aliens.append([2 + c * 5, 2 + r * 2 + min(wave - 1, 4), kind])
    return aliens


def draw(aliens, ship, shots, bombs, score, best, lives, wave, message):
    rows = [[" "] * W for _ in range(H)]
    colour = {}
    for x, y, kind in aliens:
        art = ALIEN_ROWS[kind][0]
        for i, ch in enumerate(art):
            if 0 <= x + i < W and 0 <= y < H:
                rows[y][x + i] = ch
                colour[(x + i, y)] = (GREEN, YELLOW, CYAN)[kind]
    for x, y in shots:
        if 0 <= y < H:
            rows[y][x] = "|"
    for x, y in bombs:
        if 0 <= y < H:
            rows[y][x] = "*"
            colour[(x, y)] = RED
    for i, ch in enumerate("_/A\\_"):
        rows[H - 1][ship - 2 + i] = ch
        colour[(ship - 2 + i, H - 1)] = BOLD
    out = [BOLD + "INVADERS" + OFF + "  score %-5d best %-5d lives %s  wave %d\x1b[K"
           % (score, best, "A " * lives, wave), "+" + "-" * W + "+"]
    for y in range(H):
        line = ""
        last = ""
        for x in range(W):
            c = colour.get((x, y), "")
            if c != last:
                line += OFF + c
                last = c
            line += rows[y][x]
        out.append("|" + line + OFF + "|")
    out.append("+" + "-" * W + "+")
    out.append(("  " + message if message else "  arrows move  space fire  p pause  q quit") + "\x1b[K")
    myos.move(0, 0)
    print("\n".join(out))


def game(best):
    score, lives, wave = 0, 3, 1
    ship = W // 2
    aliens = new_wave(wave)
    shots, bombs = [], []
    direction, step_ms = 1, 600
    next_step = time.ticks_ms() + step_ms
    message = ""
    myos.clear()
    while True:
        frame_end = time.ticks_ms() + 90
        while True:                                 # all keys pressed since the last frame
            k = myos.key()
            if not k:
                break
            if k in ("left", "a") and ship > 2:
                ship -= 2 if ship > 3 else 1
            elif k in ("right", "d") and ship < W - 3:
                ship += 2 if ship < W - 4 else 1
            elif k == " " and len(shots) < 2:
                shots.append([ship, H - 2])
            elif k == "q":
                return score, True
            elif k == "p":
                draw(aliens, ship, shots, bombs, score, best, lives, wave, "paused - any key")
                myos.getkey()
        for s in shots:
            s[1] -= 1
        shots = [s for s in shots if s[1] >= 0]
        for s in shots[:]:                          # hits
            for a in aliens:
                if a[1] == s[1] and a[0] <= s[0] < a[0] + 3:
                    aliens.remove(a)
                    if s in shots:
                        shots.remove(s)
                    score += ALIEN_ROWS[a[2]][1]
                    step_ms = max(80, step_ms - 8)
                    break
        if not aliens:
            wave += 1
            aliens = new_wave(wave)
            shots, bombs = [], []
            step_ms = max(150, 600 - wave * 60)
            message = "Wave %d!" % wave
        now = time.ticks_ms()
        if now >= next_step:                        # the whole fleet moves
            next_step = now + step_ms
            edge = any((a[0] + direction < 0 or a[0] + 3 + direction > W) for a in aliens)
            if edge:
                direction = -direction
                for a in aliens:
                    a[1] += 1
            else:
                for a in aliens:
                    a[0] += direction
            message = ""
        if aliens and random.randint(0, 100) < 10 + wave * 3:
            shooter = random.choice(aliens)
            bombs.append([shooter[0] + 1, shooter[1] + 1])
        for b in bombs:
            b[1] += 1
        hit = any(b[1] == H - 1 and ship - 2 <= b[0] <= ship + 2 for b in bombs)
        bombs = [b for b in bombs if b[1] < H - 1]
        landed = any(a[1] >= H - 1 for a in aliens)
        if hit or landed:
            lives = 0 if landed else lives - 1
            draw(aliens, ship, shots, bombs, score, best, lives, wave, "BOOM!")
            time.sleep_ms(700)
            if lives <= 0:
                return score, False
            bombs, shots = [], []
            ship = W // 2
        draw(aliens, ship, shots, bombs, score, max(best, score), lives, wave, message)
        while time.ticks_ms() < frame_end:
            time.sleep_ms(5)


def main():
    best = load_best()
    myos.cursor(False)
    try:
        while True:
            score, quit_ = game(best)
            if score > best:
                best = score
                save_best(best)
            if quit_:
                break
            myos.move(0, H + 4)
            print("  Game over. Score %d (best %d). Play again? (y/n)\x1b[K" % (score, best))
            while True:
                k = myos.getkey()
                if k in ("y", "n", "q"):
                    break
            if k != "y":
                break
    finally:
        myos.cursor(True)
        myos.move(0, H + 5)
        print()


main()
