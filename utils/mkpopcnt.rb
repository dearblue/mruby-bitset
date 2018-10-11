#!ruby

puts <<-C_CODE
    static const char table[] = {
C_CODE

16.times do |i|
  print "       "
  16.times do |j|
    x = i * 16 + j
    printf " %d,", 8.times.reduce(0) { |a, k| a + x[k] }
  end
  puts
end

puts <<-C_CODE
    };
C_CODE
