from pathlib import Path,PurePosixPath
import hashlib,json,os,sys
ROOT=Path(__file__).resolve().parent.parent
def save(path,value):
 with Path(path).open('x')as f:json.dump(value,f,indent=2);f.write('\n')
def verify():
 assert sys.platform=='darwin'and os.geteuid()==0 and __debug__ and ROOT.resolve()==ROOT
 raw=(ROOT/'package-manifest.json').read_bytes();pin=(ROOT/'package-pin.txt').read_text().strip();assert hashlib.sha256(raw).hexdigest()==pin
 rows=json.loads(raw);seen=set()
 for row in rows:
  name=row['path'];n=PurePosixPath(name);assert not n.is_absolute()and '..'not in n.parts and '\\'not in name and ':'not in name and name not in seen;seen.add(name)
  p=ROOT/name;assert p.resolve()==p and p.stat().st_uid==0 and not p.stat().st_mode&0o022
  b=p.read_bytes();assert len(b)==row['bytes']and hashlib.sha256(b).hexdigest()==row['sha256'],name
 return dict(manifest_sha256=pin,files=len(rows))
