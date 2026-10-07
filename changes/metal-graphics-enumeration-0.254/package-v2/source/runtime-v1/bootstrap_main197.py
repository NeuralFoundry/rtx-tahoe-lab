from pathlib import Path
import argparse,hashlib,json,os,sys

def activate(source):
    runtime=source/'runtime-v1';decoder=source/'decoder-v1';admission=source/'admission-v1';base=source/'base-v1'
    sys.path[:0]=list(map(str,(runtime,decoder,admission,base)))
    import runtime_admission,gsp_bootstrap197,native_bootstrap197,channel_chain_audit,startup_evidence
    for module in (runtime_admission,):
        if not Path(module.__file__).resolve().is_relative_to(admission):raise ValueError('Mixed compiler import')
    for module in (channel_chain_audit,startup_evidence):
        if not Path(module.__file__).resolve().is_relative_to(decoder):raise ValueError('Mixed diagnostic import')
    release=json.loads((admission/'admission-release.json').read_bytes());row=release['programs'][0]
    catalog,image,receipt=runtime_admission.load(admission/row['path'],row['manifest_sha256'])
    if image!=(runtime/'selected.rtxlib').read_bytes():raise ValueError('Exact initial program image')
    return catalog,image,receipt

def main():
    if not __debug__ or sys.platform!='darwin' or os.geteuid()!=0:raise RuntimeError('Assertions and root macOS required')
    parser=argparse.ArgumentParser();parser.add_argument('--root',required=True,type=Path);args=parser.parse_args();root=args.root
    if root.resolve()!=root or not root.is_dir():raise ValueError('Canonical experiment root')
    from machine197 import observe,require_cold
    # The controller has already checked the complete frozen file inventory.
    # Capture a new current OS binding directly before the sole native open.
    output=root/'evidence';output.mkdir(mode=0o700)
    result=dict(passed=False,launch_requested=False,compute_verified=False,metal_verified=False)
    try:
        current=observe();(output/'machine-before.json').write_text(json.dumps(current,indent=2)+'\n');binding=require_cold(current)
        catalog,image,receipt=activate(root/'source')
        import native_bootstrap197,gsp_bootstrap197
        (output/'compiler-admission.json').write_text(json.dumps(receipt,indent=2)+'\n')
        runtime=root/'source/runtime-v1';owned=(runtime/'owned-64x1x1.rtxlib').read_bytes()
        backend=native_bootstrap197.MacIOKitBackend(catalog,image,owned,output/'native-host',root/'owner.bundle/Contents/MacOS/RTXMetalDriver',root/'RTXProbe.kext/Contents/MacOS/RTXProbe',binding)
        base=root/'source/base-v1'
        result=gsp_bootstrap197.run(backend,base/'firmware/570.144',base/'results/gsp-preflight-20260906T122322Z/snapshot.plist',output)
    except Exception as error:result.update(passed=False,error=repr(error))
    with(output/'result.json').open('x')as f:json.dump(result,f,indent=2);f.write('\n')
    print(json.dumps({k:result.get(k)for k in ('passed','generation','launch_requested','restart_required','error','close_error','initial_root_verified','compute_verified','metal_verified')}),flush=True)
    return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
