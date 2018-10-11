#!ruby

require "yaml"

configurations = YAML.load(<<"CONFIGURATIONS")
build:
  host:
    enables: [debug, test]
    gems:
    - :core: mruby-sprintf
    - :core: mruby-print
    - :core: mruby-random
    - :core: mruby-bin-mrbc
    - :core: mruby-bin-mirb
    - :core: mruby-bin-mruby
  host16-nan:
    enables: [debug, test]
    defines: [MRB_NAN_BOXING, MRB_INT16]
    gems:
    - :core: mruby-print
    - :core: mruby-bin-mrbc
    - :core: mruby-bin-mruby
  host-word:
    enables: [debug, test, word boxing]
    gems:
    - :core: mruby-print
    - :core: mruby-bin-mrbc
    - :core: mruby-bin-mruby
  host-opt:
  host-opt-word:
    enables: word boxing
  host-small:
    enables: word boxing
    cflags: "-Os"
  host-small-m32:
    enables: word boxing
    cflags: ["-Os", "-m32"]
    ldflags: "-m32"
CONFIGURATIONS

configurations["build"].each_pair do |n, c|
  c ||= {}

  MRuby::Build.new n do |conf|
    toolchain :clang

    conf.build_dir = c["build dir"] || name

    if n == "host"
      if cc.command =~ /\b(?:g?cc|clang)\d*\b/
        cc.flags << "-std=c11"
        cc.flags << "-pedantic"
        cc.flags << "-Wall"
      end
    end

    if d = c["defines"]
      d.compact!
      cc.defines << d
      cxx.defines << d
    end

    cc.flags << Array(c["cflags"]).compact
    linker.flags << Array(c["ldflags"]).compact
    cc.defines << Array(c["c defines"]).compact
    cxx.defines << Array(c["cxx defines"]).compact

    Array(c["enables"]).each do |f|
      case f
      when "debug"
        enable_debug
        cc.defines  << "MRB_ENABLE_DEBUG_HOOK"
        gem core: "mruby-bin-debugger"
      when "test"
        enable_test
      when /\Ac(?:xx|\+\+)[ _]exception\z/
        enable_cxx_exception
      when /\Ac(?:xx|\+\+)[ _]abi\z/
        enable_cxx_abi
      when "bintest"
        enable_bintest
      when "word boxing", "word_boxing"
        cc.defines << "MRB_WORD_BOXING"
        #cc.defines << "MRB_INT64" if [nil].pack("P").bytesize == 8
      when "nan boxing", "nan_boxing"
        cc.defines << "MRB_NAN_BOXING"
      else
        $stderr.puts "[IGNORED] unknown feature ``#{f}'' for build:#{n}:enables"
      end
    end

    Array(c["gems"]).each { |*g| gem *g }

    gem File.dirname(__FILE__)
  end
end
