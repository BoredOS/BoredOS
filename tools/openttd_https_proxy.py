#!/usr/bin/env python3
import http.client
import lzma
import os
import socket
import ssl
import sys
import threading
import time
import urllib.parse


HOST = "0.0.0.0"
PORT = 8173
CONTENT_PROXY_PORT = 8174
CONTENT_HOST = "content.openttd.org"
CONTENT_PORT = 3978
PIDFILE = "/tmp/boredos-openttd-https-proxy.pid"
MAX_BODY = 128 * 1024 * 1024


def process_is_this_proxy(pid):
    try:
        with open(f"/proc/{pid}/cmdline", "rb") as f:
            parts = [part.decode("utf-8", "replace") for part in f.read().split(b"\0") if part]
    except OSError:
        return False
    if not parts or "python" not in os.path.basename(parts[0]):
        return False
    return any(part.endswith("/openttd_https_proxy.py") or part == "tools/openttd_https_proxy.py" or part == "openttd_https_proxy.py" for part in parts[1:])


def claim_single_instance():
    current_pid = os.getpid()
    try:
        with open(PIDFILE, "r", encoding="ascii") as f:
            old_pid = int(f.read().strip() or "0")
    except (OSError, ValueError):
        old_pid = 0

    stale_pids = []
    if old_pid > 0 and old_pid != current_pid and process_is_this_proxy(old_pid):
        stale_pids.append(old_pid)

    try:
        for name in os.listdir("/proc"):
            if not name.isdigit():
                continue
            pid = int(name)
            if pid == current_pid or pid in stale_pids:
                continue
            if process_is_this_proxy(pid):
                stale_pids.append(pid)
    except OSError:
        pass

    for old_pid in stale_pids:
        try:
            os.kill(old_pid, 15)
            for _ in range(20):
                time.sleep(0.05)
                try:
                    os.kill(old_pid, 0)
                except OSError:
                    break
            else:
                os.kill(old_pid, 9)
        except OSError:
            pass

    with open(PIDFILE, "w", encoding="ascii") as f:
        f.write(f"{os.getpid()}\n")


def release_single_instance():
    try:
        with open(PIDFILE, "r", encoding="ascii") as f:
            pid = int(f.read().strip() or "0")
        if pid == os.getpid():
            os.unlink(PIDFILE)
    except (OSError, ValueError):
        pass


def read_line(conn):
    data = bytearray()
    while True:
        ch = conn.recv(1)
        if not ch:
            raise EOFError("connection closed")
        if ch == b"\n":
            return data.decode("utf-8", "replace").rstrip("\r")
        data.extend(ch)
        if len(data) > 8192:
            raise ValueError("line too long")


def read_exact(conn, size):
    out = bytearray()
    while len(out) < size:
        chunk = conn.recv(size - len(out))
        if not chunk:
            raise EOFError("connection closed")
        out.extend(chunk)
    return bytes(out)


def send_error(conn, message):
    payload = message.encode("utf-8", "replace")
    conn.sendall(f"ERR {len(payload)}\n".encode("ascii") + payload)


def send_response(conn, data):
    conn.sendall(f"OK {len(data)}\n".encode("ascii"))
    sent = 0
    while sent < len(data):
        end = min(sent + 16384, len(data))
        conn.sendall(data[sent:end])
        sent = end


def fetch(method, uri, body):
    parsed = urllib.parse.urlsplit(uri)
    if parsed.scheme != "https" or not parsed.hostname:
        raise ValueError("only https URLs are supported")

    path = urllib.parse.urlunsplit(("", "", parsed.path or "/", parsed.query, ""))
    context = ssl.create_default_context()
    conn = http.client.HTTPSConnection(parsed.hostname, parsed.port or 443, context=context, timeout=30)
    headers = {
        "User-Agent": "OpenTTD-BoredOS/1.0",
        "Accept": "*/*",
        "Connection": "close",
    }
    conn.request(method, path, body=body if body else None, headers=headers)
    response = conn.getresponse()
    data = response.read(MAX_BODY + 1)
    conn.close()

    if response.status < 200 or response.status >= 300:
        raise RuntimeError(f"upstream status {response.status}")
    if len(data) > MAX_BODY:
        raise RuntimeError("response too large")
    return data


def decompress_savegame(body):
    if len(body) < 8:
        raise ValueError("savegame too small")
    if body[:4] != b"OTTX":
        raise ValueError("not an OTTX savegame")

    print(f"[openttd-https-proxy] lzma decompress {len(body)} bytes", flush=True)
    payload = lzma.decompress(body[8:])
    print(f"[openttd-https-proxy] lzma decompressed to {len(payload) + 8} bytes", flush=True)
    return b"OTTN" + body[4:8] + payload


def handle_client(conn, addr):
    try:
        conn.settimeout(60)
        magic = read_line(conn)
        if magic != "OTHP1":
            raise ValueError("bad magic")

        method = read_line(conn)
        uri = read_line(conn)
        body_len = int(read_line(conn))
        if method not in ("GET", "POST", "LZMA_DECOMPRESS"):
            raise ValueError("bad method")
        if body_len < 0 or body_len > MAX_BODY:
            raise ValueError("body too large")

        body = read_exact(conn, body_len) if body_len else b""
        if method == "LZMA_DECOMPRESS":
            data = decompress_savegame(body)
        else:
            data = fetch(method, uri, body)
        print(f"[openttd-https-proxy] sending {len(data)} bytes to {addr}", flush=True)
        send_response(conn, data)
        print(f"[openttd-https-proxy] sent {len(data)} bytes to {addr}", flush=True)
    except Exception as exc:
        try:
            send_error(conn, str(exc))
        except Exception:
            pass
        print(f"[openttd-https-proxy] {addr}: {exc}", file=sys.stderr, flush=True)
    finally:
        conn.close()


def main():
    claim_single_instance()
    try:
        threading.Thread(target=run_content_proxy, daemon=True).start()
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind((HOST, PORT))
            server.listen(16)
            print(f"[openttd-https-proxy] listening on {HOST}:{PORT}", flush=True)
            while True:
                conn, addr = server.accept()
                threading.Thread(target=handle_client, args=(conn, addr), daemon=True).start()
    finally:
        release_single_instance()


def pipe_socket(src, dst):
    try:
        while True:
            data = src.recv(16384)
            if not data:
                break
            dst.sendall(data)
    except Exception:
        pass
    finally:
        try:
            dst.shutdown(socket.SHUT_WR)
        except Exception:
            pass


def handle_content_client(client, addr):
    try:
        upstream = socket.create_connection((CONTENT_HOST, CONTENT_PORT), timeout=30)
        upstream.settimeout(None)
        client.settimeout(None)
        t1 = threading.Thread(target=pipe_socket, args=(client, upstream), daemon=True)
        t2 = threading.Thread(target=pipe_socket, args=(upstream, client), daemon=True)
        t1.start()
        t2.start()
        t1.join()
        t2.join()
    except Exception as exc:
        print(f"[openttd-content-proxy] {addr}: {exc}", file=sys.stderr, flush=True)
    finally:
        try:
            client.close()
        except Exception:
            pass


def run_content_proxy():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((HOST, CONTENT_PROXY_PORT))
        server.listen(16)
        print(
            f"[openttd-content-proxy] listening on {HOST}:{CONTENT_PROXY_PORT} -> {CONTENT_HOST}:{CONTENT_PORT}",
            flush=True,
        )
        while True:
            conn, addr = server.accept()
            threading.Thread(target=handle_content_client, args=(conn, addr), daemon=True).start()


if __name__ == "__main__":
    main()
