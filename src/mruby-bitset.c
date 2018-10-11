#include "internals.h"

#if MRB_INT_MAX < UINTPTR_MAX
# define BITSET_WIDTH_MAX (MRB_INT_BIT - MRB_FIXNUM_SHIFT)
#else
# define BITSET_WIDTH_MAX (8 * sizeof(uintptr_t))
#endif

#define BS_WORDBITS     (8 * sizeof(uintptr_t))
#define BS_EMBEDBITS    (3 * BS_WORDBITS)

// BS_EXPAND_SIZE は sizeof(uintptr_t) 単位
#ifdef MRUBY_BITSET_EXPAND_HEAP
# define BS_EXPAND_SIZE (MRUBY_BITSET_EXPAND_HEAP)
#else
# define BS_EXPAND_SIZE 4
#endif

MRBX_FORCE_INLINE size_t
unit_ceil(size_t n, size_t unit)
{
    return (n + (unit - 1)) / unit;
}

MRBX_FORCE_INLINE size_t
align_ceil(size_t n, size_t unit)
{
    return unit_ceil(n, unit) * unit;
}

MRBX_FORCE_INLINE uintptr_t
getmask(int width)
{
    if (width > 0) {
        return ~((uintptr_t)-1 << 1 << (width - 1));
    } else {
        return 0;
    }
}

MRBX_FORCE_INLINE uintptr_t
getbits(uintptr_t n, int off, int width)
{
    return (n >> (off - width)) & getmask(width);
}

MRBX_FORCE_INLINE bool
iswordover(uintptr_t off, int width)
{
    if (off / BS_WORDBITS == (off + width - 1) / BS_WORDBITS) {
        return false;
    } else {
        return true;
    }
}

static int
aux_obj_to_bit(mrb_state *mrb, mrb_value obj)
{
    if (mrb_test(obj)) {
        if (mrbx_true_p(obj)) {
            return 1;
        } else {
            return mrb_int(mrb, obj);
        }
    } else {
        return 0;
    }
}

static uintptr_t
aux_make_bits_from_bit_array(mrb_state *mrb, struct RArray *ary)
{
    uintptr_t n = 0;
    const mrb_value *p = ARY_PTR(ary);
    for (size_t size = ARY_LEN(ary); size > 0; size --, p ++) {
        n <<= 1;
        if (mrb_test(*p) && (mrb_type(*p) == MRB_TT_TRUE || mrb_int(mrb, *p) != 0)) {
            n |= 1;
        }
    }
    return n;
}

static uintptr_t
aux_make_bits_from_bit_string(mrb_state *mrb, struct RString *ary)
{
    TODO("文字列の変換処理を書く");
}

static uintptr_t
aux_make_bits(mrb_state *mrb, mrb_value obj)
{
    switch (mrb_type(obj)) {
    case MRB_TT_FALSE:
        return 0;
    case MRB_TT_TRUE:
        return -1;
    case MRB_TT_ARRAY:
        return aux_make_bits_from_bit_array(mrb, RARRAY(obj));
    case MRB_TT_STRING:
        return aux_make_bits_from_bit_string(mrb, RSTRING(obj));
    case MRB_TT_FLOAT:
        TODO("浮動小数点数の変換処理を書く");
    default:
        return mrb_int(mrb, obj);
    }
}

/*
 * 実装について
 *
 * uintptr_t 単位でビット列を管理する
 * ビット列は上位ビットに詰めて管理する
 * 基本方針は下位に差し込んで上位から消費する
 * ワード長単位でビットシフトを行う
 */

struct bitset
{
    size_t is_embed:1;

    /* 0..192; is_embed が 1 の場合、ary メンバによって格納される要素数 */
    size_t embed_len:8;

    union {
        uintptr_t ary[3];           /* is_embed が 1 の時に要素が格納される */

        struct {
            uintptr_t *ptr;
            uintptr_t total_len;    /* 有効ビット数 */
            uintptr_t capacity;     /* ptr の確保した要素数 (uintptr_t 換算) */
        };
    };
};

static void
bitset_free(mrb_state *mrb, void *ptr)
{
    if (ptr) {
        struct bitset *p = (struct bitset *)ptr;
        if (!p->is_embed && p->ptr) {
            mrb_free(mrb, p->ptr);
        }
        memset(p, 0, sizeof(*p));
        mrb_free(mrb, p);
    }
}

static const mrb_data_type bitset_type = { "Bitset@mruby-bitset", bitset_free };

static struct bitset *
get_bitset_ptr(mrb_state *mrb, mrb_value bs)
{
    return (struct bitset *)mrb_data_get_ptr(mrb, bs, &bitset_type);
}

static struct bitset *
get_bitset(mrb_state *mrb, mrb_value bs)
{
    struct bitset *p = get_bitset_ptr(mrb, bs);

    if (!p) {
        mrb_raisef(mrb, E_RUNTIME_ERROR,
                   "not initialized - %S",
                   mrb_any_to_s(mrb, bs));
    }

    return p;
}

static void
bitset_check_uninitialized(mrb_state *mrb, mrb_value bs)
{
    struct RData *p = mrb_type(bs) == MRB_TT_DATA ? (struct RData *)mrb_ptr(bs) : NULL;
    if (!p || p->data || (p->type != NULL && p->type != &bitset_type)) {
        mrb_raisef(mrb, E_RUNTIME_ERROR,
                   "wrong re-initializing - %S",
                   mrb_any_to_s(mrb, bs));
    }

    mrbx_obj_modify(mrb, bs);
}

static mrb_value
bitset_new(mrb_state *mrb, struct RClass *klass, struct bitset **bsp)
{
    if (!klass) { klass = mrb_class_get(mrb, "Bitset"); }

    mrb_value obj = mrb_obj_value(mrb_obj_alloc(mrb, MRB_TT_DATA, klass));
    struct bitset *bs = mrb_calloc(mrb, 1, sizeof(struct bitset));
    bs->is_embed = 1;
    mrb_data_init(obj, bs, &bitset_type);
    if (bsp) { *bsp = bs; }

    return obj;
}

static inline void
bitset_getmem_const(const struct bitset *bs, const uintptr_t **ptr, size_t *size)
{
    if (bs->is_embed) {
        if (ptr) { *ptr = bs->ary; }
        if (size) { *size = bs->embed_len; }
    } else {
        if (ptr) { *ptr = bs->ptr; }
        if (size) { *size = bs->total_len; }
    }
}

static inline void
bitset_getmem(struct bitset *bs, uintptr_t **ptr, size_t *size)
{
    bitset_getmem_const(bs, (const uintptr_t **)ptr, size);
}

static inline uintptr_t *
bitset_ptr(struct bitset *bs)
{
    uintptr_t *ptr;
    bitset_getmem(bs, &ptr, NULL);
    return ptr;
}

static inline const uintptr_t *
bitset_ptr_const(const struct bitset *bs)
{
    const uintptr_t *ptr;
    bitset_getmem_const(bs, &ptr, NULL);
    return ptr;
}

static inline size_t
bitset_size(const struct bitset *bs)
{
    size_t size;
    bitset_getmem_const(bs, NULL, &size);
    return size;
}

static inline void
bitset_set_size(struct bitset *bs, size_t bitsize)
{
    if (bs->is_embed) {
        bs->embed_len = bitsize;
    } else {
        bs->total_len = bitsize;
    }
}

static void
bitset_reserve(mrb_state *mrb, struct bitset *bs, ssize_t reserve_bitsize)
{
    if (reserve_bitsize <= (ssize_t)BS_EMBEDBITS) { return; }

    size_t words = unit_ceil(reserve_bitsize, BS_WORDBITS * BS_EXPAND_SIZE) * BS_EXPAND_SIZE;

    if (bs->is_embed) {
        uintptr_t *ptr = mrb_calloc(mrb, words, sizeof(uintptr_t));
        memcpy(ptr, bs->ary, sizeof(bs->ary));
        bs->capacity = words;
        bs->total_len = bs->embed_len;
        bs->ptr = ptr;
        bs->is_embed = 0;
    } else if (words > bs->capacity) {
        bs->ptr = mrb_realloc(mrb, bs->ptr, words * sizeof(uintptr_t));
        bs->capacity = words;
    }
}

static inline uintptr_t
bitset_correct_index(mrb_state *mrb, mrb_value bitset, const struct bitset *bs, intptr_t index)
{
    size_t num = bitset_size(bs);
    mrb_int index_mod = index;

    if (index_mod < 0) {
        index_mod += num;
        if (index_mod < 0) {
            mrb_raisef(mrb, E_INDEX_ERROR,
                       "wrong index for %S (expect -%S or more, but given %S)",
                       mrb_any_to_s(mrb, bitset),
                       mrb_fixnum_value(num), mrb_fixnum_value(index));
        }
    }

    return index_mod;
}

