#!/usr/bin/env python3
"""Compare the running ZMK Studio keymap with the compiled devicetree.

The command is read-only unless --clear is passed. Clearing sends
core.reset_settings, which removes Studio-saved bindings without deleting
Bluetooth pairings, and then confirms the live keymap matches the build.

Usage:  scripts/live-keymap.py [--clear] [/dev/cu.usbmodemXXXX]
"""

import glob
import os
import re
import sys
import time

FRAME_START = 0xAB
FRAME_ESCAPE = 0xAC
FRAME_END = 0xAD
COMPILED_DTS_PATH = ".build/firmware/left/zephyr/zephyr.dts"
LAYER_NODE_NAMES = (
    "base_layer",
    "symbols_layer",
    "functional_layer",
    "magic_layer",
    "settings_layer",
)


# Studio RPC framing; see zmk/app/src/studio/msg_framing.c.
def frame_payload(payload):
    framed = bytearray([FRAME_START])
    for byte in payload:
        if byte in (FRAME_START, FRAME_ESCAPE, FRAME_END):
            framed.append(FRAME_ESCAPE)
        framed.append(byte)
    framed.append(FRAME_END)
    return bytes(framed)


def unframe_messages(data):
    message = bytearray()
    state = "idle"

    for byte in data:
        if state == "idle":
            if byte == FRAME_START:
                message = bytearray()
                state = "data"
        elif state == "data":
            if byte == FRAME_ESCAPE:
                state = "escaped"
            elif byte == FRAME_END:
                yield bytes(message)
                state = "idle"
            elif byte == FRAME_START:
                message = bytearray()
            else:
                message.append(byte)
        else:
            message.append(byte)
            state = "data"


# Minimal dependency-free protobuf decoding.
def decode_varint(data, offset):
    value = 0
    shift = 0

    while True:
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return value, offset
        shift += 7


def protobuf_fields(message):
    offset = 0

    while offset < len(message):
        tag, offset = decode_varint(message, offset)
        field_number = tag >> 3
        wire_type = tag & 7

        if wire_type == 0:
            value, offset = decode_varint(message, offset)
            yield field_number, value
        elif wire_type == 2:
            length, offset = decode_varint(message, offset)
            yield field_number, message[offset : offset + length]
            offset += length
        elif wire_type == 5:
            yield field_number, message[offset : offset + 4]
            offset += 4
        elif wire_type == 1:
            yield field_number, message[offset : offset + 8]
            offset += 8
        else:
            raise ValueError(f"unsupported wire type {wire_type}")


def protobuf_field(message, requested_number):
    for field_number, value in protobuf_fields(message):
        if field_number == requested_number:
            return value
    return None


def decode_zigzag(value):
    return (value >> 1) ^ -(value & 1)


def parse_compiled_keymap():
    """Read bindings from the generated devicetree embedded in the UF2."""
    if not os.path.exists(COMPILED_DTS_PATH):
        sys.exit(f"{COMPILED_DTS_PATH} not found - run 'make build-left' first.")

    with open(COMPILED_DTS_PATH) as devicetree_file:
        devicetree = devicetree_file.read()

    layers = {}
    for node_name in LAYER_NODE_NAMES:
        bindings_match = re.search(node_name + r" \{.*?bindings = < (.*?) >;", devicetree, re.S)
        if not bindings_match:
            sys.exit(f"could not find {node_name} bindings in {COMPILED_DTS_PATH}")

        bindings = []
        current_binding = []
        for token in bindings_match.group(1).split():
            if token.startswith("&"):
                if current_binding:
                    bindings.append(current_binding)
                current_binding = [token]
            else:
                current_binding.append(token)
        bindings.append(current_binding)

        parsed_bindings = []
        for binding in bindings:
            parameters = [int(value, 0) for value in binding[1:]]
            parsed_bindings.append(
                (
                    binding[0],
                    parameters[0] if parameters else 0,
                    parameters[1] if len(parameters) > 1 else 0,
                )
            )
        layers[node_name] = parsed_bindings

    return layers


def send_request(port, payload, subsystem_field, response_field, timeout=6):
    """Send one RPC request and return the requested response submessage."""
    os.system(f"stty -f {port} raw -echo 115200 >/dev/null 2>&1")
    file_descriptor = os.open(port, os.O_RDWR | os.O_NOCTTY)

    try:
        os.write(file_descriptor, frame_payload(payload))
        os.set_blocking(file_descriptor, False)
        received = bytearray()
        deadline = time.time() + timeout

        while time.time() < deadline:
            try:
                received += os.read(file_descriptor, 4096)
            except BlockingIOError:
                time.sleep(0.05)
                continue

            for message in unframe_messages(bytes(received)):
                request_response = protobuf_field(message, 1)
                if request_response is None:
                    continue
                subsystem_response = protobuf_field(request_response, subsystem_field)
                if subsystem_response is None:
                    continue
                requested_response = protobuf_field(subsystem_response, response_field)
                if requested_response is not None:
                    return requested_response
    finally:
        os.close(file_descriptor)

    return None


