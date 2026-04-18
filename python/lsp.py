import json
import os
import pathlib
import shlex
import subprocess
import threading
import time
from urllib.parse import unquote, urlparse

from jot_api import Editor, config_path


def _to_uri(path):
    return pathlib.Path(path).resolve().as_uri()


def _from_uri(uri):
    parsed = urlparse(uri)
    if parsed.scheme == "file":
        return os.path.abspath(unquote(parsed.path))
    return os.path.abspath(uri.replace("file://", ""))


def _detect_language_id(filepath):
    lower = filepath.lower()
    if lower.endswith(".py"):
        return "python"
    if lower.endswith(".ts"):
        return "typescript"
    if lower.endswith(".tsx"):
        return "typescriptreact"
    if lower.endswith(".js"):
        return "javascript"
    if lower.endswith(".jsx"):
        return "javascriptreact"
    if lower.endswith(".json"):
        return "json"
    if lower.endswith(".css"):
        return "css"
    if lower.endswith(".html"):
        return "html"
    if lower.endswith(".md"):
        return "markdown"
    if lower.endswith(".c"):
        return "c"
    if lower.endswith(".cpp") or lower.endswith(".cc") or lower.endswith(".cxx"):
        return "cpp"
    if lower.endswith(".h") or lower.endswith(".hpp") or lower.endswith(".hh"):
        return "cpp"
    if lower.endswith(".rs"):
        return "rust"
    if lower.endswith(".go"):
        return "go"
    return "plaintext"


