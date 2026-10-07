"""Scoped local compiler service; Unix peers authenticate with kernel credentials."""
from pathlib import Path
import collections,json,os,socket,threading
from compiler_protocol import MAX_REQUEST,MAX_RESPONSE,peer_identity154,receive,send

class Service:
    def __init__(self,compiler,path,allowed_uid=501):
        if os.geteuid()!=0 or type(allowed_uid) is not int or allowed_uid!=501:raise PermissionError('Compiler service owner/peer policy')
        self.path=Path(path)
        if self.path.resolve()!=self.path or self.path.exists() or self.path.parent.stat().st_uid!=0 or self.path.parent.stat().st_mode&0o022:raise ValueError('Root-owned compiler socket path')
        if len(os.fsencode(self.path))>=104:raise ValueError('Darwin Unix socket path length')
        self.compiler=compiler;self.allowed_uid=allowed_uid;self.stop_event=threading.Event();self.records=collections.deque(maxlen=256);self.dropped=0;self.active=False
        self.socket=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);self.socket.bind(str(self.path));os.chmod(self.path,0o666);self.socket.listen(4);self.socket.settimeout(.25)
        self.thread=threading.Thread(target=self._serve,name='RTXRuntimeCompiler062',daemon=False)
    def start(self):self.thread.start()
    def _serve(self):
        while not self.stop_event.is_set():
            try:connection,_=self.socket.accept()
            except socket.timeout:continue
            with connection:
                record=dict(peer_uid=None,peer_pid=None,air_sha256=None,entry=None,accepted=False,compiled=False,error=None);self.active=True
                try:
                    connection.settimeout(45);identity=peer_identity154(connection);uid=identity['uid'];record['peer_uid']=uid;record['peer_pid']=identity['pid']
                    if uid!=self.allowed_uid:raise PermissionError('Compiler peer UID')
                    row=receive(connection,MAX_REQUEST);record['accepted']=True;record['air_sha256']=row.get('air_sha256');record['entry']=row.get('entry')
                    result=self.compiler.compile(row,peer=identity);record['compiled']=result['ok'];send(connection,result,MAX_RESPONSE)
                except Exception as e:
                    record['error']=type(e).__name__+': '+str(e)
                    try:send(connection,dict(version=1,ok=False,error=record['error']),MAX_RESPONSE)
                    except Exception:pass
                finally:
                    self.active=False
                    if len(self.records)==self.records.maxlen:self.dropped+=1
                    self.records.append(record)
    def close(self):
        self.stop_event.set();self.thread.join(50)
        if self.thread.is_alive() or self.active:raise RuntimeError('Compiler handler has not drained')
        self.socket.close();self.path.unlink()
        return dict(stopped=True,active=False,records=list(self.records),dropped_records=self.dropped,socket_removed=not self.path.exists())
