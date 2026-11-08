#!/usr/bin/env python3
"""Show which forms outside plain C90 each gcc mode and each grammar accepts.

usage: compare_forms.py AGAS C90_PACKAGE CGNU_PACKAGE

Every form is a small complete translation unit without macros or includes, so
that what gcc reports is about the form itself and not about its origin.
"""
import os
import subprocess
import sys
import tempfile

MODES = ["-std=c90 -pedantic-errors", "-std=c90", "-std=c99 -pedantic-errors", "-std=gnu90"]

FORMS = [
    # Words with two underscores.
    ("__attribute__ on a function", "int f(void) __attribute__((noinline));"),
    ("__attribute__ before a type", "__attribute__((unused)) int x;"),
    ("__attribute__ on a struct", "struct __attribute__((packed)) S { int a; } s;"),
    ("__attribute (short form)", "int x __attribute((unused));"),
    ("__asm__ statement", "void f(void) { __asm__(\"\"); }"),
    ("__asm statement", "void f(void) { __asm(\"\"); }"),
    ("__asm__ with operands", "void f(int a) { __asm__(\"\" : : \"r\" (a)); }"),
    ("__asm__ at file scope", "__asm__(\"\");"),
    ("__asm__ label on a declarator", "int f(void) __asm__(\"g\");"),
    ("__extension__ on a declaration", "__extension__ int x;"),
    ("__extension__ in an expression", "int x = __extension__ 1;"),
    ("__const", "__const int x = 1;"),
    ("__const__", "__const__ int x = 1;"),
    ("__volatile__", "__volatile__ int x;"),
    ("__volatile", "__volatile int x;"),
    ("__restrict", "void f(int * __restrict p);"),
    ("__restrict__", "void f(int * __restrict__ p);"),
    ("__inline", "static __inline int f(void) { return 0; }"),
    ("__inline__", "static __inline__ int f(void) { return 0; }"),
    ("__signed__", "__signed__ int x;"),
    ("__signed", "__signed int x;"),
    ("__thread", "__thread int x;"),
    ("__typeof__", "int a; __typeof__(a) b;"),
    ("__typeof", "int a; __typeof(a) b;"),
    ("__alignof__", "int x = __alignof__(int);"),
    ("__alignof", "int x = __alignof(int);"),
    ("__real__ and __imag__", "double _Complex z; double r = 0; void f(void) { r = __real__ z + __imag__ z; }"),
    ("__complex__", "__complex__ double z;"),
    ("__int128", "__int128 x;"),
    ("__float128", "__float128 x;"),
    ("__auto_type", "void f(void) { __auto_type x = 1; (void) x; }"),
    ("__label__", "void f(void) { __label__ a; a: ; }"),
    # Builtins.
    ("__builtin_abort call", "void f(void) { __builtin_abort(); }"),
    ("__builtin_va_list", "__builtin_va_list x;"),
    ("__builtin_va_arg", "int f(__builtin_va_list a) { return __builtin_va_arg(a, int); }"),
    ("__builtin_offsetof", "struct S { int a; }; int x = __builtin_offsetof(struct S, a);"),
    ("__builtin_types_compatible_p", "int x = __builtin_types_compatible_p(int, long);"),
    ("__builtin_choose_expr", "int x = __builtin_choose_expr(1, 2, 3);"),
    # GNU syntax without special words.
    ("statement expression", "int f(void) { return ({ 1; }); }"),
    ("conditional without middle", "int f(int a) { return a ?: 1; }"),
    ("label address", "void *f(void) { a: return &&a; }"),
    ("computed goto", "void f(void *p) { goto *p; }"),
    ("case range", "void f(int a) { switch (a) { case 1 ... 2: ; } }"),
    ("nested function", "void f(void) { void g(void) { } g(); }"),
    ("empty struct", "struct S { };"),
    ("empty initializer", "int a[1] = { };"),
    ("designator without =", "int a[2] = { [1] 2 };"),
    ("old designator", "struct S { int a; } s = { a: 1 };"),
    ("cast to union", "union U { int a; }; union U f(int x) { return (union U) x; }"),
    ("zero length array", "int a[0];"),
    ("dollar in a name", "int a$b;"),
    ("escape \\e", "int x = '\\e';"),
    # Bare words which are keywords in other modes.
    ("asm statement", "void f(void) { asm(\"\"); }"),
    ("inline function", "static inline int f(void) { return 0; }"),
    ("typeof", "int a; typeof(a) b;"),
    ("restrict", "void f(int * restrict p);"),
    ("asm as a name", "int asm;"),
    ("inline as a name", "int inline;"),
    ("typeof as a name", "int typeof;"),
    ("restrict as a name", "int restrict;"),
    # Forms of C99 and later.
    ("line comment", "int x; // comment\n"),
    ("declaration after statement", "void f(void) { int a; a = 1; int b = a; (void) b; }"),
    ("for declaration", "void f(void) { for (int i = 0; i < 1; i++) ; }"),
    ("designated initializer", "struct S { int a; } s = { .a = 1 };"),
    ("array designator", "int a[2] = { [1] = 2 };"),
    ("compound literal", "int *p = (int[]) { 1 };"),
    ("long long", "long long x;"),
    ("LL suffix", "long x = 1LL;"),
    ("_Bool", "_Bool x;"),
    ("_Complex", "double _Complex z;"),
    ("variable length array", "void f(int n) { int a[n]; (void) a; }"),
    ("static in array parameter", "void f(int a[static 1]);"),
    ("hex float", "double x = 0x1p3;"),
    ("trailing comma in enum", "enum E { A, };"),
    ("universal character name", "int x = '\\u00e4';"),
    ("_Static_assert", "_Static_assert(1, \"x\");"),
    ("_Generic", "int x = _Generic(1, int: 2);"),
    ("_Alignas", "_Alignas(8) int x;"),
    ("_Alignof", "int x = _Alignof(int);"),
    ("_Atomic", "_Atomic int x;"),
    ("_Noreturn", "_Noreturn void f(void);"),
    ("_Thread_local", "_Thread_local int x;"),
    ("[[attribute]]", "[[deprecated]] int x;"),
    ("bool", "bool x;"),
    ("digraph", "int a<:1:>;"),
    ("binary constant", "int x = 0b1;"),
    ("label at end of block", "void f(void) { a: }"),
    ("declaration after label", "void f(void) { a: int b; (void) b; }"),
    ("enum with a type", "enum E : int { A };"),
]


def accepted(command, input_path):
    return subprocess.run(command + [input_path], capture_output=True, timeout=30).returncode == 0


def main():
    agas, c90_package, cgnu_package = sys.argv[1:4]
    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, "form.c")
        print("\t".join(["form", *MODES, "c90.ag", "cGNU.ag"]))
        for name, source in FORMS:
            with open(path, "w", encoding="utf-8") as output:
                output.write(source + "\n")
            row = [accepted(["gcc", "-x", "c", *mode.split(), "-fsyntax-only"], path) for mode in MODES]
            row.append(accepted([agas, "--ast-stats", c90_package], path))
            row.append(accepted([agas, "--ast-stats", cgnu_package], path))
            print("\t".join([name, *("yes" if value else "-" for value in row)]))


main()
