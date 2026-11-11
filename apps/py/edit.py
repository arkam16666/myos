# edit - a small full-screen text editor (like nano)
#   edit FILE
# Arrow keys move, type to insert, Backspace deletes.
# Ctrl+S save, Ctrl+X exit, Ctrl+K cut line, Ctrl+U paste line, Ctrl+R run (.py)
# @version 1.0
# @description Full-screen text editor (like nano)
import sys
import myos

ROWS, COLS = 22, 80                 # text area (the terminal is 80 x 24)
CTRL_S, CTRL_X, CTRL_K, CTRL_U, CTRL_R = "\x13", "\x18", "\x0b", "\x15", "\x12"


class Editor:
    def __init__(self, name):
        self.name = name
        try:
            with open(name) as f:
                self.lines = f.read().split("\n")
            self.msg = "Opened %s (%d lines)" % (name, len(self.lines))
        except OSError:
            self.lines = [""]
            self.msg = "New file %s" % name
        self.x = self.y = self.top = self.left = 0
        self.dirty = False
        self.clip = None

    def save(self):
        try:
            with open(self.name, "w") as f:
                f.write("\n".join(self.lines))
            self.dirty = False
            self.msg = "Saved %s" % self.name
        except OSError:
            self.msg = "Could not save (disk full?)"

    def draw(self):
        if self.y < self.top:
            self.top = self.y
        if self.y >= self.top + ROWS:
            self.top = self.y - ROWS + 1
        if self.x < self.left:
            self.left = self.x
        if self.x >= self.left + COLS - 1:
            self.left = self.x - COLS + 2
        myos.move(0, 0)
        title = " edit: %s%s   line %d/%d  col %d" % (self.name, " *" if self.dirty else "",
                                                     self.y + 1, len(self.lines), self.x + 1)
        print("\x1b[7m" + title + " " * max(0, COLS - len(title)) + "\x1b[0m")
        for i in range(ROWS):
            n = self.top + i
            text = self.lines[n][self.left:self.left + COLS - 1] if n < len(self.lines) else "~"
            print(text + "\x1b[K")
        help_ = "^S save  ^X exit  ^K cut  ^U paste  ^R run   " + self.msg
        print("\x1b[7m" + help_[:COLS - 1] + " " * max(0, COLS - 1 - len(help_)) + "\x1b[0m", end="")
        myos.move(self.x - self.left, self.y - self.top + 1)

    def key(self, k):
        line = self.lines[self.y]
        self.msg = ""
        if k == "up" and self.y > 0:
            self.y -= 1
        elif k == "down" and self.y < len(self.lines) - 1:
            self.y += 1
        elif k == "left":
            if self.x > 0:
                self.x -= 1
            elif self.y > 0:
                self.y -= 1
                self.x = len(self.lines[self.y])
        elif k == "right":
            if self.x < len(line):
                self.x += 1
            elif self.y < len(self.lines) - 1:
                self.y += 1
                self.x = 0
        elif k == "\n":
            indent = len(line) - len(line.lstrip(" "))
            if line[:self.x].rstrip().endswith(":"):
                indent += 4                     # Python block
            self.lines[self.y] = line[:self.x]
            self.lines.insert(self.y + 1, " " * indent + line[self.x:])
            self.y += 1
            self.x = indent
            self.dirty = True
        elif k == "\x08":
            if self.x > 0:
                self.lines[self.y] = line[:self.x - 1] + line[self.x:]
                self.x -= 1
                self.dirty = True
            elif self.y > 0:
                self.x = len(self.lines[self.y - 1])
                self.lines[self.y - 1] += line
                del self.lines[self.y]
                self.y -= 1
                self.dirty = True
        elif k == "\t":
            self.key(" ")
            while self.x % 4:
                self.key(" ")
        elif k == CTRL_K:
            self.clip = line
            if len(self.lines) > 1:
                del self.lines[self.y]
                self.y = min(self.y, len(self.lines) - 1)
            else:
                self.lines[0] = ""
            self.dirty = True
            self.msg = "Cut a line (^U pastes it)"
        elif k == CTRL_U and self.clip is not None:
            self.lines.insert(self.y, self.clip)
            self.dirty = True
        elif len(k) == 1 and " " <= k <= "~":
            self.lines[self.y] = line[:self.x] + k + line[self.x:]
            self.x += 1
            self.dirty = True
        self.x = min(self.x, len(self.lines[self.y]))


def main():
    if len(sys.argv) < 2:
        print("usage: edit FILE")
        return
    ed = Editor(sys.argv[1])
    myos.clear()
    while True:
        ed.draw()
        k = myos.getkey()
        if k == CTRL_S:
            ed.save()
        elif k == CTRL_X:
            if ed.dirty:
                ed.msg = "Unsaved changes! ^S saves, ^X again quits"
                ed.draw()
                if myos.getkey() != CTRL_X:
                    continue
            break
        elif k == CTRL_R:
            if not ed.name.endswith(".py"):
                ed.msg = "Only .py files can be run"
                continue
            ed.save()
            myos.clear()
            print("--- running %s (any key returns) ---" % ed.name)
            try:
                exec(open(ed.name).read(), {"__name__": "__main__"})
            except Exception as e:                  # show the error, keep editing
                print("Error:", type(e).__name__, e)
            except SystemExit:
                pass
            print("\n--- finished: press a key ---")
            myos.getkey()
            myos.clear()
        else:
            ed.key(k)
    myos.clear()


main()
