# notes.py - a tiny note keeper
#   python notes.py buy milk     add a note
#   python notes.py              show all notes
import sys

if len(sys.argv) > 1:
    with open("notes.txt", "a") as f:
        f.write(" ".join(sys.argv[1:]) + "\n")
    print("Saved.")

try:
    with open("notes.txt") as f:
        for number, line in enumerate(f, 1):
            print(number, line.rstrip())
except OSError:
    print("No notes yet. Try: python notes.py buy milk")
