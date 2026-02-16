#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===== Utils ===== */

#define arrlen(arr) (ssize) (sizeof(arr)/sizeof((arr)[0]))

typedef ptrdiff_t ssize;
typedef uint8_t u8;
typedef uint64_t u64;

#define KiB (ssize) (1 << 10)
#define MiB (ssize) (1 << 20)
#define GiB (ssize) (1 << 30)

u64 hash_mem(u8 *buf, ssize len) {
    u64 h = 0x100;
    for (ssize i = 0; i < len; i++) {
        h ^= buf[i];
        h *= 1111111111111111111u;
    }
    return h;
}

/* ===== Readers ===== */

#define Reader(lit) (Reader) { .buf = (u8 *) (lit), .len = arrlen(lit) - 1, }

typedef struct {
    u8 *buf;
    ssize len, i;
} Reader;

#define char_set(set) (bool [256]) { set }

#define whitespace_set \
    [' '] = 1, ['\t'] = 1, ['\n'] = 1, ['\v'] = 1, ['\f'] = 1, ['\r'] = 1,

#define alpha_lower_set \
    ['a'] = 1, ['b'] = 1, ['c'] = 1, ['d'] = 1, ['e'] = 1, ['f'] = 1, \
    ['g'] = 1, ['h'] = 1, ['i'] = 1, ['j'] = 1, ['k'] = 1, ['l'] = 1, \
    ['m'] = 1, ['n'] = 1, ['o'] = 1, ['p'] = 1, ['q'] = 1, ['r'] = 1, \
    ['s'] = 1, ['t'] = 1, ['u'] = 1, ['v'] = 1, ['w'] = 1, ['x'] = 1, \
    ['y'] = 1, ['z'] = 1,

#define alpha_upper_set \
    ['A'] = 1, ['B'] = 1, ['C'] = 1, ['D'] = 1, ['E'] = 1, ['F'] = 1, \
    ['G'] = 1, ['H'] = 1, ['I'] = 1, ['J'] = 1, ['K'] = 1, ['L'] = 1, \
    ['M'] = 1, ['N'] = 1, ['O'] = 1, ['P'] = 1, ['Q'] = 1, ['R'] = 1, \
    ['S'] = 1, ['T'] = 1, ['U'] = 1, ['V'] = 1, ['W'] = 1, ['X'] = 1, \
    ['Y'] = 1, ['Z'] = 1,

#define alpha_set alpha_lower_set alpha_upper_set

#define digit_set \
    ['0'] = 1, ['1'] = 1, ['2'] = 1, ['3'] = 1, ['4'] = 1, ['5'] = 1, \
    ['6'] = 1, ['7'] = 1, ['8'] = 1, ['9'] = 1,

#define ident_set alpha_set digit_set ['_'] = 1

bool are_reader_strs_equal(Reader a, Reader b) {
    if (a.len != b.len) return false;
    if (!a.len && !b.len) return true;
    if (!a.buf && !b.buf) return true;
    if (!a.buf || !b.buf) return false;

    for (int i = 0; i < a.len; i++) {
        if (a.buf[i] != b.buf[i]) return false;
    }

    return true;
}

ssize try_read_char_set(Reader *in, bool set[256], Reader *out) {
    ssize begin = in->i;

    for (; in->i <= in->len - 1; in->i++) {
        u8 c = in->buf[in->i];
        if (!set[c]) break;
    }

    if (out) *out = (Reader) {
        .buf = &in->buf[begin],
        .len = in->i - begin,
    };

    return in->i - begin;
}

ssize try_read_whitespace(Reader *in) {
    return try_read_char_set(in, char_set(whitespace_set), NULL);
}

ssize try_read_ident(Reader *in, Reader *out) {
    return try_read_char_set(in, char_set(ident_set), out);
}

ssize try_read_ident_str(Reader *in, Reader str) {
    Reader tmp = *in;
    Reader r = {0};
    try_read_char_set(&tmp, char_set(ident_set), &r);
    if (are_reader_strs_equal(str, r)) {
        *in = tmp;
        return str.len;
    } else return 0;
}

ssize try_read_str(Reader *in, Reader str) {
    if (str.len > in->len - in->i) return 0;

    for (ssize i = 0; i < str.len; i++) {
        if (in->buf[i + in->i] != str.buf[i]) return 0;
    }

    in->i += str.len;

    return str.len;
}

Reader read_file_malloc(const char *path) {
    Reader ret = {0};

    FILE *f = fopen(path, "rb");
    if (!f) return (Reader) {0};

    if (fseek(f, 0, SEEK_END) != 0) goto done;

    long size = ftell(f);
    if (size < 0) goto done;

    rewind(f);

    u8 *buf = malloc(size);
    if (!buf) goto done;

    if (fread(buf, 1, size, f) != (size_t) size) {
        free(buf);
        goto done;
    }

    ret = (Reader) {
        .buf = buf,
        .len = size,
    };

done:
    fclose(f);
    return ret;
}

