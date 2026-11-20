#!/usr/bin/env python3
"""Real no-disk MiniOS acceptance. All artifacts stay in a new explicit work dir."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

FIXTURES = Path(__file__).resolve().parent / "minios"

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--mode", choices=("smoke", "bootstrap", "runtime"), default="smoke")
    for name in ("kernel", "mkinitrd", "krtc", "arklink", "minios", "sdk-build", "work"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--source", type=Path)
    p.add_argument("--source-root", type=Path, help="Frozen root containing original SelfHost sources; identical absolute guest paths")
    p.add_argument("--stage2-kro", type=Path)
    p.add_argument("--qemu", default="qemu-system-x86_64")
    p.add_argument("--accel", choices=("kvm", "tcg"), default="kvm")
    p.add_argument("--memory", default="6G")
    p.add_argument("--timeout", type=int, default=900)
    args = p.parse_args()
    for name in ("kernel", "mkinitrd", "krtc", "arklink", "minios", "sdk_build", "work"):
        setattr(args, name, getattr(args, name).resolve())
    if args.mode == "bootstrap" and (not (args.source or args.source_root) or not args.stage2_kro):
        p.error("bootstrap requires --source or --source-root, and --stage2-kro")
    if args.source and args.source_root:
        p.error("choose source or source-root, not both")
    work = args.work
    work.mkdir(parents=True, exist_ok=False)
    report = {"status": "running", "mode": args.mode, "commands": [], "inputs": {}}
    def save():
        (work / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    def run(command, log):
        command = list(map(str, command))
        with (work / log).open("w") as stream:
            result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
        report["commands"].append({"argv": command, "returncode": result.returncode, "log": log})
        save()
        if result.returncode:
            raise RuntimeError(f"Command failed; see {work / log}")
    try:
        for path in (args.kernel, args.mkinitrd, args.krtc, args.arklink,
                     args.sdk_build / "sdk/crt0.o", args.sdk_build / "sdk/libminios.a"):
            report["inputs"][str(path)] = sha(path)
        init_source = FIXTURES / ("bootstrap-init.c" if args.mode == "bootstrap" else ("runtime-init.c" if args.mode == "runtime" else "smoke-init.c"))
        report["inputs"][str(init_source)] = sha(init_source)
        bootstrap_inputs = []
        if args.mode == "bootstrap":
            if args.source_root:
                root = args.source_root.resolve()
                bootstrap_inputs = [(str(path.relative_to(root)), path) for path in
                    sorted((root / "SelfHost").rglob("*.krt")) if path.name != "project.krt"]
                if not bootstrap_inputs:
                    raise RuntimeError("No original SelfHost sources under source-root")
            else:
                bootstrap_inputs = [("Compiler-verified-input.krt", args.source.resolve())]
            for _, path in bootstrap_inputs:
                if not str(path).startswith("/home/") or any(len(part.encode()) > 31 for part in path.parts[1:]):
                    raise RuntimeError("Use a frozen /home source path with components <=31 bytes for exact guest identity")
            header = work / "bootstrap-inputs.h"
            header.write_text("#define KRT_BOOTSTRAP_INPUTS 1\nstatic const char *krt_bootstrap_sources[]={" +
                ",".join(json.dumps(str(path)) for _, path in bootstrap_inputs) + ",0};\n" +
                "static const char *krt_bootstrap_packed[]={" +
                ",".join(json.dumps("/src/" + name) for name, _ in bootstrap_inputs) + ",0};\n")
        compiler_options = ["-include", str(work / "bootstrap-inputs.h")] if bootstrap_inputs else []
        obj, binary = work / "init.o", work / "init"
        run(["gcc", "-m64", "-mno-red-zone", "-msse2", "-fcf-protection=none",
             "-fno-pic", "-fno-pie", "-ffreestanding", "-fno-stack-protector",
             "-std=gnu11", "-Wall", "-Wextra", "-O2", "-I" + str(args.minios / "sdk/include"),
             *compiler_options, "-c", init_source, "-o", obj], "init-compile.log")
        run(["ld", "-n", "-z", "max-page-size=0x1000", "--no-warn-rwx-segments",
             "-T", args.minios / "sdk/user.ld", "-o", binary,
             args.sdk_build / "sdk/crt0.o", obj, args.sdk_build / "sdk/libminios.a"], "init-link.log")
        packs = [f"init={binary}", f"krtc={args.krtc}", f"ArkLink={args.arklink}"]
        fixtures = ["answer.krt", "bad.krt", "vm.krt"] if args.mode == "smoke" else (["bad-name.krt", "bad-type.krt", "bad-parse.krt"] if args.mode == "bootstrap" else [])
        for name in fixtures:
            path = FIXTURES / name
            report["inputs"][str(path)] = sha(path)
            packs.append(f"{name}={path}")
        if args.mode == "bootstrap":
            for name, path in [*bootstrap_inputs, ("stage2-final.kro", args.stage2_kro.resolve())]:
                report["inputs"][str(path)] = sha(path)
                packs.append(f"{name}={path}")
        initrd, serial = work / "initrd.img", work / "serial.log"
        run([sys.executable, args.mkinitrd, initrd, *packs], "initrd.log")
        command = [args.qemu, "-m", args.memory, "-no-reboot", "-display", "none",
                   "-monitor", "none", "-serial", f"file:{serial}", "-net", "none",
                   "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04", "-kernel", str(args.kernel),
                   "-initrd", str(initrd), "-accel", args.accel, "-smp", "2", "-cpu",
                   "host" if args.accel == "kvm" else "qemu64"]
        (work / "qemu-command.json").write_text(json.dumps(command, indent=2))
        start = time.monotonic()
        with (work / "qemu.log").open("w") as log:
            proc = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
            (work / "qemu.pid").write_text(str(proc.pid) + "\n")
            try:
                proc.wait(timeout=args.timeout)
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
        report["seconds"] = time.monotonic() - start
        report["qemu_returncode"] = proc.returncode
        text = serial.read_text(errors="replace") if serial.exists() else ""
        expected = ["PORT-SMOKE-PASS", "VM-SERVICE-PASS", "minios-answer: 42"] if args.mode == "smoke" else (["BOOTSTRAP-RAW-BYTES-PASS", "BOOTSTRAP-DIAGNOSTICS-PASS"] if args.mode == "bootstrap" else ["SYS-RUNTIME-PASS", "minios-answer: 42"])
        report["expected_markers"] = {marker: marker in text for marker in expected}
        report["inputs_unchanged"] = all(sha(Path(path)) == value for path, value in report["inputs"].items())
        report["status"] = "passed" if proc.returncode == 1 and all(report["expected_markers"].values()) and report["inputs_unchanged"] else "failed"
        save()
        print(work / "result.json")
        return 0 if report["status"] == "passed" else 1
    except Exception as error:
        report["status"] = "failed"
        report["error"] = str(error)
        save()
        print(error, file=sys.stderr)
        return 1

if __name__ == "__main__":
    raise SystemExit(main())
