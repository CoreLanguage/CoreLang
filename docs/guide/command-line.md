# Command line

`core` with no arguments prints a command list. `core <command> --help`
prints per-command help. This page documents all of it with the
details that matter.

| Command | What it does |
|---------|--------------|
| `core init [name]` | scaffold a project: `core.toml`, `core.lock`, `src/main.cr`, `tests/`, `README.md`. Refuses to overwrite an existing `core.toml` |
| `core compile <out> <file.cr>` | compile one file (plus its imports) to a named executable |
| `core build` | build the project in the current directory from `core.toml` |
| `core run [args...]` | build, then run; args pass to the program |
| `core test` | compile and run every `tests/*.cr` file |
| `core check [file.cr]` | type-check without generating code; in a project, checks the main source |
| `core install <pkg>` | add a git package to `[dependencies]` and pin it in `core.lock` |
| `core remove <pkg>` | remove a package |
| `core update` | re-resolve dependencies to the newest compatible versions |
| `core list` | list dependencies |
| `core emit-ir <file>` | print the generated LLVM IR |
| `core emit-asm <file>` | print the generated assembly |
| `core version` | print the compiler version and LLVM backend version |

## Flags

Apply to `compile`, `build`, `run`, `test`:

| Flag | Meaning |
|------|---------|
| `-O0` (default) `-O1` `-O2` `-O3` `-Os` | optimization level; `-Os` optimizes for size |
| `--debug` / `-g` | emit DWARF debug info; GDB breakpoints resolve to `.cr` lines |
| `--target=<triple>` | cross compile: `x86_64`, `aarch64`, `riscv64`. Emits a relocatable object; linking needs a target toolchain |
| `--freestanding` | no OS runtime, no libc; custom entry point (kernels, bare metal) |
| `--emit-object` | stop after the object file, skip linking |
| `--force`, `-f` | overwrite an existing output file |
| `--link=<lib>` | link a library (`-l<lib>`), repeatable |
| `--link-arg=<arg>` | raw linker argument, repeatable |
| `--lib-path=<dir>` | library search path (`-L<dir>`), repeatable |
| `--entry=<name>` | entry symbol for `--freestanding` (default `main`) |
| `--help`, `-h` | help for the command |

## Behaviors worth knowing

- **No silent overwrites.** `compile` refuses to overwrite an existing
  output file (or its intermediate object) unless you pass `--force`.
  `build`/`run`/`test` overwrite their own artifacts freely; they are
  repeatable by design.
- **Whole-program builds.** Every build recompiles everything the entry
  imports; there is no incremental compilation or object cache.
- **Build artifacts.** The object file (`<name>.coreobj.o`) is kept next
  to the output. `core test` compiles each `tests/*.cr` as its own
  program (each defines `main()`), runs it, and reports
  `N/M test files passed`; a nonzero exit counts as failure, so use
  `assert` in tests.
- **Linking.** The final link always includes the Core runtime and
  `-lm -lpthread -ldl -latomic`, plus project `[build]` settings and
  your `--link*` flags.
- `compile` takes the output name and entry file positionally:
  `core compile mycoolbinary main.cr`. The output name is exact, no
  extension is added.
- `emit-ir` / `emit-asm` print to stdout and stop before linking; they
  are the fastest way to see what the optimizer did to your code.

## Examples

```console
core compile server src/main.cr        # project file, standalone binary
core compile demo main.cr -O2 --debug  # optimized, debuggable in gdb
core build -O3                         # project build, optimized
core run --verbose                     # everything after "run" goes to the program
core test                              # run the test suite
core compile kernel.elf main.cr --freestanding --target=x86_64 --emit-object
```
