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

    specify 'two-way assocication (GC)' do
      arr = 20.times.map { Associated.new }
      arr.each do |assoc|
        expect(assoc.return_self).to eq assoc
      end
    end

    specify 'C++ exception' do
      obj = Base.new('hello')
      expect { obj.cxx_exception }.to raise_error(RuntimeError, 'std::out_of_range: pui')
    end

    specify 'Unknown C++ exception' do
      obj = Base.new('hello')
      expect { obj.cxx_exception_unknown }.to raise_error(RuntimeError, /\Aint/)
    end

    specify 'Ruby exception' do
      obj = Base.new('hello')
      expect { obj.ruby_exception(RangeError.new('pui')) }.to raise_error(RangeError, 'pui')
    end

    specify 'Ruby format' do
      obj = Base.new('hello')
      expect { obj.ruby_exception_format(RangeError, 'pui') }.to raise_error(RangeError, 'format pui')
    end
  end
end