def read_live_keymap(port):
    # zmk.studio.Request{request_id=1, keymap=Request{get_keymap=true}}
    return send_request(port, bytes([0x08, 0x01, 0x2A, 0x02, 0x08, 0x01]), 5, 1)


def reset_keymap_settings(port):
    """Send core.reset_settings, which calls zmk_keymap_reset_settings()."""
    # zmk.studio.Request{request_id=2, core=Request{reset_settings=true}}
    return send_request(port, bytes([0x08, 0x02, 0x1A, 0x02, 0x20, 0x01]), 3, 4)


def find_default_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("No /dev/cu.usbmodem* found - plug the LEFT half in over USB.")
    return ports[0]


def parse_running_binding(message):
    """Return behavior id and parameters from one live Binding submessage."""
    behavior_id = 0
    parameter_1 = 0
    parameter_2 = 0

    for field_number, value in protobuf_fields(message):
        if field_number == 1:
            behavior_id = decode_zigzag(value)
        elif field_number == 2:
            parameter_1 = value
        elif field_number == 3:
            parameter_2 = value

    return behavior_id, parameter_1, parameter_2


def report_keymap_differences(port, compiled_keymap):
    """Print the live/build diff and return the number of differing positions."""
    live_keymap = read_live_keymap(port)
    if live_keymap is None:
        sys.exit(
            "No keymap response.\n"
            "  - Is this the LEFT half? Only the central runs the keymap and the RPC UART.\n"
            "  - If Studio locking is enabled, tap the &studio_unlock key first."
        )

    compiled_layer_names = list(compiled_keymap)
    total_differences = 0

    live_layers = [
        value for field_number, value in protobuf_fields(live_keymap) if field_number == 1
    ]
    for layer_index, live_layer in enumerate(live_layers):
        if layer_index >= len(compiled_layer_names):
            break

        label = (protobuf_field(live_layer, 2) or b"").decode()
        compiled_bindings = compiled_keymap[compiled_layer_names[layer_index]]
        differences = []
        live_bindings = [
            value for field_number, value in protobuf_fields(live_layer) if field_number == 3
        ]

        for position, live_binding in enumerate(live_bindings):
            if position >= len(compiled_bindings):
                continue

            behavior_id, parameter_1, parameter_2 = parse_running_binding(live_binding)
            compiled_behavior, compiled_parameter_1, compiled_parameter_2 = compiled_bindings[
                position
            ]
            if (parameter_1, parameter_2) != (compiled_parameter_1, compiled_parameter_2):
                differences.append(
                    f"  position {position:>2}: running(behavior_id={behavior_id}, "
                    f"0x{parameter_1:05x}, 0x{parameter_2:05x})  !=  built({compiled_behavior} "
                    f"0x{compiled_parameter_1:05x} 0x{compiled_parameter_2:05x})"
                )

        total_differences += len(differences)
        status = "matches the build" if not differences else f"{len(differences)} OVERRIDDEN"
        print(f"\nlayer {layer_index} '{label}': {status}")
        for difference in differences:
            print(difference)

    return total_differences


def main():
    arguments = [argument for argument in sys.argv[1:] if argument != "--clear"]
    clear_requested = "--clear" in sys.argv[1:]
    port = arguments[0] if arguments else find_default_port()
    print(f"Reading the running keymap from {port} ...")

    compiled_keymap = parse_compiled_keymap()
    difference_count = report_keymap_differences(port, compiled_keymap)

    if not clear_requested:
        print()
        if difference_count == 0:
            print("The keyboard is running exactly what the build produced.")
        else:
            print(
                f"{difference_count} position(s) are pinned by saved ZMK Studio settings "
                f"and will survive any reflash."
            )
            print("Clear them without losing Bluetooth pairings:")
            print("    make clear-pinned-keymap")
            sys.exit(1)
        return

    if difference_count == 0:
        print("\nNothing pinned - no reset needed.")
        return

    print(
        "\nSending core.reset_settings (clears the saved keymap; "
        "Bluetooth pairings are untouched) ..."
    )
    if reset_keymap_settings(port) is None:
        sys.exit("No reset_settings response - is Studio unlocked?")

    print("Re-reading the keymap to confirm ...")
    remaining_differences = report_keymap_differences(port, compiled_keymap)
    print()
    if remaining_differences == 0:
        print("Cleared. The keyboard now runs exactly what the build produced.")
    else:
        sys.exit(
            f"{remaining_differences} position(s) still differ - reset did not fully "
            f"apply. Fall back to 'make flash-reset'."
        )


if __name__ == "__main__":
    main()
