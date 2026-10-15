#!/usr/bin/env python3
"""Deterministic local HTTP fixture; optional executable receives its base URL."""
import argparse,contextlib,http.server,json,socket,subprocess,threading,time

DATA=bytes((i*31+7)&255 for i in range(1024*1024))
class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version='HTTP/1.1'
    def log_message(self,*args): pass
    def do_GET(self):
        try:
            if self.path=='/ambiguous':
                self.wfile.write(b'HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n');self.wfile.flush();self.close_connection=True;return
            if self.path=='/conflicting-length':
                self.wfile.write(b'HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nab');self.wfile.flush();self.close_connection=True;return
            if self.path=='/redirect':
                self.send_response(302);self.send_header('Location','/large');self.send_header('Content-Length','0');self.end_headers();return
            chunked=self.path=='/chunked'
            payload=DATA[:262144] if self.path=='/baseline' else DATA
            self.send_response(200)
            self.send_header('Content-Type','application/octet-stream')
            self.send_header('Set-Cookie','a=1');self.send_header('Set-Cookie','b=2')
            self.send_header('Transfer-Encoding','chunked') if chunked else self.send_header('Content-Length',str(len(payload)))
            self.end_headers();self.wfile.flush();time.sleep(.08)
            for at in range(0,len(payload),4096):
                block=payload[at:at+4096]
                if chunked:self.wfile.write(f'{len(block):x}\r\n'.encode())
                self.wfile.write(block)
                if chunked:self.wfile.write(b'\r\n')
                self.wfile.flush()
            if chunked:self.wfile.write(b'0\r\n\r\n');self.wfile.flush()
        except (BrokenPipeError,ConnectionResetError): pass
    def handle_expect_100(self):
        if self.path=='/reject':
            self.send_response(413);self.send_header('Content-Length','0');self.send_header('Connection','close');self.end_headers();self.close_connection=True;return False
        return super().handle_expect_100()
    def do_POST(self):
        if self.path=='/reject':
            self.send_response(413);self.send_header('Content-Length','0');self.send_header('Connection','close');self.end_headers();self.close_connection=True;return
        count=int(self.headers.get('Content-Length',0));body=self.rfile.read(count)
        valid=all(c==((i*31+7)&255) for i,c in enumerate(body))
        response=str(len(body)).encode()
        self.send_response(200 if valid else 400);self.send_header('Content-Length',str(len(response)));self.end_headers();self.wfile.write(response)
class Server(http.server.ThreadingHTTPServer):
    daemon_threads=True
    def handle_error(self,*args): pass

def main():
    p=argparse.ArgumentParser();p.add_argument('--port',type=int,default=0);p.add_argument('--bind',default='127.0.0.1');p.add_argument('command',nargs=argparse.REMAINDER);a=p.parse_args()
    with Server((a.bind,a.port),Handler) as server:
        base=f'http://{a.bind}:{server.server_port}'
        print(base,flush=True)
        if a.command:
            worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
            try:return subprocess.call(a.command+[base])
            finally:server.shutdown();worker.join()
        server.serve_forever()
if __name__=='__main__':raise SystemExit(main())
