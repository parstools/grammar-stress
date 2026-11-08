/*
 * What gcc accepts only outside -std=c90 -pedantic-errors: GNU syntax without reserved
 * words, and forms of C99 and later. cGNU.ag has to accept them, and c90.ag does not.
 *
 *     gcc -std=gnu23 -c gnu-only.c
 *
 * No macros or includes: every form stands for itself.
 */

// A line comment.

/* GNU syntax. */

int statement_expression(void) { return ({ int a = 1; a + 1; }); }
int conditional_without_middle(int a) { return a ?: 1; }
void *label_address(void) { here: return &&here; }
void computed_goto(void *target) { goto *target; }
void case_range(int a) { switch (a) { case 1 ... 2: break; default: ; } }

void nested_function(void)
{
    int inner(void) { return 1; }
    inner();
}

void local_label(void) { __label__ out; goto out; out: ; }
struct EmptyStruct { };
int empty_initializer[1] = { };
int designator_without_equals[2] = { [1] 2 };
struct OldDesignator { int a; } old_designator = { a: 1 };
int range_designator[4] = { [1 ... 2] = 3 };
int zero_length_array[0];
int dollar$name;
int escape_e = '\e';
__int128 type_int128;
__complex__ double type_complex;

double real_and_imaginary(__complex__ double z)
{
    return __real__ z + __imag__ z + __real z + __imag z;
}

/* __extension__ lets gcc -std=c90 -pedantic-errors accept any GNU syntax after it. */

__extension__ int extension_on_declaration;
__extension__ typedef int ExtensionTypedef;
int extension_in_expression = __extension__ 1;
struct ExtensionMember { __extension__ int a; };

int extension_in_block(void)
{
    __extension__ int inner;
    inner = __extension__ ({ 1; });
    return inner;
}

/* Words which become keywords outside strict modes. */

void asm_statement(void) { asm(""); }
static inline int inline_function(void) { return 0; }
int typeof_value;
typeof(typeof_value) typeof_copy;
void restrict_parameter(int * restrict p);

/* C99. */

void declaration_after_statement(void) { int a; a = 1; int b = a; (void) b; }
void for_declaration(void) { for (int i = 0; i < 1; i++) ; }
struct Designated { int a; int b; } designated = { .a = 1, .b = 2 };
int array_designator[2] = { [1] = 2 };
int *compound_literal = (int[]) { 1, 2 };
long long type_long_long;
long long_long_suffix = 1LL;
_Bool type_bool;
double _Complex type_c99_complex;
void variable_length_array(int n) { int a[n]; (void) a; }
void array_qualifiers(int a[static 1], int b[const 2], int c[*]);
double hex_float = 0x1p3;
enum TrailingComma { TRAILING_FIRST, };

/* C11. */

_Static_assert(1, "file scope");
int generic_selection = _Generic(1, int: 2, default: 3);
_Alignas(8) int aligned_object;
int alignof_operator = _Alignof(int);
_Atomic int atomic_qualifier;
_Atomic(int) atomic_specifier;
_Noreturn void no_return(void);
_Thread_local int thread_local_object;

/* C23. */

[[deprecated]] int standard_attribute;
[[gnu::unused]] int scoped_attribute;
bool c23_bool;
int binary_constant = 0b101;
int digit_separator = 1'000;
void label_at_end(void) { a: }
void declaration_after_label(void) { a: int b; (void) b; }
enum TypedEnum : int { TYPED_FIRST };
int variadic_only(...);
_BitInt(8) bit_precise;

/* Digraphs. */

int digraph_array<:1:> = <% 1 %>;
