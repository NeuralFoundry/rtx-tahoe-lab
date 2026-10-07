"""Portable protocol subset. Never opens hardware or runs remote commands."""
from pathlib import Path
import subprocess,sys
ROOT=Path(__file__).resolve().parent.parent
TESTS=('test_gsp_boot_args','test_gsp_memory','test_gsp_startup','test_gsp_rpc')
if __name__=='__main__':
    raise SystemExit(subprocess.call([sys.executable,'-B','-m','unittest',*TESTS],cwd=ROOT))
