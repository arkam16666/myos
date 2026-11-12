# mip - MicroPython's name for pip: runs pip with the same arguments
#   mip install NAME   ->   pip install NAME
# @version 1.1
# @description Same as pip (MicroPython's name for it)
import sys

try:
    try:
        source = open("/bin/pip.py").read()     # where pkg installs apps (MyOS with folders)
    except OSError:
        source = open("/pip.py").read()
except OSError:
    source = None
if source is None:
    print("mip runs pip, which isn't installed: pkg install pip")
else:
    sys.argv[0] = "mip"
    exec(source, {"__name__": "__main__"})
