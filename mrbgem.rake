#!ruby

MRuby::Gem::Specification.new("mruby-bitset") do |s|
  s.summary = "variable length bit map manipulator"
  s.version = File.read(File.join(File.dirname(__FILE__), "README.md")).scan(/^ *[-*] version: *(\d+(?:\.\w+)+)/i).flatten[-1]
  s.license = "BSD-2-Clause"
  s.author  = "dearblue"
  s.homepage = "https://github.com/dearblue/mruby-bitset"

  add_test_dependency "mruby-random", core: "mruby-random"
  #add_dependency "mruby-enum-ext", core: "mruby-enum-ext", weak: true
  #add_dependency "mruby-enumerator", core: "mruby-enumerator", weak: true
end
