# Host builds of the BBSHD firmware logic, compiled with gcc.
#
#   make test   build and run the unit tests
#   make sim    build the ride simulator and run every scenario into out/
#   make plots  plot each out/<name>.csv to out/<name>.svg (run make sim first)
#
# See BUILDING.md at the repository root.

.PHONY: all test sim plots clean

CC ?= gcc
FW = ..

# BBSHD is the only target this fork tests. fake/ comes first on the include
# path so its bbsx/stc15.h replaces the SDCC-only one.
CFLAGS = -std=c99 -DBBSHD -g -Wall -Wextra -Werror \
	-Wno-sign-compare -Wno-unused-parameter -Wno-unused-function -Wno-unknown-pragmas \
	-Ifake -I$(FW) -Itest

# Logic modules shared by both builds, compiled unmodified.
LOGIC_SRCS = \
	$(FW)/app.c \
	$(FW)/battery.c \
	$(FW)/cfgstore.c \
	$(FW)/eventlog.c \
	$(FW)/throttle.c

TEST_SRCS = \
	fake/fake_hw.c \
	fake/fake_sensors.c \
	test/test_main.c \
	test/test_app.c \
	test/test_battery.c \
	test/test_cfgstore.c \
	test/test_throttle.c

# The simulator runs the real PAS/speed/brake sensor code.
SIM_SRCS = \
	$(FW)/bbsx/sensors.c \
	fake/fake_hw.c \
	fake/fake_pins.c \
	sim/sim.c \
	sim/scenario.c

HEADERS = $(wildcard $(FW)/*.h $(FW)/bbsx/*.h fake/*.h fake/bbsx/*.h test/*.h sim/*.h)

SCENARIOS = $(wildcard sim/scenarios/*.sim)

all: test_runner ride_sim

test_runner: $(LOGIC_SRCS) $(TEST_SRCS) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(LOGIC_SRCS) $(TEST_SRCS)

ride_sim: $(LOGIC_SRCS) $(SIM_SRCS) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(LOGIC_SRCS) $(SIM_SRCS) -lm

test: test_runner
	./test_runner

sim: ride_sim
	@mkdir -p out
	@status=0; for s in $(SCENARIOS); do \
		./ride_sim $$s out/$$(basename $$s .sim).csv || status=1; \
	done; exit $$status

plots:
	@for c in out/*.csv; do python3 sim/plot.py $${c%.csv}.svg $$c; done

clean:
	rm -rf test_runner ride_sim out
