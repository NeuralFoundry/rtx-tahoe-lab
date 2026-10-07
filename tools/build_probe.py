"""Compile/link the experimental parent kext; never load, sign or execute it."""
from pathlib import Path
import argparse,plistlib,subprocess,sys
ROOT=Path(__file__).resolve().parent.parent
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,default=ROOT/'out/probe-build',help='New output directory; must not already exist')
    args=parser.parse_args()
    if sys.platform!='darwin':parser.error('Requires an x86-64 macOS SDK and Xcode command-line tools')
    kernel=ROOT/'changes/metal-native-draw-0.250/source-v1/native-source-v1/probe/kernel'
    for name in ('fwsec-payload.hpp','sec2-payload.hpp'):
        if not(kernel/'driver/generated'/name).is_file():parser.error('Generate local firmware input first: '+name+' (see BUILDING.md)')
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    def xcrun(*argv):return subprocess.check_output(['xcrun',*argv],text=True).strip()
    clang=xcrun('--find','clang++');sdk=xcrun('--show-sdk-path');version=xcrun('--show-sdk-version')
    flags=['-arch','x86_64','-std=c++14','-mkernel','-DKERNEL','-DKERNEL_EXTENSION','-DRTX_GRAPHICS242',
        '-fno-builtin','-fno-exceptions','-fno-rtti','-fno-common','-fno-stack-protector','-mgeneral-regs-only','-mno-red-zone',
        '-mmacosx-version-min='+version,'-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-Wno-unused-parameter',
        '-Wframe-larger-than=4096','-fstack-usage','-isysroot',sdk,'-isystem',sdk+'/System/Library/Frameworks/Kernel.framework/Headers']
    subprocess.run([clang,*flags,'-c',str(kernel/'driver/GSPLibraryUploadProbe.cpp'),'-o',str(out/'kernel.o')],check=True)
    frames=(out/'kernel.su').read_text().splitlines()
    if not frames or any(line.split('\t')[-1]!='static'or int(line.split('\t')[-2])>4096 for line in frames):raise RuntimeError('Kernel stack budget check failed')
    info=plistlib.loads((kernel/'driver/Info.plist').read_bytes());assert info['CFBundleVersion']=='0.83.1'
    contents=out/'RTXProbe.kext/Contents';(contents/'MacOS').mkdir(parents=True)
    (contents/'Info.plist').write_bytes(plistlib.dumps(info));binary=contents/'MacOS/RTXProbe'
    subprocess.run([clang,'-arch','x86_64','-nostdlib','-isysroot',sdk,'-mmacosx-version-min='+version,
        '-Wl,-kext','-Wl,-undefined,dynamic_lookup','-Wl,-no_fixup_chains',str(out/'kernel.o'),'-lkmod','-lkmodc++','-lcc_kext','-o',str(binary)],check=True)
    print('Built unsigned kext at '+str(contents.parent)+'. Not loaded; no GPU execution.')
if __name__=='__main__':main()
