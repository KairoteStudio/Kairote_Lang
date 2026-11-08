# Native projects

`SelfHost/krtc` directly executes the native Stage 2 compiler. Its Kairote driver
implements source loading, diagnostics, project configuration, caching, atomic
output publication and linker invocation. Build Stage 2 with
`python3 Test/SelfHost/Bootstrap.py` before using the default compiler.
Python is used by test and bootstrap harnesses only; project commands also work
by calling `build/selfhost/stage2/program` directly.

```sh
./SelfHost/krtc new console /tmp/hello-kairote
./SelfHost/krtc build /tmp/hello-kairote
/tmp/hello-kairote/bin/release/hello-kairote
./SelfHost/krtc check /tmp/hello-kairote
./SelfHost/krtc clean /tmp/hello-kairote
```

`build`, `check`, and `clean` accept a project directory, an explicit project
file, or no argument to use the current directory. `--compiler PATH` selects a
particular native compiler, and `SELFHOST_COMPILER` sets its default. `--linker
PATH`, `-I DIRECTORY`, `--config debug`, and `-o OUTPUT` are available on project
commands. `-j N`, `-jN`, and `--jobs N` choose 1–256 parallel compilation workers;
the default is 8, matching the seed project driver. `-O0` through `-O3` are supported; the default is `-O2`. `-O1` folds
integer constants and simplifies constant branches. `-O2` also removes dead SSA
values and converts safe direct self-tail calls to loops. `-O3` adds bounded
small-function inlining and repeats constant folding.

## Configuration

The Re.KrtC `Configure(Project p)` spelling works directly:

```krt
function Configure(Project p) {
    p.Name("Example");
    p.Type("console");
    p.Sources("src/**/*.krt");
    p.Includes("modules", "libs");
    p.Libraries("System.Console");
    p.Output("bin/${config}/Example");
    p.Optimization("0");
}
```

The receiverless `function Configure() { Name("Example"); Sources("main.krt"); }`
form also works. `string`, `var`, and `let` configuration variables, string
concatenation, `if`/`else`, equality and boolean conditions are supported.
`${config}`, `${os}`, `${platform}`, `${variable}` and `${env:VARIABLE}` expand
inside strings. The default configuration is `release`, or `KRT_CONFIG` when
set. Unknown variables, methods, statements and unsupported options fail with
a project diagnostic.

`project.json` provides the same configuration as data:

```json
{
  "name": "Example",
  "type": "console",
  "sources": ["src/**/*.krt"],
  "includes": ["modules"],
  "libraries": ["System.Console"],
  "output": "bin/Example",
  "optimization": 0,
  "target": "elf"
}
```

Legacy `project: Example`, `sources: main.krt helper.krt` and `output: Example`
configuration is accepted too. Project source patterns expand in sorted order;
duplicate and transitively imported files are compiled once. Relative paths
resolve from the project file. A bare output name uses `bin/linux/NAME`, matching
Re.KrtC on Linux; an output containing a directory uses that explicit project
relative path. `-o` is relative to the invoking directory.

`Libraries` loads `.krt` source modules or explicitly named `.kro` objects.
Standard libraries resolve to source, so their implementation goes through the
native compiler. `using` and quoted `import` use the ordinary module loader;
cyclic imports share declarations once. Namespace and using scopes reset at
source-file boundaries.

## Incremental builds and checks

Successful builds retain an artifact and manifest under `.krtcache`. The key
includes all explicit and imported source bytes, object bytes, expanded project
configuration, output path, include paths, native compiler and linker hashes.
A changed dependency or tool rebuilds; `--rebuild` forces a build.
Missing or modified output is restored from the matching cached artifact.

Compilation and linking use temporary output. A failed compile or link preserves
the previous executable. `check` runs native parsing, binding, lowering and code
generation without linking or publishing an output. `clean` removes the recorded
build output only when its bytes still match the successful artifact, and then
removes this project's cache. It preserves a file the user has replaced.

Executable `console`, `exe`, and `system` projects compile their combined source
module and link an ELF executable. Each independently compilable source also
gets a native `obj/<source-path>.kro`; imports are included in that source's
object. Unit compilation runs concurrently in native fork workers, without
external compiler processes or a host-language scheduler. Worker results are
collected in source order. These objects and the final output are cached,
restored, and cleaned. A worker signal, filesystem failure, or scheduling error
fails the build and preserves the previous final artifact and manifest.
A source that requires declarations from the combined project context is
reported as `object deferred`. Semantic dependency failures are distinguished
from filesystem and worker failures. The combined build must still succeed; deferred objects are
never substituted into its link.

`target ir`, `target asm`, and `target vm` publish native IR, assembly, and real
EBC bytecode artifacts respectively. VM artifacts execute with `run-vm`; see
[the bytecode format and runtime](Vm.md). `--keep-temp` and `target eo` retain
the combined KRO alongside the executable. Retained objects and VM sidecars
are recorded for cache restore and protected cleanup.

`new library`, `Type("library")`, and `Type("lib")` produce a `.kro` instead of
an executable. Sources with no `main` have no startup or process-exit stub.
Scalar function libraries export stable `_KRT1$` names containing namespace, declaring
type, parameter types, `ref` markers, and return type. Concrete struct signatures
use `_KRT2$` plus an exact ABI2 manifest. Named class/interface/enum types,
constructors, object references, and direct wide value/ref/callback boundaries
use ABI3. Overloads can live in separate objects. KRO files carry public type and
member declarations and open generic templates, so a consumer can compile from
the object without the producer's original source. Data-only and template-only
units also emit valid module objects. See [ModuleAbi.md](ModuleAbi.md) for the
canonical contracts and runtime identities.

