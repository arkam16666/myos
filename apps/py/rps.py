# rps - rock, paper, scissors against a computer that learns your habits
# @version 1.0
# @description Rock paper scissors (the computer learns)
import random

BEATS = {"r": "s", "p": "r", "s": "p"}            # key beats value
NAMES = {"r": "rock", "p": "paper", "s": "scissors"}


def main():
    history = []
    you = cpu = 0
    print("r = rock, p = paper, s = scissors, q = quit. First to 5 wins.")
    while you < 5 and cpu < 5:
        # guess your next move from what you played after your last move
        follow = [history[i + 1] for i in range(len(history) - 1) if history and history[i] == history[-1]]
        guess = random.choice(follow) if follow and random.random() < 0.7 else random.choice("rps")
        mine = [k for k, v in BEATS.items() if v == guess][0]   # what beats the guess
        t = input("You: ").strip().lower()[:1]
        if t == "q":
            return
        if t not in BEATS:
            continue
        history.append(t)
        print("  you %s, computer %s:" % (NAMES[t], NAMES[mine]), end=" ")
        if t == mine:
            print("draw")
        elif BEATS[t] == mine:
            you += 1
            print("you win this one")
        else:
            cpu += 1
            print("computer wins this one")
        print("  score  you %d - %d computer" % (you, cpu))
    print("You won the match!" if you == 5 else "The computer won the match.")


main()
