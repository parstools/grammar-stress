# grammar-stress

Exhaustive tests of [Agas](https://github.com/parstools/borsuk) `.ag` grammars on
thousands of real source files. This repository holds the tools which run the grammars
and show what they still reject, and in `grammars/` the grammars growing on those files:
`c90.ag` on sources without includes, and later `cGNU.ag` on everything. A finished
grammar goes to Borsuk. Source files are not copied here: the tools take a path to
them, such as the test suite of a gcc checkout.

C comes first, and C++ sources can already be prepared. The tools are meant to take
other languages later.

## Build

Requires CMake, a C++20 compiler and a POSIX system.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Prepare the sources

Checking and preprocessing takes far longer than parsing, so it is done once:

```sh
build/bin/grammar-stress prepare path/to/gcc/gcc/testsuite --exclude-dir gcc.target \
    --out results/lists/c --preprocessed-out /tmp/grammar-stress/c
```

`gcc.target` and `g++.target` are worth leaving out: they test particular processors,
largely with `asm`, intrinsics and headers of that architecture, and they hold over half
of the C files.

Each source goes to the first standard which the compiler accepts with
`-pedantic-errors`, so a file belongs to C99 only when it is not C90. The lists are
`<std>.txt`, and also `too-big`, `skipped` and `none`. The preprocessed text goes to
`/tmp/grammar-stress/c/<std>/<source>.i`, line markers included.

`/tmp` is usually in memory, so its content ends with a restart. The lists stay, and
restore the files much faster, as this only preprocesses, without checking anything:

```sh
build/bin/grammar-stress prepare path/to/gcc/gcc/testsuite \
    --from-lists results/lists/c --preprocessed-out /tmp/grammar-stress/c
```

`--language c++` takes `*.C`, `*.cc`, `*.cpp` and `*.cxx` with g++ and the C++ standards.

A source is judged with its comments removed by `gcc -fpreprocessed -dD -E`, with macros
still unexpanded. A grammar never sees comments, so `//` should not keep a file out of
C90, while `gcc -std=c90` would read it as two divisions.

Some test files are made to be enormous, and only waste time. `--max-bytes` (200000 by
default) finds them by what they produce after preprocessing, not counting system
headers. `--skip-list` skips the files which it names, relative to the given directory,
before any work; `lists/` has the known ones.

## Run a grammar

Build a parser package with `agas` from Borsuk, then run it over prepared files:

```sh
agas --emit-package /tmp/c90 grammars/c90.ag

build/bin/grammar-stress run /tmp/grammar-stress/c/c90 --preprocessed \
    --agas path/to/agas --package /tmp/c90 --out results/c90 --pure
```

While a grammar changes, `--sample 10` takes about every tenth file for a quick pass,
chosen by path and `--seed`. A pass over all files comes when the sample is clean.

Without `--preprocessed`, `run` takes sources and calls the compiler itself, in the
mode given by `--std` (`c90` by default) and `--no-pedantic`.

Each file gets one category in `results.tsv`, and `summary.txt` counts them together
with the tokens at which parsing stopped:

| Category | Meaning |
|---|---|
| `parsed` | the grammar accepts the preprocessed file |
| `rejected` | syntax error; the report gives its line, column, token and source line |
| `lexer-error` | the grammar has no token for some input, such as bytes outside UTF-8 |
| `too-big` | not parsed, as the file produces more than `--max-bytes` |
| `skipped` | named in `--skip-list` |
| `not-accepted-by-compiler` | the compiler rejects the source in the requested mode, so it says nothing about the grammar |
| `has-include`, `has-gnu-words` | left out by `--pure` |
| `preprocessor-failed`, `agas-error`, `timeout` | the file could not be judged |

`--pure` keeps files without includes and without words starting with `__` after
preprocessing. `-pedantic-errors` alone does not reject `__attribute__` or `__asm__`,
so a file which passes it is not yet pure C90.
