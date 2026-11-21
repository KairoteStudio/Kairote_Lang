# MiniOS native x86-64 target

`--platform minios-x64` selects MiniOS; `linux-x64` retains Linux. A Linux-built compiler defaults to Linux; a
MiniOS-built compiler defaults to MiniOS.
The platform is independent of the artifact (`-c`, `target exe|ir|asm|kro`).
MiniOS native code uses `int 0x80`, argument four in RCX, EXIT=0, MMAP=49 and
MUNMAP=50. Lowercase `syscall()` remains target-native: user numbers are not
silently rewritten. `__krt_target_minios()` is a compile-time native-target
boolean; the VM retains its virtual Linux service model and reports false.
C Stage0 recognizes this query as false on its existing platforms.

Compiler host services are centralized in `PlatformServices.krt`. Host identity
is fixed when the compiler is built, not changed by `--platform`; a Linux
cross-compiler continues to use Linux host files/processes. MiniOS uses real
file/stat/directory/map/fork/exec/wait services. ArkLink launch uses SPAWN_ARGS=76
and versioned ARG1 blocks (512 tokens / 65536 bytes), preserving empty, UTF-8 and
space-containing argument strings. Native ELF entry takes command-block and
environment pointers, **not** POSIX argc/argv. Incoming registers survive module
initialization. Structured and legacy command lines are read without mutation.

`System.Sys.Syscall` retains its POSIX/Linux-shaped service numbering and
translates the implemented subset on MiniOS. It maps PRIVATE/SHARED/FIXED/ANON
flags explicitly; unsupported extensions return ENOSYS, not silent success.
MiniOS does not support anonymous shared mappings and returns EINVAL. Compiler
allocations use the native map/unmap boundary in both stack and SSA emitters.

## Reproducible isolated build

Build a current C Stage0 and host ArkLink from this branch; no historical/private
seed is required. Necessary Stage0 fixes preserve class-reference null semantics
and bool comparison results while retaining raw-pointer nullability and named
type mismatch checks. Stale parser references to removed lexer cache fields are
also removed. Original individual compiler modules are retained, not replaced
with an old monolithic compiler.

Use **new** artifact directories and a frozen source copy. The snapshot's absolute
path matters: KRO ABI metadata intentionally records source paths. Host stage2
and guest stage3/4 must see the same source bytes, order, options **and paths** for
raw byte equality. No output metadata is stripped or normalized in the test.
The example source root is directly below the user's home to satisfy RAMFS's
31-byte component / 8-level depth limit; choose a new short name if it exists.

```sh
export V="$HOME/krt-minios-validation"       # new output root
export SNAP="$HOME/krt-minios-src"          # new frozen source root
export MINIOS="$HOME/minios-m"
mkdir "$V" "$SNAP"
cp -R SelfHost "$SNAP/SelfHost"
cmake -S . -B "$V/host-build"
cmake --build "$V/host-build" -j2
python3 Test/SelfHost/build_minios_target.py \
  --seed "$V/host-build/Re.KrtC/KrtC" --linker "$V/host-build/ArkLink/ArkLink" \
  --source-root "$SNAP" --work "$V/target"
MINIOS_SDK_BUILD="$V/sdk" bash ArkLink/build_minios.sh "$V/guest/ArkLink"
# Existing MiniOS configuration knob, independent BUILD: no default images written.
# Self-bootstrap publishes ~9 MiB files and needs more than its default 8 MiB heap.
make -C "$MINIOS" BUILD="$V/kernel" CXX='g++ -DMINIOS_KHEAP_MIB=128' "$V/kernel/minios.elf"
export KAIROTE_ROOT="$PWD"
export KRTC="$V/host-build/Re.KrtC/KrtC"
export SELFHOST_COMPILER="$V/target/host-native"
export ARKLINK="$V/host-build/ArkLink/ArkLink"
python3 -m unittest -v Test.SelfHost.test_minios_target Test.SelfHost.test_minios_stage0
```

The builder records seed/source/artifact hashes and exact commands. C Stage0's
joined seed input is only a bootstrap input; Linux-native and MiniOS-native
compilation use the unchanged individual source modules. **Build success alone
is not a guest or self-bootstrap pass.** Run the actual no-disk guest gates:

