from pathlib import Path
import hashlib,json,os,subprocess,sys,traceback
s=Path(__file__).resolve().parent;r=s.parent;out=r/'build';out.mkdir();rows=[];tests={};H=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def run(argv,name):
 argv=list(map(str,argv))
 with(out/(name+'.stdout')).open('xb')as so,(out/(name+'.stderr')).open('xb')as se:
  p=subprocess.Popen(argv,stdin=subprocess.DEVNULL,stdout=so,stderr=se);row=dict(pid=p.pid,name=name,argv=argv,state='running');rows.append(row);(out/'commands-live.json').write_text(json.dumps(rows)+'\n')
  try:rc=p.wait(timeout=40)
  except BaseException:p.kill();p.wait();raise
  row.update(returncode=rc,state='terminal');(out/'commands-live.json').write_text(json.dumps(rows)+'\n');assert rc==0,(name,(out/(name+'.stderr')).read_text()[-3000:]);return(out/(name+'.stdout')).read_bytes()
try:
 assert os.geteuid()==501;boot=sys.argv[1];assert run(['/usr/sbin/sysctl','-n','kern.bootsessionuuid'],'boot-before').decode().strip()==boot
 source=H(s/'metal-client254.mm');binaries={}
 for mode in('normal','asan'):
  flags=[]if mode=='normal'else['-fsanitize=address,undefined','-fno-sanitize-recover=all'];exe=out/('metal-client254-'+mode);evidence=out/('absence-'+mode);evidence.mkdir()
  run(['/usr/bin/xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fno-objc-arc','-fblocks',*flags,s/'metal-client254.mm','-framework','Foundation','-framework','Metal','-o',exe],'compile-'+mode)
  run(['/usr/bin/codesign','--force','--sign','-','--timestamp=none',exe],'sign-'+mode);run(['/usr/bin/codesign','--verify','--strict',exe],'verify-'+mode)
  tests[mode]=json.loads(run(['/usr/bin/env','ASAN_OPTIONS=detect_leaks=0:halt_on_error=1','UBSAN_OPTIONS=halt_on_error=1',exe,evidence,'0','0','unused','absent'],'test-'+mode));assert tests[mode]['passed']and tests[mode]['rtx_absent']
  binaries[exe.name]=dict(bytes=exe.stat().st_size,sha256=H(exe));run(['/usr/bin/xcrun','nm','-u',exe],'undefined-'+mode)
 assert H(s/'metal-client254.mm')==source
 assert run(['/usr/sbin/sysctl','-n','kern.bootsessionuuid'],'boot-after').decode().strip()==boot
 (out/'result.json').write_text(json.dumps(dict(passed=True,source_sha256=source,binaries=binaries,tests=tests,gpu_executed=False,standard_metal_absence_verified=True),indent=2)+'\n')
except BaseException:(out/'error.txt').write_text(traceback.format_exc());raise
finally:(out/'commands.json').write_text(json.dumps(rows,indent=2)+'\n')
