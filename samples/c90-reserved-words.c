/*
 * What gcc -std=c90 -pedantic-errors accepts beyond plain C90: forms with words
 * starting with __, which are reserved for the compiler. Being outside the norm, they
 * belong to cGNU.ag and not to c90.ag, which is plain C90. __extension__ is in gnu-only.c.
 *
 *     gcc -std=c90 -pedantic-errors -c c90-reserved-words.c
 *
 * No macros or includes: every form stands for itself.
 */

/* Attributes. */

int attribute_after_function(void) __attribute__((noinline));
__attribute__((unused)) int attribute_before_type;
int __attribute__((unused)) attribute_after_type;
int attribute_short_form __attribute((unused));
int * __attribute__((unused)) attribute_on_pointer;
int attribute_list __attribute__((unused, aligned(8)));
int attribute_empty __attribute__(());
int attribute_arguments(const char *format, ...) __attribute__((format(printf, 1, 2)));
int attribute_const_name(void) __attribute__((const, __const__));
int attribute_sizeof __attribute__((aligned(sizeof(int) * 2)));
int attribute_alignof __attribute__((aligned(__alignof__(long))));
int attribute_first, __attribute__((unused)) attribute_after_comma;

struct __attribute__((packed)) AttributeAfterStruct { int a; } attribute_struct_object;
struct AttributeAfterBody { int a; } __attribute__((packed));
struct AttributeMember {
    int a __attribute__((aligned(8)));
    int b : 3 __attribute__((packed));
    __attribute__((unused)) int c;
};
enum __attribute__((packed)) AttributeEnum { ATTRIBUTE_ENUMERATOR __attribute__((deprecated)) = 1 };
typedef int AttributeTypedef __attribute__((aligned(8)));
typedef struct { int a; } __attribute__((aligned(8))) AttributeStructTypedef;

int attribute_parameter(int a __attribute__((unused)), int __attribute__((unused)) b);

void attribute_statements(int a)
{
    switch (a) {
    case 1:
        a = 2;
        __attribute__((fallthrough));
    default:
        break;
    }
}

/* asm. */

int asm_label(void) __asm__("renamed_function");
int asm_label_short(void) __asm("renamed_short");
int asm_label_on_object __asm__("renamed_object");
int asm_label_strings __asm__("renamed" "_strings");
int asm_label_and_attribute(void) __asm__("renamed_both") __attribute__((noinline));

__asm__("");
__asm("");

void asm_statements(int input)
{
    int output;

    __asm__("");
    __asm("");
    __asm__("" "");
    __asm__ __volatile__("");
    __asm__ __volatile("");
    __asm__("" : "=r" (output));
    __asm__("" : "=r" (output) : "0" (input));
    __asm__("" : "=r" (output) : "0" (input) : "memory");
    __asm__("" : : "r" (input));
    __asm__("" : : : "memory", "cc");
    __asm__("" : [out] "=r" (output) : [in] "r" (input));
    __asm__ __inline__("");
}

void asm_goto(void)
{
    __asm__ goto("" : : : : done);
done:
    ;
}

/* Other spellings of keywords. */

__const int spelled_const = 1;
__const__ int spelled_const_long = 1;
__volatile int spelled_volatile;
__volatile__ int spelled_volatile_long;
void spelled_restrict(int * __restrict a, int * __restrict__ b);
__signed int spelled_signed;
__signed__ int spelled_signed_long;
static __inline int spelled_inline(void) { return 0; }
static __inline__ int spelled_inline_long(void) { return 0; }
__thread int spelled_thread;

/* Types. */

__float128 type_float128;
__builtin_va_list type_va_list;
__int128_t type_int128;
__uint128_t type_uint128;

/* Operators. */

int operator_value;
__typeof__(operator_value) typeof_of_expression;
__typeof(operator_value) typeof_short;
__typeof__(int *) typeof_of_type;
int alignof_of_type = __alignof__(int);
int alignof_short = __alignof(int);
int alignof_of_object = __alignof__ operator_value;

void auto_type(void)
{
    __auto_type value = 1;
    (void) value;
}

/* Builtins, some of them with a type as an argument. */

struct BuiltinStruct { int a; int b; };

int builtins(__builtin_va_list list)
{
    int value = __builtin_va_arg(list, int);
    value += __builtin_offsetof(struct BuiltinStruct, b);
    value += __builtin_types_compatible_p(int, long);
    value += __builtin_choose_expr(1, 2, 3);
    value += __builtin_strlen("abc");
    if (value < 0)
        __builtin_abort();
    return value;
}

/* Names which are keywords only in other modes. */

int asm;
int typeof;
int inline;
int restrict;

/* A universal character name in a character constant. */

int universal_character = 'ä';
