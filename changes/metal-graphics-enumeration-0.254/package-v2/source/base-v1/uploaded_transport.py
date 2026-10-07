"""One checked library transfer before any DMA/bootstrap action; never retries."""
from pathlib import Path
import json,struct
import library_upload as codec
from uploaded_library import Catalog,need

def prepare(backend,catalog,generation,output):
    need(type(catalog) is Catalog and backend.catalog is catalog,'connection catalog binding')
    output=Path(output);output.mkdir();result=dict(passed=False,generation=generation,phases=[],payload_sha256=catalog.digest,kernel_bootstrap_attempted=False)
    def capture(name,phase,written,chunks,sealed):
        raw=backend.upload_info();(output/(name+'-info.bin')).write_bytes(raw);r=codec.decode_info(raw)
        need((r['phase'],r['written'],r['chunks'],r['sealed'],r['error'])==(phase,written,chunks,sealed,0),'upload state changed')
        if phase:
            need(r['generation']==generation and r['expected_sha256']==catalog.digest,'upload identity/digest')
            if sealed:need(r['actual_sha256']==catalog.digest and r['programs']==len(catalog.programs) and r['code_bytes']==struct.unpack_from('<I',catalog.library,20)[0],'sealed upload metadata')
        result['phases'].append(r);return r
    try:
        capture('empty',0,0,0,False)
        header=codec.begin_header(generation,catalog.library,catalog.code);(output/'begin-header.bin').write_bytes(header)
        result['begin_attempted']=True;backend.upload_begin(header);capture('begin',1,0,0,False)
        for i,(offset,data) in enumerate(codec.upload_chunks(catalog.library,catalog.code)):
            (output/('chunk-%d.bin'%i)).write_bytes(data);result['last_append_attempted']=i;backend.upload_append(offset,data)
            capture('chunk-%d'%i,1,offset+len(data),i+1,False)
        result['seal_attempted']=True;backend.upload_seal();capture('sealed',2,4608,5,True)
        captures=[]
        for part,(name,total) in enumerate((('library',512),('code',4096))):
            with (output/(name+'-readback.bin')).open('xb') as f:
                for off in range(0,total,1024):
                    n=min(1024,total-off);b=backend.upload_read(part,off,n);need(type(b) is bytes and len(b)==n,'partial upload readback');f.write(b)
            captures.append((output/(name+'-readback.bin')).read_bytes())
        catalog.verify(*captures);capture('ready',2,4608,5,True);result['passed']=True
    except (ValueError,OSError,RuntimeError) as error:
        result['error']=str(error)
        try:(output/'failure-info.bin').write_bytes(backend.upload_info())
        except (ValueError,OSError,RuntimeError) as diagnostic:result['diagnostic_error']=str(diagnostic)
    finally:(output/'decoded.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

def consumed(backend,catalog,generation,path):
    raw=backend.upload_info();Path(path).write_bytes(raw);r=codec.decode_info(raw)
    need(r['phase']==3 and r['generation']==generation and r['sealed'] and r['expected_sha256']==r['actual_sha256']==catalog.digest and r['programs']==len(catalog.programs),'native bootstrap did not consume selected library')
    return r
