// VALIDATION: compile-pass
// N3485 focus: 11.3 [class.friend], 14.6 [temp.res]
// `friend typename C::self;` names a class no reading of the definition can
// see, because which class it is an argument list is what says - so the grant
// is made where 14.7.1p1 reads the declaration again.

template<class C>
struct ref
{
  typedef typename C::held_type held_type;

  friend typename C::self;

private:
  ref(held_type v) : held(v) {}
  held_type held;
};

struct host
{
  typedef host self;
  typedef int held_type;

  ref<host> make(int v) { return ref<host>(v); }
};

struct other
{
  typedef other self;
  typedef long held_type;

  ref<other> make(long v) { return ref<other>(v); }
};

int main()
{
  host one;
  other two;
  ref<host> a = one.make(3);
  ref<other> b = two.make(4);
  (void)a;
  (void)b;
  return 0;
}
