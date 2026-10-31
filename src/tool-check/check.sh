#!/usr/bin/env bash
# Checks the config tool (C#) against the firmware:
# - layout: dumps a config_t from C, parses and re-writes it with the tool's code;
# - end to end: the tool's connection, bench and flash wizard code against a
#   virtual controller (the ride simulator running the firmware's serial
#   protocol code on a pseudo-terminal) and a stand-in for stcgal.
# Needs gcc, make, python3 and the .NET 8 SDK; runs on Linux.
set -euo pipefail
cd "$(dirname "$0")"

gcc -DBBSHD -I../firmware -I../firmware/host/fake config_dump.c -o config_dump
./config_dump > config.bin
dotnet run --project tool-check.csproj -- config.bin

make -s -C ../firmware/host ride_sim
# the real stcgal too, when this python has it (CI installs it)
if [ -z "${STCGAL_PYTHON:-}" ] && python3 -c "import stcgal" 2>/dev/null; then
	export STCGAL_PYTHON=python3
fi
dotnet run --project tool-check.csproj -- e2e ../firmware/host/ride_sim ../firmware/host/sim
