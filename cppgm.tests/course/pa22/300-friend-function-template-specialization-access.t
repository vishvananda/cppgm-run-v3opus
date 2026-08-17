// VALIDATION: compile-pass
// N3485 focus: 14.5.4 [temp.friend], 11.3 [class.friend]
// A friend declaration whose declarator-id is a template-id names a
// specialization of a function template and declares nothing of its own: the
// template is what the enclosing namespace already declared, and what is left
// is the grant.

namespace n
{

template<class C>
struct box;

template<class C>
int deduced(const box<C>&);

template<class C>
int written(const box<C>&);

template<class C>
struct box
{
  box(int v) : held(v) {}

private:
  int held;

  friend int deduced<>(const box&);
  friend int written<C>(const box<C>&);

  template<class D>
  friend int under_a_head(const box<D>&);
};

template<class C>
int deduced(const box<C>& b)
{
  return b.held;
}

template<class C>
int written(const box<C>& b)
{
  return b.held;
}

template<class D>
int under_a_head(const box<D>& b)
{
  return b.held;
}

}

int main()
{
  n::box<char> one(1);
  n::box<long> two(2);
  return n::deduced(one) + n::written(two) + n::under_a_head(one) - 4;
}