static void
bitset_check_width(mrb_state *mrb, int bitwidth)
{
    if (bitwidth < 0 || bitwidth > BS_WORDBITS) {
        mrb_raisef(mrb, E_RUNTIME_ERROR,
                   "wrong bitwidth (expect 0..%S, but given %S)",
                   mrb_fixnum_value(BS_WORDBITS),
                   mrb_fixnum_value(bitwidth));
    }
}

static inline uintptr_t
bitset_aref(mrb_state *mrb, mrb_value self, size_t index, int bitwidth)
{
    // TODO: bitwidth == 1 の場合に特化した処理を書く

    const struct bitset *bs = get_bitset(mrb, self);
    size_t size = bitset_size(bs);
    int pad = 0;
    index = bitset_correct_index(mrb, self, bs, index);
    bitset_check_width(mrb, bitwidth);

    if (index >= size) { return 0; }

    if (index + bitwidth > size) {
        pad = index + bitwidth - size;
        bitwidth = size - index;
    }

    size_t bits;
    const uintptr_t *ptr = bitset_ptr_const(bs);

    ptr += index / BS_WORDBITS;

    if (iswordover(index, bitwidth)) {
        // ワード境界をまたぐ
        size_t low = (index + bitwidth) % BS_WORDBITS;
        bits = (getbits(ptr[0], BS_WORDBITS - index % BS_WORDBITS, bitwidth - low) << low) |
               (getbits(ptr[1], BS_WORDBITS, low));
    } else {
        bits = getbits(ptr[0], BS_WORDBITS - index % BS_WORDBITS, bitwidth);
    }

    return bits << pad;
}


static inline void
slide_bitset_expand(uintptr_t *ary, const uintptr_t *const term, intptr_t index, ssize_t width)
{
    uintptr_t *p = ary + 1 + (width - 1) / BS_WORDBITS;
    const uintptr_t *q = ary + 1;
    q += (term - p) - 1;
    p += (term - p) - 1;

    int shlo = width % BS_WORDBITS;
    int shhi = (BS_WORDBITS - shlo) % BS_WORDBITS;
    int maskhi;
    int masklo = shlo == 0 ? 0 : -1;

    //if (p >= term) { LOGS("BUFFER OVER RUN! - p"); }

    if (q > ary) {
        for (size_t i = term - ary - (index + width) / BS_WORDBITS - 1; i > 0; i --, p --, q --) {
            *p = (q[-1] << shhi) | ((q[0] >> shlo) & masklo);
        }

        //if (q < ary) { LOGS("BUFFER UNDER RUN! - q"); }
    }

    if (index + width < BS_WORDBITS) {
        shhi = BS_WORDBITS - index;
        shlo = index + width;
        maskhi = shhi < BS_WORDBITS ? -1 : 0;
        *p = ((q[0] >> shhi << shhi) & maskhi) | ((q[0] << index >> shlo) & masklo);
    } else {
        if (q > ary) {
            *p = (q[-1] << index >> index << shhi) | ((q[0] >> shlo) & masklo);
            p --; q --;
        } else if (q >= ary && p > ary) {
            *p = ((q[0] << index >> index >> shlo) & masklo);
            p --;
        }

        for (size_t i = p - ary; i > 0; i --, p --) {
            *p = 0;
        }

        shhi = BS_WORDBITS - index;
        maskhi = shhi < BS_WORDBITS ? -1 : 0;
        *p = ((q[0] >> shhi << shhi) & maskhi);
    }
}

static inline void
slide_bitset_collapse(uintptr_t *ary, const uintptr_t *const term, intptr_t index, ssize_t width)
{
    uintptr_t *p = ary;
    const uintptr_t *q = ary + (index + width) / BS_WORDBITS;

    width %= BS_WORDBITS;

    //先頭ワードの処理;
    if (index + width >= BS_WORDBITS) {
        int shhi = BS_WORDBITS - index;
        int shlo = index;
        uintptr_t hi = (*p >> shhi << shhi) & (shhi < BS_WORDBITS ? -1 : 0);
        uintptr_t lo = (term - q > 0) ? (q[0] << ((index + width) % BS_WORDBITS) >> shlo) : 0;
        *p = hi | lo;
        p ++;
    } else {
        int shtop = BS_WORDBITS - index;
        int shhi = index + width;
        int shlo = BS_WORDBITS - width;
        uintptr_t top = (*p >> shtop << shtop) & (shtop < BS_WORDBITS ? -1 : 0);
        uintptr_t hi = (term - q > 0) ? (q[0] << shhi >> index) : 0;
        uintptr_t lo = (term - q > 1) ? ((q[1] >> shlo) & (shlo < BS_WORDBITS ? -1 : 0)) : 0;
        *p = top | hi | lo;
        p ++; q ++;
    }

    //有効ワードの処理;
    {
        int shhi = width % BS_WORDBITS;
        int shlo = (BS_WORDBITS - width) % BS_WORDBITS;
        int maskhi = shhi > 0 ? -1 : 0;
        for (ssize_t i = term - q - 1; i > 0; i --, p ++, q ++) {
            *p = ((q[0] << shhi) & maskhi) | (q[1] >> shlo);
        }
    }

    //有効ワードの端数処理;
    if (term - q > 0) {
        int shhi = width;
        *p = q[0] << shhi;
        p ++;
    }

    //穴埋め;
    memset(p, 0, sizeof(uintptr_t) * (term - p));
}

static inline void
slide_bitset(uintptr_t *ary, const uintptr_t *const term, intptr_t index, ssize_t width)
{
    if (index < 0) { index = 0; }

    ary += index / BS_WORDBITS;
    index %= BS_WORDBITS;

    if (term - ary < 1) { return; }

    if (width == 0) {
        /* do nothing */
    } else if (width > 0) {
        slide_bitset_expand(ary, term, index, width);
    } else {
        slide_bitset_collapse(ary, term, index, -width);
    }
}

static void
bitset_slide(mrb_state *mrb, struct bitset *bs, intptr_t index, ssize_t width)
{
    if (index < 0) { index = 0; }

    size_t s = bitset_size(bs);
    if (s < index) { s = index; }

    bitset_reserve(mrb, bs, s + width);

    uintptr_t *ptr = bitset_ptr(bs);
    slide_bitset(ptr, ptr + unit_ceil(s, BS_WORDBITS), index, width);

    s += width;
    if ((ssize_t)s < 0) { s = 0; }
    bitset_set_size(bs, s);
}

static void
replace_bitset(uintptr_t *ptr, uintptr_t index, int width, uintptr_t bits)
{
    uintptr_t mask = getmask(width);
    bits &= mask;
    ptr += index / BS_WORDBITS;
    index %= BS_WORDBITS;
    if (iswordover(index, width)) {
        int shhi = width - (BS_WORDBITS - index);
        int shlo = BS_WORDBITS - (index + width) % BS_WORDBITS;
        uintptr_t hi = bits >> shhi;
        uintptr_t lo = bits << shlo;
        ptr[0] = (ptr[0] & ~(mask >> shhi)) | hi;
        ptr[1] = lo;
    } else {
        int sh = BS_WORDBITS - (index + width);
        *ptr = (*ptr & ~(mask << sh)) | (bits << sh);
    }
}

static void
bitset_aset(mrb_state *mrb, mrb_value self, intptr_t index, int width, uintptr_t bits, int bitwidth)
{
    mrbx_obj_modify(mrb, self);

    struct bitset *bs = get_bitset(mrb, self);
    index = bitset_correct_index(mrb, self, bs, index);
    bitset_check_width(mrb, width);
    bitset_check_width(mrb, bitwidth);

    bitset_slide(mrb, bs, index, bitwidth - width);
    size_t size = bitset_size(bs);
    if (index + bitwidth >= size) {
        bitset_slide(mrb, bs, size, index + bitwidth - size);
    }
    if (bitwidth > 0) { replace_bitset(bitset_ptr(bs), index, bitwidth, bits); }
}

static void
bitset_aset_bitset(mrb_state *mrb, mrb_value self, intptr_t index, int width, struct bitset *bs, int bitwidth)
{
    TODO("何か書く");
}

static void
flip_bitset(mrb_state *mrb, struct bitset *bs)
{
    uintptr_t *p;
    size_t size;
    if (bs->is_embed) {
        p = bs->ary;
        size = bs->embed_len;
    } else {
        p = bs->ptr;
        size = bs->total_len;
    }

    int rest = BS_WORDBITS - size % BS_WORDBITS;
    for (int i = size / BS_WORDBITS; i > 0; i --, p ++) {
        *p = ~*p;
    }

    if (rest > 0) {
        *p ^= ((uintptr_t)-1 << rest);
    }
}