void print_reader(Reader in) {
    printf("%.*s", (int) in.len, in.buf);
}

/* ===== Arenas ===== */

typedef struct Arena Arena;
typedef struct ArenaManager ArenaManager;
struct ArenaManager {
    Arena *buf;
    ssize cap, len;
    ssize min_arena_cap;
};

struct Arena {
    ArenaManager *manager;
    u8 *buf;
    ssize cap, len, arena_index;
};

void *arena_alloc(Arena *a, ssize size, ssize align, ssize count) {
    assert(size >= 0);
    assert(align > 0);
    assert(count >= 0);
    assert((align & (align - 1)) == 0);

    if (!a) return calloc(count, size);
    if (size == 0 || count == 0) return &a->buf[a->len];

    ssize pad = -(uintptr_t) (&a->buf[a->len]) & (align - 1);
    ssize available = a->cap - a->len - pad;
    ssize alloc_size = pad + count * size;

    if (alloc_size > available) {
        ssize wanted_arena_cap = alloc_size > a->manager->min_arena_cap
                                 ? alloc_size
                                 : a->manager->min_arena_cap;
        Arena got = {0};

        for (int i = a->arena_index + 1; i < a->manager->len; i++) {
            Arena tmp = a->manager->buf[i];
            Arena *cur = &a->manager->buf[a->arena_index + 1];
            if (tmp.cap >= wanted_arena_cap) {
                a->manager->buf[i] = *cur;
                a->manager->buf[i].arena_index = i;
                *cur = tmp;
                cur->arena_index = a->arena_index + 1;
                got = *cur;
                got.len = 0; // should't matter, but just in case
                break;
            }
        }

        if (got.buf) *a = got;
        else {
            a->manager->len += 1;
            if (a->manager->len > a->manager->cap) {
                a->manager->cap = 2 * a->manager->cap + 10;
                a->manager->buf = realloc(
                    a->manager->buf,
                    a->manager->cap * sizeof(*a->manager->buf)
                );
            }
            a->manager->buf[a->manager->len - 1] = (Arena) {
                .cap = wanted_arena_cap,
                .buf = malloc(wanted_arena_cap),
                .manager = a->manager,
                .arena_index = a->arena_index + 1,
            };
            *a = a->manager->buf[a->manager->len - 1];
        }
    }

    void *p = &a->buf[a->len] + pad;
    a->len += alloc_size;
    return memset(p, 0, count * size);
}

#define new(a, T, c) arena_alloc(a, sizeof(T), _Alignof(T), c)

/* ===== Main ===== */

#define error(reader, ...) \
    do { \
        fprintf(stderr, "Error at byte %ld: ", (reader).i); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, ".\n"); \
        exit(1); \
    } while (0)

#define expected(reader, ...) error(reader, "Expected " __VA_ARGS__)

typedef struct Type Type;
struct Type {
    Reader name, key;
    struct {
        Type **buf, **tmp_buf;
        ssize cap, len;
    } children;
};

typedef struct Name Name;
struct Name {
    enum {
        NAME_NONE = 0,
        NAME_TYPE,
        NAME_THEOREM,
        NAME_AXIOM,
    } type;
    Reader key;
    Type *val;
    Name *children[4];
};

Name *lookup_name(Arena *perm, Name **name, Reader key, bool make) {
    u64 hash = hash_mem(key.buf, key.len), h = hash;
    Name **item = name;

    while (*item) {
        if (are_reader_strs_equal(key, (*item)->key)) return *item;
        item = &(*item)->children[(h >> 62) & 3];
        h <<= 2;
    }

    if (make) {
        *item = new(perm, Name, 1);
        (*item)->key = key;
    }

   return *item;
}

void print_type(Type type) {
    print_reader(type.name);
    if (type.children.len) printf("(");
    for (int i = 0; i < type.children.len; i++) {
        if (type.children.buf[i]) print_type(*type.children.buf[i]);
        if (i != type.children.len - 1) {
            printf(", ");
        }
    }
    if (type.children.len) printf(")");
}

#define list_push(perm, list) \
    do { \
        (list)->len += 1; \
        if ((list)->len > (list)->cap) { \
            (list)->cap = (list)->len + 4 + 2 * (list)->cap; \
            (list)->tmp_buf = new((perm), (list)->tmp_buf, (list)->cap); \
            for (ssize i = 0; i < (list)->len - 1; i++) { \
                (list)->tmp_buf[i] = (list)->buf[i]; \
            } \
            (list)->buf = (list)->tmp_buf; \
            (list)->tmp_buf = NULL; \
        } \
    } while (0)

