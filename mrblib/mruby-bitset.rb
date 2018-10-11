#!ruby

class Bitset
  Bitset = self

  def each
    return to_enum(:each) unless block_given?
    size.times { |i| yield aref(i) }
    self
  end

  def each_byte
    return to_enum(:each_byte) unless block_given?
    ((size + 7) / 8).to_i.times { |i| yield aref(i * 8, 8) }
    self
  end

  def each_slice(bitsize)
    return to_enum(:each_slice, bitsize) unless block_given?
    ((size + bitsize - 1) / bitsize).to_i.times { |i| i *= bitsize; yield aref(i, bitsize) }
    self
  end

  def each_with_index
    return to_enum(:each_with_index) unless block_given?
    size.times { |i| yield aref(i), i }
    self
  end

  def each_with_object(o)
    return to_enum(:each_with_object, o) unless block_given?
    size.times { |i| yield aref(i), o }
    self
  end

  def each_boolean
    return to_enum(:each_boolean) unless block_given?
    size.times { |i| yield test(i) }
    self
  end

  alias each_bool each_boolean

  def reverse_each
    return to_enum(:reverse_each) unless block_given?
    ss = size
    ss.times { |i| i = ss - i - 1; yield aref(i) }
    self
  end

  def reverse_each_byte
    return to_enum(:reverse_each_byte) unless block_given?
    ss = ((size + 7) / 8).to_i
    ss.times { |i| i = (ss - i - 1) * 8; yield aref(i, 8) }
    self
  end

  def reverse_each_slice(bitsize)
    return to_enum(:reverse_each_slice, bitsize) unless block_given?
    ss = ((size + bitsize - 1) / bitsize).to_i
    ss.times { |i| i = (ss - i - 1) * bitsize; yield aref(i, bitsize) }
    self
  end

  def push(bitset, width = 1)
    aset(size, 0, width.to_i, bitset)
    self
  end

  def pop(width = 1)
    width = width.to_i
    bitset = aref(size - width, width)
    aset(size - width, width, 0, 0)
    bitset
  end

  def unshift(bitset, width = 1)
    aset(0, 0, width.to_i, bitset)
    self
  end

  def shift(width = 1)
    width = width.to_i
    bitset = aref(0, width)
    aset(0, width, 0, 0)
    bitset
  end

  def empty?
    size == 0
  end

  def bytes(&block)
    a = []
    ((size + 7) / 8).to_i.times { |i| a << aref(i * 8, 8) }
    a
  end

  def slices(bitsize)
    a = []
    ((size + bitsize - 1) / bitsize).to_i.times { |i| i *= bitsize; a << aref(i, bitsize) }
    a
  end

  def to_a
    a = []
    size.times { |i| a << aref(i) }
    a
  end

  def aref_byte(index)
    aref(index * 8, 8)
  end

  def aset_byte(index, byte)
    aset(index * 8, 8, byte.to_i)
  end

  def first(width = 1)
    aref(0, width.to_i)
  end

  def last(width = 1)
    width = width.to_i
    aref(size - width, width)
  end

  def test(index)
    aref(index.to_i) != 0
  end

  def drop(index, width = 1)
    aset(index.to_i, width.to_i, 0, 0)
    self
  end

  def hamming(other)
    (self ^ other).popcount
  end

  def |(other)
    dup.msb_or other
  end

  def &(other)
    dup.msb_and other
  end

  def ^(other)
    dup.msb_xor other
  end

  alias [] aref
  alias []= aset
  alias len size
  alias length size
  #alias << lsh
  #alias >> rsh
  alias << push
  alias reverse bitreflect
  alias reverse! bitreflect!
  alias +@ clone
  alias -@ minus
  alias twos_complement minus
  alias twos_complement! minus!
  alias ~ flip
  alias == eql?
  alias delete_at drop
  alias nlz clz
  alias count_nlz clz
  alias ntz ctz
  alias count_ntz ctz
  alias to_s bindigest

  def inspect
    s = "#<#{self.class} [#{size}]"

    unless empty?
      s[s.size, 0] = " "
      s[s.size, 0] = bindigest
    end

    s[s.size, 0] = ">"
    s
  end

  def Bitset.spec
    {
      BITWIDTH_MAX: BITWIDTH_MAX,
      WORD_BITSIZE: WORD_BITSIZE,
      EMBED_BITSIZE: EMBED_BITSIZE,
    }
  end
end

BitSet ||= Bitset