static void
bitset_copy(mrb_state *mrb, struct bitset *dest, const struct bitset *src)
{
    if (src->is_embed) {
        memcpy(dest, src, sizeof(*dest));
    } else if (src->total_len < BS_EMBEDBITS) {
        // embed にする
        dest->is_embed = 1;
        dest->embed_len = src->total_len;
        memcpy(dest->ary, src->ptr, sizeof(dest->ary));
    } else {
        size_t capacity = unit_ceil(src->total_len, BS_WORDBITS * BS_EXPAND_SIZE) * BS_EXPAND_SIZE;
        dest->ptr = mrb_calloc(mrb, capacity, sizeof(*src->ptr));
        memcpy(dest->ptr, src->ptr, capacity * sizeof(*src->ptr));
        dest->total_len = src->total_len;
        dest->capacity = capacity;
        dest->is_embed = 0;
    }
}

static void
bitset_load_from_bit_string(mrb_state *mrb, struct bitset *bs, struct RString *src, ssize_t width)
{
    if (width < 0) {
        const char *ch = RSTR_PTR(src);
        width = 0;
        for (mrb_int i = RSTR_LEN(src); i > 0; i --, ch ++) {
            switch (*ch) {
            case '0': case '1':
                width ++;
                break;
            case ' ': case '_': case '.': case '-': case ':':
                break;
            default:
                i = 0; /* for ループから脱出 */
                break;
            }
        }
    }

    bitset_reserve(mrb, bs, width);
    bitset_set_size(bs, width);
    if (width < 1) { return; }

    const char *ch = RSTR_PTR(src);
    uintptr_t *p = bitset_ptr(bs);

    for (size_t i = width / BS_WORDBITS; i > 0; i --, p ++, width -= BS_WORDBITS) {
        uintptr_t n = 0;
        for (int j = BS_WORDBITS; j > 0; j --) {
            unsigned char tmp;

            for (;;) {
                tmp = *ch ++;
                if (tmp == '0' || tmp == '1') {
                    tmp -= '0';
                    break;
                }

                switch (tmp) {
                case ' ': case '_': case '.': case '-': case ':':
                    break;
                default:
                    *p ++ = n;
                    width -= BS_WORDBITS;
                    goto fill_zero;
                }
            }

            n |= (uintptr_t)tmp << (j - 1);
        }
        *p = n;
    }

    {
        uintptr_t n = 0;
        int rest = width % BS_WORDBITS;
        int off = BS_WORDBITS - rest - 1;
        for (int j = rest; j > 0; j --) {
            unsigned char tmp;

            for (;;) {
                tmp = *ch ++;
                if (tmp == '0' || tmp == '1') {
                    tmp -= '0';
                    break;
                }

                switch (tmp) {
                case ' ': case '_': case '.': case '-': case ':':
                    break;
                default:
                    *p ++ = n;
                    width -= BS_WORDBITS;
                    goto fill_zero;
                }
            }

            n |= (uintptr_t)tmp << (off + j);
        }
        *p = n;
        width -= BS_WORDBITS;
    }

fill_zero:
    if (width >= 0) {
        memset(p, 0, unit_ceil(width, BS_WORDBITS) * sizeof(*p));
    }
}

static void
bitset_load_from_bit_array(mrb_state *mrb, struct bitset *bs, struct RArray *src, ssize_t width)
{
    if (width < 0) { width = ARY_LEN(src); }
    bitset_reserve(mrb, bs, width);
    bitset_set_size(bs, width);

    const mrb_value *obj = ARY_PTR(src);
    uintptr_t *p = bitset_ptr(bs);

    for (size_t i = width / BS_WORDBITS; i > 0; i --, p ++) {
        uintptr_t n = 0;
        for (int j = BS_WORDBITS; j > 0; j --, obj ++) {
            uintptr_t tmp = aux_obj_to_bit(mrb, *obj);

            if (tmp > 1) {
                *p = n;
                bitset_set_size(bs, ((width / BS_WORDBITS) - i) * BS_WORDBITS + (BS_WORDBITS - j));
                return;
            }

            n = (n << 1) | tmp;
        }
        *p = n;
    }

    {
        uintptr_t n = 0;
        for (int j = width % BS_WORDBITS; j > 0; j --, obj ++) {
            uintptr_t tmp = aux_obj_to_bit(mrb, *obj);

            if (tmp > 1) {
                *p = n;
                bitset_set_size(bs, (width / BS_WORDBITS) * BS_WORDBITS + (width % BS_WORDBITS - j));
                return;
            }

            n = (n << 1) | tmp;
        }
        n <<= BS_WORDBITS - (width % BS_WORDBITS);
        *p = n;
    }
}

static void
bitset_load_from_bitset(mrb_state *mrb, struct bitset *bs, const struct bitset *src, ssize_t width)
{
    TODO("変換処理を書く");
}

static void
bitset_load_from_integer(mrb_state *mrb, struct bitset *bs, uint64_t src, ssize_t width)
{
    if (width < 0) { mrb_raise(mrb, E_RUNTIME_ERROR, "not given bit size"); }

    bitset_reserve(mrb, bs, width);
    bitset_set_size(bs, width);
    if (width == 0) { return; }

    if (width < 64) { src <<= 64 - width; }

#if UINT64_MAX == UINTPTR_MAX
    uintptr_t *p = bitset_ptr(bs);
    *p ++ = src;
#else
    uintptr_t *p = bitset_ptr(bs);
    *p ++ = src >> 32;
    *p ++ = src; /* 最低でも3ワード長が確保されるため、ポインタの確認は不要 */
#endif

    if (width > 64) {
        width -= 64;
        memset(p, 0, unit_ceil(width, BS_WORDBITS) * sizeof(*p));
    }
}

static void
bitset_load_from_object(mrb_state *mrb, struct bitset *bs, mrb_value srcobj, ssize_t width)
{
    switch (mrb_type(srcobj)) {
    case MRB_TT_STRING:
        bitset_load_from_bit_string(mrb, bs, RSTRING(srcobj), width);
        break;
    case MRB_TT_ARRAY:
        bitset_load_from_bit_array(mrb, bs, RARRAY(srcobj), width);
        break;
    case MRB_TT_DATA:
        if (DATA_TYPE(srcobj) == &bitset_type) {
            bitset_load_from_bitset(mrb, bs, (const struct bitset *)DATA_PTR(srcobj), width);
            break;
        }
    default:
        bitset_load_from_integer(mrb, bs, mrb_int(mrb, srcobj), width);
        break;
    }
}

/*
 * call-seq:
 *  initialize
 *  initialize(capacity)
 *  initialize(width, bitset)
 *  initialize(bitset)
 */
static mrb_value
bs_init(mrb_state *mrb, mrb_value self)
{
    mrb_value arg1 = mrb_undef_value();
    mrb_value arg2 = mrb_undef_value();
    mrb_get_args(mrb, "|oo", &arg1, &arg2);

    bitset_check_uninitialized(mrb, self);

    struct bitset *bs = mrb_calloc(mrb, 1, sizeof(struct bitset));
    mrb_data_init(self, bs, &bitset_type);
    bs->is_embed = true;

    if (!mrb_undef_p(arg2)) {
        size_t size = mrb_int(mrb, arg1);
        if ((ssize_t)size < 0) { mrb_raise(mrb, E_RUNTIME_ERROR, "wrong negative bit width"); }
        bitset_load_from_object(mrb, bs, arg2, size);
    } else if (!mrb_undef_p(arg1)) {
        switch (mrb_type(arg1)) {
        case MRB_TT_FIXNUM:
        case MRB_TT_FLOAT:
            bitset_reserve(mrb, bs, mrb_int(mrb, arg1));
            break;
        default:
            bitset_load_from_object(mrb, bs, arg1, -1);
            break;
        }
    }

    return self;
}

static mrb_value
bs_init_copy(mrb_state *mrb, mrb_value self)
{
    mrb_value origv;
    mrb_get_args(mrb, "o", &origv);
    const struct bitset *orig = get_bitset(mrb, origv);

    bitset_check_uninitialized(mrb, self);

    struct bitset *bs = mrb_calloc(mrb, 1, sizeof(struct bitset));
    mrb_data_init(self, bs, &bitset_type);
    bs->is_embed = true;

    bitset_copy(mrb, bs, orig);

    return self;
}

static mrb_value
bs_size(mrb_state *mrb, mrb_value self)
{
    mrb_get_args(mrb, "");
    return mrb_fixnum_value(bitset_size(get_bitset(mrb, self)));
}

static mrb_value
bs_capacity(mrb_state *mrb, mrb_value self)
{
    mrb_get_args(mrb, "");
    const struct bitset *bs = get_bitset(mrb, self);
    size_t capacity = bs->is_embed ? BS_EMBEDBITS : bs->capacity * BS_WORDBITS;
    return mrb_fixnum_value(capacity);
}

static mrb_value
bs_reserve(mrb_state *mrb, mrb_value self)
{
    mrb_int bitsize;
    mrb_get_args(mrb, "i", &bitsize);
    mrbx_obj_modify(mrb, self);
    bitset_reserve(mrb, get_bitset(mrb, self), bitsize);
    return self;
}

