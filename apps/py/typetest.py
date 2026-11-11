# typetest - how fast can you type? (words per minute and accuracy)
# @version 1.0
# @description Typing speed test (words per minute)
import random
import time

SENTENCES = [
    "the quick brown fox jumps over the lazy dog",
    "a small computer can still do big things",
    "practice every day and your fingers will get faster",
    "my operating system runs on a raspberry pi",
    "programs are just instructions for the computer to follow",
    "keep your eyes on the screen and not on the keyboard",
    "python is a friendly language for beginners",
    "every expert was once a beginner who did not give up",
]


def score(target, typed, seconds):
    words = len(typed) / 5
    wpm = words / (seconds / 60) if seconds > 0 else 0
    right = sum(1 for a, b in zip(target, typed) if a == b)
    acc = 100 * right / max(len(target), len(typed))
    return wpm, acc


def main():
    print("Type each sentence exactly and press Enter. 3 rounds.\n")
    total_wpm = total_acc = 0
    for rnd in range(1, 4):
        target = random.choice(SENTENCES)
        input("Round %d - press Enter when ready..." % rnd)
        print("\n  " + target)
        start = time.time()
        typed = input("  ")
        wpm, acc = score(target, typed, time.time() - start)
        total_wpm += wpm
        total_acc += acc
        print("  -> %d words per minute, %d%% accurate\n" % (wpm, acc))
    print("Average: %d wpm, %d%% accuracy" % (total_wpm / 3, total_acc / 3))


main()
