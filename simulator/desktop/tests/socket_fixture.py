#!/usr/bin/env python3
"""Run bounded TCP echo/TLS rejection tests; all output files stay in --artifacts."""
import argparse, pathlib, socketserver, ssl, subprocess, threading
p=argparse.ArgumentParser();p.add_argument('binary');p.add_argument('--artifacts',required=True);args=p.parse_args()
root=pathlib.Path(args.artifacts);root.mkdir(parents=True,exist_ok=True)
subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-keyout',str(root/'key.pem'),'-out',str(root/'cert.pem'),'-days','1','-subj','/CN=localhost'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
class Echo(socketserver.BaseRequestHandler):
    def handle(self):
        try:
            while data:=self.request.recv(79):self.request.sendall(data)
        except OSError:pass
class TLS(Echo):
    def handle(self):
        try:
            self.request=context.wrap_socket(self.request,server_side=True)
            super().handle()
        except (OSError,ssl.SSLError):pass
class Server(socketserver.ThreadingTCPServer):daemon_threads=True;allow_reuse_address=True
context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(root/'cert.pem',root/'key.pem')
with Server(('127.0.0.1',0),Echo) as tcp,Server(('127.0.0.1',0),TLS) as tls:
    for server in (tcp,tls):threading.Thread(target=server.serve_forever,daemon=True).start()
    subprocess.run([args.binary,str(tcp.server_address[1]),str(tls.server_address[1]),str(root/'network-state')],check=True,timeout=25)
    for server in (tcp,tls):server.shutdown()
