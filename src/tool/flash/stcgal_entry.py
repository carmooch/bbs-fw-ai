# Entry point for the stcgal.exe bundled with the config tool (built by CI
# with PyInstaller). stcgal flushes its "please cycle power" prompt itself;
# unbuffered output also shows the rest of its log (chip details, options)
# live in the tool, instead of all at once when it exits.
import sys

sys.stdout.reconfigure(write_through=True)
sys.stderr.reconfigure(write_through=True)

import stcgal.frontend

sys.exit(stcgal.frontend.cli())
