"""Verify source inventory; --write refreshes it using tracked source paths."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parent.parent
MANIFEST='publication/files.json'
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--write',action='store_true');args=parser.parse_args()
    names=sorted(filter(None,subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).decode().split('\0')))
    names=[n for n in names if n!=MANIFEST]
    rows=[]
    for n in names:
        p=Path(n);assert not p.is_absolute()and '..'not in p.parts and '\\'not in n and ':'not in n
        f=ROOT/p;assert f.is_file()and not f.is_symlink();b=f.read_bytes();rows.append(dict(path=n,bytes=len(b),sha256=hashlib.sha256(b).hexdigest()))
    target=ROOT/MANIFEST
    if args.write:target.parent.mkdir(exist_ok=True);target.write_text(json.dumps(rows,indent=2)+'\n')
    else:assert json.loads(target.read_bytes())==rows,'Published file list/content changed; review and update inventory'
    print(json.dumps(dict(passed=True,source_files=len(rows),inventory_written=args.write,gpu_executed=False)))
if __name__=='__main__':main()
