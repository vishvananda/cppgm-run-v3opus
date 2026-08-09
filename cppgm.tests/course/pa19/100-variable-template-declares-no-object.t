// N3485 focus: 14p1 [temp] - a template-declaration declares no object.  A
// declarator under a template head that is not a function names storage an
// argument list is what makes, so the declaration is a pattern exactly as a
// class template's is and this unit lays out nothing for it.

template<class T, class U, class = void>
const bool same_type = false;

template<class T>
const bool same_type<T, T> = true;

template<class T>
struct holder
{
  T held;
};

int main()
{
  holder<int> one;
  one.held = 0;
  return one.held;
}
