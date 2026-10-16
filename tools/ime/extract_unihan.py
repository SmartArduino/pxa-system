#!/usr/bin/env python3
"""Refresh the small audited snapshot from the pinned Unicode archive."""
import argparse,hashlib,json,unicodedata,zipfile
from collections import defaultdict
from pathlib import Path
ROOT=Path(__file__).resolve().parent
KEYS={'kTGH','kTGHZ2013','kTraditionalVariant','kBigFive','kMandarin','kXHC1983'}

def spelling(text):
    text=unicodedata.normalize('NFD',text.lower()).replace('u\u0308','v')
    text=''.join(c for c in text if not unicodedata.combining(c))
    text={'lve':'lue','nve':'nue'}.get(text,text)
    if not text.isascii() or not text.isalpha() or len(text)>6:raise ValueError('Unsupported reading: '+text)
    return text

def extract(archive,output):
    metadata=json.loads((ROOT/'upstream.json').read_text(encoding='utf-8'))
    if hashlib.sha256(archive.read_bytes()).hexdigest()!=metadata['sha256']:raise ValueError('Archive differs from pinned Unihan 17.0.0')
    properties=defaultdict(dict)
    with zipfile.ZipFile(archive) as z:
        for filename in ('Unihan_OtherMappings.txt','Unihan_Readings.txt','Unihan_Variants.txt'):
            for raw in z.read(filename).decode('utf8').splitlines():
                if not raw.startswith('U+'):continue
                code,key,value=raw.split('\t')
                if key in KEYS:properties[int(code[2:],16)][key]=value
    rows=[]
    for code,p in properties.items():
        if 'kTGH' not in p:continue
        index=int(p['kTGH'].split(':')[1])
        readings=sorted({spelling(t.split(':',1)[1]) for t in p['kTGHZ2013'].split()})
        variants=[int(t.split('<')[0][2:],16) for t in p.get('kTraditionalVariant',f'U+{code:X}').split()]
        rows.append((index,code,readings,variants))
    assert sorted(r[0] for r in rows)==list(range(1,8106))
    text='# SPDX-License-Identifier: Unicode-3.0\n# Unihan 17.0.0: kTGH, kTGHZ2013, kTraditionalVariant. See NOTICE.md.\n# index\tcharacter\ttoneless_readings\ttraditional_counterparts\n'
    text+=''.join(f'{i}\t{chr(cp)}\t{",".join(py)}\t{"".join(map(chr,variants))}\n' for i,cp,py,variants in sorted(rows))
    output.mkdir(parents=True,exist_ok=True);(output/'tgh2013.tsv').write_text(text,encoding='utf-8')
    traditional={cp for _,_,_,variants in rows for cp in variants}
    for code,p in properties.items():
        if any(0xA440<=int(v.rstrip("'"),16)<=0xC67E for v in p.get('kBigFive','').split()):traditional.add(code)
    text='# SPDX-License-Identifier: Unicode-3.0\n# Unihan 17.0.0: traditional counterparts plus Big5 level-one Han characters; own Mandarin/XHC/TGH readings.\n'
    for code in sorted(traditional):
        p=properties[code];values=p.get('kMandarin','').split()
        for key in ('kTGHZ2013','kXHC1983'):values.extend(t.split(':',1)[1] for t in p.get(key,'').split())
        readings=sorted(set(map(spelling,values)));assert readings,hex(code)
        text+=chr(code)+'\t'+','.join(readings)+'\n'
    (output/'traditional-readings.tsv').write_text(text,encoding='utf-8')
    print(f'Extracted {len(rows)} standard and {len(traditional)} traditional characters')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('archive',type=Path);p.add_argument('--output',type=Path,default=ROOT/'data');args=p.parse_args();extract(args.archive,args.output)
if __name__=='__main__':main()
