"""Publisher claims must be bounded, signer-covered and stale-output safe."""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile
import unittest
from publisher_profile import encode_publisher, decode_publisher, PublisherProfileError, PROFILE_PATH
from build_package_manifest import main as build_manifest

class ProfileTests(unittest.TestCase):
    def test_roundtrip_and_bounds(self):
        value=encode_publisher({'handle':'lucinhu','display_name':'作者 Lucin'})
        self.assertEqual(decode_publisher(value)['handle'],'lucinhu')
        for handle in ['../x','con','system','official','a--b','A','a'*25]:
            with self.assertRaises(PublisherProfileError):encode_publisher({'handle':handle,'display_name':'x'})
        for data in [b'{}',b'[]',b' '*1025,b'{"schema":"pxa.publisher-profile/1","handle":"a","handle":"b","display_name":"x"}']:
            with self.assertRaises(PublisherProfileError):decode_publisher(data)
    def test_real_signed_inventory_and_rebuild(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);package=root/'package';(package/'artifacts').mkdir(parents=True)
            # Manifest inventories opaque AOT; executing it is outside this test.
            (package/'artifacts/main.esp32-s31.aot').write_bytes(b'opaque-aot-fixture')
            key=root/'signing.pem';subprocess.run(['openssl','genpkey','-algorithm','EC','-pkeyopt','ec_paramgen_curve:P-256','-out',str(key)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
            source=root/'package.json';metadata={'format':'pxa-package-source-0.1','id':'profile-fixture','version':'1','min_sdk':[2,0],
                'components':[{'id':'main','kind':'ui','artifact':'aot','services':['log']}],
                'publisher':{'handle':'lucinhu','display_name':'Lucin Hu'}}
            def build():
                source.write_text(json.dumps(metadata));build_manifest([str(source),str(package),str(key),'esp32-s31','wamr-profile-fixture'])
            build();profile=(package/PROFILE_PATH).read_bytes()
            manifest=(package/'manifest.pxm').read_bytes()
            self.assertIn(PROFILE_PATH.encode(),manifest);self.assertIn(hashlib.sha256(profile).digest(),manifest)
            metadata['publisher']['display_name']='New Display';build()
            changed=(package/PROFILE_PATH).read_bytes();self.assertNotEqual(profile,changed)
            self.assertIn(hashlib.sha256(changed).digest(),(package/'manifest.pxm').read_bytes())
            del metadata['publisher'];build()
            self.assertFalse((package/PROFILE_PATH).exists());self.assertNotIn(PROFILE_PATH.encode(),(package/'manifest.pxm').read_bytes())

if __name__=='__main__':unittest.main(verbosity=2)
