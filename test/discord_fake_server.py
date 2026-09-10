#!/usr/bin/env python3
"""Fake Discord IPC server for the presence smoke test.

Speaks just enough of the protocol to exercise the real client in
src/tools/discord_rpc.cpp: answers the handshake with READY, records every frame
it receives as JSON lines, and can answer with an error reply (to check that the
editor surfaces Discord's rejections).

Usage: discord_fake_server.py <socket-path> <out.jsonl> [error_reply]
"""
import json
import os
import socket
import struct
import sys
import threading


def read_frame(conn):
    header = b""
    while len(header) < 8:
        chunk = conn.recv(8 - len(header))
        if not chunk:
            return None
        header += chunk
    opcode, length = struct.unpack("<II", header)
    body = b""
    while len(body) < length:
        chunk = conn.recv(length - len(body))
        if not chunk:
            return None
        body += chunk
    return opcode, body.decode("utf-8", "replace")


def send_frame(conn, opcode, payload):
    data = payload.encode("utf-8")
    conn.sendall(struct.pack("<II", opcode, len(data)) + data)


def main():
    path, out_path = sys.argv[1], sys.argv[2]
    error_reply = sys.argv[3] if len(sys.argv) > 3 else ""

    if os.path.exists(path):
        os.unlink(path)
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(path)
    server.listen(4)

    def serve(conn):
        send_frame(conn, 1, json.dumps({
            "cmd": "DISPATCH",
            "evt": "READY",
            "data": {"v": 1, "heartbeat_interval": 30000},
        }))
        while True:
            frame = read_frame(conn)
            if frame is None:
                return
            opcode, body = frame
            with open(out_path, "a", encoding="utf-8") as handle:
                handle.write(json.dumps({"opcode": opcode, "body": body}) + "\n")
            if opcode == 1 and error_reply and "SET_ACTIVITY" in body:
                send_frame(conn, 1, json.dumps({
                    "cmd": "SET_ACTIVITY",
                    "evt": "ERROR",
                    "data": {"code": 4000, "message": error_reply},
                }))

    while True:
        try:
            conn, _ = server.accept()
        except OSError:
            return
        threading.Thread(target=serve, args=(conn,), daemon=True).start()


if __name__ == "__main__":
    main()