class LSPClient:
    def __init__(self, command, root_path, server_name, workspace_name=None):
        self.server_name = server_name
        self.root_path = os.path.abspath(root_path)
        self.root_uri = _to_uri(self.root_path)
        self.workspace_name = workspace_name or os.path.basename(self.root_path) or self.root_path
        self.command = self._resolve_command(command)
        self.process = subprocess.Popen(
            self.command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            shell=False,
            cwd=self.root_path,
        )

        self.request_id = 0
        self.lock = threading.RLock()
        self.open_files = {}
        self.server_capabilities = {}
        self.is_initialized = False
        self.message_queue = []
        self.initialize_req_id = -1
        self.alive = True

        log_dir = config_path("logs")
        os.makedirs(log_dir, exist_ok=True)
        safe_server = "".join(ch if ch.isalnum() else "_" for ch in server_name)
        self.log_file = open(
            os.path.join(log_dir, f"lsp_{safe_server}.log"),
            "a",
            encoding="utf-8",
        )
        self.log(f"--- LSP Client Started ({server_name}) root={self.root_path} ---")

        self.reader_thread = threading.Thread(target=self._read_loop, daemon=True)
        self.reader_thread.start()
        self.stderr_thread = threading.Thread(target=self._stderr_loop, daemon=True)
        self.stderr_thread.start()

        with self.lock:
            self.request_id += 1
            self.initialize_req_id = self.request_id
            self.send_request(
                "initialize",
                {
                    "processId": os.getpid(),
                    "clientInfo": {"name": "jot", "version": "0.1"},
                    "rootUri": self.root_uri,
                    "rootPath": self.root_path,
                    "workspaceFolders": [
                        {"uri": self.root_uri, "name": self.workspace_name}
                    ],
                    "capabilities": {
                        "workspace": {
                            "workspaceFolders": True,
                            "didChangeConfiguration": {"dynamicRegistration": False},
                        },
                        "textDocument": {
                            "publishDiagnostics": {
                                "relatedInformation": True,
                                "versionSupport": False,
                            },
                            "synchronization": {
                                "didSave": True,
                                "willSave": False,
                                "didClose": True,
                            },
                            "completion": {
                                "completionItem": {
                                    "snippetSupport": False,
                                    "documentationFormat": ["markdown", "plaintext"],
                                }
                            },
                            "hover": {
                                "contentFormat": ["markdown", "plaintext"]
                            },
                        },
                    },
                    "trace": "off",
                },
                bypass_queue=True,
            )

    def _resolve_command(self, command):
        if command in ("pylsp", "python-lsp-server"):
            venv_pylsp = config_path("venv", "bin", "pylsp")
            if os.path.exists(venv_pylsp):
                return [venv_pylsp]
        if isinstance(command, (list, tuple)):
            return list(command)
        return shlex.split(command)

    def log(self, msg):
        try:
            self.log_file.write(f"[{time.time():.3f}] {msg}\n")
            self.log_file.flush()
        except Exception:
            pass

    def stop(self):
        if not self.alive:
            return
        self.alive = False
        try:
            self.send_notification("shutdown", {}, bypass_queue=True)
        except Exception:
            pass
        try:
            self.send_notification("exit", {}, bypass_queue=True)
        except Exception:
            pass
        try:
            if self.process.poll() is None:
                self.process.terminate()
                self.process.wait(timeout=1.0)
        except Exception:
            try:
                self.process.kill()
            except Exception:
                pass
        try:
            self.log("--- LSP Client Stopped ---")
            self.log_file.close()
        except Exception:
            pass

    def is_running(self):
        return self.alive and self.process.poll() is None

    def _stderr_loop(self):
        while self.alive:
            line = self.process.stderr.readline()
            if not line:
                break
            self.log(f"STDERR: {line.decode('utf-8', errors='replace').rstrip()}")

    def _read_loop(self):
        content_length = None
        while self.alive:
            try:
                line = self.process.stdout.readline()
                if not line:
                    break

                parts = line.decode("utf-8", errors="replace").strip()
                if not parts:
                    if content_length is not None:
                        content = self.process.stdout.read(content_length).decode(
                            "utf-8", errors="replace"
                        )
                        self.log(f"RECV: {content}")
                        try:
                            self._handle_message(json.loads(content))
                        except Exception as exc:
                            self.log(f"JSON ERROR: {exc}")
                        content_length = None
                elif parts.lower().startswith("content-length:"):
                    try:
                        content_length = int(parts.split(":", 1)[1].strip())
                    except ValueError:
                        self.log(f"BAD HEADER: {parts}")
            except Exception as exc:
                self.log(f"READ ERROR: {exc}")
                break
        self.alive = False

    def _handle_message(self, msg):
        if msg.get("id") == self.initialize_req_id:
            self.server_capabilities = msg.get("result", {}).get("capabilities", {})
            self.log(f"LSP Initialized with capabilities: {self.server_capabilities}")
            self.is_initialized = True
            self.send_notification("initialized", {}, bypass_queue=True)
            self.flush_queue()
            return

        if msg.get("method") == "textDocument/publishDiagnostics":
            params = msg.get("params", {})
            filepath = _from_uri(params.get("uri", ""))
            diagnostics = params.get("diagnostics", [])

            flat_diagnostics = []
            for diagnostic in diagnostics:
                start = diagnostic.get("range", {}).get("start", {})
                end = diagnostic.get("range", {}).get("end", {})
                flat_diagnostics.append(
                    {
                        "line": start.get("line", 0),
                        "col": start.get("character", 0),
                        "end_line": end.get("line", start.get("line", 0)),
                        "end_col": end.get("character", start.get("character", 0)),
                        "message": diagnostic.get("message", ""),
                        "severity": diagnostic.get("severity", 1),
                    }
                )

            self.log(f"DIAGNOSTICS: {len(flat_diagnostics)} for {filepath}")
            Editor.set_diagnostics(filepath, flat_diagnostics)
            return

    def flush_queue(self):
        queued = list(self.message_queue)
        self.message_queue.clear()
        self.log(f"Flushing {len(queued)} queued messages")
        for req in queued:
            self._send(req)

    def send_request(self, method, params, bypass_queue=False):
        if not self.is_initialized and not bypass_queue:
            with self.lock:
                self.request_id += 1
                req = {
                    "jsonrpc": "2.0",
                    "id": self.request_id,
                    "method": method,
                    "params": params,
                }
                self.message_queue.append(req)
            return self.request_id

        with self.lock:
            if not bypass_queue:
                self.request_id += 1
            req = {
                "jsonrpc": "2.0",
                "id": self.request_id,
                "method": method,
                "params": params,
            }
            self._send(req)
            return self.request_id

    def send_notification(self, method, params, bypass_queue=False):
        if not self.is_initialized and not bypass_queue:
            req = {"jsonrpc": "2.0", "method": method, "params": params}
            self.message_queue.append(req)
            return
        self._send({"jsonrpc": "2.0", "method": method, "params": params})

    def _send(self, req):
        if not self.is_running():
            self.log(f"SEND SKIPPED (dead): {req}")
            return
        try:
            content = json.dumps(req)
            header = f"Content-Length: {len(content)}\r\n\r\n"
            self.log(f"SEND: {content}")
            self.process.stdin.write((header + content).encode("utf-8"))
            self.process.stdin.flush()
        except Exception as exc:
            self.log(f"SEND ERROR: {exc}")
            self.alive = False

    def did_open(self, filepath, text):
        abs_path = os.path.abspath(filepath)
        version = self.open_files.get(abs_path, 0) + 1
        self.open_files[abs_path] = version
        self.send_notification(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": _to_uri(abs_path),
                    "languageId": _detect_language_id(abs_path),
                    "version": version,
                    "text": text,
                }
            },
        )

    def did_change(self, filepath, text):
        abs_path = os.path.abspath(filepath)
        if abs_path not in self.open_files:
            self.did_open(abs_path, text)
            return

        self.open_files[abs_path] += 1
        self.send_notification(
            "textDocument/didChange",
            {
                "textDocument": {
                    "uri": _to_uri(abs_path),
                    "version": self.open_files[abs_path],
                },
                "contentChanges": [{"text": text}],
            },
        )

    def did_save(self, filepath, text=None):
        abs_path = os.path.abspath(filepath)
        params = {"textDocument": {"uri": _to_uri(abs_path)}}
        if text is not None:
            params["text"] = text
        self.send_notification("textDocument/didSave", params)

    def did_close(self, filepath):
        abs_path = os.path.abspath(filepath)
        if abs_path in self.open_files:
            self.send_notification(
                "textDocument/didClose",
                {"textDocument": {"uri": _to_uri(abs_path)}},
            )
            self.open_files.pop(abs_path, None)
