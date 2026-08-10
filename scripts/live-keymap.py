#!/usr/bin/env python3
"""Diff the keymap the keyboard is actually running against the compiled one.

Bindings saved from ZMK Studio outrank the firmware: `keymap_handle_set()` in
zmk/app/src/keymap.c reapplies them over `zmk_keymap` on every boot, and a
reflash does not dislodge them.

Talks to the left/central half over the Studio RPC UART from the
`studio-rpc-usb-uart` snippet. Reporting is read-only; --clear also sends
core.reset_settings to drop the saved bindings, which leaves Bluetooth pairings
alone, then re-reads the keymap to confirm.

Positions are compared by parameters, not by behavior, so two parameterless
behaviors look alike - fine for catching pinned bindings, which always carry
parameters.

Usage:  scripts/live-keymap.py [--clear] [/dev/cu.usbmodemXXXX]
"""
import glob
import os
import re
import sys
import time

SOF, ESC, EOF = 0xAB, 0xAC, 0xAD
DTS = "build/left/zephyr/zephyr.dts"
LAYER_NODES = ("base_layer", "symbols_layer", "functional_layer", "magic_layer",
               "settings_layer")


# --- Studio RPC framing (mirrors zmk/app/src/studio/msg_framing.c) ---


def frame(payload):
    out = bytearray([SOF])
    for b in payload:
        if b in (SOF, ESC, EOF):
            out.append(ESC)
        out.append(b)
    out.append(EOF)
    return bytes(out)


def unframe(data):
    buf, state = bytearray(), "idle"
    for b in data:
        if state == "idle":
            if b == SOF:
                buf, state = bytearray(), "data"
        elif state == "data":
            if b == ESC:
                state = "esc"
            elif b == EOF:
                yield bytes(buf)
                state = "idle"
            elif b == SOF:
                buf = bytearray()
            else:
                buf.append(b)
        else:
            buf.append(b)
            state = "data"


# --- minimal protobuf reader (no dependencies) ---


def _varint(data, i):
    val = shift = 0
    while True:
        b = data[i]
        i += 1
        val |= (b & 0x7F) << shift
        if not b & 0x80:
            return val, i
        shift += 7


def fields(msg):
    i = 0
    while i < len(msg):
        tag, i = _varint(msg, i)
        fn, wt = tag >> 3, tag & 7
        if wt == 0:
            v, i = _varint(msg, i)
            yield fn, v
        elif wt == 2:
            ln, i = _varint(msg, i)
            yield fn, msg[i:i + ln]
            i += ln
        elif wt == 5:
            yield fn, msg[i:i + 4]
            i += 4
        elif wt == 1:
            yield fn, msg[i:i + 8]
            i += 8
        else:
            raise ValueError(f"unsupported wire type {wt}")


def field(msg, num):
    for fn, v in fields(msg):
        if fn == num:
            return v
    return None


def zigzag(n):
    return (n >> 1) ^ -(n & 1)


# --- the two keymaps ---


def compiled_keymap():
    """Parse the generated devicetree, which is what the .uf2 actually holds."""
    if not os.path.exists(DTS):
        sys.exit(f"{DTS} not found - run 'make build-left' first.")
    dts = open(DTS).read()
    layers = {}
    for node in LAYER_NODES:
        m = re.search(node + r" \{.*?bindings = < (.*?) >;", dts, re.S)
        if not m:
            sys.exit(f"could not find {node} bindings in {DTS}")
        binds, cur = [], []
        for tok in m.group(1).split():
            if tok.startswith("&"):
                if cur:
                    binds.append(cur)
                cur = [tok]
            else:
                cur.append(tok)
        binds.append(cur)
        out = []
        for b in binds:
            params = [int(x, 0) for x in b[1:]]
            out.append((b[0], params[0] if params else 0,
                        params[1] if len(params) > 1 else 0))
        layers[node] = out
    return layers


