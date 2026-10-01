# Native projects

`SelfHost/krtc` runs the Python build driver and the verified native Stage 2
compiler. It never falls back to Re.KrtC. Build Stage 2 with
`python3 Test/SelfHost/Bootstrap.py` before using the default compiler.

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
commands. `-O0` is supported; higher optimization levels return an error.

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
configuration, output path, include paths, native compiler, linker, and build
driver hashes. A changed dependency or tool rebuilds; `--rebuild` forces a build.
Missing or modified output is restored from the matching cached artifact.

Compilation and linking use temporary output. A failed compile or link preserves
the previous executable. `check` runs native parsing, binding, lowering and code
generation without linking or publishing an output. `clean` removes the recorded
build output only when its bytes still match the successful artifact, and then
removes this project's cache. It preserves a file the user has replaced.

Executable `console`, `exe`, and `system` projects compile their combined source
module and link an ELF executable. Each independently compilable source also
gets a native `obj/<source-path>.kro`; imports are included in that source's
object. These objects and the final output are cached, restored, and cleaned.
A source that requires declarations from the combined project context is
reported as `object deferred`, with the compiler diagnostic retained in the
cache manifest. The combined build must still succeed; deferred objects are
never substituted into its link.

`new library`, `Type("library")`, and `Type("lib")` produce a `.kro` instead of
an executable. Sources with no `main` have no startup or process-exit stub.
Function libraries export stable `_KRT1$` names containing namespace, declaring
class, parameter types, `ref` markers, and return type. Overloads can live in
separate objects. Generic specializations, generic-owner methods, and functions
whose signatures contain class/interface types remain local with unique names;
their source declarations must be available in the compiling module. Data-only units emit real data sections
and symbols, including 128-bit scalar literals. Scalar integer constant
expressions and casts are evaluated with their declared widths. Template-only
units can produce valid empty objects.

An `extern` declaration supplies the type contract for a function in another
native KRO object. Direct calls and function addresses produce real undefined
symbols and PC-relative relocations. The linker resolves every input object;
a missing or incompatible signature is a link error. Duplicate strong `_KRT1$`
definitions are also link errors, independent of input order; ArkLink retains
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

The native ABI supports integer widths through 128 bits, floating-point values,
`ref`, primitive arrays, strings, function pointers, and static methods with
these signatures. Primitive exceptions unwind across linked calls, including
`catch`, `finally`, catch-all, and rethrow. Integer exception matching preserves
declared bit width, so `int30` does not match `int32`. Class/interface-containing extern signatures,
class/interface/enum throws and typed catches in library mode, generic extern templates,
shared libraries, and library functions using mutable global storage are
rejected. Class identities currently belong to their source module.

Data objects with dynamic, floating-point, object, array, or string initializers
are not supported. These objects use the native compiler's calling convention;
they are not compatible with C/foreign-function ABIs or Re.KrtC object symbols. Package downloads and arbitrary shell build hooks are not part of this
configuration language.

## Verification

```sh
python3 -m unittest Test.SelfHost.test_project Test.SelfHost.test_driver Test.SelfHost.test_native_linking
python3 Test/SelfHost/RunStandardLibrary.py --compiler build/selfhost/stage2/program
```

The standard-library runner executes the existing Re.KrtC library contract tests
through SelfHost at `-O0`, records the compiler and library source hashes, and
exits with failure if any contract fails. Its `report.json` and `tests.log` keep
unsupported language features and diagnostic or artifact-format differences
visible.