static void
bitset_shrink(mrb_state *mrb, struct bitset *bs)
{
    if (bs->is_embed) { return; }

    if (bs->total_len <= BS_EMBEDBITS) {
        uintptr_t *ptr = bs->ptr;
        bs->embed_len = bs->total_len;
        bs->is_embed = 1;
        memcpy(bs->ary, ptr, sizeof(bs->ary)); /* ptr は常に 4 以上のはず */
    } else {
        size_t used = unit_ceil(bs->total_len, BS_WORDBITS);

        if (bs->capacity - used >= BS_EXPAND_SIZE) {
            size_t shrinkwords = align_ceil(used, BS_EXPAND_SIZE);
            bs->ptr = mrb_realloc(mrb, bs->ptr, shrinkwords * sizeof(uintptr_t));
            bs->capacity = shrinkwords;
        }
    }
}

static mrb_value
bs_shrink(mrb_state *mrb, mrb_value self)
{
    mrb_get_args(mrb, "");
    mrbx_obj_modify(mrb, self);
    bitset_shrink(mrb, get_bitset(mrb, self));
    return self;
}

static mrb_value
bs_fill(mrb_state *mrb, mrb_value self)
{
    mrb_value fill = mrb_true_value();
    mrb_get_args(mrb, "|o", &fill);

    struct bitset *bs = get_bitset(mrb, self);
    size_t size = bitset_size(bs);
    uintptr_t *p = bitset_ptr(bs);
    uintptr_t bits;

    mrbx_obj_modify(mrb, self);

    if (mrb_bool(fill)) {
        bits = -1;
    } else {
        bits = 0;
    }

    for (; size >= BS_WORDBITS; size -= BS_WORDBITS, p ++) {
        *p = bits;
    }

    if (size > 0) {
        *p = bits << (BS_WORDBITS - size);
    }

    return self;
}

static mrb_value
bs_clear(mrb_state *mrb, mrb_value self)
{
    mrb_get_args(mrb, "");

    struct bitset *bs = get_bitset(mrb, self);
    size_t size = bitset_size(bs);
    uintptr_t *p = bitset_ptr(bs);

    mrbx_obj_modify(mrb, self);
    bitset_set_size(bs, 0);
    memset(p, 0, unit_ceil(size, BS_WORDBITS) * sizeof(uintptr_t));

    return self;
}

/*
 * call-seq:
 *  bitset[index, width = 1] -> integer as bitset
 */
static mrb_value
bs_aref(mrb_state *mrb, mrb_value self)
{
    mrb_int index, bitwidth;
    switch (mrb_get_args(mrb, "i|i", &index, &bitwidth)) {
    case 1:
        bitwidth = 1;
        break;
    case 2:
        break;
    }
    return mrb_fixnum_value(bitset_aref(mrb, self, index, bitwidth));
}

/*
 * call-seq:
 *  aset(index, bit) -> self
 *  aset(index, width, bitsize = ???, bits) -> self
 *
 * [index] integer
 * [bit] 0, 1, false or true
 * [width] integer (1..32; 32-bit mode) (1..64; 64-bit mode)
 * [bitsize] integer (same as width if integer or string) (same as bits.size if bits is array or bitset)
 * [bits] integer, string, array of integer, array of boolean, bitset
 */
static mrb_value
bs_aset(mrb_state *mrb, mrb_value self)
{
    mrb_int index;
    mrb_value args[3];

    switch (mrb_get_args(mrb, "io|oo", &index, &args[0], &args[1], &args[2])) {
    case 2:
        bitset_aset(mrb, self, index, 1, aux_make_bits(mrb, args[0]), 1);

        break;
    case 3:
        // TODO: case 3 と case 4 をうまく統合する
        {
            mrb_int width = mrb_int(mrb, args[0]);

            switch (mrb_type(args[1])) {
            case MRB_TT_ARRAY:
                bitset_aset(mrb, self, index, width, aux_make_bits(mrb, args[1]), RARRAY_LEN(args[1]));
                break;
            case MRB_TT_STRING:
                bitset_aset(mrb, self, index, width, aux_make_bits(mrb, args[1]), RSTRING_LEN(args[1]));
                break;
            case MRB_TT_DATA:
                if (DATA_TYPE(args[1]) == &bitset_type) {
                    struct bitset *bs = get_bitset(mrb, args[1]);
                    bitset_aset_bitset(mrb, self, index, width, bs, bitset_size(bs));
                    break;
                }
                /* fall through */
            default:
                bitset_aset(mrb, self, index, width, aux_make_bits(mrb, args[1]), width);
                break;
            }
        }
        break;
    case 4:
        {
            mrb_int width = mrb_int(mrb, args[0]);
            mrb_int bitwidth = mrb_int(mrb, args[1]);

            switch (mrb_type(args[2])) {
            case MRB_TT_ARRAY:
                bitset_aset(mrb, self, index, width, aux_make_bits(mrb, args[2]), bitwidth);
                break;
            case MRB_TT_STRING:
                bitset_aset(mrb, self, index, width, aux_make_bits(mrb, args[2]), bitwidth);
                break;
            case MRB_TT_DATA:
                if (DATA_TYPE(args[2]) == &bitset_type) {
                    struct bitset *bs = get_bitset(mrb, args[2]);
                    bitset_aset_bitset(mrb, self, index, width, bs, bitwidth);
                    break;
                }
                /* fall through */
            default:
                bitset_aset(mrb, self, index, width, aux_make_bits(mrb, args[2]), bitwidth);
                break;
            }
        }
        break;
    }

    return self;
}

static mrb_value
bs_flip(mrb_state *mrb, mrb_value self)
{
    mrb_get_args(mrb, "");
    const struct bitset *src = get_bitset(mrb, self);
    struct bitset *dest;
    mrb_value dup = bitset_new(mrb, mrb_obj_class(mrb, self), &dest);
    bitset_copy(mrb, dest, src);

    flip_bitset(mrb, dest);

    return dup;
}

static mrb_value
bs_flip_bang(mrb_state *mrb, mrb_value self)
{
    mrb_get_args(mrb, "");
    struct bitset *bs = get_bitset(mrb, self);
    mrbx_obj_modify(mrb, self);

    flip_bitset(mrb, bs);

    return self;
}

typedef uintptr_t operator_f(uintptr_t a, uintptr_t b);

static inline operator_f operator_or;
static inline operator_f operator_nor;
static inline operator_f operator_and;
static inline operator_f operator_nand;
static inline operator_f operator_xor;
static inline operator_f operator_xnor;

static void
bitset_msb_operate(mrb_state *mrb, mrb_value self, operator_f *operator)
{
    const struct bitset *other;
    mrb_get_args(mrb, "d", &other, &bitset_type);
    struct bitset *bs = get_bitset(mrb, self);
    mrbx_obj_modify(mrb, self);

    size_t size1 = bitset_size(bs);
    size_t size2 = bitset_size(other);

    if (size1 < size2) {
        bitset_reserve(mrb, bs, size2);
        bitset_set_size(bs, size2);
    }

    uintptr_t *p1 = bitset_ptr(bs);
    const uintptr_t *p2 = bitset_ptr_const(other);

    for (size_t i = size2 / BS_WORDBITS; i > 0; i --, p1 ++, p2 ++) {
        *p1 = operator(*p1, *p2);
    }

    size1 -= (size2 / BS_WORDBITS) * BS_WORDBITS;
    size2 -= (size2 / BS_WORDBITS) * BS_WORDBITS;

    if (size1 < BS_WORDBITS) {
        int pad1 = BS_WORDBITS - size1;
        int pad2 = BS_WORDBITS - size2;
        *p1 = operator(size1 > 0 ? (*p1 >> pad1 << pad1) : 0,
                       size2 > 0 ? (*p2 >> pad2 << pad2) : 0);
    } else {
        if (size2 > 0) {
            int pad2 = BS_WORDBITS - size2;
            uintptr_t n2 = *p2 >> pad2 << pad2;
            *p1 = operator(*p1, n2);
            p1 ++;
            size1 -= BS_WORDBITS;
        }

        for (size_t i = size1 / BS_WORDBITS; i > 0; i --, p1 ++) {
            *p1 = operator(*p1, 0);
        }
        size1 %= BS_WORDBITS;

        if (size1 > 0) {
            int pad1 = BS_WORDBITS - size1;
            *p1 = operator(*p1 >> pad1 << pad1, 0);
        }
    }
}

