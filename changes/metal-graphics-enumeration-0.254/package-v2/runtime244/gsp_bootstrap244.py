"""One owning connection: native bootstrap, HOST proof and owned-root admission."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import native244 as client
from bootstrap_gate244 import require_clean_bootstrap
import flush_evidence
import startup_evidence
import gsp_rpc
import gsp_startup
import gsp_event_codec as events
import gsp_init_event_codec as ring_events
import gsp_sequence_codec as sequencer
import gsp_compute_prep_codec as rm_codec
import gsp_application_bar1_codec as bar1_codec
import gsp_application_page_tables_native as pt_codec
import gsp_application_channel_native as channel_codec
import gsp_application_execution_native as execution_codec
import gsp_uploaded_run as application
import uploaded_transport
import uploaded_request
import ondemand_workload
import uploaded_library
from gsp_seal_rehearsal import decode_seal
from prepare_seal_fixture import system_fields
import prepare_gsp

FIELDS = ('magic abi generation phase execution_attempted start_mask provider_open pinned region_owned pci_command '
          'borrowed_owner_verified post_fwsec_seal fwsec_passed fwsec_start_attempted fwsec_start_accepted '
          'fwsec_mailbox0 fwsec_wpr_lo fwsec_wpr_hi fwsec_imem fwsec_dmem fwsec_matched fwsec_cleanup fwsec_command_after '
          'sec2_staged sec2_imem sec2_dmem sec2_matched sec2_dma_attempted sec2_command_enabled gsp_reset_verified '
          'sec2_start_attempted sec2_start_accepted sec2_alias sec2_cpu_before sec2_cpu_after sec2_mailbox0 sec2_mailbox1 '
          'sec2_halted gsp_active gsp_riscv gsp_bcr wpr_lo wpr_hi handoff boot_passed failed_register boot_writes halt_polls '
          'status_header_valid record_captured record_bytes record_function record_result record_sequence payload_bytes '
          'init_done sequencer status_polls producer_index rom_stable resources_held last_poll_register last_poll_value last_poll_count').split()
BOOLS = ('execution_attempted provider_open pinned region_owned borrowed_owner_verified post_fwsec_seal fwsec_passed '
         'fwsec_start_attempted fwsec_start_accepted fwsec_cleanup sec2_staged sec2_dma_attempted '
         'gsp_reset_verified sec2_start_attempted sec2_start_accepted sec2_alias sec2_halted gsp_active boot_passed '
         'status_header_valid record_captured init_done sequencer rom_stable resources_held').split()


def decode_launch(data, generation):
    if type(data) is not bytes or len(data) != 512 or len(FIELDS) != 64:
        raise ValueError('LaunchInfo requires exactly 64 u64 words')
    row = dict(zip(FIELDS, struct.unpack('<64Q', data)))
    if (row['magic'], row['abi'], row['generation']) != (0x525458424f4f5431, 2, generation) or not generation:
        raise ValueError('Wrong launch ABI or generation')
    if any(row[k] not in (0, 1) for k in BOOLS) or row['phase'] > 18 or row['start_mask'] > 3:
        raise ValueError('Invalid launch state or boolean')
    if row['last_poll_register'] > 26 or row['last_poll_count'] > 200:
        raise ValueError('Invalid bounded reset polling evidence')
    if row['execution_attempted'] and not all(row[k] for k in ('provider_open', 'pinned', 'region_owned', 'resources_held')):
        raise ValueError('Exposed resources were not retained')
    if row['boot_passed']:
        required = ('execution_attempted', 'borrowed_owner_verified', 'post_fwsec_seal', 'fwsec_passed',
                    'fwsec_start_attempted', 'fwsec_start_accepted', 'fwsec_cleanup', 'sec2_staged',
                    'gsp_reset_verified', 'sec2_start_attempted', 'sec2_start_accepted', 'sec2_halted', 'gsp_active', 'rom_stable')
        if (not all(row[k] for k in required) or row['phase'] not in (7, 16, 18) or
                (row['start_mask'], row['pci_command'], row['fwsec_command_after'], row['sec2_mailbox0']) != (3, 6, 0, 0) or
                (row['sec2_imem'], row['sec2_dmem'], row['sec2_matched']) != (137, 98, 6272) or
                row['sec2_command_enabled'] != 6 or
                not row['gsp_riscv'] & 0x80):
            raise ValueError('Incomplete native boot evidence')
    if row['record_captured'] and (not row['boot_passed'] or not row['status_header_valid'] or
            not 4096 <= row['record_bytes'] <= 65536 or row['record_bytes'] % 4096 or row['record_sequence'] != 0):
        raise ValueError('Invalid captured record evidence')
    return row


def capture_record(backend, row):
    raw = b''.join(backend.first_record(off, min(4096, row['record_bytes']-off))
                   for off in range(0, row['record_bytes'], 4096))
    record = gsp_rpc.decode_record(raw, expected_sequence=0)
    if (record.rpc.function, record.rpc.result, len(record.rpc.payload)) != (
            row['record_function'], row['record_result'], row['payload_bytes']):
        raise ValueError('Python decoder differs from native captured record')
    init = record.rpc.function == 0x1001 and record.rpc.result == 0 and len(record.rpc.payload) == 4
    if bool(row['init_done']) != init or bool(row['sequencer']) != (record.rpc.function == 0x1002):
        raise ValueError('Native event classification differs from Python')
    details = dict(function=record.rpc.function, result=record.rpc.result, sequence=record.sequence,
                   rpc_sequence=record.rpc.sequence, payload_bytes=len(record.rpc.payload),
                   sha256=hashlib.sha256(raw).hexdigest(), init_done_observed=init,
                   sequencer_handler_required=record.rpc.function == 0x1002)
    if init:
        details['init_done'] = gsp_startup.parse_init_done(raw, expected_sequence=0)
    return raw, details


def finalize_result(result):
    result['passed']=bool(result.get('passed') and result.get('connection_closed') and not any(result.get(k) for k in ('error','launch_error','close_error','final_diagnostic_error')) and result.get('native_root',{}).get('passed'))
    result['host_command_verified']=bool(result['passed'])
    result['compute_verified']=False
    result['initial_root_verified']=bool(result['passed'] and result.get('hardware_backend'))
    result['metal_verified']=False
    return result


def run(backend, firmware, snapshot, output, after_admission):
    result = dict(passed=False, probe_version='0.83.1', connection_closed=False,
                  hardware_backend=type(backend) is client.MacIOKitBackend,
                  launch_requested=False, compute_verified=False, status_queue_consumed=False)
    generation = None
    try:
        catalog=backend.catalog
        if type(catalog) is not uploaded_library.Catalog:raise ValueError('Immutable reviewed catalog required')
        info=client.decode_info(backend.info());generation=info['generation']
        if info['state']!=0 or info['allocated'] or info['published']:raise ValueError('Fresh pre-bootstrap owner required')
        result.update(generation=generation,catalog_sha256=catalog.digest,catalog=catalog.describe())
        result['shader_upload']=uploaded_transport.prepare(backend,catalog,generation,output/'shader-upload')
        if not result['shader_upload']['passed']:raise ValueError('Selected shader upload did not pass')
        backend.begin()
        after=client.decode_info(backend.info())
        if after['generation']!=generation or after['state']!=1 or not after['allocated']:raise ValueError('Bootstrap owner generation/state changed')
        info=after
        result['library_consumed']=uploaded_transport.consumed(backend,catalog,generation,output/'shader-upload/consumed-info.bin')
        pages = {}
        for index, name in enumerate(client.NAMES):
            count = info['resources'][name]['page_count']
            pages[name] = []
            for start in range(0, count, 256):
                pages[name].extend(backend.pages(index, start, min(256, count-start)))
        (output/'bootstrap-pages.json').write_text(json.dumps(pages,sort_keys=True,separators=(',',':'))+'\n')
        _, buffers = prepare_gsp.bind(firmware, pages, snapshot)
        for index, name in enumerate(client.NAMES):
            for off in range(0, len(buffers[name]), 4096):
                backend.write(index, off, buffers[name][off:off+4096])
        backend.prepare_startup(); backend.publish(); backend.seal()
        sealed = decode_seal(backend.seal_info(), generation)
        if not sealed['sealed']:
            raise ValueError('Native host content seal missing')
        fields = system_fields(*(sealed[k] for k in ('bar0', 'bar1', 'bar3', 'max_user_va', 'revision', 'link_cap')))
        buffers['queues'] = gsp_startup.prefill_queue(pages['queues'], gsp_startup.encode_system_info(fields),
            gsp_startup.encode_registry({'RMForcePcieConfigSave': 1, 'RMSecBusResetEnable': 1}))['shared_memory']
        digests = {}
        for index, name in enumerate(client.NAMES):
            digest = hashlib.sha256()
            for off in range(0, len(buffers[name]), 4096):
                digest.update(backend.read(index, off, min(4096, len(buffers[name])-off)))
            digests[name] = digest.hexdigest()
            if digests[name] != hashlib.sha256(buffers[name]).hexdigest():
                raise ValueError('Independent prelaunch readback mismatch: '+name)
        result.update(seal=sealed, prelaunch_readback_sha256=digests, launch_requested=True)
        (output/'prelaunch.json').write_text(json.dumps(result, indent=2)+'\n')
        result['restart_required'] = True  # Conservatively retain this if post-call diagnostics fail.
        try:
            backend.launch()
        except client.BindingError as error:
            result['launch_error'] = str(error)
        flush_raw=backend.flush_info();(output/'flush-info.bin').write_bytes(flush_raw)
        result['flush']=flush_evidence.decode(flush_raw,generation)
        raw = backend.launch_info()
        (output/'launch-info.bin').write_bytes(raw)
        row = decode_launch(raw, generation); result['launch'] = row
        result['channel']=channel_codec.capture(backend,generation,output/'channel')
        result['execution']=execution_codec.capture(backend,generation,output/'execution',result['channel'],output/'channel')
        result['runtime_bootstrap']=application.bootstrap(backend,generation,output/'runtime-bootstrap',result['execution'],output/'execution')
        if row['record_captured']:
            record, details = capture_record(backend, row)
            (output/'first-status.bin').write_bytes(record)
            result['first_record'] = details
        event_raw = backend.event_info()
        (output/'event-info.bin').write_bytes(event_raw)
        summary = events.decode_info(event_raw, generation)
        result['collection'] = summary
        if bool(summary['count']) != bool(row['record_captured']) or (summary['count'] and not row['boot_passed']):
            raise ValueError('Event collection differs from first-record/boot evidence')
        result['events'] = events.capture(backend, summary, output)
        if summary['count'] and result['events'][0]['sha256'] != result['first_record']['sha256']:
            raise ValueError('First-record and event capture APIs disagree')
        result['records_verified'] = len(result['events'])
        sequence_raw = backend.sequence_info()
        (output/'sequence-info.bin').write_bytes(sequence_raw)
        sequence = sequencer.decode(sequence_raw, generation)
        result['sequence'] = sequence
        result['status_queue_consumed'] = bool(sequence['consumer_written'])
        result['consumer_publication_verified'] = bool(sequence['consumer_verified'])
        after_raw = backend.after_info()
        (output/'after-info.bin').write_bytes(after_raw)
        after = ring_events.decode_info(after_raw, generation, sequence['after_slot'], sequence['after_sequence'])
        result['after_collection'] = after
        sequencer.cross_check(sequence, summary, after)
        after_dir = output/'after-sequence'
        after_dir.mkdir()
        result['after_events'] = ring_events.capture(backend, after, after_dir)
        result['init_done_observed'] = bool(summary['init_done'] or after['init_done'])
        result['sequencer_handler_required'] = bool(after['sequencer'] or summary['sequencer'] and not sequence['passed'])
        result['passed'] = bool(row['boot_passed'] and summary['passed'] and 'launch_error' not in result and
            (summary['init_done'] or sequence['passed'] and after['passed']))
        rm_raw=backend.rm_info()
        (output/'rm-info.bin').write_bytes(rm_raw)
        rm=rm_codec.decode(rm_raw,generation);result['rm']=rm
        rm_dir=output/'rm';rm_dir.mkdir()
        result['rm_exchange']=rm_codec.capture(backend,rm,after,rm_dir)
        result['passed']=bool(result['passed'] and result['init_done_observed'] and rm['passed'] and
                              result['rm_exchange']['exchanges_verified'])
        bar1_raw=backend.bar1_info();(output/'bar1-info.bin').write_bytes(bar1_raw)
        bar1=bar1_codec.decode(bar1_raw,generation);result['bar1']=bar1
        result['bar1_readback']=bar1_codec.capture(backend,bar1,output)
        result['passed']=bool(result['passed'] and bar1['passed'] and result['bar1_readback']['readback_verified'])
        pt_raw=backend.page_info();(output/'page-table-info.bin').write_bytes(pt_raw)
        pt=pt_codec.decode(pt_raw,generation);result['page_tables']=pt
        pd_raw=backend.page_rm_info();(output/'page-table-rm-info.bin').write_bytes(pd_raw)
        pd=pt_codec.decode_rm(pd_raw,generation);result['page_rm']=pd
        result['page_table_captures']=pt_codec.capture_tables(backend,pt,output)
        result['page_rm_exchange']=pt_codec.capture_rm(backend,pd,output/'page-table-rm')
        result['passed']=bool(result['passed'] and pt['passed'] and pd['passed'] and result['page_table_captures']['captures_verified'] and result['page_rm_exchange']['exchanges_verified'])
        result['passed']=bool(result['passed'] and result['channel']['passed'] and result['execution']['passed'] and result['runtime_bootstrap']['passed'])
        if not result['passed']:raise ValueError('Bootstrap evidence rejected; no program work submitted')
        result['flush_mapping']=flush_evidence.require_disjoint(result['flush'],generation,pages)
        result['startup_chain']=startup_evidence.require_chain(result)
        result['pre_dispatch_gate']=require_clean_bootstrap(result)
        result['native_root']=backend.admit_root()
        result['application212']=after_admission(backend)
        result['expected_invocations']=result['expected_outputs']=0
        result['expected_programs']=[]
        result['passed']=bool(result['native_root']['passed'] and result['application212']['passed'])
        result['gpu_translation_verified']=False
        result['restart_required'] = bool(row['execution_attempted'])
    except Exception as error:
        result['passed'] = False
        result['error'] = str(error)
        if generation is not None and result['launch_requested'] and 'flush' not in result:
            try:
                diagnostic=backend.flush_info();(output/'failure-flush-info.bin').write_bytes(diagnostic)
                result['failure_flush']=flush_evidence.decode(diagnostic,generation)
            except (ValueError,OSError,client.BindingError) as diagnostic_error:
                result['flush_diagnostic_error']=str(diagnostic_error)
        if generation is not None:
            for name,getter,decoder in (('page_tables',backend.page_info,pt_codec.decode),('page_rm',backend.page_rm_info,pt_codec.decode_rm)):
                try:
                    diagnostic=getter();(output/('failure-'+name+'-info.bin')).write_bytes(diagnostic)
                    result['failure_'+name]=decoder(diagnostic,generation)
                except (ValueError,OSError,client.BindingError) as diagnostic_error:
                    result[name+'_diagnostic_error']=str(diagnostic_error)
        if generation is not None:
            try:
                diagnostic=backend.bar1_info();(output/'failure-bar1-info.bin').write_bytes(diagnostic)
                result['failure_bar1']=bar1_codec.decode(diagnostic,generation)
            except (ValueError,OSError,client.BindingError) as bar1_error:
                result['bar1_diagnostic_error']=str(bar1_error)
        if generation is not None:
            try:
                diagnostic=backend.rm_info()
                (output/'failure-rm-info.bin').write_bytes(diagnostic)
                result['failure_rm']=rm_codec.decode(diagnostic,generation)
            except (ValueError,OSError,client.BindingError) as rm_error:
                result['rm_diagnostic_error']=str(rm_error)
        if generation is not None:
            try:
                diagnostic = backend.sequence_info()
                (output/'failure-sequence-info.bin').write_bytes(diagnostic)
                result['failure_sequence'] = sequencer.decode(diagnostic, generation)
            except (ValueError, OSError, client.BindingError) as diagnostic_error:
                result['sequence_diagnostic_error'] = str(diagnostic_error)
        if generation is not None:
            try:
                raw = backend.launch_info(); (output/'failure-launch-info.bin').write_bytes(raw)
                result['failure_launch'] = decode_launch(raw, generation)
            except (ValueError, OSError, client.BindingError) as diagnostic_error:
                result['diagnostic_error'] = str(diagnostic_error)
    finally:
        try:
            # Capture before closing the owning connection, including launch/earlier
            # decoder failures. These selectors only return retained CPU snapshots.
            if generation is not None and 'runtime_bootstrap' not in result:
                try: result['runtime_bootstrap']=application.collect(backend,generation,output/'failure-runtime')
                except (ValueError,OSError,client.BindingError) as error: result['runtime_diagnostic_error']=str(error)
            if generation is not None and 'execution' not in result:
                try: result['execution']=execution_codec.capture(backend,generation,output/'failure-execution')
                except (ValueError,OSError,client.BindingError) as error: result['execution_diagnostic_error']=str(error)
            if generation is not None and 'channel' not in result:
                try: result['channel']=channel_codec.capture(backend,generation,output/'failure-channel')
                except (ValueError,OSError,client.BindingError) as error: result['channel_diagnostic_error']=str(error)
        except Exception as error:
            result['final_diagnostic_error']=str(error);result['passed']=False
        finally:
            try:
                if backend.close_guard.permitted():
                    backend.close(); result['connection_closed'] = True
                else:
                    result['connection_retained'] = True
                    result['close_permitted'] = False
            except Exception as error:
                result['close_error'] = str(error)
    return finalize_result(result)

