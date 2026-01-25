import subprocess
import threading
import json
import time
import os
from jcode_api import Editor

class LSPClient:
    def __init__(self, command, root_uri):
        self.process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            shell=True
        )
        self.root_uri = root_uri
        self.request_id = 0
        self.lock = threading.Lock()
        self.open_files = {} # path -> version

        # Start reading thread
        self.reader_thread = threading.Thread(target=self._read_loop)
        self.reader_thread.daemon = True
        self.reader_thread.start()
        
        # Initialize
        self.send_request("initialize", {
            "processId": os.getpid(),
            "rootUri": root_uri,
            "capabilities": {}
        })

    def _read_loop(self):
        while True:
            line = self.process.stdout.readline()
            if not line: break
            
            # Parse Content-Length
            try:
                header = line.decode('utf-8').strip()
                if header.startswith("Content-Length:"):
                    length = int(header.split(":")[1])
                    self.process.stdout.readline() # Empty line
                    content = self.process.stdout.read(length).decode('utf-8')
                    self._handle_message(json.loads(content))
            except Exception as e:
                pass

    def _handle_message(self, msg):
        # Handle diagnostics
        if "method" in msg and msg["method"] == "textDocument/publishDiagnostics":
            params = msg["params"]
            uri = params["uri"]
            filepath = uri.replace("file://", "")
            diagnostics = params["diagnostics"]
            
            Editor.clear_diagnostics(filepath)
            for d in diagnostics:
                line = d["range"]["start"]["line"]
                col = d["range"]["start"]["character"]
                message = d["message"]
                severity = d.get("severity", 1) # 1=Error
                Editor.add_diagnostic(filepath, line, col, message, severity)

    def send_request(self, method, params):
        with self.lock:
            self.request_id += 1
            req = {
                "jsonrpc": "2.0",
                "id": self.request_id,
                "method": method,
                "params": params
            }
            self._send(req)

    def send_notification(self, method, params):
        req = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params
        }
        self._send(req)

    def _send(self, req):
        content = json.dumps(req)
        header = f"Content-Length: {len(content)}\r\n\r\n"
        self.process.stdin.write((header + content).encode('utf-8'))
        self.process.stdin.flush()

    def did_open(self, filepath, text):
        self.open_files[filepath] = 1
        uri = f"file://{filepath}"
        self.send_notification("textDocument/didOpen", {
            "textDocument": {
                "uri": uri,
                "languageId": "python", # TODO: detect
                "version": 1,
                "text": text
            }
        })

    def did_change(self, filepath, text):
        if filepath not in self.open_files: return
        self.open_files[filepath] += 1
        uri = f"file://{filepath}"
        self.send_notification("textDocument/didChange", {
            "textDocument": {
                "uri": uri,
                "version": self.open_files[filepath]
            },
            "contentChanges": [{"text": text}]
        })
