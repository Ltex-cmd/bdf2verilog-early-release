#!/usr/bin/env python3
"""Bounded serial, current-only fresh conversion and startup-inclusive CLI timings."""
import argparse, datetime, hashlib, json, os, platform, statistics, subprocess, time
from pathlib import Path
from generate import generate
FLAGS=['-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-DBDF_CONTEXT_TAPE']
SOURCES=['parser','schematic','signal_name','lpm','verilog','project','bdf_tape','bdf_span','project_context']
def call(command, **kwargs):
    return subprocess.run([str(x) for x in command],check=True,capture_output=True,timeout=120,**kwargs)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def summary(samples):return {'samples_seconds':samples,'median_seconds':statistics.median(samples),'min_seconds':min(samples),'max_seconds':max(samples)}
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root',type=Path,required=True)
    parser.add_argument('--commit',required=True,help='Exact verified 40-character source commit')
    parser.add_argument('--output',type=Path,default=Path('benchmark-results'))
    parser.add_argument('--cxx',default=os.environ.get('CXX','c++'))
    args=parser.parse_args()
    if len(args.commit)!=40 or any(c not in '0123456789abcdef' for c in args.commit):parser.error('--commit must be a full lowercase SHA')
    source=args.source_root.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    here=Path(__file__).resolve().parent
    if (source/'.git').exists():
        actual=call(['git','-C',source,'rev-parse','HEAD'],text=True).stdout.strip()
        if actual!=args.commit:raise RuntimeError('Source commit does not match --commit')
        if call(['git','-C',source,'status','--porcelain','--untracked-files=no'],text=True).stdout:raise RuntimeError('Tracked source has modifications')
    cases=generate(out/'fixtures')
    files=[source/'src'/f'{s}.cpp' for s in SOURCES]
    binary=out/'converter-measure'
    command=[args.cxx,*FLAGS,'-I'+str(source/'include'),*files,here/'measure.cpp','-o',binary]
    call(command)
    cli=source/'build/bdf-tool'
    if not cli.is_file():raise RuntimeError('Build the converter with make -j1 all before running')
    try:cpu=next(line.split(':',1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('model name'))
    except (OSError,StopIteration):cpu=platform.processor()
    result={'schema':1,'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'commit':args.commit,'platform':platform.platform(),'machine':platform.machine(),'cpu':cpu,'python':platform.python_version(),'compiler':call([args.cxx,'--version'],text=True).stdout,'flags':FLAGS,'compile_command':'$CXX '+ ' '.join(FLAGS)+' -I$SOURCE/include '+ ' '.join('$SOURCE/src/'+s+'.cpp' for s in SOURCES)+' $HARNESS/measure.cpp -o $OUTPUT/converter-measure','reproduce_command':'python3 benchmarks/early-release/run.py --source-root . --commit '+args.commit+' --output benchmark-results','source_sha256':{str(p.relative_to(source)):sha(p) for p in [*files,*sorted((source/'include').glob('*.hpp'))]},'harness_sha256':{p.name:sha(p) for p in sorted(here.glob('*')) if p.is_file()},'binary_sha256':{'converter':sha(cli),'measure':sha(binary)},'method':{'serial':True,'trials':7,'warmups_per_fresh_launch':2,'warmups_cli':2,'target_batch_seconds':0.04,'max_repetitions':1000,'fresh_timed':'public emit_verilog_project: new context, file read, parse, resolve, validate, emit, destruction; no process startup or output file write','cli_timed':'new process, full conversion, stdout captured in pipe; no disk output; Python subprocess overhead included','cache':'No retained converter context; OS filesystem cache warm. No cold-cache claim.','comparison':'Current revision only; no baseline or Quartus speedup claim.'},'cases':[]}
    for case in cases:
        path=out/'fixtures'/case['top'];row=dict(case)
        baseline=call([cli,'verilog',path]).stdout
        (out/(case['name']+'.v')).write_bytes(baseline)
        if ('module '+case['name']+'(').encode() not in baseline.splitlines():raise RuntimeError('Unexpected top module')
        if call([cli,'verilog',path]).stdout!=baseline:raise RuntimeError('Non-deterministic CLI output')
        row['smoke']={'exit_code':0,'deterministic_repeats':2,'output_sha256':hashlib.sha256(baseline).hexdigest(),'output_bytes':len(baseline),'semantic_equivalence_proven':False}
        def measure(n):return json.loads(call([binary,path,n,2],text=True).stdout)
        calibration=measure(1);n=max(1,min(1000,int(0.04/max(calibration['seconds'],1e-9))))
        raw=[measure(n) for _ in range(7)]
        row['fresh']=summary([r['seconds']/r['repetitions'] for r in raw]);row['fresh']['raw_batches']=raw
        for _ in range(2):call([cli,'verilog',path])
        samples=[]
        for _ in range(7):
            start=time.perf_counter();completed=call([cli,'verilog',path]);samples.append(time.perf_counter()-start)
            if completed.stdout!=baseline:raise RuntimeError('Timed CLI output changed')
        row['cli']=summary(samples);result['cases'].append(row)
        (out/'results.json').write_text(json.dumps(result,indent=2)+'\n')
        print(f"{case['name']}: fresh {row['fresh']['median_seconds']*1000:.3f} ms; CLI {row['cli']['median_seconds']*1000:.3f} ms",flush=True)
    print('Completed 12 synthetic cases; raw samples in results.json. Correctness smoke checks are not semantic proof.')
if __name__=='__main__':main()
