# hangman - guess the word one letter at a time
# @version 1.0
# @description Hangman word guessing game
import random

WORDS = ("python computer keyboard raspberry kernel memory network program "
         "printer monitor elephant giraffe penguin dolphin octopus kangaroo "
         "mountain volcano thunder rainbow sunshine blizzard tornado island "
         "banana mango pineapple coconut avocado chocolate sandwich noodles "
         "guitar trumpet violin drummer orchestra library teacher student "
         "bicycle airplane rocket submarine tractor scooter elevator bridge").split()

PICS = [
    "     \n     \n     \n     ",
    "  O  \n     \n     \n     ",
    "  O  \n  |  \n     \n     ",
    "  O  \n /|  \n     \n     ",
    "  O  \n /|\\ \n     \n     ",
    "  O  \n /|\\ \n /   \n     ",
    "  O  \n /|\\ \n / \\ \n     ",
]


def play():
    word = random.choice(WORDS)
    guessed = set()
    misses = 0
    while True:
        print("\n+---+")
        print("\n".join("|" + line for line in PICS[misses].split("\n")))
        shown = " ".join(c if c in guessed else "_" for c in word)
        print("\nWord:  " + shown)
        print("Tried: " + " ".join(sorted(guessed)))
        if "_" not in shown:
            print("You got it!")
            return
        if misses == len(PICS) - 1:
            print("Out of tries. The word was:", word)
            return
        g = input("Letter: ").strip().lower()
        if len(g) != 1 or not g.isalpha():
            print("Type one letter.")
        elif g in guessed:
            print("You tried that already.")
        else:
            guessed.add(g)
            if g not in word:
                misses += 1


while True:
    play()
    if input("Play again? (y/n) ").strip().lower() != "y":
        break
