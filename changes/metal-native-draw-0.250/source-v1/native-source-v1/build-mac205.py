"""CPU and freestanding selection checks; no IOKit connection or GPU access."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,traceback
s=Path(__file__).resolve().parent;r=s.parent;out=r/'build';out.mkdir()
rows=[]
def save(p,v):
 with p.open('x')as f:json.dump(v,f,indent=2);f.write('\n')
def run(argv,name,env=None):
 with(out/(name+'.stdout')).open('xb')as stdout,(out/(name+'.stderr')).open('xb')as stderr:
  p=subprocess.Popen(argv,cwd=s,env=env,stdin=subprocess.DEVNULL,stdout=stdout,stderr=stderr)
  try:rc=p.wait(timeout=240)
  except BaseException:p.kill();p.wait();raise
 rows.append(dict(name=name,argv=argv,pid=p.pid,returncode=rc))
 assert rc==0,(name,rc)
try:
 assert sys.platform=='darwin'and os.geteuid()==501
 boot=subprocess.check_output(['/usr/sbin/sysctl','-n','kern.bootsessionuuid'],text=True).strip();assert boot==sys.argv[1]
 raw=(s/'source-manifest.json').read_bytes()
 for row in json.loads(raw):
  p=s/row['path'];b=p.read_bytes();assert not p.is_symlink()and len(b)==row['bytes']and hashlib.sha256(b).hexdigest()==row['sha256']
 run(['/usr/bin/clang++','--version'],'compiler')
 for mode in ('normal','asan'):
  flags=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation']
  env=os.environ.copy()
  if mode=='asan':
   flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer'];env['ASAN_OPTIONS']='detect_leaks=0:abort_on_error=1';env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
  for name in ('program-tests205','regression-native205','regression-kernel183'):
   label=mode+'-'+name;binary=out/label
   run(['/usr/bin/clang++',*flags,'-MMD','-MF',str(out/(label+'.d')),str(s/(name+'.cpp')),'-o',str(binary)],label+'-build')
   run([str(binary),str(s),str(out/(label+'-results'))],label+'-run',env)
 run(['/usr/bin/clang++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-ffreestanding','-fno-exceptions','-fno-rtti','-mno-red-zone','-fstack-usage','-Wframe-larger-than=2048','-MMD','-MF',str(out/'kernel-selection205.d'),'-c',str(s/'kernel-selection205.cpp'),'-o',str(out/'kernel-selection205.o')],'kernel-selection')
 assert subprocess.check_output(['/usr/sbin/sysctl','-n','kern.bootsessionuuid'],text=True).strip()==boot
 save(out/'result.json',dict(passed=True,source_manifest_sha256=hashlib.sha256(raw).hexdigest(),commands=len(rows),gpu_executed=False))
finally:save(out/'commands.json',rows)
