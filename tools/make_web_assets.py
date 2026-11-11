#!/usr/bin/env python3
# Copyright (C) 2026 Manuel - EA7JRS
# Generates the files of the web installer / on-line update site (GitHub Pages).
#
#   make_web_assets.py board --board NAME --chip esp32s3 --build-dir .pio/build/NAME --boot-app0 PATH --out DIR
#       copies the four images of one board and writes manifests/NAME.json (ESP Web Tools) and ota/NAME.json (read by the tracker)
#   make_web_assets.py site --root DIR --page web/index.html
#       joins the per-board files: latest.json + index.html
#
# Layout of the site:
#   index.html, latest.json
#   firmware/<board>/{bootloader,partitions,boot_app0,firmware}.bin
#   manifests/<board>.json      for <esp-web-install-button>
#   ota/<board>.json            {"version","size","sha256","elfSha","url","ota"}
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import sys

CHIP_FAMILY = {"esp32": "ESP32", "esp32s3": "ESP32-S3", "esp32c3": "ESP32-C3", "esp32s2": "ESP32-S2"}
BOOTLOADER_OFFSET = {"esp32": 0x1000, "esp32s2": 0x1000, "esp32s3": 0x0, "esp32c3": 0x0}


def read_version(src="src/LoRa_APRS_Tracker.cpp"):
    with open(src, encoding="utf-8") as f:
        m = re.search(r'String\s+versionNumber\s*=\s*"([^"]+)"', f.read())
    if not m:
        sys.exit("versionNumber not found in " + src)
    return m.group(1)


def partition_info(path):
    """Returns (has_second_app_slot, first_app_size) from a partitions.bin (ESP-IDF partition table)."""
    with open(path, "rb") as f:
        data = f.read()
    apps = []
    for i in range(0, len(data) - 32 + 1, 32):
        magic, ptype, subtype, offset, size = struct.unpack_from("<HBBII", data, i)
        if magic != 0x50AA:
            break
        if ptype == 0 and (subtype == 0x00 or 0x10 <= subtype <= 0x1F):
            apps.append((subtype, offset, size))
    ota = any(0x10 <= s <= 0x1F for s, _, _ in apps) and sum(1 for s, _, _ in apps if s == 0x00 or s >= 0x10) >= 2
    return ota, (apps[0][2] if apps else 0)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def elf_sha256(path):
    # app_elf_sha256 of esp_app_desc_t: the app description is the start of the first segment (image header 24 bytes + segment
    # header 8 bytes = 0x20) and the ELF hash is at 0x90 inside it. The tracker reports the same 32 bytes (event info "elf"), so
    # the app can tell an official build of this release from a custom one.
    with open(path, "rb") as f:
        head = f.read(0x20 + 0x90 + 32)
    if len(head) < 0x20 + 0x90 + 32 or head[0] != 0xE9 or int.from_bytes(head[0x20:0x24], "little") != 0xABCD5432:
        return None
    return head[0x20 + 0x90:0x20 + 0x90 + 32].hex()


def cmd_board(a):
    version = read_version()
    fw_dir = os.path.join(a.out, "firmware", a.board)
    os.makedirs(fw_dir, exist_ok=True)
    os.makedirs(os.path.join(a.out, "manifests"), exist_ok=True)
    os.makedirs(os.path.join(a.out, "ota"), exist_ok=True)

    src = {
        "bootloader.bin": os.path.join(a.build_dir, "bootloader.bin"),
        "partitions.bin": os.path.join(a.build_dir, "partitions.bin"),
        "boot_app0.bin": a.boot_app0,
        "firmware.bin": os.path.join(a.build_dir, "firmware.bin"),
    }
    for name, path in src.items():
        shutil.copyfile(path, os.path.join(fw_dir, name))

    ota, slot = partition_info(src["partitions.bin"])
    fw = os.path.join(fw_dir, "firmware.bin")
    size = os.path.getsize(fw)
    if ota and slot and size > slot:
        sys.exit("firmware.bin (%d bytes) does not fit its slot (%d bytes)" % (size, slot))

    parts = [
        {"path": "../firmware/%s/bootloader.bin" % a.board, "offset": BOOTLOADER_OFFSET[a.chip]},
        {"path": "../firmware/%s/partitions.bin" % a.board, "offset": 0x8000},
        {"path": "../firmware/%s/boot_app0.bin" % a.board, "offset": 0xE000},
        {"path": "../firmware/%s/firmware.bin" % a.board, "offset": 0x10000},
    ]
    # No SPIFFS part: the settings and messages stored in the tracker are kept when it is re-flashed
    manifest = {
        "name": "APRS LoRa+ %s" % a.board,
        "version": version,
        "new_install_prompt_erase": False,
        "builds": [{"chipFamily": CHIP_FAMILY[a.chip], "parts": parts}],
    }
    with open(os.path.join(a.out, "manifests", a.board + ".json"), "w") as f:
        json.dump(manifest, f, indent=2)

    ota_info = {
        "board": a.board,
        "chip": a.chip,
        "version": version,
        "size": size,
        "sha256": sha256(fw),
        "elfSha": elf_sha256(fw),
        "url": "firmware/%s/firmware.bin" % a.board,
        "ota": ota,
    }
    with open(os.path.join(a.out, "ota", a.board + ".json"), "w") as f:
        json.dump(ota_info, f, indent=2)
    print("%s: version %s, %d bytes, second slot: %s" % (a.board, version, size, "yes" if ota else "no"))


def cmd_site(a):
    boards = {}
    ota_dir = os.path.join(a.root, "ota")
    for name in sorted(os.listdir(ota_dir)):
        if not name.endswith(".json"):
            continue
        with open(os.path.join(ota_dir, name)) as f:
            d = json.load(f)
        boards[d["board"]] = {"version": d["version"], "chip": d["chip"], "ota": d["ota"]}
    if not boards:
        sys.exit("no boards found in " + ota_dir)
    versions = {b["version"] for b in boards.values()}
    if len(versions) != 1:
        sys.exit("boards with different versions: %s" % sorted(versions))
    latest = {"version": versions.pop(), "boards": boards}
    with open(os.path.join(a.root, "latest.json"), "w") as f:
        json.dump(latest, f, indent=2)
    shutil.copyfile(a.page, os.path.join(a.root, "index.html"))
    # other pages next to the installer page (privacy policy of the Android app...)
    page_dir = os.path.dirname(os.path.abspath(a.page))
    for name in sorted(os.listdir(page_dir)):
        src = os.path.join(page_dir, name)
        if name != os.path.basename(a.page) and name.endswith(".html") and os.path.isfile(src):
            shutil.copyfile(src, os.path.join(a.root, name))
    open(os.path.join(a.root, ".nojekyll"), "w").close()
    print("site: version %s, %d boards" % (latest["version"], len(boards)))


def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("board")
    b.add_argument("--board", required=True)
    b.add_argument("--chip", required=True, choices=sorted(CHIP_FAMILY))
    b.add_argument("--build-dir", required=True)
    b.add_argument("--boot-app0", required=True)
    b.add_argument("--out", required=True)
    b.set_defaults(fn=cmd_board)
    s = sub.add_parser("site")
    s.add_argument("--root", required=True)
    s.add_argument("--page", required=True)
    s.set_defaults(fn=cmd_site)
    a = p.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
