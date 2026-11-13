# SPDX-License-Identifier: MIT
import copy
import unittest

from interaction_contract import (APP_SCHEMA, DEVICE_SCHEMA, canonical, decode,
                                  descriptor_hash, evaluate, validate_app, validate_device)


def device():
    return {'schema': DEVICE_SCHEMA,
        'identity': {key: 'test' for key in ('vendor','model','hardware_revision','firmware_id','firmware_version','build_id')},
        'profile': {'id':'three-key','revision':1},
        'display': {'width':240,'height':284,'shape':'rounded'},
        'physical': {'touch':False,'max_contacts':0,'simultaneous':True,
            'controls':[{'id':name,'label':name} for name in ('up','down','power')], 'exclusive_groups':[]},
        'modes':[{'id':'focus','kind':'focus','actions': {
            name: {'control':control,'events':[event], 'hold_limit_ms':0,'tap_latency_ms':100}
            for name,control,event in [('previous','up','tap'),('next','down','tap'),('confirm','power','tap'),('back','power','double_tap')]}}],
        'reserved':[{'control':'power','event':'hold','action':'system.menu','threshold_ms':350}]}


def profile(kind='focus', requires=None, **options):
    return {'id':kind,'kind':kind,'complete':True,'requires':requires or {'previous':'tap','next':'tap','confirm':'tap','back':'double_tap'},**options}


def app(*profiles):
    return {'schema':APP_SCHEMA,'profiles':list(profiles or [profile()])}


class InteractionTest(unittest.TestCase):
    def test_three_key_path_and_declaration_is_not_physical_verification(self):
        result=evaluate(app(),device())
        self.assertEqual(result['status'],'compatible')
        self.assertFalse(result['verified'])
        self.assertEqual(result['selected_profile'],'focus')

    def test_legacy_is_unknown_in_both_directions(self):
        for a,d in [(None,device()),(app(),None),(None,None)]:
            self.assertEqual(evaluate(a,d)['status'],'unknown')

    def test_touch_alternative_does_not_hide_complete_button_alternative(self):
        result=evaluate(app(profile('touch',{'tap':'tap'}),profile()),device())
        self.assertEqual(result['status'],'compatible')
        self.assertEqual(result['profiles'][0]['status'],'incompatible')

    def test_cannot_combine_incomplete_modes(self):
        d=device();mode=d['modes'][0];other=copy.deepcopy(mode);other['id']='secondary'
        del mode['actions']['confirm'];other['actions']={'confirm':other['actions']['confirm']}
        d['modes'].append(other)
        self.assertEqual(evaluate(app(),d)['status'],'incompatible')

    def test_adc_group_and_shared_power_disallow_chords(self):
        for group in (['previous','next'],['confirm','back']):
            d=device();d['physical']['exclusive_groups']=[['up','down']]
            self.assertEqual(evaluate(app(profile(simultaneous=[group])),d)['status'],'incompatible')

    def test_unknown_simultaneous_is_not_compatible(self):
        d=device();d['physical']['simultaneous']=None
        self.assertEqual(evaluate(app(profile(simultaneous=[['previous','next']])),d)['status'],'unknown')

    def held(self):
        d=device();binding=d['modes'][0]['actions']['confirm']
        binding.update(events=['press','release','hold'],hold_limit_ms=-1)
        return d

    def test_system_long_press_overrides_unbounded_binding(self):
        for duration in (0,350,8000):
            self.assertEqual(evaluate(app(profile(requires={'confirm':'press'},holds={'confirm':duration})),self.held())['status'],'incompatible')
        self.assertEqual(evaluate(app(profile(requires={'confirm':'press'},holds={'confirm':200})),self.held())['status'],'compatible')

    def test_hold_event_implicitly_requires_indefinite_hold(self):
        self.assertEqual(evaluate(app(profile(requires={'confirm':'hold'})),self.held())['status'],'incompatible')

    def test_unknown_hold_policy_and_latency_are_unknown(self):
        d=self.held();d['reserved'][0]['threshold_ms']=None
        self.assertEqual(evaluate(app(profile(requires={'confirm':'press'},holds={'confirm':10})),d)['status'],'unknown')
        d=device();d['modes'][0]['actions']['confirm']['tap_latency_ms']=None
        self.assertEqual(evaluate(app(profile(max_latency_ms=100)),d)['status'],'unknown')

    def test_multitouch_and_unknown_contact_count(self):
        d=device();d['physical']['touch']=True;d['physical']['max_contacts']=1
        d['modes']=[{'id':'touch','kind':'touch','actions':{'tap':{'control':'power','events':['tap']}}}]
        a=app(profile('touch',{'tap':'tap'},contacts=2))
        self.assertEqual(evaluate(a,d)['status'],'incompatible')
        d['physical']['max_contacts']=None
        self.assertEqual(evaluate(a,d)['status'],'unknown')

    def test_hash_ignores_context_but_changes_with_firmware_and_rules(self):
        d=device();original=descriptor_hash(d)
        d['runtime']={'context':'native'};self.assertEqual(descriptor_hash(d),original)
        d['reserved'][0]['threshold_ms']=8000;self.assertNotEqual(descriptor_hash(d),original)
        d=device();d['identity']['firmware_version']='new';self.assertNotEqual(descriptor_hash(d),original)

    def test_malformed_and_oversized_documents(self):
        for data in (b'{"schema":1,"schema":2}',b'{"x":NaN}',b'[]',b'\xff'):
            with self.assertRaises(ValueError):decode(data,8192)
        with self.assertRaises(ValueError):decode('"界"',4)
        with self.assertRaises(ValueError):validate_app(app(profile(complete=False)))
        d=device();d['physical']['controls'].append(d['physical']['controls'][0])
        with self.assertRaises(ValueError):validate_device(d)

    def test_round_trip_and_no_positive_result_without_an_action_path(self):
        d=device();self.assertEqual(validate_device(decode(canonical(d),8192)),d)
        d['modes']=[];self.assertEqual(evaluate(app(),d)['status'],'incompatible')




