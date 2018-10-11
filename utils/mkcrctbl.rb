#!ruby

[[32, 0x1edc6f41], [64, 0x42f0e1eba9ea3693]].each do |bits, poly|
  msb = bits - 1

  puts <<-C_CODE
    static const uint#{bits}_t table[] = {
  C_CODE

  (0...16).step(2).map { |i|
    print ",\n" unless i == 0
    print "        ", 2.times.map { |j|
      n = (i + j) << (bits - 4)
      4.times { n = (n << 1) ^ (n[msb].zero? ? 0 : poly) }
      n &= ~(-1 << bits)
      %(UINT%d_C(0x%0*x)) % [bits, bits / 4, n]
    }.join(", ")
  }

  puts <<-C_CODE

    };
  C_CODE
end