static void
bitset_lsb_operate(mrb_state *mrb, mrb_value self, operator_f *operator)
{
    const struct bitset *other;
    mrb_get_args(mrb, "d", &other, &bitset_type);
    struct bitset *bs = get_bitset(mrb, self);
    mrbx_obj_modify(mrb, self);

    size_t size1 = bitset_size(bs);
    size_t size2 = bitset_size(other);

    if (size1 < size2) {
        bitset_slide(mrb, bs, 0, size2 - size1);
        size1 = size2;
    }

    uintptr_t *p1 = bitset_ptr(bs);
    const uintptr_t *p2 = bitset_ptr_const(other);

    ssize_t pad = size1 - size2;

    for (ssize_t i = pad / BS_WORDBITS; i > 0; i --, p1 ++, size1 -= BS_WORDBITS, pad -= BS_WORDBITS) {
        *p1 = operator(*p1, 0);
    }

    if (pad == 0) {
        if (size1 != size2) { mrb_raisef(mrb, E_RUNTIME_ERROR, "[BUG] size1(%S) != size2(%S) [BUG]", mrb_fixnum_value(size1), mrb_fixnum_value(size2)); }

        for (int i = unit_ceil(size1, BS_WORDBITS); i > 0; i --, p1 ++, p2 ++) {
            *p1 = operator(*p1, *p2);
        }
    } else {
        if (size1 <= size2) { mrb_raisef(mrb, E_RUNTIME_ERROR, "[BUG] size1(%S) <= size2(%S) [BUG]", mrb_fixnum_value(size1), mrb_fixnum_value(size2)); }

        /* NOTE: pad は [1, 32) の値域を取る */
        int shhi = BS_WORDBITS - pad;
        int shlo = pad;

        //最初はp2の上位ビットを切り出して下位ビットとする;
        *p1 = operator(*p1, *p2 >> shlo);
        p1 ++;
        size1 -= BS_WORDBITS;
        size2 -= shhi;

        if (size1 != size2) { mrb_raisef(mrb, E_RUNTIME_ERROR, "[BUG] size1(%S) != size2(%S) [BUG]", mrb_fixnum_value(size1), mrb_fixnum_value(size2)); }

        for (int i = size1 / BS_WORDBITS; i > 0; i --, p1 ++, p2 ++, size1 -= BS_WORDBITS, size2 -= BS_WORDBITS) {
            *p1 = operator(*p1, (p2[0] << shhi) | (p2[1] >> shlo));
        }

        if (size1 != size2) { mrb_raisef(mrb, E_RUNTIME_ERROR, "[BUG] size1(%S) != size2(%S) [BUG]", mrb_fixnum_value(size1), mrb_fixnum_value(size2)); }

        if (size1 > 0) {
            *p1 = operator(*p1, p2[0] << shhi);
        }
    }
}

static inline uintptr_t operator_or(uintptr_t a, uintptr_t b) { return a | b; }

static mrb_value
bs_msb_or(mrb_state *mrb, mrb_value self)
{
    bitset_msb_operate(mrb, self, operator_or);
    return self;
}

static mrb_value
bs_lsb_or(mrb_state *mrb, mrb_value self)
{
    bitset_lsb_operate(mrb, self, operator_or);
    return self;
}

static inline uintptr_t operator_nor(uintptr_t a, uintptr_t b) { return ~(a | b); }

static mrb_value
bs_msb_nor(mrb_state *mrb, mrb_value self)
{
    bitset_msb_operate(mrb, self, operator_nor);
    return self;
}

static mrb_value
bs_lsb_nor(mrb_state *mrb, mrb_value self)
{
    bitset_lsb_operate(mrb, self, operator_nor);
    return self;
}

static inline uintptr_t operator_and(uintptr_t a, uintptr_t b) { return a & b; }

static mrb_value
bs_msb_and(mrb_state *mrb, mrb_value self)
{
    bitset_msb_operate(mrb, self, operator_and);
    return self;
}

static mrb_value
bs_lsb_and(mrb_state *mrb, mrb_value self)
{
    bitset_lsb_operate(mrb, self, operator_and);
    return self;
}

static inline uintptr_t operator_nand(uintptr_t a, uintptr_t b) { return ~(a & b); }

static mrb_value
bs_msb_nand(mrb_state *mrb, mrb_value self)
{
    bitset_msb_operate(mrb, self, operator_nand);
    return self;
}

static mrb_value
bs_lsb_nand(mrb_state *mrb, mrb_value self)
{
    bitset_lsb_operate(mrb, self, operator_nand);
    return self;
}

static inline uintptr_t operator_xor(uintptr_t a, uintptr_t b) { return a ^ b; }

static mrb_value
bs_msb_xor(mrb_state *mrb, mrb_value self)
{
    bitset_msb_operate(mrb, self, operator_xor);
    return self;
}

static mrb_value
bs_lsb_xor(mrb_state *mrb, mrb_value self)
{
    bitset_lsb_operate(mrb, self, operator_xor);
    return self;
}

static inline uintptr_t operator_xnor(uintptr_t a, uintptr_t b) { return ~(a ^ b); }

static mrb_value
bs_msb_xnor(mrb_state *mrb, mrb_value self)
{
    bitset_msb_operate(mrb, self, operator_xnor);
    return self;
}

static mrb_value
bs_lsb_xnor(mrb_state *mrb, mrb_value self)
{
    bitset_lsb_operate(mrb, self, operator_xnor);
    return self;
}

static uintptr_t
bitreflect(uintptr_t n)
{
#if UINTPTR_MAX > UINT32_MAX
    // バイトオーダスワップ (__buildin_bswapll) から始める
    // 逆から行うと、最適化で bswap に置き換えられなくなる
    n = ( n                          >> 32) | ( n << 32                         );
    n = ((n & 0x0000ffff0000ffffULL) << 16) | ((n >> 16) & 0x0000ffff0000ffffULL);
    n = ((n & 0x00ff00ff00ff00ffULL) <<  8) | ((n >>  8) & 0x00ff00ff00ff00ffULL);
    n = ((n & 0x0f0f0f0f0f0f0f0fULL) <<  4) | ((n >>  4) & 0x0f0f0f0f0f0f0f0fULL);
    n = ((n & 0x3333333333333333ULL) <<  2) | ((n >>  2) & 0x3333333333333333ULL);
    n = ((n & 0x5555555555555555ULL) <<  1) | ((n >>  1) & 0x5555555555555555ULL);
#else
    n = ( n                 << 16) | ( n >> 16                );
    n = ((n & 0x00ff00ffUL) <<  8) | ((n >>  8) & 0x00ff00ffUL);
    n = ((n & 0x0f0f0f0fUL) <<  4) | ((n >>  4) & 0x0f0f0f0fUL);
    n = ((n & 0x33333333UL) <<  2) | ((n >>  2) & 0x33333333UL);
    n = ((n & 0x55555555UL) <<  1) | ((n >>  1) & 0x55555555UL);
#endif
    return n;
}

static void
bitset_bitreflect(mrb_state *mrb, struct bitset *dest, const struct bitset *src)
{
    uintptr_t *p;
    size_t size;

    if (!src || src == dest) {
        size = bitset_size(dest);
        uintptr_t *head = bitset_ptr(dest);
        uintptr_t *tail = head + unit_ceil(size, BS_WORDBITS);
        uintptr_t *q;

        // 最初は単純なワード単位でのスワップとビットの逆順
        for (p = head, q = tail - 1; p < q; p ++, q --) {
            uintptr_t t = bitreflect(*q);
            *q = bitreflect(*p);
            *p = t;
        }

        if (p == q) {
            *p = bitreflect(*p);
        }

        p = head;
    } else {
        const uintptr_t *q;
        bitset_getmem_const(src, &q, &size);

        bitset_reserve(mrb, dest, size);
        bitset_set_size(dest, size);

        p = bitset_ptr(dest) + unit_ceil(size, BS_WORDBITS);
        for (size_t i = unit_ceil(size, BS_WORDBITS); i > 0; i --, p --, q ++) {
            p[-1] = bitreflect(*q);
        }

        mrb_assert(p == bitset_ptr(dest));
    }

    size_t rest = size % BS_WORDBITS;
    if (rest > 0) {
        // ビットシフト
        int shhi = BS_WORDBITS - rest;
        int shlo = rest;
        for (size_t i = size / BS_WORDBITS; i > 0; i --, p ++) {
            p[0] = (p[0] << shhi) | (p[1] >> shlo);
        }
        *p <<= shhi;
    }
}

static mrb_value
bs_bitreflect(mrb_state *mrb, mrb_value self)
{
    mrb_get_args(mrb, "");
    struct bitset *dest;
    mrb_value obj = bitset_new(mrb, mrb_obj_class(mrb, self), &dest);
    bitset_bitreflect(mrb, dest, get_bitset(mrb, self));
    return obj;
}

static mrb_value
bs_bitreflect_bang(mrb_state *mrb, mrb_value self)
{
    mrb_get_args(mrb, "");
    bitset_bitreflect(mrb, get_bitset(mrb, self), NULL);
    return self;
}

