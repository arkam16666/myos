# words - guess the 5-letter word in 6 tries (green = right spot, yellow = in the word)
# @version 1.0
# @description Guess the 5-letter word in 6 tries
import random

WORDS = ("apple beach brain bread brick chair chalk charm cheap chess chick child "
         "cloud coast crane crowd dance dream drink eagle earth field flame flute "
         "fruit ghost giant grape grass heart honey horse house juice knife laser "
         "lemon light lunch magic mango march money mouse music night ocean olive "
         "onion paint panda paper peach piano pilot pizza plane plant queen radio "
         "river robot salad shark sheep shirt smile snake space spoon stone storm "
         "sugar table tiger toast tower train truck uncle video water whale world "
         "zebra").split()

GREEN, YELLOW, GREY, RESET = "\x1b[42;30m", "\x1b[43;30m", "\x1b[100;37m", "\x1b[0m"


def colour(guess, answer):
    marks = ["grey"] * 5
    left = list(answer)
    for i in range(5):                      # right letter, right place first
        if guess[i] == answer[i]:
            marks[i] = "green"
            left[i] = None
    for i in range(5):
        if marks[i] != "green" and guess[i] in left:
            marks[i] = "yellow"
            left[left.index(guess[i])] = None
    col = {"green": GREEN, "yellow": YELLOW, "grey": GREY}
    return "".join(col[m] + " " + guess[i].upper() + " " + RESET for i, m in enumerate(marks))


def play():
    answer = random.choice(WORDS)
    print("Guess the 5-letter word. You have 6 tries.")
    for turn in range(1, 7):
        while True:
            g = input("Try %d: " % turn).strip().lower()
            if len(g) == 5 and g.isalpha():
                break
            print("  Type a 5-letter word.")
        print("        " + colour(g, answer))
        if g == answer:
            print("Got it in %d!" % turn)
            return
    print("The word was:", answer.upper())


while True:
    play()
    if input("Again? (y/n) ").strip().lower() != "y":
        break
