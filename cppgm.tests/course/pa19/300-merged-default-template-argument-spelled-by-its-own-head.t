// 14.1p10 and 14.1p2: the default template-arguments available to a use are
// the ones the definition and every declaration in scope wrote, merged - and
// each of those heads spells the places as it likes, so a default a
// redeclaration adds names the parameters before it by the names that head
// gave them and not by the definition's.
template<class T>
struct fallback
{
  typedef T type;
};

template<class First, class Second>
struct box
{
  typedef Second type;
};

template<class Written, class Filled = fallback<Written> >
struct box;

typedef box<int>::type result;

int expect(fallback<int> *);

int main()
{
  result * ptr = 0;
  return expect(ptr);
}
