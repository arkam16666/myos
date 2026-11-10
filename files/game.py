# game.py - guess the number; your best score is saved in best.txt
#   python game.py
import random


def load_best():
    try:
        with open("best.txt") as f:
            return int(f.read())
    except OSError:
        return None


secret = random.randint(1, 100)
best = load_best()
print("I'm thinking of a number from 1 to 100.")
if best:
    print("Best score so far:", best, "tries")

tries = 0
while True:
    text = input("Your guess: ")
    if not text.isdigit():
        print("  Please type a number.")
        continue
    tries += 1
    guess = int(text)
    if guess < secret:
        print("  Higher!")
    elif guess > secret:
        print("  Lower!")
    else:
        print("  Correct in", tries, "tries!")
        break

if best is None or tries < best:
    with open("best.txt", "w") as f:
        f.write(str(tries))
    print("  New best score saved to best.txt")
