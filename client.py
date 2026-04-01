#!/usr/bin/env python3
"""
client.py — Python Task Submission Client

Connects to the Master node via TCP and submits shell commands.
Implements the same length-prefix wire protocol used by the C++ components.

Usage:
    python3 client.py [master_ip] [port]
    python3 client.py                       # defaults to 127.0.0.1:8080
"""

import socket
import struct
import sys

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8080


class TaskClient:
    """TCP client for the Distributed Task Scheduler.
    
    Implements the length-prefix wire protocol:
        [4 bytes: message length (big-endian)] + [N bytes: payload]
    """

    def __init__(self, host: str, port: int):
        self.host = host
        self.port = port
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    def connect(self):
        """Establish TCP connection to the Master."""
        try:
            self.sock.connect((self.host, self.port))
            print(f"[CLIENT] Connected to Master at {self.host}:{self.port}")
        except ConnectionRefusedError:
            print(f"[CLIENT] ERROR: Cannot connect to {self.host}:{self.port}")
            print("[CLIENT] Is the Master running?")
            sys.exit(1)

    def send_message(self, msg: str):
        """Send a length-prefixed message (matches C++ protocol)."""
        data = msg.encode("utf-8")
        header = struct.pack("!I", len(data))   # 4-byte big-endian length
        self.sock.sendall(header + data)

    def recv_message(self) -> str:
        """Receive a length-prefixed message (matches C++ protocol)."""
        header = self._recv_exact(4)
        if not header:
            return ""
        length = struct.unpack("!I", header)[0]
        if length == 0:
            return ""
        data = self._recv_exact(length)
        return data.decode("utf-8") if data else ""

    def _recv_exact(self, n: int) -> bytes:
        """Read exactly n bytes from the socket."""
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                return b""
            buf += chunk
        return buf

    def close(self):
        """Close the connection."""
        self.sock.close()


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_HOST
    port = int(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_PORT

    client = TaskClient(host, port)
    client.connect()

    print("=" * 47)
    print("  DISTRIBUTED TASK SCHEDULER — PYTHON CLIENT")
    print("=" * 47)
    print("[CLIENT] Type commands to submit (or 'quit' to exit):\n")

    try:
        while True:
            try:
                command = input(">>> ").strip()
            except EOFError:
                break

            if not command:
                continue
            if command.lower() == "quit":
                print("[CLIENT] Disconnecting...")
                break

            # Send the command
            client.send_message(command)
            print(f"[CLIENT] Sent: {command}")

            # Receive the result
            result = client.recv_message()
            if not result:
                print("[CLIENT] Master disconnected.")
                break

            print(f"\n{'─' * 47}")
            print(f"  Result:")
            print(f"{'─' * 47}")
            print(result)
            print(f"{'─' * 47}\n")

    except KeyboardInterrupt:
        print("\n[CLIENT] Interrupted.")

    client.close()
    print("[CLIENT] Disconnected.")


if __name__ == "__main__":
    main()
