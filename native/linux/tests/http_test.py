#!/usr/bin/env python3
"""Exercise native libcurl multipart uploads against a local server, never an STT API."""
import email.parser
import email.policy
import http.server
import json
import os
import pathlib
import struct
import subprocess
import sys
import threading

requests = []
failures = []

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        key=self.headers.get('xi-api-key') or self.headers.get('Authorization', '').removeprefix('Bearer ')
        if key=='gsk_denied':
            self.send_response(403);self.end_headers();self.wfile.write(b'PRIVATE SERVER DETAIL');return
        if key=='sk_fixture': body=[{'model_id':'scribe_v2'}, {'model_id':'voice_other'}, {'model_id':'scribe_v1'}, {'model_id':'scribe_v2'}]
        elif key=='gsk_fixture': body={'data':[{'id':'whisper-large-v3-turbo'}, {'id':'llama-3'}, {'id':'whisper-large-v3'}]}
        else: body={'data':[{'id':'gpt-4o-transcribe'}, {'id':'gpt-4o-mini-transcribe'}, {'id':'whisper-1'}, {'id':'gpt-4o'}, {'id':'gpt-4o-mini-transcribe-2025-01-01'}, {'id':'gpt-realtime-transcribe'}, {'id':'gpt-4o-transcribe'}, {'id':42}]}
        self.send_response(200);self.end_headers();self.wfile.write(json.dumps(body).encode())

    def do_POST(self):
        try:
            data = self.rfile.read(int(self.headers['Content-Length']))
            message = email.parser.BytesParser(policy=email.policy.default).parsebytes(
                ('Content-Type: ' + self.headers['Content-Type'] + '\r\n\r\n').encode() + data)
            fields = {}
            for part in message.iter_parts():
                fields.setdefault(part.get_param('name', header='content-disposition'), []).append(part.get_payload(decode=True))
            eleven = 'model_id' in fields
            assert ('model' in fields) != eleven
            assert self.headers.get('xi-api-key') == 'sk_fixture' if eleven else self.headers.get('Authorization') in ('Bearer gsk_fixture', 'Bearer sk-fixture')
            assert fields['language_code' if eleven else 'language'] == [b'ko']
            assert fields['keyterms'] == [b'KeyScribe', '우분투'.encode()] if eleven else fields['prompt'] == ['고유명사 표기 참고: KeyScribe, 우분투'.encode()]
            wav = fields['file'][0]
            assert wav[:4] == b'RIFF' and wav[8:12] == b'WAVE'
            assert struct.unpack_from('<I', wav, 40)[0] == len(wav) - 44
            assert struct.unpack_from('<I', wav, 24)[0] == 16000
            model = fields['model_id' if eleven else 'model'][0].decode()
            if model == 'scribe_v2': assert fields['no_verbatim'] == [b'true']
            requests.append((model, len(wav) - 44))
            status = 401 if model == 'mock-http-error' else 200
            body = {'text': '  안녕 [noise] 우분투  '}
            if model == 'mock-missing':
                body = {'other': 'missing'}
            if model == 'mock-array':
                body = {'text': ['invalid']}
            if model == 'mock-http-error':
                body = {'error': 'PRIVATE SERVER DETAIL'}
            raw = b'not-json' if model == 'mock-invalid' else json.dumps(body).encode()
            self.send_response(status)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)
        except Exception as error:
            failures.append(repr(error))
            self.send_error(500)

server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
env = dict(os.environ, KEYSCRIBE_TEST_ENDPOINT=f'http://127.0.0.1:{server.server_port}/transcribe')
binary = str(pathlib.Path(sys.argv[1]).resolve())

def run(key, model, size='small', success=True):
    result = subprocess.run([binary, key, model, size], env=env, text=True, capture_output=True, timeout=30)
    assert (result.returncode == 0) == success, (result.returncode, result.stderr)
    if success:
        assert result.stdout.strip() == ('안녕 우분투 안녕 우분투' if size == 'large' else '안녕 우분투')
    assert 'PRIVATE SERVER DETAIL' not in result.stderr
    assert key not in result.stderr

try:
    for key in ('gsk_fixture', 'sk-fixture', 'sk_fixture'):
        run(key, 'mock-ok')
    run('sk_fixture','scribe_v2')
    run('gsk_fixture', 'mock-large', 'large')
    chunks = [n for model, n in requests if model == 'mock-large']
    assert len(chunks) == 2 and sum(chunks) == 25 * 1024 * 1024
    assert max(chunks) <= 9 * 60 * 32000
    for model in ('mock-http-error', 'mock-missing', 'mock-array', 'mock-invalid'):
        run('gsk_fixture', model, success=False)
    for key, expected in (
        ('sk-fixture', ['gpt-4o-mini-transcribe','gpt-4o-transcribe','whisper-1']),
        ('sk_fixture', ['scribe_v1','scribe_v2']),
        ('gsk_fixture', ['whisper-large-v3','whisper-large-v3-turbo'])):
        result=subprocess.run([binary,key,'unused','models'],env=env,text=True,capture_output=True,timeout=30)
        assert result.returncode==0, result.stderr
        assert result.stdout.splitlines()==expected, result.stdout
    denied=subprocess.run([binary,'gsk_denied','unused','models'],env=env,text=True,capture_output=True,timeout=30)
    assert denied.returncode==1 and '403' in denied.stderr and 'PRIVATE' not in denied.stderr and 'gsk_denied' not in denied.stderr
    assert not failures, failures
    print('PASS: all three providers, Korean multipart fields, WAV upload/chunks, HTTP errors, malformed responses, secret redaction, provider model lists/filtering')
finally:
    server.shutdown()
    server.server_close()