class SignedInteractionTest(unittest.TestCase):
    def test_real_packager_covers_optional_declaration_and_removes_stale_record(self):
        import json
        from pathlib import Path
        import struct
        import subprocess
        import tempfile
        from build_package_manifest import main as build_manifest
        with tempfile.TemporaryDirectory(prefix='signed-interaction-') as folder:
            root=Path(folder);package=root/'package';(package/'artifacts').mkdir(parents=True)
            (package/'artifacts/main.esp32-s3.aot').write_bytes(b'opaque-test-artifact')
            key=root/'key.pem';public=root/'public.pem'
            subprocess.run(['openssl','genpkey','-algorithm','EC','-pkeyopt','ec_paramgen_curve:P-256','-out',str(key)],check=True,capture_output=True)
            subprocess.run(['openssl','pkey','-in',str(key),'-pubout','-out',str(public)],check=True,capture_output=True)
            metadata={'format':'pxa-package-source-0.1','id':'interaction-fixture','version':'1','min_sdk':[2,0],
                'components':[{'id':'main','kind':'ui','artifact':'aot','services':['log']}],'interaction':app()}
            source=root/'package.json'
            def build():
                source.write_text(json.dumps(metadata));build_manifest([str(source),str(package),str(key),'esp32-s3','test-engine'])
            build();manifest=(package/'manifest.pxm').read_bytes()
            offset=12;fields=[]
            while offset<len(manifest):
                tag,size=struct.unpack_from('<HH',manifest,offset);offset+=4
                fields.append((tag,manifest[offset:offset+size]));offset+=size
            system=Path(__file__).resolve().parents[2]
            binary=root/'manifest-reader'
            subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections',
                '-Wl,--gc-sections','-I'+str(system/'libpxa/include'),'-I'+str(system/'libpxa/src'),
                str(system/'tools/package/test_manifest_interaction.c'),
                *[str(system/'libpxa/src'/name) for name in ('core/wire.c','common/bytes.c','package/manifest.c','package/manifest_decode.c','package/manifest_security.c')],
                '-o',str(binary)],check=True,capture_output=True)
            subprocess.run([str(binary),str(package/'manifest.pxm')],check=True,capture_output=True)
            self.assertEqual(fields[-1],(0x8015,canonical(app())))
            self.assertEqual([tag for tag,_ in fields],sorted(tag for tag,_ in fields))
            envelope=(package/'signature.pxs').read_bytes();signature=envelope[44:]
            def der_integer(raw):
                raw=raw.lstrip(b'\0') or b'\0'
                if raw[0]&128:raw=b'\0'+raw
                return b'\2'+bytes([len(raw)])+raw
            encoded=der_integer(signature[:32])+der_integer(signature[32:])
            der=root/'signature.der';der.write_bytes(b'\x30'+bytes([len(encoded)])+encoded)
            result=subprocess.run(['openssl','dgst','-sha256','-verify',str(public),'-signature',str(der)],
                input=b'PXA-PACKAGE-MANIFEST\0'+manifest,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
            tampered=manifest.replace(b'"complete":true',b'"complete":null')
            result=subprocess.run(['openssl','dgst','-sha256','-verify',str(public),'-signature',str(der)],
                input=b'PXA-PACKAGE-MANIFEST\0'+tampered,capture_output=True)
            self.assertNotEqual(result.returncode,0)
            del metadata['interaction'];build()
            self.assertNotIn(b'pxa-app-interaction-1',(package/'manifest.pxm').read_bytes())

if __name__=='__main__':unittest.main()
