.PHONY: all run clean

CC = gcc
TARGET = test_runner

INC_DIRS = -I.. -I./mocks -I.

CFLAGS = -std=c99 -Wall -Wextra -Wno-unused-function $(INC_DIRS)

SRCS = \
	../throttle.c \
	../battery.c \
	../cfgstore.c \
	mocks/mock_hw.c \
	mocks/mock_eeprom.c \
	test_util.c \
	test_throttle.c \
	test_battery.c \
	test_cfgstore.c \
	test_main.c

all: run

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
