# MicroPython for MyOS.
# Built by build.ps1 / arm\build.ps1; needs make, python and clang (LLVM) on the PATH.
#   make              -> build/python        (MyOS PC, x86-64)
#   make ARCH=arm64   -> build-arm64/python  (MyOS Pi, ARM64)

ARCH ?= x86_64
ifeq ($(ARCH),arm64)
BUILD = build-arm64
TARGET_ARCH = --target=aarch64-unknown-none-elf -mcpu=cortex-a53
LDARCH = -Wl,--defsym=MYOS_ARCH=2
else
BUILD = build
TARGET_ARCH = --target=x86_64-unknown-none-elf -mno-red-zone
LDARCH =
endif

include ../third_party/micropython/py/mkenv.mk

QSTR_DEFS = qstrdefsport.h
# Off: it relies on the compiler folding strcmp() at compile time,
# which clang won't do in freestanding mode (the binary grows ~4x).
MICROPY_ROM_TEXT_COMPRESSION ?= 0

include $(TOP)/py/py.mk

CC = clang
CPP = clang -E
LD = clang
PYTHON = python

TARGET = $(TARGET_ARCH) -ffreestanding -fno-pic -fno-pie -fno-stack-protector -nostdlib

INC += -I. -Iinclude -I../csdk -I$(TOP) -I$(BUILD)
CFLAGS += $(INC) $(TARGET) -std=gnu99 -Os -DNDEBUG -Wall -Wno-unused-command-line-argument \
          -fdata-sections -ffunction-sections -fomit-frame-pointer
LDFLAGS = $(TARGET) -fuse-ld=lld -Wl,-T,../csdk/link.ld $(LDARCH) -Wl,--gc-sections -Wl,--oformat=binary

SRC_C = \
	main.c \
	hal.c \
	file.c \
	crt0.c \
	mathextra.c \
	shared/libc/printf.c \
	shared/libc/string0.c \
	shared/readline/readline.c \
	shared/runtime/gchelper_generic.c \
	shared/runtime/pyexec.c \
	shared/runtime/stdout_helpers.c \
	$(LIBM_C) \
	$(EXTMOD_C) \

# extra Python modules: random, json, time
EXTMOD_C = extmod/modrandom.c extmod/modjson.c extmod/modtime.c

# double-precision math library that ships with MicroPython
LIBM_C = $(addprefix lib/libm_dbl/, \
	__cos.c __expo2.c __fpclassify.c __rem_pio2.c __rem_pio2_large.c \
	__signbit.c __sin.c __tan.c acos.c acosh.c asin.c asinh.c atan.c \
	atan2.c atanh.c ceil.c cos.c cosh.c copysign.c erf.c exp.c expm1.c \
	floor.c fmod.c frexp.c ldexp.c lgamma.c log.c log10.c log1p.c modf.c \
	nearbyint.c pow.c rint.c round.c scalbn.c sin.c sinh.c sqrt.c tan.c \
	tanh.c tgamma.c trunc.c)
$(BUILD)/lib/libm_dbl/%.o: CFLAGS += -w

SRC_QSTR += file.c shared/readline/readline.c shared/runtime/pyexec.c $(EXTMOD_C)

OBJ += $(PY_CORE_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))

all: $(BUILD)/python

$(BUILD)/python: $(OBJ)
	$(ECHO) "LINK $@"
	$(Q)$(LD) $(LDFLAGS) -o $@ $^

include $(TOP)/py/mkrules.mk
