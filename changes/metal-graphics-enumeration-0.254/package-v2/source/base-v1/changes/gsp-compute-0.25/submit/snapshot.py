"""Structural checks for captured first-shader bytes; provenance is external.

The bundled fixtures are CPU simulations, so validating them is not hardware
or compute evidence. A future live client must bind captures to one owner/run.
"""
import struct

def validate(queue,backing,initial_image,host_command,host_entry,command,entry):
    if any(type(v) is not bytes for v in (queue,backing,initial_image,host_command,host_entry,command,entry)):raise ValueError('Immutable bytes required')
    if (len(queue),len(backing),len(initial_image),len(host_command),len(host_entry),len(command),len(entry))!=(16384,24576,24576,20,8,32,8):raise ValueError('Snapshot sizes')
    if struct.unpack_from('<II',queue,0x888)!=(2,2):raise ValueError('Queue completion pair')
    if queue[:16]!=host_entry+entry or any(queue[16:256]):raise ValueError('Ring entries and guard')
    expected_command=host_command+bytes(44)+command+bytes(4096-96)
    if queue[0x2000:0x3000]!=expected_command:raise ValueError('Command page and guard')
    if queue[0x3000:0x4000]!=struct.pack('<I',0x30602401)+bytes(4092):raise ValueError('Original HOST completion')
    if backing[:12288]!=initial_image[:12288] or backing[12544:16384]!=initial_image[12544:16384]:raise ValueError('Immutable shader/constants/QMD tail')
    if struct.unpack_from('<I',backing,16384)[0]!=0x30602501:raise ValueError('Shader output missing')
    if struct.unpack_from('<I',backing,20480)[0]!=0x306025f0:raise ValueError('Separate completion missing')
    if backing[16388:20480]!=initial_image[16388:20480] or backing[20484:]!=initial_image[20484:]:raise ValueError('Output/completion guard')
    return dict(valid_snapshot=True,get=2,put=2,output=0x30602501,completion=0x306025f0,guards_match=True)