static void
bitset_minus(mrb_state *mrb, struct bitset *dest, const struct bitset *src)
{
    size_t size = bitset_size(src);
    bitset_reserve(mrb, dest, size);
    bitset_set_size(dest, size);
    if (size < 1) { return; }

    size_t words = unit_ceil(size, BS_WORDBITS);
    uintptr_t *p = bitset_ptr(dest);
    const uintptr_t *q = bitset_ptr_const(src);

    p += words - 1;
    q += words - 1;

    int overflow = 0;
    int pad = BS_WORDBITS - size % BS_WORDBITS;
    if (pad < BS_WORDBITS) {
        uintptr_t n = -(*q >> pad) << pad;
        if (n == 0) {
            overflow = 1;
        }
        *p = n;
        p --; q --; words --;
    }

    for (; words > 0; words --, p --, q --) {
        uintptr_t n = ~*q;
        if (n == 0) {
            n = overflow;
            overflow = 1;
        } else {
            n += overflow;
            if (n == 0) {
                overflow = 1;
            } else {
                overflow = 0;
            }
        }

        *p = n;
    }
}

static mrb_value
bs_minus(mrb_state *mrb, mrb_value self)
{
    mrb_get_args(mrb, "");

    const struct bitset *src = get_bitset(mrb, self);
    struct bitset *dest;
    mrb_value obj = bitset_new(mrb, mrb_obj_class(mrb, self), &dest);

    bitset_minus(mrb, dest, src);

    return obj;
}

static mrb_value
bs_minus_bang(mrb_state *mrb, mrb_value self)
{
    mrb_get_args(mrb, "");

    struct bitset *bs = get_bitset(mrb, self);

    mrbx_obj_modify(mrb, self);
    bitset_minus(mrb, bs, bs);

    return self;
}

static int
popcount(uintptr_t n)
{
#if 0 && (defined(__GNUC__) || defined(__clang__))
    if (!n) { return 0; }

# if UINTPTR_MAX > UINT32_MAX
    return __builtin_popcountll(n);
# else
    return __builtin_popcount(n);
# endif
#elif 1
    /* utils/mkpopcnt.rb で出力 */
    static const char table[] = {
        0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
        1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
        1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
        2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
        1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
        2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
        2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
        3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
        1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
        2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
        2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
        3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
        2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
        3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
        3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
        4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8,
    };

    int count = 0;
    for (int i = 0; i < sizeof(n); i ++) {
        count += table[(uint8_t)(n >> (i * 8)) & 0xff];
    }
    return count;
#else
# if UINTPTR_MAX > UINT32_MAX
    //n = ( n       & 0x1111111111111111ULL) +
    //    ((n >> 1) & 0x1111111111111111ULL) +
    //    ((n >> 2) & 0x1111111111111111ULL) +
    //    ((n >> 3) & 0x1111111111111111ULL);
    n = (n & 0x5555555555555555ULL) + ((n >>  1) & 0x5555555555555555ULL);
    n = (n & 0x3333333333333333ULL) + ((n >>  2) & 0x3333333333333333ULL);
    n = (n + (n >> 4)) & 0x0f0f0f0f0f0f0f0fULL; /* 4 + 4 = 8 が最大なので、加算前のビットマスクは不要 */
    n += n >>  8; /* 以降は 0..64 に収まるため、ビットマスクは不要 */
    n += n >> 16;
    n += n >> 32;
# else
    n = (n & 0x55555555UL) + ((n >>  1) & 0x55555555UL);
    n = (n & 0x33333333UL) + ((n >>  2) & 0x33333333UL);
    n = (n + (n >> 4)) & 0x0f0f0f0fUL; /* 4 + 4 = 8 が最大なので、加算前のビットマスクは不要 */
    n += n >>  8; /* 以降は 0..32 に収まるため、ビットマスクは不要 */
    n += n >> 16;
# endif
    return n & 0xff;
#endif
}

static int
bitset_popcount(const struct bitset *bs)
{
    const uintptr_t *p = bs->is_embed ? bs->ary : bs->ptr;
    size_t size = bitset_size(bs);
    size_t cnt = 0;

    for (int i = size / BS_WORDBITS; i > 0; i --, p ++) {
        cnt += popcount(*p);
    }

    size %= BS_WORDBITS;
    if (size > 0) {
        cnt += popcount(*p >> (BS_WORDBITS - size));
    }

    return cnt;
}

MRB_API int
mruby_bitset_popcount(mrb_state *mrb, mrb_value bitset)
{
    return bitset_popcount(get_bitset(mrb, bitset));
}

static mrb_value
bs_popcount(mrb_state *mrb, mrb_value self)
{
    mrb_get_args(mrb, "");
    return mrb_fixnum_value(mruby_bitset_popcount(mrb, self));
}

static int
count_nlz(uintptr_t n)
{
    /*
     * ゆくゆくは __builtin_clz() / __builtin_clzll() を使うかもしれない
     * https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html
     */

#if UINTPTR_MAX > UINT32_MAX
    n |= n >> 32;
#endif
    n |= n >> 16;
    n |= n >>  8;
    n |= n >>  4;
    n |= n >>  2;
    n |= n >>  1;
    return popcount(~n);
}

static size_t
bitset_clz(const struct bitset *bs)
{
    const uintptr_t *p = bs->is_embed ? bs->ary : bs->ptr;
    size_t size = bitset_size(bs);
    size_t cnt = 0;

    for (int i = size / BS_WORDBITS; i > 0; i --, p ++) {
        if (*p) {
            return cnt + count_nlz(*p);
        }

        cnt += BS_WORDBITS;
    }

    size %= BS_WORDBITS;
    if (size > 0) {
        uintptr_t sh = BS_WORDBITS - size;
        uintptr_t nn = *p >> sh << sh;
        if (nn) {
            cnt += count_nlz(nn);
        } else {
            cnt += size;
        }
    }

    return cnt;
}

static mrb_value
bs_clz(mrb_state *mrb, mrb_value self)
{
    mrb_get_args(mrb, "");
    return mrb_fixnum_value(bitset_clz(get_bitset(mrb, self)));
}

static int
count_ntz(uintptr_t n)
{
    /*
     * ゆくゆくは __builtin_ctz() / __builtin_ctzll() を使うかもしれない
     * https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html
     */

    return popcount((n & -n) - 1);
}

static size_t
bitset_ctz(const struct bitset *bs)
{
    size_t size = bitset_size(bs);
    const uintptr_t *head = bs->is_embed ? bs->ary : bs->ptr;
    const uintptr_t *p = head + unit_ceil(size, BS_WORDBITS) - 1;
    size_t cnt = 0;
    size_t rest = size % BS_WORDBITS;

    if (rest > 0) {
        uintptr_t sh = BS_WORDBITS - rest;
        uintptr_t nn = *p >> sh;
        if (nn) {
            return count_ntz(nn);
        } else {
            cnt += rest;
        }
        size -= rest;
        p --;
    }

    for (; p >= head; p --) {
        if (*p) {
            cnt += count_ntz(*p);
            break;
        }

        cnt += BS_WORDBITS;
    }

    return cnt;
}

static mrb_value
bs_ctz(mrb_state *mrb, mrb_value self)
{
    mrb_get_args(mrb, "");
    return mrb_fixnum_value(bitset_ctz(get_bitset(mrb, self)));
}

static int
fold_parity(uintptr_t n)
{
#if UINTPTR_MAX > UINT32_MAX
    n ^= n >> 32;
#endif
    n ^= n >> 16;
    n ^= n >>  8;
    n ^= n >>  4;
    n ^= n >>  2;
    n ^= n >>  1;
    return n & 1;
}

static int
bitset_parity(const struct bitset *bs)
{
    const uintptr_t *p = bs->is_embed ? bs->ary : bs->ptr;
    size_t size = bitset_size(bs);
    uintptr_t total = 0;

    for (; size >= BS_WORDBITS; size -= BS_WORDBITS, p ++) {
        total ^= *p;
    }

    if (size > 0) {
        /* 下位ビットを無効化するために右シフト */
        total ^= *p >> (BS_WORDBITS - size);
    }

    return fold_parity(total);
}

static mrb_value
bs_parity(mrb_state *mrb, mrb_value self)
{
    mrb_get_args(mrb, "");
    return mrb_fixnum_value(bitset_parity(get_bitset(mrb, self)));
}

static bool
bitset_all(const struct bitset *bs)
{
    const uintptr_t *p = bs->is_embed ? bs->ary : bs->ptr;
    size_t size = bitset_size(bs);

    for (; size >= BS_WORDBITS; size -= BS_WORDBITS, p ++) {
        if (~*p) { return false; }
    }

    if (size > 0) {
        if (~((intptr_t)*p >> (BS_WORDBITS - size))) {
            return false;
        }
    }

    return true;
}

static mrb_value
bs_all(mrb_state *mrb, mrb_value self)
{
    mrb_get_args(mrb, "");
    return mrb_bool_value(bitset_all(get_bitset(mrb, self)));
}

static bool
bitset_any(const struct bitset *bs)
{
    const uintptr_t *p = bs->is_embed ? bs->ary : bs->ptr;
    size_t size = bitset_size(bs);

    for (; size >= BS_WORDBITS; size -= BS_WORDBITS, p ++) {
        if (*p) { return true; }
    }

    if (size > 0) {
        if ((*p >> (BS_WORDBITS - size)) > 0) {
            return true;
        }
    }

    return false;
}

