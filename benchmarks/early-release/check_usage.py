#!/usr/bin/env python3
"""Separate CLI/driver smoke validation; does not time or simulate circuits."""
import argparse, hashlib, json, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source-root',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
s=a.source_root.resolve();out=a.output.resolve();checks=[]
def run(label,args,expected=0):
    result=subprocess.run([str(x) for x in args],capture_output=True,text=True,timeout=30)
    if result.returncode!=expected:raise RuntimeError(f'{label}: exit {result.returncode}: {result.stderr}')
    checks.append({'name':label,'exit_code':result.returncode,'expected_exit_code':expected,'stdout_sha256':hashlib.sha256(result.stdout.encode()).hexdigest(),'stderr':result.stderr})
    return result.stdout
json.loads(run('inspect minimal',[s/'build/bdf-tool','inspect',s/'tests/minimal.bdf']))
assert 'module example(' in run('verilog explicit module',[s/'build/bdf-tool','verilog',s/'tests/minimal.bdf','example'])
run('CLI missing arguments',[s/'build/bdf-tool'],2)
run('unknown command',[s/'build/bdf-tool','unknown',s/'tests/minimal.bdf'],1)
run('missing library argument',[s/'build/bdf-tool','verilog',s/'tests/minimal.bdf','--library'],1)
for name,args in [('benchmark memory',[s/'build/benchmark-core',s/'tests/minimal.bdf']),('benchmark write',[s/'build/benchmark-core',s/'tests/minimal.bdf',out/'existing-core-output.v'])]:
    data=json.loads(run(name,args));assert len(data['per_conversion_seconds'])==7
    checks[-1]['writes_output_file']=data['writes_output_file']
assert (out/'existing-core-output.v').read_text()==run('reference for benchmark output',[s/'build/bdf-tool','verilog',s/'tests/minimal.bdf'])
for case in json.loads((out/'fixtures/manifest.json').read_text())['cases']:
    if case['family'].startswith('hierarchy'):continue
    text=(out/(case['name']+'.v')).read_text()
    if case['family']=='dense_bus_scalar_early_hit':
        for i in range(case['size']):assert f'assign Y{i} = A;' in text
    elif case['family']=='flat_scalar_chain':assert 'assign Y = A;' in text
    else:
        for i in range(32):assert f'assign Y[{i}] = A[{i}];' in text
    checks.append({'name':case['name']+' direct identity statements','passed':True,'scope':'literal emitted identity assignments only; not general semantic simulation'})
(out/'usage-validation.json').write_text(json.dumps({'checks':checks,'semantic_simulation':'Not performed by this script; use generated check.v with a separate HDL simulator.'},indent=2)+'\n')
print(f'Passed {len(checks)} CLI/harness usage and emitted-identity checks')
