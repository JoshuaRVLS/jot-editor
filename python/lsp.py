import subprocess
import threading
import json
import time
import os
import pathlib
from jcode_api import Editor, config_path

class LSPClient:
    def __init__(self, command, root_uri):
        # Resolve command path if it's pylsp and installed in venv
        if command == "pylsp" or command == "python-lsp-server":
             venv_pylsp = config_path("venv", "bin", "pylsp")
             if os.path.exists(venv_pylsp):
                 command = venv_pylsp
     
        self.process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            shell=True
        )
        
        # Ensure root_uri is a proper URI
        try:

            self.root_uri = pathlib.Path(root_uri).resolve().as_uri()
        except:
            self.root_uri = f"file://{os.path.abspath(root_uri)}"

        self.request_id = 0
        self.lock = threading.RLock()
        self.open_files = {} # abs_path -> version
        
        # Initialization state
        self.is_initialized = False
        self.message_queue = []
        self.initialize_req_id = -1

        # Start reading thread
        self.reader_thread = threading.Thread(target=self._read_loop)
        self.reader_thread.daemon = True
        self.reader_thread.start()

       # Start stderr drain thread (Prevent deadlock)
        self.stderr_thread = threading.Thread(target=self._stderr_loop)
        self.stderr_thread.daemon = True
        self.stderr_thread.start()
        # Initialize
        self.log_file = open("jcode_lsp.log", "a")
        self.log("--- LSP Client Started ---")
        
        # Move request generation here to capture ID
        with self.lock:
            self.request_id += 1
            self.initialize_req_id = self.request_id
            self.send_request("initialize", {
                "processId": os.getpid(),
                "rootUri": self.root_uri,
                "capabilities": {}
            }, bypass_queue=True)

    def log(self, msg):
        try:
            self.log_file.write(f"[{time.time()}] {msg}\n")
            self.log_file.flush()
        except: pass

    def _stderr_loop(self):
        while True:
            line = self.process.stderr.readline()
            if not line: break
            self.log(f"STDERR: {line.decode('utf-8').strip()}")

    def _read_loop(self):
        content_length = None
        while True:
            try:
                line = self.process.stdout.readline()
                if not line: break
                
                parts = line.decode('utf-8').strip()
                
                if not parts:
                    # Empty line -> End of headers?
                    if content_length is not None:
                         # Read content
                         content = self.process.stdout.read(content_length).decode('utf-8')
                         self.log(f"RECV: {content}")
                         try:
                             self._handle_message(json.loads(content))
                         except Exception as e:
                             self.log(f"JSON ERROR: {e}")
                         content_length = None
                elif parts.startswith("Content-Length:"):
                    try:
                        content_length = int(parts.split(":")[1].strip())
                    except ValueError:
                        self.log(f"BAD HEADER: {parts}")
            except Exception as e:
                self.log(f"READ ERROR: {e}")

    def _handle_message(self, msg):
        # Handle initialize response
        if "id" in msg and msg.get("id") == self.initialize_req_id:
             self.log("LSP Initialized!")
             self.is_initialized = True
             self.send_notification("initialized", {}, bypass_queue=True)
             self.flush_queue()
             return

        # Handle diagnostics
        if "method" in msg and msg["method"] == "textDocument/publishDiagnostics":
            params = msg["params"]
            uri = params["uri"]
            # Convert URI back to path
            try:
                 from urllib.parse import urlparse, unquote
                 parsed = urlparse(uri)
                 filepath = unquote(parsed.path)
            except:
                 filepath = uri.replace("file://", "")
                     

            diagnostics = params["diagnostics"]
            
            flat_diagnostics = []
            for d in diagnostics:
                flat_d = {
                    "line": d["range"]["start"]["line"],
                    "col": d["range"]["start"]["character"],
                    "end_line": d["range"]["end"]["line"],
                    "end_col": d["range"]["end"]["character"],
                    "message": d["message"],
                    "severity": d.get("severity", 1)
                }
                flat_diagnostics.append(flat_d)
            
            self.log(f"DIAGNOSTICS: {len(diagnostics)} for {filepath}")
            try:
                Editor.set_diagnostics(filepath, flat_diagnostics)
            except AttributeError:
                # Fallback if API hasn't updated yet (reloading plugin issue?)
                Editor.clear_diagnostics(filepath)
                for d in diagnostics:
                    line = d["range"]["start"]["line"]
                    col = d["range"]["start"]["character"]
                    end_line = d["range"]["end"]["line"]
                    end_col = d["range"]["end"]["character"]
                    message = d["message"]
                    severity = d.get("severity", 1) 
                    Editor.add_diagnostic(filepath, line, col, end_line, end_col, message, severity)

    def flush_queue(self):
        self.log(f"Flushing {len(self.message_queue)} queued messages")
        for req in self.message_queue:
            self._send(req)
        self.message_queue = []

    def send_request(self, method, params, bypass_queue=False):
        if not self.is_initialized and not bypass_queue:
            self.log(f"Queuing request {method}")
            with self.lock:
                self.request_id += 1
                req = {
                    "jsonrpc": "2.0",
                    "id": self.request_id,
                    "method": method,
                    "params": params
                }
                self.message_queue.append(req)
            return

        with self.lock:
            if not bypass_queue: # ID already incremented if queued? No, define new ID when queuing
                 # Logic fix: if queued, we assign ID then.
                 # If not queued, assign ID now.
                 self.request_id += 1
            
            # Note: loop above for flush calls _send directly with req object
            # This method constructs req object
            
            req = {
                "jsonrpc": "2.0",
                "id": self.request_id,
                "method": method,
                "params": params
            }
            self._send(req)

    def send_notification(self, method, params, bypass_queue=False):
        if not self.is_initialized and not bypass_queue:
            self.log(f"Queuing notification {method}")
            req = {
                "jsonrpc": "2.0",
                "method": method,
                "params": params
            }
            self.message_queue.append(req)
            return

        req = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params
        }
        self._send(req)

    def _send(self, req):
        try:
            content = json.dumps(req)
            self.log(f"SEND: {content}")
            header = f"Content-Length: {len(content)}\r\n\r\n"
            self.process.stdin.write((header + content).encode('utf-8'))
            self.process.stdin.flush()
        except BrokenPipeError:
            self.log("SEND ERROR: BrokenPipe")

    def did_open(self, filepath, text):
        try:
             abs_path = str(pathlib.Path(filepath).resolve())
             uri = pathlib.Path(abs_path).as_uri()
        except:
             abs_path = os.path.abspath(filepath)
             uri = f"file://{abs_path}"

        self.open_files[abs_path] = 1
        
        self.send_notification("textDocument/didOpen", {
            "textDocument": {
                "uri": uri,
                "languageId": "python", # TODO: detect
                "version": 1,
                "text": text
            }
        })

    def did_change(self, filepath, text):
        try:
             abs_path = str(pathlib.Path(filepath).resolve())
             uri = pathlib.Path(abs_path).as_uri()
        except:
             abs_path = os.path.abspath(filepath)
             uri = f"file://{abs_path}"

        if abs_path not in self.open_files: return
        self.open_files[abs_path] += 1
        
        self.send_notification("textDocument/didChange", {
            "textDocument": {
                "uri": uri,
                "version": self.open_files[abs_path]
            },
            "contentChanges": [{"text": text}]
        })
