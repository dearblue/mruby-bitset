#ifndef MRUBY_BITSET_INTERNALS_H
#define MRUBY_BITSET_INTERNALS_H 1

#include <mruby.h>
#include <mruby/class.h>
#include <mruby/variable.h>
#include <mruby/array.h>
#include <mruby/hash.h>
#include <mruby/value.h>
#include <mruby/data.h>
#include <mruby/error.h>
#include <stdlib.h>
#include "../include/mruby-bitset.h"
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

#if MRUBY_RELEASE_NO < 10400
# define ARY_LEN(A) ((A)->len)
# define ARY_PTR(A) (RARRAY(A)->ptr)
#endif

#ifndef MRBX_FORCE_INLINE
# ifdef MRB_FORCE_INLINE
#  define MRBX_FORCE_INLINE MRB_FORCE_INLINE
# elif defined(__GNUC__) || defined (__clang__)
#  define MRBX_FORCE_INLINE static inline __attribute__((always_inline))
# elif defined _MSC_VER
#  define MRBX_FORCE_INLINE static __forceinline
# else
#  define MRBX_FORCE_INLINE MRBX_INLINE
# endif
#endif

#define MRBX_FILE   mrb_str_new_lit(mrb, __FILE__)
#define MRBX_LINE   mrb_fixnum_value(__LINE__)
#define MRBX_FUNC   mrb_str_new_static(mrb, __func__, strlen(__func__))

#ifndef E_FROZEN_ERROR
# define E_FROZEN_ERROR E_RUNTIME_ERROR
#endif

#define LOG() do { fprintf(stderr, "\x1b[1m[%s:%d:%s]\x1b[m\n", __FILE__, __LINE__, __func__); } while (0)
#define LOGS(STR) do { fprintf(stderr, "\x1b[1m[%s:%d:%s]\x1b[m %s\n", __FILE__, __LINE__, __func__, STR); } while (0)
#define LOGF(FORMAT, ...) do { fprintf(stderr, "\x1b[1m[%s:%d:%s]\x1b[m " FORMAT "\n", __FILE__, __LINE__, __func__, __VA_ARGS__); } while (0)

#define TODO(STR)                                                           \
    do {                                                                    \
        mrb_raisef(mrb, E_NOTIMP_ERROR,                                     \
                   "TODO - %S - %S#%S (%S:%S:%S)",                          \
                   mrb_str_new_lit(mrb, STR),                               \
                   mrb_obj_value(mrb->c->ci->target_class),                 \
                   mrb_symbol_value(mrb_get_mid(mrb)),                      \
                   MRBX_FILE, MRBX_LINE, MRBX_FUNC);                        \
    } while (0)                                                             \

MRBX_FORCE_INLINE bool
mrbx_true_p(mrb_value obj)
{
    return mrb_type(obj) == MRB_TT_TRUE;
}

MRBX_FORCE_INLINE bool
mrbx_false_p(mrb_value obj)
{
    return mrb_type(obj) == MRB_TT_FALSE && !mrb_nil_p(obj);
}

MRBX_FORCE_INLINE bool
mrbx_frozen_p(mrb_value obj)
{
    struct RBasic *p = mrb_basic_ptr(obj);
    return (bool)MRB_FROZEN_P(p);
}

MRBX_FORCE_INLINE void
mrbx_obj_modify(mrb_state *mrb, mrb_value obj)
{
    if (mrb_immediate_p(obj) || mrbx_frozen_p(obj)) {
        mrb_raisef(mrb, E_FROZEN_ERROR, "can't modify frozen object - %S", mrb_any_to_s(mrb, obj));
    }
}

static mrb_value
aux_implement_me(mrb_state *mrb, mrb_value self)
{
    mrb_raisef(mrb, E_NOTIMP_ERROR,
               "IMPLEMENT ME! - %S#%S",
               mrb_obj_value(mrb_obj_class(mrb, self)),
               mrb_symbol_value(mrb_get_mid(mrb)));
    return mrb_nil_value();
}

#endif /* MRUBY_BITSET_INTERNALS_H */