static mrb_value
bs_any(mrb_state *mrb, mrb_value self)
{
    mrb_get_args(mrb, "");
    return mrb_bool_value(bitset_any(get_bitset(mrb, self)));
}

static bool
bitset_none(const struct bitset *bs)
{
    const uintptr_t *p = bs->is_embed ? bs->ary : bs->ptr;
    size_t size = bitset_size(bs);

    for (; size >= BS_WORDBITS; size -= BS_WORDBITS, p ++) {
        if (*p) { return false; }
    }

    if (size > 0) {
        if ((*p >> (BS_WORDBITS - size)) > 0) {
            return false;
        }
    }

    return true;
}

static mrb_value
bs_none(mrb_state *mrb, mrb_value self)
{
    mrb_get_args(mrb, "");
    return mrb_bool_value(bitset_none(get_bitset(mrb, self)));
}

static bool
bitset_equal(const struct bitset *a, const struct bitset *b)
{
    size_t size = bitset_size(a);
    if (size != bitset_size(b)) { return false; }

    const uintptr_t *p = bitset_ptr_const(a);
    const uintptr_t *q = bitset_ptr_const(b);

    /* TODO: memcmp を使う */

    for (size_t i = size / BS_WORDBITS; i > 0; i --, p ++, q ++) {
        if (*p != *q) { return false; }
    }

    size %= BS_WORDBITS;

    if (size > 0) {
        size_t pad = BS_WORDBITS - size;
        if ((*p >> pad) != (*q >> pad)) { return false; }
    }

    return true;
}

static mrb_value
bs_eql(mrb_state *mrb, mrb_value self)
{
    mrb_value other;
    mrb_get_args(mrb, "o", &other);
    return mrb_bool_value(bitset_equal(get_bitset(mrb, self), get_bitset(mrb, other)));
}

static mrb_int
bitset_hash(const struct bitset *bs)
{
    /*
     * テーブルは utils/mkcrctbl.rb で算出したもの
     */
#if MRB_INT_MAX > INT32_MAX
    static const uint64_t table[] = {
        UINT64_C(0x0000000000000000), UINT64_C(0x42f0e1eba9ea3693),
        UINT64_C(0x85e1c3d753d46d26), UINT64_C(0xc711223cfa3e5bb5),
        UINT64_C(0x493366450e42ecdf), UINT64_C(0x0bc387aea7a8da4c),
        UINT64_C(0xccd2a5925d9681f9), UINT64_C(0x8e224479f47cb76a),
        UINT64_C(0x9266cc8a1c85d9be), UINT64_C(0xd0962d61b56fef2d),
        UINT64_C(0x17870f5d4f51b498), UINT64_C(0x5577eeb6e6bb820b),
        UINT64_C(0xdb55aacf12c73561), UINT64_C(0x99a54b24bb2d03f2),
        UINT64_C(0x5eb4691841135847), UINT64_C(0x1c4488f3e8f96ed4)
    };

    uint64_t crc = -1;
#else
    static const uint32_t table[] = {
        UINT32_C(0x00000000), UINT32_C(0x1edc6f41),
        UINT32_C(0x3db8de82), UINT32_C(0x2364b1c3),
        UINT32_C(0x7b71bd04), UINT32_C(0x65add245),
        UINT32_C(0x46c96386), UINT32_C(0x58150cc7),
        UINT32_C(0xf6e37a08), UINT32_C(0xe83f1549),
        UINT32_C(0xcb5ba48a), UINT32_C(0xd587cbcb),
        UINT32_C(0x8d92c70c), UINT32_C(0x934ea84d),
        UINT32_C(0xb02a198e), UINT32_C(0xaef676cf)
    };

    uint32_t crc = -1;
#endif

    size_t size = bitset_size(bs);
    const uintptr_t *p = bitset_ptr_const(bs);

    /*
     * NOTE: CRC 算出部分を共通化したほうが良さげ
     */

    for (; size >= BS_WORDBITS; size -= BS_WORDBITS, p ++) {
        uintptr_t n = *p;
        for (int i = BS_WORDBITS / 4; i > 0; i --, n <<= 4) {
            crc = (crc << 4) ^ table[0x0f & ((uint8_t)(crc >> (sizeof(crc) * 8 - 4)) ^ (uint8_t)(n >> (BS_WORDBITS - 4)))];
        }
    }

    if (size > 0) {
        int pad = BS_WORDBITS - size;
        uintptr_t n = *p >> pad << pad;
        for (; size >= 4; size -= 4, n <<= 4) {
            crc = (crc << 4) ^ table[0x0f & ((uint8_t)(crc >> (sizeof(crc) * 8 - 4)) ^ (uint8_t)(n >> (BS_WORDBITS - 4)))];
        }
        if (size > 0) {
            crc = (crc << size) ^ table[0x0f & ((uint8_t)(crc >> (sizeof(crc) * 8 - size)) ^ (uint8_t)(n >> (BS_WORDBITS - size)))];
        }
    }

#ifdef MRB_INT16
    return ~crc >> 8;
#else
    return ~crc;
#endif
}

MRB_API mrb_int
mruby_bitset_hash(mrb_state *mrb, mrb_value bitset)
{
    return bitset_hash(get_bitset(mrb, bitset));
}

/*
 * 32ビット CRC (0x1edc6f41) あるいは 64ビット CRC (0x42f0e1eba9ea3693) による算出を行う。
 * ビットは左送り (CRC-32-MPEG-2 や CRC-64-ECMA のように)。0 を後置する (“appends n 0-bits”)。
 * 初期値 (内部状態の値) は -1 で、出力時に -1 で 排他的論理和を取る。
 * 1 段ハーフバイトテーブルによるアルゴリズムを用いる。
 * MRB_INT16 の場合は、32ビット CRC 値を算出し、8ビット右シフトして下位16ビットを取り出した値が使われる。
 */
static mrb_value
bs_hash(mrb_state *mrb, mrb_value self)
{
    mrb_get_args(mrb, "");
    return mrb_fixnum_value(mruby_bitset_hash(mrb, self));
}

/*
 * TODO: bitset_digest/bitset_hexdigest/bitset_bindigest を共通化する
 */

static struct RString *
bitset_digest_common(mrb_state *mrb, const struct bitset *bs, int slicesize, int func(int, void *), void *opaque)
{
    size_t size = bitset_size(bs);
    if (size == 0) { return RSTRING(mrb_str_new(mrb, NULL, 0)); }

    const uintptr_t *p = bitset_ptr_const(bs);
    struct RString *digest = RSTRING(mrb_str_new(mrb, NULL, unit_ceil(size, slicesize)));
    char *d = RSTR_PTR(digest);

    for (; size >= BS_WORDBITS; size -= BS_WORDBITS, p ++) {
        uintptr_t n = *p;
        for (int i = BS_WORDBITS / slicesize; i > 0; i --, d ++, n <<= slicesize) {
            *d = func(n >> (BS_WORDBITS - slicesize), opaque);
        }
    }

    if (size > 0) {
        int pad = BS_WORDBITS - size;
        uintptr_t n = *p >> pad << pad;
        size += pad % slicesize; /* slicesize ビット境界を強制する */
        for (; size > 0; size -= slicesize, d ++, n <<= slicesize) {
            *d = func(n >> (BS_WORDBITS - slicesize), opaque);
        }
    }

    return digest;
}

static int
bitset_digest_char(int code, void *opaque)
{
    return code;
}

static struct RString *
bitset_digest(mrb_state *mrb, const struct bitset *bs)
{
    return bitset_digest_common(mrb, bs, 8, bitset_digest_char, NULL);
}

MRB_API struct RString *
mruby_bitset_digest(mrb_state *mrb, mrb_value bitset)
{
    return bitset_digest(mrb, get_bitset(mrb, bitset));
}

static mrb_value
bs_digest(mrb_state *mrb, mrb_value self)
{
    mrb_get_args(mrb, "");
    return mrb_obj_value(mruby_bitset_digest(mrb, self));
}

static int
bitset_hexdigest_char(int code, void *opaque)
{
    static const char table[] = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
    };

    return table[code];
}

static struct RString *
bitset_hexdigest(mrb_state *mrb, const struct bitset *bs)
{
    return bitset_digest_common(mrb, bs, 4, bitset_hexdigest_char, NULL);
}

MRB_API struct RString *
mruby_bitset_hexdigest(mrb_state *mrb, mrb_value bitset)
{
    return bitset_hexdigest(mrb, get_bitset(mrb, bitset));
}

static mrb_value
bs_hexdigest(mrb_state *mrb, mrb_value self)
{
    mrb_get_args(mrb, "");
    return mrb_obj_value(mruby_bitset_hexdigest(mrb, self));
}

