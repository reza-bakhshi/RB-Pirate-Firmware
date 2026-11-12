#!/usr/bin/env python3
# Copyright (c) 2026 RB-Pirate contributors. SPDX-License-Identifier: MIT
"""
platform_identity.py: Extract, audit, and verify machine-readable build identity
from actual CMake configuration, definitions, ELF headers, and memory sections.

Never relies on directory name alone to verify target silicon.
"""

import argparse
import json
import os
import re
import subprocess
import sys

STATIC_RAM_CEILING = 389120  # 380 KB main RAM budget on RP2350


def find_readelf():
    for cmd in ["arm-none-eabi-readelf", "readelf"]:
        try:
            res = subprocess.run([cmd, "--version"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if res.returncode == 0:
                return cmd
        except FileNotFoundError:
            continue
    return None


def parse_cmake_cache(cache_path):
    cache = {}
    if not os.path.isfile(cache_path):
        return cache
    with open(cache_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or line.startswith("//"):
                continue
            if ":" in line and "=" in line:
                var_part, val = line.split("=", 1)
                var_name = var_part.split(":", 1)[0]
                cache[var_name] = val
    return cache


def inspect_elf(elf_path, readelf_cmd):
    if not os.path.isfile(elf_path):
        raise FileNotFoundError(f"ELF binary not found: {elf_path}")

    # 1. Header (-h)
    h_out = subprocess.check_output([readelf_cmd, "-h", elf_path], text=True)
    elf_class = ""
    elf_machine = ""
    entry_point = ""
    for line in h_out.splitlines():
        if "Class:" in line:
            elf_class = line.split(":", 1)[1].strip()
        elif "Machine:" in line:
            elf_machine = line.split(":", 1)[1].strip()
        elif "Entry point address:" in line:
            entry_point = line.split(":", 1)[1].strip()

    # 2. Attributes (-A)
    a_out = subprocess.check_output([readelf_cmd, "-A", elf_path], text=True)
    tag_cpu_name = ""
    tag_cpu_arch = ""
    tag_fp_arch = ""
    for line in a_out.splitlines():
        if "Tag_CPU_name:" in line:
            tag_cpu_name = line.split(":", 1)[1].strip().strip('"')
        elif "Tag_CPU_arch:" in line:
            tag_cpu_arch = line.split(":", 1)[1].strip()
        elif "Tag_FP_arch:" in line:
            tag_fp_arch = line.split(":", 1)[1].strip()

    # 3. Sections (-S)
    s_out = subprocess.check_output([readelf_cmd, "-S", elf_path], text=True)
    # Parse section addresses and sizes
    flash_start = None
    flash_end = 0
    ram_start = None
    ram_end = 0
    scratch_x_addr = None
    scratch_y_addr = None

    for line in s_out.splitlines():
        # format: [Nr] Name Type Addr Off Size ...
        m = re.search(r'\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-fA-F]{8})\s+[0-9a-fA-F]+\s+([0-9a-fA-F]+)', line)
        if m:
            s_name, s_addr_s, s_size_s = m.group(1), m.group(2), m.group(3)
            addr = int(s_addr_s, 16)
            size = int(s_size_s, 16)
            if size == 0:
                continue

            # Flash regions (0x10000000..0x11000000)
            if 0x10000000 <= addr < 0x11000000:
                if flash_start is None or addr < flash_start:
                    flash_start = addr
                if addr + size > flash_end:
                    flash_end = addr + size

            # Main RAM regions (0x20000000..0x20080000 for RP2350 main RAM)
            if 0x20000000 <= addr < 0x20080000:
                if ram_start is None or addr < ram_start:
                    ram_start = addr
                if addr + size > ram_end:
                    ram_end = addr + size

            # Scratch stacks
            if addr == 0x20080000 or (addr == 0x20040000 and "scratch" in s_name):
                scratch_x_addr = hex(addr)
            if addr == 0x20081000 or (addr == 0x20041000 and "scratch" in s_name):
                scratch_y_addr = hex(addr)

    static_ram_bytes = (ram_end - ram_start) if (ram_start is not None and ram_end > ram_start) else 0
    flash_bytes = (flash_end - flash_start) if (flash_start is not None and flash_end > flash_start) else 0

    # Determine silicon
    detected_silicon = "UNKNOWN"
    core_profile = "UNKNOWN"
    if "v8-M" in tag_cpu_arch or tag_cpu_name == "8-M.MAIN":
        detected_silicon = "RP2350"
        core_profile = "Cortex-M33"
    elif "v6S-M" in tag_cpu_arch or "Cortex-M0+" in tag_cpu_name:
        detected_silicon = "RP2040"
        core_profile = "Cortex-M0+"

    return {
        "elf_class": elf_class,
        "elf_machine": elf_machine,
        "entry_point": entry_point,
        "tag_cpu_name": tag_cpu_name,
        "tag_cpu_arch": tag_cpu_arch,
        "tag_fp_arch": tag_fp_arch,
        "detected_silicon": detected_silicon,
        "core_profile": core_profile,
        "flash_start": hex(flash_start) if flash_start is not None else None,
        "flash_bytes": flash_bytes,
        "ram_start": hex(ram_start) if ram_start is not None else None,
        "static_ram_bytes": static_ram_bytes,
        "scratch_x_addr": scratch_x_addr,
        "scratch_y_addr": scratch_y_addr,
    }


def audit_build_identity(target_path):
    # Resolve ELF path and build directory
    if os.path.isdir(target_path):
        build_dir = os.path.abspath(target_path)
        elf_path = os.path.join(build_dir, "src", "bus_pirate6.elf")
    else:
        elf_path = os.path.abspath(target_path)
        # Check parent hierarchy for CMakeCache.txt
        cur = os.path.dirname(elf_path)
        build_dir = None
        while cur and cur != "/":
            if os.path.isfile(os.path.join(cur, "CMakeCache.txt")):
                build_dir = cur
                break
            cur = os.path.dirname(cur)

    readelf_cmd = find_readelf()
    if not readelf_cmd:
        raise RuntimeError("Neither arm-none-eabi-readelf nor readelf found in PATH.")

    cache_path = os.path.join(build_dir, "CMakeCache.txt") if build_dir else ""
    cmake_cache = parse_cmake_cache(cache_path) if cache_path else {}

    elf_info = inspect_elf(elf_path, readelf_cmd)

    cmake_platform = cmake_cache.get("PICO_PLATFORM", "")
    cmake_board = cmake_cache.get("PICO_BOARD", "")

    # Audit directory name vs actual silicon
    dirname = os.path.basename(build_dir) if build_dir else os.path.basename(os.path.dirname(elf_path))
    misleading_dirname = False
    dirname_suggests = "unknown"
    if "rp2040" in dirname.lower():
        dirname_suggests = "rp2040"
        if elf_info["detected_silicon"] != "RP2040":
            misleading_dirname = True
    elif "rp2350" in dirname.lower():
        dirname_suggests = "rp2350"
        if elf_info["detected_silicon"] != "RP2350":
            misleading_dirname = True

    # Validate against supported platform contract (RP2350, rb-pirate, static RAM <= 389120)
    contract_passed = True
    failure_reasons = []

    if elf_info["detected_silicon"] != "RP2350":
        contract_passed = False
        failure_reasons.append(f"Target silicon is {elf_info['detected_silicon']}, required RP2350.")

    if elf_info["tag_cpu_arch"] != "v8-M.mainline":
        contract_passed = False
        failure_reasons.append(f"Target CPU arch is {elf_info['tag_cpu_arch']}, required v8-M.mainline.")

    if elf_info["static_ram_bytes"] > STATIC_RAM_CEILING:
        contract_passed = False
        failure_reasons.append(
            f"Static RAM usage {elf_info['static_ram_bytes']} exceeds ceiling {STATIC_RAM_CEILING} bytes."
        )

    if cmake_platform and not re.match(r"^(rp2350|rp2350-arm-s)$", cmake_platform):
        contract_passed = False
        failure_reasons.append(f"CMake cached PICO_PLATFORM='{cmake_platform}' is not supported.")

    if cmake_board and cmake_board != "rb-pirate":
        contract_passed = False
        failure_reasons.append(f"CMake cached PICO_BOARD='{cmake_board}' is not supported.")

    headroom = (STATIC_RAM_CEILING - elf_info["static_ram_bytes"]) if elf_info["static_ram_bytes"] else 0

    identity = {
        "build_directory": build_dir,
        "elf_path": elf_path,
        "cmake_platform": cmake_platform or "not_found",
        "cmake_board": cmake_board or "not_found",
        "elf_class": elf_info["elf_class"],
        "elf_machine": elf_info["elf_machine"],
        "entry_point": elf_info["entry_point"],
        "cpu_arch": elf_info["tag_cpu_arch"],
        "cpu_name": elf_info["tag_cpu_name"],
        "fp_arch": elf_info["tag_fp_arch"],
        "detected_silicon": elf_info["detected_silicon"],
        "core_profile": elf_info["core_profile"],
        "memory_regions": {
            "flash_start": elf_info["flash_start"],
            "flash_bytes": elf_info["flash_bytes"],
            "ram_start": elf_info["ram_start"],
            "static_ram_bytes": elf_info["static_ram_bytes"],
            "static_ram_ceiling_bytes": STATIC_RAM_CEILING,
            "static_ram_headroom_bytes": headroom,
            "scratch_x_addr": elf_info["scratch_x_addr"],
            "scratch_y_addr": elf_info["scratch_y_addr"],
        },
        "directory_audit": {
            "directory_name": dirname,
            "directory_suggests": dirname_suggests,
            "actual_silicon": elf_info["detected_silicon"],
            "is_misleading": misleading_dirname,
            "notice": (
                f"Directory name '{dirname}' contradicts actual ELF silicon '{elf_info['detected_silicon']}'. "
                "Directory names never prove platform identity."
                if misleading_dirname
                else "Directory name agrees with detected silicon."
            ),
        },
        "supported_contract_passed": contract_passed,
        "contract_failure_reasons": failure_reasons,
    }
    return identity


def main():
    parser = argparse.ArgumentParser(
        description="Emit and verify machine-readable build identity from CMake definitions, ELF headers, and memory regions."
    )
    parser.add_argument("target", help="Path to build directory or bus_pirate6.elf")
    parser.add_argument("--json", action="store_true", help="Print JSON output to stdout")
    parser.add_argument("-o", "--output", help="Write JSON output to designated file")
    parser.add_argument(
        "--verify",
        action="store_true",
        help="Strictly verify that the target meets the supported platform contract (RP2350, rb-pirate, RAM <= ceiling)",
    )
    parser.add_argument(
        "--assert-misleading-dir",
        action="store_true",
        help="Assert that the directory name is misleading (for test verification)",
    )

    args = parser.parse_args()

    try:
        identity = audit_build_identity(args.target)
    except Exception as e:
        sys.stderr.write(f"Error auditing build identity: {e}\n")
        sys.exit(2)

    json_str = json.dumps(identity, indent=2)

    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(json_str + "\n")

    if args.json or not args.verify:
        print(json_str)

    if args.assert_misleading_dir and not identity["directory_audit"]["is_misleading"]:
        sys.stderr.write(f"Assertion failed: expected misleading directory name for {args.target}\n")
        sys.exit(1)

    if args.verify:
        if not identity["supported_contract_passed"]:
            sys.stderr.write("Verification FAILED:\n")
            for r in identity["contract_failure_reasons"]:
                sys.stderr.write(f"  - {r}\n")
            sys.exit(1)
        else:
            print("Verification PASSED: Binary matches supported platform contract (RP2350 Arm Secure, rb-pirate).")

    sys.exit(0)


if __name__ == "__main__":
    main()