ssize try_read_type(Arena *perm, Name **namespace, Reader *in, Type **out) {
    Reader old = *in;

    bool expect_ident = true;
    struct {
        Type **buf, **tmp_buf;
        ssize len, cap;
    } stack = { .len = 1, .cap = 5, };
    stack.buf = new(perm, *stack.buf, stack.cap);

    stack.buf[0] = new(perm, *stack.buf[0], 1);

    while (in->i < in->len) {
        Type **cur = &stack.buf[stack.len - 1];
        assert(*cur);

        try_read_whitespace(in);

        Reader ident = {0};
        if (expect_ident && try_read_ident(in, &ident)) {
            Reader key = {0};
            if (are_reader_strs_equal(ident, Reader("let"))) {
                try_read_whitespace(in);
                if (!try_read_ident(in, &key)) {
                    expected(*in, "name for binding");
                }

                try_read_whitespace(in);
                if (!try_read_ident(in, &ident)) {
                    expected(*in, "type for binding");
                }
            }

            Type *t = new(perm, *t, 1);
            t->name = ident;
            t->key = key;

            if (try_read_str(in, Reader("("))) {
                list_push(perm, &stack);
                stack.buf[stack.len - 1] = t;
                expect_ident = true;
            } else {
                list_push(perm, &(*cur)->children);
                (*cur)->children.buf[(*cur)->children.len - 1] = t;
                expect_ident = false;
            }
        } else if (expect_ident && try_read_str(in, Reader("("))) {
            expected(*in, "type before `(`");
        } else if (try_read_str(in, Reader(")"))) {
            if (stack.len <= 1) error(*in, "Unmatched `)`");
            if (expect_ident) expected(*in, "type");
            Type **prev = &stack.buf[stack.len - 2];
            list_push(perm, &(*prev)->children);
            (*prev)->children.buf[(*prev)->children.len - 1] = stack.buf[stack.len - 1];
            stack.buf[stack.len - 1] = NULL;
            stack.len -= 1;
            expect_ident = false;
        } else if (try_read_str(in, Reader(","))) {
            if (!(*cur)) expected(*in, "type before `,`");
            expect_ident = true;
        } else break;
    }

    if (stack.len > 1) error(*in, "Unmatched `(`");

    if (out) *out = stack.buf[0];

    return in->i - old.i;
}

int main(int argc, char *argv[]) {
    ArenaManager arena_manager = { .min_arena_cap = 1 * MiB, };
    Arena arena = { .manager = &arena_manager, };

    if (argc != 2) {
        printf("status: %s <proof.plea>\n", argv[0]);
    }

    Name *namespace = NULL;
    Reader in = read_file_malloc(argv[1]);

    while (in.i < in.len) {
        try_read_whitespace(&in);

        if (try_read_str(&in, Reader("#"))) {
            for (; in.i < in.len - 1 && in.buf[in.i] != '\n'; in.i++) {}
            in.i += 1;
            continue;
        }

        Reader keyword = {0};
        if (!try_read_ident(&in, &keyword)) {
            expected(in, "keyword");
        }

        if (are_reader_strs_equal(keyword, Reader("type"))) {
            try_read_whitespace(&in);

            Reader name = {0};
            if (!try_read_ident(&in, &name)) {
                expected(in, "identifier");
            }
        } else if (are_reader_strs_equal(keyword, Reader("axiom")) ||
                   are_reader_strs_equal(keyword, Reader("theorem"))) {
            bool is_axiom = are_reader_strs_equal(keyword, Reader("axiom"));

            try_read_whitespace(&in);

            Reader name = {0};
            if (!try_read_ident(&in, &name)) {
                expected(in, "identifier");
            }

            try_read_whitespace(&in);

            Type *args = NULL;
            if (!try_read_type(&arena, &namespace, &in, &args)) {
                expected(in, "type");
            }

            try_read_whitespace(&in);
            if (!try_read_str(&in, Reader("->"))) {
                expected(in, "`->`");
            }
            try_read_whitespace(&in);

            Type *out = NULL;
            if (!try_read_type(&arena, &namespace, &in, &out)) {
                expected(in, "type");
            }

            print_type(*args);
            printf(" -> ");
            print_type(*out);
            printf("\n");

            if (!is_axiom) {
                if (!try_read_ident_str(&in, Reader("proof"))) expected(in, "proof for theorem");
                while (in.i < in.len) {
                    Type *type = NULL;
                    Reader ident = {0};
                    Reader tmp = in;
                    bool got_ret = try_read_ident_str(&in, Reader("return")) != 0;
                    if (!try_read_type(&arena, &namespace, &in, &type)) error(in, "type"); 
                }
            }
        } else {
            error(in, "Invalid keyword");
        }
     }

    // asan
    arena = (Arena) {0};
    for (int i = 0; i < arena_manager.len; i++) {
        free(arena_manager.buf[i].buf);
    }
    free(arena_manager.buf);
    arena_manager = (ArenaManager) {0};

    free(in.buf);

    return 0;
}
