#!/usr/bin/env python3
"""
serial_console.py - Interactive serial terminal for Astra Base STM32.
Incoming data prints above; your typed command stays at the bottom.

Usage:
    python tools/serial_console.py
    python tools/serial_console.py --port COM6 --baud 115200

Quit: Ctrl+C
"""

import argparse
import serial
import sys
import threading

PORT = "COM6"
BAUD = 115200

_stdout_lock = threading.Lock()


def reader(ser, stop_event):
    """Background thread - prints incoming lines."""
    while not stop_event.is_set():
        try:
            line = ser.readline().decode("utf-8", errors="replace").strip()
            if line:
                with _stdout_lock:
                    sys.stdout.write(f"\r\033[K{line}\n> ")
                    sys.stdout.flush()
        except Exception:
            break


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=PORT)
    parser.add_argument("--baud", type=int, default=BAUD)
    args = parser.parse_args()

    print(f"Connecting to {args.port} @ {args.baud} baud  (Ctrl+C to quit)")
    print("-" * 50)
    print("Quick commands:")
    print('  diag     ->  {"T":200}             system health + stack watermarks')
    print('  i2c      ->  {"T":127}             I2C spot-check (MPU-6050 + INA219)')
    print('  echo     ->  {"T":143}             connectivity check')
    print('  imu      ->  {"T":126}             IMU snapshot (T:1002 response)')
    print('  cal      ->  {"T":160}             IMU calibration (keep robot still)')
    print('  start    ->  {"T":131,"cmd":1,"interval":100}   enable 10 Hz telemetry')
    print('  fast     ->  {"T":131,"cmd":1,"interval":20}    enable 50 Hz telemetry')
    print('  stop     ->  {"T":130,"cmd":0}     disable telemetry')
    print('  piddbg   ->  {"T":132,"cmd":1}     enable PID debug fields in T:1001')
    print('  pidoff   ->  {"T":132,"cmd":0}     disable PID debug fields')
    print('  fwd      ->  {"T":1,"L":0.3,"R":0.3}')
    print('  rev      ->  {"T":1,"L":-0.3,"R":-0.3}')
    print('  spin     ->  {"T":1,"L":0.3,"R":-0.3}')
    print('  x        ->  {"T":1,"L":0,"R":0}  stop motors')
    print("-" * 50)

    shortcuts = {
        "diag":   '{"T":200}',
        "i2c":    '{"T":127}',
        "echo":   '{"T":143}',
        "imu":    '{"T":126}',
        "cal":    '{"T":160}',
        "start":  '{"T":131,"cmd":1,"interval":100}',
        "fast":   '{"T":131,"cmd":1,"interval":20}',
        "stop":   '{"T":130,"cmd":0}',
        "piddbg": '{"T":132,"cmd":1}',
        "pidoff": '{"T":132,"cmd":0}',
        "fwd":    '{"T":1,"L":0.3,"R":0.3}',
        "rev":    '{"T":1,"L":-0.3,"R":-0.3}',
        "spin":   '{"T":1,"L":0.3,"R":-0.3}',
        "x":      '{"T":1,"L":0,"R":0}',
    }

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
    except serial.SerialException as e:
        print(f"Error opening {args.port}: {e}")
        sys.exit(1)

    stop_event = threading.Event()
    thread = threading.Thread(target=reader, args=(ser, stop_event), daemon=True)
    thread.start()

    try:
        while True:
            with _stdout_lock:
                sys.stdout.write("> ")
                sys.stdout.flush()
            cmd = input().strip()
            if not cmd:
                continue
            if cmd in shortcuts:
                cmd = shortcuts[cmd]
                with _stdout_lock:
                    print(f"  -> {cmd}")
            ser.write((cmd + "\n").encode())
    except KeyboardInterrupt:
        print("\nQuitting.")
    finally:
        stop_event.set()
        ser.close()


if __name__ == "__main__":
    main()
