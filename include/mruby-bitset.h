#ifndef MRUBY_BITSET_H
#define MRUBY_BITSET_H 1

#ifndef CHAR_BIT
# include <sys/limits.h>
#endif

#if CHAR_BIT != 8
# error <<< CHAR_BIT is not 8. mruby-bitset is not running on your system. >>>
#endif

#include <stdlib.h>
#include <stdint.h>
#include <mruby.h>
#include <mruby/string.h>

MRB_BEGIN_DECL

MRB_API mrb_value mruby_bitset_new(mrb_state *mrb);
MRB_API void mruby_bitset_resize(mrb_state *mrb, mrb_value bitset, size_t bitsize);
MRB_API size_t mruby_bitset_size(mrb_state *mrb, mrb_value bitset);
MRB_API size_t mruby_bitset_capacity(mrb_state *mrb, mrb_value bitset);
MRB_API uintptr_t mruby_bitset_aref(mrb_state *mrb, mrb_value bitset, intptr_t index, int bitwidth);
MRB_API void mruby_bitset_aset(mrb_state *mrb, mrb_value bitset, intptr_t index, int width, uintptr_t bits, int bitwidth);
MRB_API void mruby_bitset_aset_bitset(mrb_state *mrb, mrb_value bitset, intptr_t index, int width, mrb_value bits, int bitwidth);

/*
 * bitset のビット値を反転する
 */
MRB_API void mruby_bitset_flip(mrb_state *mrb, mrb_value bitset);

/*
 * bitset のビット順を前後反転する
 */
MRB_API void mruby_bitset_reverse(mrb_state *mrb, mrb_value bitset);

MRB_API int mruby_bitset_popcount(mrb_state *mrb, mrb_value bitset);

MRB_API mrb_int mruby_bitset_hash(mrb_state *mrb, mrb_value bitset);
MRB_API struct RString *mruby_bitset_digest(mrb_state *mrb, mrb_value bitset);
MRB_API struct RString *mruby_bitset_hexdigest(mrb_state *mrb, mrb_value bitset);
MRB_API struct RString *mruby_bitset_bindigest(mrb_state *mrb, mrb_value bitset);

MRB_END_DECL

#endif /* MRUBY_BITSET_H */