```sh
# Common options for each fresh acceptance work directory (Bash array).
COMMON=(--kernel "$V/kernel/minios.elf" --mkinitrd "$MINIOS/tools/mkinitrd.py"
  --arklink "$V/guest/ArkLink" --minios "$MINIOS" --sdk-build "$V/sdk")
python3 Test/SelfHost/run_minios_acceptance.py "${COMMON[@]}" --mode smoke \
  --krtc "$V/target/krtc" --work "$V/smoke"
python3 Test/SelfHost/run_minios_acceptance.py "${COMMON[@]}" --mode bootstrap \
  --krtc "$V/target/krtc" --source-root "$SNAP" --stage2-kro "$V/target/minios.kro" \
  --work "$V/bootstrap"
"$SELFHOST_COMPILER" --platform minios-x64 --linker "$ARKLINK" \
  Test/SelfHost/minios/runtime.krt -o "$V/runtime"
python3 Test/SelfHost/run_minios_acceptance.py "${COMMON[@]}" --mode runtime \
  --krtc "$V/runtime" --work "$V/runtime-test"
```

The runner attaches **no disk drives**, uses a serial-only private VM, writes a
PID/log/result manifest, checks immutable inputs, bounds runtime and reaps its
own VM. KVM is default; use `--accel tcg --timeout 1800` if needed. It checks QEMU's
debug-exit encoding as well as required serial markers, not just the shell exit.
Bootstrap copies the packed sources to the identical absolute RAM paths before
compiling. Full KRO **and ELF** bytes must match stage2→3 and stage3→4. Unknown
name, type mismatch and syntax-error diagnostics must have the same nonzero exit
and complete nonempty diagnostic bytes across all three generations.
Smoke checks guest version, direct and compiler-launched linking, output 42,
real file IO, argv boundaries and bad input. Runtime additionally verifies
formatted string ownership, fork/exec argv and MAP_FIXED / true file-backed
MAP_SHARED visibility across fork, plus unsupported/error boundaries.

Host regressions are the normal `Bootstrap.NATIVE_DRIVER_MODULES` suite with the
explicit environment above, plus `Test/StandardLibrary` (four optimization levels;
set `KRT_STDLIB_LINKER="$ARKLINK"` for its native compiler). The full-compiler
single-translation-unit backend fixture has a bounded 180-second compile deadline;
its runtime, invalid IR, ABI and byte-behavior assertions are unchanged.

## Capability limits

- `/home` and compilation temporaries are RAM-backed and lost at shutdown.
- RAMFS filenames cannot contain spaces and components are at most 31 bytes.
  **Argument strings** may contain spaces, be empty, or contain UTF-8.
- RAMFS lacks chmod/readlink; publication preserves native RAM permissions.
  Unsupported services return errors. Missing POSIX metadata is not a real mtime.
- The native compiler/linker run in MiniOS; Python is used only on the host to
  build/package/launch the test fixture, not to compile inside the guest.
- This target does not add drivers, persistence, GUI/IME features or fix the
  separate MiniOS shutdown-dialog host test. Linux execution is regressed;
  Windows guest execution is not certified by these tests.

## Legacy C Stage0 standard-library control (not green)

An additional probe runs the 59 standard-library tests directly through C
Stage0, rather than through the native compiler they normally validate. It has
48 failing optimization subcases. An isolated, completely unmodified 81447f6
source/compiler/library control produces the same failed test IDs and assertion
results (temporary directory names aside). These are retained as failures, not
skipped, and are not a claim that the C standard library is fully supported.
The native compiler's standard-library suite passes. The directly relevant C
Stage0 class-reference, target-query, pointer rejection and scalar-width probes
are separate executable tests. Keep the C control logs when assessing release
readiness; a zero-new-regression comparison is not a zero-failure certificate.

## Host VM lifecycle baseline comparison

The final host suite ran 463 test methods, with two failing VM subcases of the
same 25,000-iteration generator lifecycle test (O0/O2): DONE was not received
within the unchanged 120-second deadline. Native subcases passed their memory
bounds. Serial, isolated reruns reproduced exactly the same two failures.

An 81447f6 source control reproduced the same failed subcases and assertion
results under the same deadline. The entirely untouched baseline compiler
cannot build natively because ParserCommit references three nonexistent lexer
cache fields. The buildable control removes only that single stale parser line
(the same prerequisite fix in this port); every VM runtime source file and the
lifecycle test itself remain byte-identical to 81447f6. This distinction matters:
it is not a claim that an entirely untouched full baseline compiler built.

These are baseline-equivalent failures, not a green 463-test certificate or a
claim that the slow lifecycle case was fixed. No runtime deadline, iteration
count, memory bound, or behavior assertion was relaxed. Other exercised gates
include the passing 59-method native standard library, 32 directly related C
Stage0 regressions, real guest compilation/IO/argv/VM services, and raw stage2/3/4
KRO+ELF equality with nine repeated error diagnostics. Windows execution remains
outside this validation coverage.