def _request(port, payload, subsystem_field, response_field, timeout=6):
    """Send one framed RPC request, return the named response submessage."""
    os.system(f"stty -f {port} raw -echo 115200 >/dev/null 2>&1")
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
    try:
        os.write(fd, frame(payload))
        os.set_blocking(fd, False)
        chunks, deadline = bytearray(), time.time() + timeout
        while time.time() < deadline:
            try:
                chunks += os.read(fd, 4096)
            except BlockingIOError:
                time.sleep(0.05)
                continue
            for f in unframe(bytes(chunks)):
                rr = field(f, 1)                       # Response.request_response
                if rr is None:
                    continue
                sub = field(rr, subsystem_field)
                if sub is None:
                    continue
                got = field(sub, response_field)
                if got is not None:
                    return got
    finally:
        os.close(fd)
    return None


def live_keymap(port):
    # zmk.studio.Request{request_id=1, keymap=Request{get_keymap=true}}
    return _request(port, bytes([0x08, 0x01, 0x2A, 0x02, 0x08, 0x01]), 5, 1)


def reset_settings(port):
    """core.reset_settings -> zmk_keymap_reset_settings(). Keymap only."""
    # zmk.studio.Request{request_id=2, core=Request{reset_settings=true}}
    return _request(port, bytes([0x08, 0x02, 0x1A, 0x02, 0x20, 0x01]), 3, 4)


def default_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("No /dev/cu.usbmodem* found - plug the LEFT half in over USB.")
    return ports[0]


def running_binding(msg):
    """(behavior_id, param1, param2) out of one live Binding submessage."""
    bid = p1 = p2 = 0
    for fn, v in fields(msg):
        if fn == 1:
            bid = zigzag(v)
        elif fn == 2:
            p1 = v
        elif fn == 3:
            p2 = v
    return bid, p1, p2


def report(port, compiled):
    """Print the live-vs-built diff. Returns the number of differing positions."""
    km = live_keymap(port)
    if km is None:
        sys.exit(
            "No keymap response.\n"
            "  - Is this the LEFT half? Only the central runs the keymap and the RPC UART.\n"
            "  - If Studio locking is enabled, tap the &studio_unlock key first."
        )

    names = list(compiled)
    total = 0

    for index, layer in enumerate([v for fn, v in fields(km) if fn == 1]):
        if index >= len(names):
            break
        label = (field(layer, 2) or b"").decode()
        built = compiled[names[index]]
        diffs = []

        for pos, msg in enumerate([v for fn, v in fields(layer) if fn == 3]):
            if pos >= len(built):
                continue
            bid, p1, p2 = running_binding(msg)
            cbeh, cp1, cp2 = built[pos]
            if (p1, p2) != (cp1, cp2):
                diffs.append(f"  position {pos:>2}: running(behavior_id={bid}, "
                             f"0x{p1:05x}, 0x{p2:05x})  !=  built({cbeh} "
                             f"0x{cp1:05x} 0x{cp2:05x})")

        total += len(diffs)
        status = "matches the build" if not diffs else f"{len(diffs)} OVERRIDDEN"
        print(f"\nlayer {index} '{label}': {status}")
        for line in diffs:
            print(line)

    return total


def main():
    args = [a for a in sys.argv[1:] if a != "--clear"]
    clear = "--clear" in sys.argv[1:]
    port = args[0] if args else default_port()
    print(f"Reading the running keymap from {port} ...")

    compiled = compiled_keymap()
    total = report(port, compiled)

    if not clear:
        print()
        if total == 0:
            print("The keyboard is running exactly what the build produced.")
        else:
            print(f"{total} position(s) are pinned by saved ZMK Studio settings "
                  f"and will survive any reflash.")
            print("Clear them without losing Bluetooth pairings:")
            print("    make clear-pinned-keymap")
            sys.exit(1)
        return

    if total == 0:
        print("\nNothing pinned - no reset needed.")
        return

    print("\nSending core.reset_settings (clears the saved keymap; "
          "Bluetooth pairings are untouched) ...")
    if reset_settings(port) is None:
        sys.exit("No reset_settings response - is Studio unlocked?")

    print("Re-reading the keymap to confirm ...")
    remaining = report(port, compiled)
    print()
    if remaining == 0:
        print("Cleared. The keyboard now runs exactly what the build produced.")
    else:
        sys.exit(f"{remaining} position(s) still differ - reset did not fully "
                 f"apply. Fall back to 'make flash-reset'.")


if __name__ == "__main__":
    main()
