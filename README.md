# mruby-bitset - variable length bit map manipulator

mruby の配列をビット値に特化したようなナニカです。

ビット長を固定する機能はありません。


## できること

ビット配列は最上位ビットから連続して最下位ビットへと続きます。0 と 1 から構成される mruby の配列と同じように考えて下さい。

  - 初期化 (`Bitset.new`)
  - ビット単位の追加 (`Bitset#push` / `Bitset#unshift`)
  - ビット単位の削除 (`Bitset#pop` / `Bitset#shift`)
  - 任意ビットの取得 (`Bitset#[]`)
  - 任意ビットの設定 (`Bitset#[]=`)
  - 全てのビットが 0 か 1 か、一つでも 1 が立っているかを確認する (`Bitset#all?` / `Bitset#none?` / `Bitset#any?`)
  - 全てのビットを列挙してブロックを呼ぶ (`Bitset#each`)
  - ビット長の取得 (`Bitset#size` / `Bitset#len`)
  - 二つのビットセットのハミング距離 (`Bitset#hamming`)
  - MSB から連続する 0 ビットの数え上げ (NLZ; Number of Leading Zero / CLZ; Counting Leading Zero) (`Bitset#clz`)
  - LSB から連続する 0 ビットの数え上げ (NTZ; Number of Trailing Zero / CTZ; Counting Trailing Zero) (`Bitset#ctz`)
  - 全体に含まれる 1 ビットの数え上げ (Counting 1 bits; Population Count) (`Bitset#popcount`)
  - 1ビットパリティの算出 (`Bitset#parity`)
  - 全ビットの反転 (`Bitset#flip` / `Bitset#flip!` / `Bitset#~`)
  - ニの補数の算出 (`Bitset#minus` / `Bitset#minus!` / `Bitset#twos_complement` / `Bitset#twos_complement!` / `Bitset#-`)
  - MSB を合わせての論理演算 (`Bitset#msb_or` / `Bitset#msb_and` / `Bitset#msb_xor` / `Bitset#msb_nor` / `Bitset#msb_nand` / `Bitset#msb_xnor` / `Bitset#|` / `Bitset#&` / `Bitset#^`)
  - LSB を合わせての論理演算 (`Bitset#lsb_or` / `Bitset#lsb_and` / `Bitset#lsb_xor` / `Bitset#lsb_nor` / `Bitset#lsb_nand` / `Bitset#lsb_xnor`)


## くみこみかた

`build_config.rb` ファイルに `gem github: "dearblue/mruby-bitset"` を任意の場所に追加して下さい。

```ruby
# build_config.rb

MRuby::Build.new do |conf|
  ...
  conf.gem github: "dearblue/mruby-bitset"
  ...
end
```

あるいは `mrbgem.rake` ファイルに依存する mrbgem として追加して下さい。

```ruby
# mrbgem.rake

MRuby::Gem::Specification.new("your-mgem") do |spec|
  ...
  spec.add_dependency "mruby-bitset", github: "dearblue/mruby-bitset"
  ...
```


## つかいかた

```ruby
a = Bitset.new
# => #<Bitset [0]>
83.times { a << rand(2) }; a
# => #<Bitset [83] 10001100 00111001 00110001 01110000  10101110 01110010 00101101 00110001  11101010 11101110 010>
b = Bitset.new("111100001111000011110000111100001111")
# => #<Bitset [36] 11110000 11110000 11110000 11110000  1111>
a | b
# => #<Bitset [83] 11111100 11111001 11110001 11110000  11111110 01110010 00101101 00110001  11101010 11101110 010>
a & b
# => #<Bitset [83] 10000000 00110000 00110000 01110000  10100000 00000000 00000000 00000000  00000000 00000000 000>
a ^ b
# => #<Bitset [83] 01111100 11001001 11000001 10000000  01011110 01110010 00101101 00110001  11101010 11101110 010>
a
# => #<Bitset [83] 10001100 00111001 00110001 01110000  10101110 01110010 00101101 00110001  11101010 11101110 010>
a.shift 5
# => 17
a
# => #<Bitset [78] 10000111 00100110 00101110 00010101  11001110 01000101 10100110 00111101  01011101 110010>
a.popcount
# => 39
```


## Specification

  - Package name: mruby-bitset
  - Version: 0.0.0.1.CONCEPT.TRYOUT
  - Licensing: [2 clause BSD License](LICENSE)
  - Product quality: CONCEPT, ***BUGGY***
  - Project page: <https://github.com/dearblue/mruby-bitset>
  - Author: [dearblue](https://github.com/dearblue)
  - Support mruby version: ?
  - Object code size: +25〜35 kb (depending on how optimization) (on FreeBSD 11.2 AMD64 with clang-6.0)
  - Used heap size per object: `sizeof(uintptr_t[3]) + sizeof(uintptr_t[4 * N])` bytes (`N` is zero or more)
  - Dependency external mrbgems: (NONE)
  - Dependency C libraries: (NONE)
