#!ruby

assert "Bitset class" do
  assert_equal Class, Bitset.class
end

assert "initialize" do
  bs = Bitset.new
  assert_equal Bitset, bs.class
end

assert "aset and aref" do
  bs = Bitset.new
  bs[0] = 0
  assert_equal 1, bs.size
  assert_equal 0, bs[0]
  bs[0] = 1
  assert_equal 1, bs[0]
  assert_equal 0, bs[1]
end

__END__

p Bitset.spec
bs = Bitset.new
xx = 0x13
bs.push 20, 6
bs.push xx, 7
bs.push xx, 7
bs.push xx, 7
bs.push xx, 7
bs.push xx, 7
bs.push xx, 7
bs.push xx, 7
bs.push xx, 7
bs.push xx, 7
p bs[0, 6].to_s(2)
p bs
bs[186, 5, 6] = 63
p bs
p bs.shift(4)
p bs.shift
p bs.pop(7)
p bs
p bs.flip!
p bs.flip
p bs
p bs.to_a
bs = Bitset.new
64.times { bs << rand(2) }
p [bs.popcount, bs]
a = []
64.times { a << bs.shift }
p [a, bs]
p bs.popcount
bs.class.new








20.times { bs = Bitset.new
bs.push(0, 56)
16.times { bs << rand(2) }
bs.push(0, 16)
printf "%3d %3d %3d %5p %5p %5p %p\n", bs.popcount, bs.clz, bs.ctz, bs.all?, bs.any?, bs.none?, bs }
bs = Bitset.new
bs.push 0, 64
bs.push 0, 64
printf "%3d %3d %3d %p\n", bs.popcount, bs.clz, bs.ctz, bs


# assert("TEST SUBJECT HERE") do
#   assert_equal EXPECT_VALUE, ACTURE_VALUE
#   assert_raise(EXPECT_ERROR) { error statment }
# end
