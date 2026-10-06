# Host-side tests: the firmware's logic modules built with gcc against a
# fake hardware layer (fake/fake_hw.c). Run with `make run`.

.PHONY: all run clean

CC ?= gcc
TARGET = test_runner

# BBSHD is the only target this fork tests.
CFLAGS = -std=c99 -DBBSHD -g -Wall -Wextra -Werror \
	-Wno-sign-compare -Wno-unused-parameter -Wno-unused-function \
	-I.. -Ifake -I.

FIRMWARE_SRCS = \
	../app.c \
	../battery.c \
	../cfgstore.c \
	../eventlog.c \
	../throttle.c

TEST_SRCS = \
	fake/fake_hw.c \
	test_main.c \
	test_app.c \
	test_battery.c \
	test_cfgstore.c \
	test_throttle.c

all: $(TARGET)

$(TARGET): $(FIRMWARE_SRCS) $(TEST_SRCS) $(wildcard *.h fake/*.h ../*.h)
	$(CC) $(CFLAGS) -o $@ $(FIRMWARE_SRCS) $(TEST_SRCS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
