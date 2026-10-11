import json,re,statistics,hashlib,shutil
from pathlib import Path
from PIL import Image
import argparse
parser=argparse.ArgumentParser();parser.add_argument("--runs",type=Path,required=True)
r=parser.parse_args().runs.resolve()
out=r/'final-summary.json'
summary={}
for variant in ['before','after','call']:
 p=r/f'device-control-{variant}-final/report.json'
 if not p.exists():continue
 d=json.loads(p.read_text());row={'control_passed':d['passed'],'control':{}}
 for phase in ['launch','steady']:
  vals=[x[phase]['ns_per_call_median'] for x in d['runs']]
  row['control'][phase]={'run_medians_ns':vals,'median_ns':statistics.median(vals)}
 p=r/f'device-voxel-{variant}-final/report.json'
 if p.exists():
  d=json.loads(p.read_text());row['voxel_passed']=d['passed'];row['voxel']={}
  for name,fn in [('display_fps',lambda x:x['display_fps']),('raster_us',lambda x:x['raster']['mean_us']),('display_interval_us',lambda x:x['present']['mean_us'])]:
   vals=[fn(x) for x in d['runs']];row['voxel'][name]={'runs':vals,'median':statistics.median(vals)}
  row['memory']={phase:[x[phase+'_heap'] for x in d['runs']] for phase in ['launch','steady']}
  row['stable_heap']=[{k:v for k,v in x['memory_warm']['heap'].items() if k.endswith('_total') or k.endswith('_free')} for x in d['runs']]
  row['surface']=d['runs'][0]['memory_capture']['surface']
  serial=r/f'device-voxel-{variant}-final/serial.jsonl';seen=set();runs=[];current=None;memorylogs=[]
  for line in serial.read_text().splitlines():
   item=json.loads(line);kind=item.get('kind');payload=item.get('payload')
   if kind=='COMMAND' and payload.startswith('PACKAGE run '):
    current={};runs.append(current)
   elif kind=='COMMAND' and payload.startswith('PACKAGE stop '):current=None
   elif kind in ['LOG','RAW'] and isinstance(payload,str) and payload not in seen:
    seen.add(payload)
    if any(s in payload for s in ['AOT mapped:','WAMR dynamic:','Activation memory:','Component memory:']):memorylogs.append(payload)
    if current is not None:
     for label in ['VOXEL-STAGES','VOXEL-PIPE']:
      if label in payload:
       values={k:int(v) for k,v in re.findall(r'(\w+)=([0-9]+)',payload.split(label,1)[1])}
       if label=='VOXEL-STAGES' and not values.get('samples'):continue
       current[label]=values
  row['guest_stages']=runs;row['allocator_logs']=memorylogs
 p=r/f'device-input-{variant}-final/report.json'
 if p.exists():
  d=json.loads(p.read_text());row['input_passed']=d['passed'];row['input']={}
  for stage in ['guest','present','visible']:
   n=sum(x['metrics'][stage+'_count'] for x in d['runs']);total=sum(x['metrics'][stage+'_total_us'] for x in d['runs'])
   row['input'][stage]={'count':n,'weighted_mean_us':total/n,'run_mean_us':[x['metrics'][stage+'_total_us']/x['metrics'][stage+'_count'] for x in d['runs']],'run_p95_histogram_upper_us':[x['metrics'][stage+'_p95_us'] for x in d['runs']],'max_us':max(x['metrics'][stage+'_max_us'] for x in d['runs'])}
 summary[variant]=row
out.write_text(json.dumps(summary,indent=2)+'\n')
for k,v in summary.items():
 print(k, 'control',v['control'])
 if 'voxel' in v:print('voxel',v['voxel'],'memory',v['memory'])
 if 'input' in v:print('input',v['input'])
