"""Subscribe to the battery, BME280 and Wi-Fi topics without external dependencies.

Optional MQTT_USERNAME / MQTT_PASSWORD environment variables supply credentials.
"""
import argparse
import os
from pathlib import Path
import socket
import struct
import time
import uuid
from firmware import read_env, TOPIC_DEFAULTS


def mqtt_string(value):
    data = value.encode("utf-8")
    return struct.pack("!H", len(data)) + data


def send_packet(sock, header, body):
    length = len(body)
    encoded = bytearray([header])
    while True:
        digit = length % 128
        length //= 128
        encoded.append(digit | (128 if length else 0))
        if not length:
            break
    sock.sendall(encoded + body)


def read_exact(sock, size):
    result = bytearray()
    while len(result) < size:
        chunk = sock.recv(size - len(result))
        if not chunk:
            raise ConnectionError("Broker closed the connection")
        result.extend(chunk)
    return bytes(result)


def receive_packet(sock):
    header = read_exact(sock, 1)[0]
    length = 0
    for i in range(4):
        digit = read_exact(sock, 1)[0]
        length += (digit & 127) << (7 * i)
        if not digit & 128:
            return header, read_exact(sock, length)
    raise ValueError("Invalid MQTT packet length")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.0.107")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--seconds", type=int, default=35)
    parser.add_argument("--env-file", type=Path, default=Path(__file__).resolve().parent.parent / ".env")
    parser.add_argument("--topic", help="Subscribe to one explicit topic/filter instead of the .env topics")
    parser.add_argument("--check", action="store_true", help="Only check broker login")
    args = parser.parse_args()
    values = read_env(args.env_file, require_wifi=False) if args.env_file.is_file() else TOPIC_DEFAULTS
    username = os.environ.get("MQTT_USERNAME", values.get("MQTT_USERNAME", ""))
    password = os.environ.get("MQTT_PASSWORD", values.get("MQTT_PASSWORD", ""))
    flags = 2 | (0xC0 if username else 0)
    payload = mqtt_string("battery-check-" + uuid.uuid4().hex[:12])
    if username:
        payload += mqtt_string(username) + mqtt_string(password)
    with socket.create_connection((args.host, args.port), timeout=5) as sock:
        send_packet(sock, 0x10, mqtt_string("MQTT") + bytes([4, flags]) + struct.pack("!H", 60) + payload)
        header, body = receive_packet(sock)
        if header != 0x20 or len(body) != 2 or body[1] != 0:
            raise RuntimeError(f"MQTT login rejected: {body.hex()}")
        print("MQTT connection accepted", flush=True)
        if args.check:
            send_packet(sock, 0xE0, b"")
            return
        topics = [args.topic] if args.topic else [values[key] for key in TOPIC_DEFAULTS]
        send_packet(sock, 0x82, b"\x00\x01" + b"".join(mqtt_string(topic) + b"\x00" for topic in topics))
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            sock.settimeout(max(0.01, deadline - time.monotonic()))
            try:
                header, body = receive_packet(sock)
            except socket.timeout:
                break
            if header >> 4 == 9:
                if body != b"\x00\x01" + b"\x00" * len(topics):
                    raise RuntimeError(f"MQTT subscription rejected: {body.hex()}")
                print("Subscribed to configured topics", flush=True)
            elif header >> 4 == 3:
                length = struct.unpack("!H", body[:2])[0]
                topic = body[2:2 + length].decode()
                value = body[2 + length:].decode()
                retained = " retained" if header & 1 else " live"
                print(f"{time.strftime('%H:%M:%S')} [{retained.strip()}] {topic} = {value}", flush=True)
        send_packet(sock, 0xE0, b"")


if __name__ == "__main__":
    main()
