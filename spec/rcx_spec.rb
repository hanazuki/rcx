# SPDX-License-Identifier: BSL-1.0
# SPDX-FileCopyrightText: Copyright 2024-2025 Kasumi Hanazuki <kasumi@rollingapple.net>
require 'test.so'

RSpec.describe RCX do
  it "has a version number" do
    expect(RCX::VERSION).not_to be nil
  end
end

module Assertions
  def assert(val) = expect(val).to be_truthy
  def assert_not(val) = expect(val).to be_falsy
  def assert_nil(val) = expect(val).to be_nil
  def assert_kind_of(cls, val) = expect(val).to be_kind_of(cls)
  def assert_equal(expected, val) = expect(val).to eq(expected)
  def assert_same(expected, val) = expect(val).to be(expected)
  def assert_send(val, *args) = expect(val.send(*args)).to be_truthy
  def assert_not_predicate(val, *args) = expect(val.send(*args)).to be_falsy
  def assert_match(matcher, val) = expect(val).to match matcher
end

RSpec.describe 'test ext' do
  around do |example|
    old, GC.stress = GC.stress, true
    example.run
  ensure
    GC.stress = old
    GC.verify_compaction_references(expand_heap: true, toward: :empty)
  end if ENV.key?('GCSTRESS')

  describe 'native tests' do
    ms = Test.instance_methods(false).grep(/^test_/).sort
    fail if ms.size == 0

    before do
      extend Assertions
      extend Test
    end

    ms.each do |m|
      specify m.to_s do
        send(m)
      end
    end
  end

  describe 'typed data' do
    specify 'classes' do
      expect(Base).to be_kind_of Class
      expect(Base.superclass).to be Object
      expect(Derived).to be_kind_of Class
      expect(Derived.superclass).to be Base
    end

    specify 'base' do
      base = Base.new('hello')
      aggregate_failures do
        expect(base).to be_kind_of Base
        expect(base.string).to eq 'hello'
        expect(base.virtual_1).to eq 'base'
      end
    end

    specify 'mutation' do
      obj = Base.new('hello')
      obj.string = 'hello world'
      expect(obj.string).to eq 'hello world'

      obj.freeze
      expect(obj).to be_frozen
      expect(obj.string).to eq 'hello world'
      expect { obj.string = 'hi again' }.to raise_error FrozenError
    end

    specify 'inheritance' do
      derived = Derived.new('hello')
      aggregate_failures do
        expect(derived).to be_kind_of Derived
        expect(derived.string).to eq 'hello'
        expect(derived.virtual_1).to eq 'derived'
      end
    end

    describe 'two-way associatetion' do
      specify 'GC safety' do
        arr = 20.times.map { Associated.new }
        arr.each do |assoc|
          expect(assoc.return_self).to be assoc
        end
      end

      specify 'clone' do
        obj = Associated.new
        expect(obj.return_self).to be obj

        obj2 = obj.clone
        expect(obj2.return_self).to be obj2

        obj3 = obj.freeze.clone(freeze: false).clone
        expect(obj3.return_self).to be obj3
      end

      specify 'tuple' do
        arr = [Associated.new, Associated.new]

        expect(Associated.swap(arr)).to contain_exactly(be(arr[1]), be(arr[0]))
      end
    end

    specify 'throw C++ exception' do
      obj = Base.new('hello')
      expect { obj.cxx_exception }.to raise_error(RuntimeError, 'std::out_of_range: pui')
    end

    specify 'throw Unknown C++ exception' do
      obj = Base.new('hello')
      expect { obj.cxx_exception_unknown }.to raise_error(RuntimeError, /\Aint/)
    end

    specify 'throw RubyError' do
      obj = Base.new('hello')
      expect { obj.ruby_exception(RangeError.new('pui')) }.to raise_error(RangeError, 'pui')
    end

    specify 'throw RubyError::format' do
      obj = Base.new('hello')
      expect { obj.ruby_exception_format(RangeError, 'pui') }.to raise_error(RangeError, 'format pui')
    end

    specify 'exception ruby->c++->ruby' do
      exc = RangeError.new('pui')

      obj = Base.new('hello')
      expect { obj.callback(-> { raise exc }) }.to raise_error {|e|
        expect(e).to be exc
      }
    end

    specify 'block' do
      obj = Base.new('hello')

      ret = obj.with_block('A') {|x|
        expect(x).to eq 'A'
        'B'
      }
      expect(ret).to eq 'B'
    end

    specify 'optional block' do
      obj = Base.new('hello')

      expect(obj.with_block_opt('A')).to eq 'A'
      expect(obj.with_block_opt('A', &:succ)).to eq 'B'
    end

    specify 'clone' do
      obj = Base.new('hello')
      obj2 = obj.clone
      expect(obj2.string).to eq 'hello'

      obj.string = 'pui'
      expect(obj.string).to eq 'pui'
      expect(obj2.string).to eq 'hello'

      obj3 = obj.freeze.clone(freeze: false)
      obj3.string = 'obj3'
      expect(obj3.string).to eq 'obj3'
    end
  end
end