An `extern` declaration supplies the type contract for a function in another
native KRO object. Direct calls and function addresses produce real undefined
symbols and PC-relative relocations. The linker resolves every input object;
a missing or incompatible signature is a link error. Duplicate strong `_KRT1$`,
`_KRT2$`, and `_KRT3$` definitions are link errors, independent of input order; ArkLink retains
its legacy duplicate policy for older Re.KrtC symbols. Unused prototypes do not
create a link dependency. Repeated matching prototypes, and a prototype with
its matching source definition, are accepted.

```kairote
// Math.krt
namespace Demo { int64 Add(int64 a, int64 b) { return a + b; } }
```

```kairote
// Main.krt
namespace Demo { extern int64 Add(int64 left, int64 right); }
int32 main() { return (int32)Demo.Add(19, 23); }
```

```sh
SelfHost/krtc Math.krt -c -o Math.kro
SelfHost/krtc Main.krt Math.kro -o Program
# project.krt can select the object with Libraries("Math.kro").
```

## Struct objects and ABI2

Ordinary public functions may pass and return public, concrete structs across
independently compiled objects. Consumers can read their declarations from KRO
metadata or supply matching declarations and `extern` prototypes. Closed generic struct values, nested
fixed arrays, wide fields, struct pointers, `ref` and typed callbacks use their
concrete layouts. Open generic template bodies are carried in native module
metadata and instantiated by the consumer.

Each ABI2 object carries a local read-only `_KRT_ABI2_MANIFEST` symbol. Contracts
record canonical nominal types, complete reachable struct layouts, ordered field
names and offsets, scalar precision, every array/pointer layer and its nullability, callback shapes,
value/ref/receiver modes, readonly receiver contracts and hidden result passing. Struct-pointer cycles use
nominal references. The complete graph is sorted by identity; source locations
and module-local runtime IDs do not participate.

The native driver checks these exact bytes before linking or publishing any
executable or retained object. Layout disagreement reports `E_ABI_LAYOUT`;
missing or duplicate definitions report `E_ABI_SYMBOL`. Missing, duplicate or
malformed manifest records report `E_ABI_MANIFEST`; an unknown manifest version
reports `E_ABI_VERSION`. ABI2 names contain a bounded FNV-1a fingerprint, which
is not a cryptographic digest. The exact comparison also handles collisions.
The object mapping budget is 1 GiB per input; contract and graph generation have
checked 16 MiB and 16,384-node limits.

Nonnullable scalar signatures with unchanged physical conventions preserve their
calling contract and `_KRT1$` names. Objects can also carry module metadata and
additional symbol records. Direct wide values use ABI3: their complete
16-byte payload is passed by value and wide results use caller-owned storage.
Callbacks containing these boundaries use shape version 3. Pure struct
signatures and callbacks such as `fn(Packet)->Packet` retain ABI2, including
structs with wide fields.
Explicit nullable types use distinct names; inner pointer-slot nullability is
encoded with the complete 64-bit bitmap, including inside callback signatures.
Raw data and function pointers are nonnullable by default; `?` marks the
applicable type layer. A nullable callback must be narrowed to a nonnullable
variable by an `is` type pattern before calling it. Casts cannot remove nullable
flags from the outer type or an inner pointer slot.
ArkLink checks the same exact ABI2/3 manifests when invoked directly. The driver
also performs these checks before output publication, including a `-c` command
that imports native objects.

Public ordinary methods on concrete struct owners, including closed generic
owners, may be defined in a producer and declared `extern` in the consumer.
Type-qualified addresses are unbound callbacks with an explicit ref receiver:
`fn(ref Pair)->int64 callback = &Pair.Read`. The callback receives the actual
storage through `callback(ref value)`; it does not capture an instance.
Methods returning a struct use the same hidden caller-owned result storage.

Shared declaration files can contain layouts and consumer `extern` prototypes.
Two ordinary strong definitions of a public function or mutable global are
rejected. Imported closed generic owners and their shared static storage use
exact nominal identities and weak merging; descriptor records also compare
their bytes and relocations before merging. Public class and struct constructors
can be imported through metadata or declared `extern`. VM bytecode executes
source modules together and does not load native KRO libraries.

The native ABI supports integer widths through 128 bits, floating-point values,
`ref`, arrays, strings, function pointers, named types, constructors, virtual
and interface dispatch, boxing, and closed generic types. Typed exceptions
unwind across linked calls, including `catch`, `finally`, catch-all, and rethrow.
Integer exception matching preserves declared bit width, so `int30` does not
match `int32`. Runtime type identities contain the complete nominal bytes and
generic arguments; declaration order and module-local IDs do not participate.

Mutable global variables and static fields use stable native data symbols.
Dynamic initializers run under shared guards, retry after exceptions, and also
run before an explicit `extern static` access. Closed generic static state is
shared for each exact type argument list. Native KRO is a relocatable static
module format; dynamic shared-library publication is not provided by these
project commands. These objects use the native compiler's calling convention;
they are not compatible with C/foreign-function ABIs or Re.KrtC object symbols. Package downloads and arbitrary shell build hooks are not part of this
configuration language.

## Verification

```sh
python3 -m unittest Test.SelfHost.test_project Test.SelfHost.test_driver \
  Test.SelfHost.test_native_driver Test.SelfHost.test_native_linking \
  Test.SelfHost.test_module_abi Test.SelfHost.test_generator_modules
python3 Test/SelfHost/RunStandardLibrary.py --compiler build/selfhost/stage2/program
```

The standard-library runner executes the existing Re.KrtC library contract tests
through the native SelfHost CLI at `-O0`, records compiler, launcher and source hashes, and
exits with failure if any contract fails. Its `report.json` and `tests.log` keep
unsupported language features and diagnostic or artifact-format differences
visible.
