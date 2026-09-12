#!/usr/bin/env python3
"""Check release inputs and compare two MCUboot application builds."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

import yaml
from west.manifest import Manifest

REPO = Path(__file__).resolve().parents[1]
WORKSPACE = REPO.parent


def run(*args, cwd=WORKSPACE):
    return subprocess.check_output(args, cwd=cwd, text=True).strip()


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def required(name):
    value = os.environ.get(name)
    if not value:
        raise ValueError(f"{name} is required")
    return value


def projects():
    manifest = Manifest.from_topdir(str(WORKSPACE))
    return [p for p in manifest.projects[1:] if manifest.is_active(p)]


def source_state(frozen):
    expected = {p["name"]: p for p in frozen["manifest"]["projects"]}
    active = projects()
    if set(expected) != {p.name for p in active}:
        raise ValueError("Frozen manifest must contain every active west project")
    for project in active:
        if expected[project.name].get("path") != project.path or expected[project.name].get("url") != project.url:
            raise ValueError(f"{project.name}: frozen path or URL differs from west")
    revisions = {}
    for name, path, wanted in [("k-fsw", REPO, required("KFSW_RELEASE_SOURCE"))] + [
        (p.name, Path(p.abspath), expected[p.name]["revision"]) for p in active
    ]:
        if not re.fullmatch(r"[0-9a-f]{40}", wanted):
            raise ValueError(f"{name}: a full commit SHA is required")
        head = run("git", "rev-parse", "HEAD", cwd=path)
        if head != wanted:
            raise ValueError(f"{name}: checkout differs from frozen revision")
        if run("git", "status", "--porcelain", "--untracked-files=normal", cwd=path):
            raise ValueError(f"{name}: source tree is dirty")
        revisions[name] = head
    return revisions


def check_inputs():
    version = required("KFSW_IMAGE_VERSION")
    match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)\+(\d+)", version)
    if not match or any(int(v) > limit for v, limit in zip(
        match.groups(), (255, 255, 65535, 4294967295)
    )):
        raise ValueError("KFSW_IMAGE_VERSION must fit MCUboot major.minor.patch+build")
    epoch = required("SOURCE_DATE_EPOCH")
    if not epoch.isdecimal() or int(epoch) > 253402300799:
        raise ValueError("SOURCE_DATE_EPOCH must be a valid UTC Unix timestamp")
    frozen_path = Path(required("KFSW_RELEASE_MANIFEST")).resolve()
    frozen = yaml.safe_load(frozen_path.read_text())
    revisions = source_state(frozen)
    sdk = Path(required("ZEPHYR_SDK_INSTALL_DIR")).resolve()
    compiler = sdk / "gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc"
    compiler_hash = sha256(compiler)
    if compiler_hash != required("KFSW_RELEASE_COMPILER_SHA256"):
        raise ValueError("Compiler does not match KFSW_RELEASE_COMPILER_SHA256")
    key_path = Path(required("KFSW_MCUBOOT_KEY")).resolve()
    mcuboot = WORKSPACE / "bootloader/mcuboot"
    sys.path.insert(0, str(mcuboot / "scripts"))
    from imgtool import keys
    from imgtool.keys.ecdsa import ECDSA256P1

    key = keys.load(str(key_path))
    if not isinstance(key, ECDSA256P1):
        raise ValueError("A readable ECDSA P-256 private signing key is required")
    public = key.get_public_bytes()
    fingerprint = hashlib.sha256(public).hexdigest()
    for relative in run("git", "ls-files", "*.pem", cwd=mcuboot).splitlines():
        try:
            development_key = keys.load(str(mcuboot / relative))
        except (ValueError, TypeError):
            continue
        if development_key and development_key.get_public_bytes() == public:
            raise ValueError("Signing key matches a development key shipped with MCUboot")
    return {
        "version": version,
        "source_date_epoch": int(epoch),
        "sources": revisions,
        "manifest_sha256": sha256(frozen_path),
        "compiler_sha256": compiler_hash,
        "compiler": run(str(compiler), "--version").splitlines()[0],
        "sdk_version": (sdk / "sdk_version").read_text().strip(),
        "cmake": run("cmake", "--version").splitlines()[0],
        "west": run(str(WORKSPACE / ".venv/bin/west"), "--version"),
        "signing_public_key_sha256": fingerprint,
    }


def verify_build(build, inputs):
    from imgtool import keys
    from imgtool.image import Image, VerifyResult

    cache = (build / "app/CMakeCache.txt").read_text()
    compiler = re.search(r"^CMAKE_C_COMPILER:(?:FILEPATH|STRING)=(.+)$", cache, re.MULTILINE)
    if not compiler or sha256(Path(compiler[1])) != inputs["compiler_sha256"]:
        raise ValueError("Build used a different compiler")
    app = build / "app/zephyr"
    image = app / "zephyr.signed.bin"
    key = keys.load(required("KFSW_MCUBOOT_KEY"))
    result, *_ = Image.verify(str(image), key)
    if result != VerifyResult.OK:
        raise ValueError(f"{build.name}: image signature verification failed")
    version = struct.unpack_from("<BBHI", image.read_bytes(), 20)
    if f"{version[0]}.{version[1]}.{version[2]}+{version[3]}" != inputs["version"]:
        raise ValueError("MCUboot image version differs from application version")
    public = key.get_public_bytes()
    generated = list((build / "mcuboot").rglob("autogen-pubkey.c"))
    if len(generated) != 1:
        raise ValueError("Cannot find the bootloader's generated trust key")
    key_bytes = bytes(int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})\b", generated[0].read_text()))
    if public not in key_bytes:
        raise ValueError("Bootloader trust key differs from the image signing key")
    return sha256(app / "zephyr.bin")


def build_release(output, target):
    inputs = check_inputs()
    output.mkdir(parents=True, exist_ok=False)
    hashes = []
    env = os.environ.copy()
    env.update(KFSW_RELEASE_BUILD="1", KFSW_SYSBUILD="1", KFSW_PRISTINE="always",
               ZEPHYR_TOOLCHAIN_VARIANT="zephyr")
    for suffix in ("a", "b"):
        build = output / f"build-{suffix}"
        env["KFSW_BUILD_DIR"] = str(build)
        with (output / f"build-{suffix}.log").open("w") as log:
            subprocess.run(["bash", str(REPO / "tools/build.sh"), target],
                           cwd=REPO, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        hashes.append(verify_build(build, inputs))
        if check_inputs() != inputs:
            raise ValueError("Release inputs changed during the build")
    if hashes[0] != hashes[1]:
        raise ValueError("Unsigned application payloads differ between builds")
    artifacts = output / "artifacts"
    artifacts.mkdir()
    for image_name in ("app", "mcuboot"):
        source = output / "build-a" / image_name / "zephyr"
        destination = artifacts / image_name
        destination.mkdir()
        for name in (".config", "zephyr.dts", "zephyr.map", "zephyr.bin", "zephyr.elf",
                     "zephyr.signed.bin", "zephyr.hex"):
            if (source / name).is_file():
                shutil.copy2(source / name, destination / name)
    shutil.copy2(required("KFSW_RELEASE_MANIFEST"), artifacts / "west-frozen.yml")
    inputs["unsigned_payload_sha256"] = hashes[0]
    inputs["artifacts"] = {
        str(p.relative_to(artifacts)): sha256(p) for p in sorted(artifacts.rglob("*")) if p.is_file()
    }
    (artifacts / "release.json").write_text(json.dumps(inputs, indent=2) + "\n")
    print(f"PASS: matching unsigned payloads; signatures and bootloader key verified: {artifacts}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("check", "build", "freeze"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--target", default="nucleo_l496zg")
    args = parser.parse_args()
    if args.action == "freeze":
        frozen = {"manifest": {"version": "1.2", "projects": [
            {"name": p.name, "url": p.url, "path": p.path,
             "revision": run("git", "rev-parse", "HEAD", cwd=p.abspath)} for p in projects()
        ], "self": {"path": "k-fsw"}}}
        print(yaml.safe_dump(frozen, sort_keys=False), end="")
    elif args.action == "check":
        inputs = check_inputs()
        if args.output:
            args.output.write_text(json.dumps(inputs, indent=2) + "\n")
        print("Release inputs checked")
    else:
        if args.output is None:
            parser.error("build requires --output")
        build_release(args.output.resolve(), args.target)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        print(f"Release check failed: {error}", file=sys.stderr)
        sys.exit(1)
