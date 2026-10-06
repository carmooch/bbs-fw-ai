#!/usr/bin/env bash
# Checks the config tool (C#) against the firmware's config layout (C):
# dumps a config_t from C, parses and re-writes it with the tool's code.
# Needs gcc and the .NET 8 SDK; runs on Linux.
set -euo pipefail
cd "$(dirname "$0")"

gcc -DBBSHD -I../firmware -I../firmware/host/fake config_dump.c -o config_dump
./config_dump > config.bin
dotnet run --project tool-check.csproj -- config.bin