static struct RString *
bitset_bindigest(mrb_state *mrb, const struct bitset *bs)
{
    size_t size = bitset_size(bs);
    if (size == 0) { return RSTRING(mrb_str_new(mrb, NULL, 0)); }

    const uintptr_t *head = bitset_ptr_const(bs);
    const uintptr_t *p = head;
    const size_t space8  = (size - 1) /  8; /* 8 ビットごとの区切り */
    const size_t space32 = (size - 1) / 32; /* 32 ビットごとの区切り */
    struct RString *digest = RSTRING(mrb_str_new(mrb, NULL, size + space8 + space32));
    char *d = RSTR_PTR(digest);

    for (; size >= BS_WORDBITS; size -= BS_WORDBITS, p ++) {
        uintptr_t n = *p;
        for (int i = BS_WORDBITS; i > 0; i --, d ++, n <<= 1) {
            if (i != BS_WORDBITS) {
                if (i % 8 == 0) { *d ++ = ' '; }
                if (i % 32 == 0) { *d ++ = ' '; }
            }
            *d = '0' + (n >> (BS_WORDBITS - 1));
        }

        if (size > 0) {
            *d ++ = ' ';
            *d ++ = ' ';
        }
    }

    if (size > 0) {
        int pad = BS_WORDBITS - size;
        uintptr_t n = *p >> pad << pad;

        for (int i = BS_WORDBITS; i > pad; i --, d ++, n <<= 1) {
            if (i != BS_WORDBITS) {
                if (i % 8 == 0) { *d ++ = ' '; }
                if (i % 32 == 0) { *d ++ = ' '; }
            }
            *d = '0' + (n >> (BS_WORDBITS - 1));
        }
    }

    return digest;
}

MRB_API struct RString *
mruby_bitset_bindigest(mrb_state *mrb, mrb_value bitset)
{
    return bitset_bindigest(mrb, get_bitset(mrb, bitset));
}

static mrb_value
bs_bindigest(mrb_state *mrb, mrb_value self)
{
    mrb_get_args(mrb, "");
    return mrb_obj_value(mruby_bitset_bindigest(mrb, self));
}

void
mrb_mruby_bitset_gem_init(mrb_state *mrb)
{
    struct RClass *bs = mrb_define_class(mrb, "Bitset", mrb->object_class);
    mrb_include_module(mrb, bs, mrb_module_get(mrb, "Enumerable"));

    mrb_define_const(mrb, bs, "BITWIDTH_MAX", mrb_fixnum_value(BITSET_WIDTH_MAX));
    mrb_define_const(mrb, bs, "WORD_BITSIZE", mrb_fixnum_value(BS_WORDBITS));
    mrb_define_const(mrb, bs, "EMBED_BITSIZE", mrb_fixnum_value(BS_EMBEDBITS));

    MRB_SET_INSTANCE_TT(bs, MRB_TT_DATA);

    mrb_define_method(mrb, bs, "initialize", bs_init, MRB_ARGS_ANY());
    mrb_define_method(mrb, bs, "initialize_copy", bs_init_copy, MRB_ARGS_ANY());
    mrb_define_method(mrb, bs, "size", bs_size, MRB_ARGS_NONE());
    mrb_define_method(mrb, bs, "capacity", bs_capacity, MRB_ARGS_NONE());
    mrb_define_method(mrb, bs, "reserve", bs_reserve, MRB_ARGS_ANY());              /* 拡張配列を予約する; c++:std::vector::reserve */
    mrb_define_method(mrb, bs, "shrink", bs_shrink, MRB_ARGS_ANY());                /* 拡張配列の空いている部分を解放する; c++:std::vector::shrink_to_fit */
    mrb_define_method(mrb, bs, "fill", bs_fill, MRB_ARGS_ANY());                    /* 全てのビットを 0 か 1 に設定する */
    mrb_define_method(mrb, bs, "clear", bs_clear, MRB_ARGS_ANY());                  /* 全てのビットを解放する; 長さを 0 にする; capacity は据え置き */
    mrb_define_method(mrb, bs, "concat", aux_implement_me, MRB_ARGS_ANY());         /* bitset の連結 */
    mrb_define_method(mrb, bs, "subset", aux_implement_me, MRB_ARGS_ANY());         /* bitset の部分取得 */

    mrb_define_method(mrb, bs, "aref", bs_aref, MRB_ARGS_ARG(1, 1));
    mrb_define_method(mrb, bs, "aset", bs_aset, MRB_ARGS_ARG(2, 2));

    mrb_define_method(mrb, bs, "popcount", bs_popcount, MRB_ARGS_ANY());            /* 1 の数を取得する; POPCNT */
    mrb_define_method(mrb, bs, "clz", bs_clz, MRB_ARGS_ANY());                      /* MSB から連続する 0 ビットを数える; Number of Leading Zero */
    mrb_define_method(mrb, bs, "ctz", bs_ctz, MRB_ARGS_ANY());                      /* LSB から連続する 0 ビットを数える; Number of Trailing Zero */
    mrb_define_method(mrb, bs, "parity", bs_parity, MRB_ARGS_ANY());                /* 1 ビットパリティを求める */
    mrb_define_method(mrb, bs, "all?", bs_all, MRB_ARGS_ANY());                     /* 全てが 1 であれば真 */
    mrb_define_method(mrb, bs, "any?", bs_any, MRB_ARGS_ANY());                     /* どこかが 1 であれば真 */
    mrb_define_method(mrb, bs, "none?", bs_none, MRB_ARGS_ANY());                   /* 全てが 0 であれば真 */

    mrb_define_method(mrb, bs, "bitreflect", bs_bitreflect, MRB_ARGS_ANY());        /* ビット列の前後を逆順にする */
    mrb_define_method(mrb, bs, "bitreflect!", bs_bitreflect_bang, MRB_ARGS_ANY());
    mrb_define_method(mrb, bs, "flip", bs_flip, MRB_ARGS_ANY());                    /* ビット反転; 1の補数 */
    mrb_define_method(mrb, bs, "flip!", bs_flip_bang, MRB_ARGS_ANY());
    mrb_define_method(mrb, bs, "minus", bs_minus, MRB_ARGS_ANY());                  /* ニの補数表現 */
    mrb_define_method(mrb, bs, "minus!", bs_minus_bang, MRB_ARGS_ANY());
    mrb_define_method(mrb, bs, "msb_or", bs_msb_or, MRB_ARGS_ANY());
    mrb_define_method(mrb, bs, "lsb_or", bs_lsb_or, MRB_ARGS_ANY());
    mrb_define_method(mrb, bs, "msb_nor", bs_msb_nor, MRB_ARGS_ANY());
    mrb_define_method(mrb, bs, "lsb_nor", bs_lsb_nor, MRB_ARGS_ANY());
    mrb_define_method(mrb, bs, "msb_and", bs_msb_and, MRB_ARGS_ANY());
    mrb_define_method(mrb, bs, "lsb_and", bs_lsb_and, MRB_ARGS_ANY());
    mrb_define_method(mrb, bs, "msb_nand", bs_msb_nand, MRB_ARGS_ANY());
    mrb_define_method(mrb, bs, "lsb_nand", bs_lsb_nand, MRB_ARGS_ANY());
    mrb_define_method(mrb, bs, "msb_xor", bs_msb_xor, MRB_ARGS_ANY());
    mrb_define_method(mrb, bs, "lsb_xor", bs_lsb_xor, MRB_ARGS_ANY());
    mrb_define_method(mrb, bs, "msb_xnor", bs_msb_xnor, MRB_ARGS_ANY());
    mrb_define_method(mrb, bs, "lsb_xnor", bs_lsb_xnor, MRB_ARGS_ANY());

    mrb_define_method(mrb, bs, "eql?", bs_eql, MRB_ARGS_ANY());
    mrb_define_method(mrb, bs, "hash", bs_hash, MRB_ARGS_ANY());

    mrb_define_method(mrb, bs, "digest", bs_digest, MRB_ARGS_ANY());
    mrb_define_method(mrb, bs, "hexdigest", bs_hexdigest, MRB_ARGS_ANY());
    mrb_define_method(mrb, bs, "bindigest", bs_bindigest, MRB_ARGS_ANY());
}

void
mrb_mruby_bitset_gem_final(mrb_state *mrb)
{
    (void)mrb;
}

mrb_value
mruby_bitset_new(mrb_state *mrb)
{
    return bitset_new(mrb, NULL, NULL);
}

size_t
mruby_bitset_size(mrb_state *mrb, mrb_value bitset)
{
    return bitset_size(get_bitset(mrb, bitset));
}

uintptr_t
mruby_bitset_aref(mrb_state *mrb, mrb_value bitset, intptr_t index, int bitwidth)
{
    return bitset_aref(mrb, bitset, index, bitwidth);
}

void
mruby_bitset_aset(mrb_state *mrb, mrb_value bitset, intptr_t index, int width, uintptr_t bits, int bitwidth)
{
    bitset_aset(mrb, bitset, index, width, bits, bitwidth);
}
