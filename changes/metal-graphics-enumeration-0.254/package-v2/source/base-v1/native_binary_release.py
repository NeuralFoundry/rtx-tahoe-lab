"""Read the pinned build receipt assembled after independent Mac verification.

Import does not read files. No default or legacy binary hashes are accepted.
The final release package includes this receipt in its source manifest.
"""
from pathlib import Path
import json,re

OWNER_PATH='/Users/DEVELOPER/rtx-native-observation037-v1/cpu/RTXNativeCommandOwner.dylib'
APP_PATH='/Users/DEVELOPER/rtx-standard-app037-v1/cpu/RTXStandardApp.dylib'
NATIVE_SOURCE='271ecff86448e0d23052597b531255a84420ee380ffff4227113345f4b3d5ada'
APP_SOURCE='4232bc8984900c2d21ad85527e893737ae1a5af76606f9e76d2cb667558e1501'
CONTAINER='1118527372cafe7ffab131bc513e96dd1548fc7921289e290ce49a9152a5737f'
KEYS={'abi','probe_version','owner_path','app_path','owner_sha256','app_sha256','app_owner_sha256',
      'native_source_manifest_sha256','app_source_manifest_sha256','native_cpu_archive_sha256','app_cpu_archive_sha256','container_sha256'}

def validate(row):
    if type(row) is not dict or set(row)!=KEYS:raise ValueError('Binary release fields')
    expected={'abi':1,'probe_version':'0.37.0','owner_path':OWNER_PATH,'app_path':APP_PATH,
              'native_source_manifest_sha256':NATIVE_SOURCE,'app_source_manifest_sha256':APP_SOURCE,'container_sha256':CONTAINER}
    for key,value in expected.items():
        if type(row[key]) is not type(value) or row[key]!=value:raise ValueError('Binary release '+key)
    for key in KEYS:
        if key.endswith('sha256') and (type(row[key]) is not str or not re.fullmatch('[0-9a-f]{64}',row[key]) or row[key]=='0'*64):raise ValueError('Binary release digest '+key)
    if row['app_owner_sha256']!=row['owner_sha256']:raise ValueError('Application owner link mismatch')
    return dict(row)

def pairs(items):
    row={}
    for key,value in items:
        if key in row:raise ValueError('Duplicate release field')
        row[key]=value
    return row

def load():
    path=Path(__file__).with_name('binary-release.json')
    if path.is_symlink() or path.resolve()!=path or not path.is_file():raise ValueError('Verified Mac binary release receipt missing')
    if not 0<path.stat().st_size<=4096:raise ValueError('Binary release receipt size')
    return validate(json.loads(path.read_bytes(),object_pairs_hook=pairs))
