// N3485 focus: 14.5.5.1 [temp.class.spec.match] p1 with 14.6.2 [temp.dep] p1 -
// the pattern an argument list is read from is chosen where the list is known,
// so a template-id written inside another template chooses nothing until that
// template is instantiated.  The choice is a fact of the template rather than
// of the naming, so the same list reaches the same pattern from a nested
// region, from a namespace, and through a typedef-name.
namespace outer {

template<class T>
struct trait {
  typedef int type;
  static const int which = 0;
};

template<class T>
struct trait<T *> {
  typedef char type;
  static const int which = 1;
};

}

template<class T>
struct through {
  typedef typename outer::trait<T>::type type;
  static const int which = outer::trait<T>::which;
};

typedef outer::trait<long *> named;

int main()
{
  if (through<long>::which != 0) { return 1; }
  if (through<long *>::which != 1) { return 2; }
  if (sizeof(through<long>::type) != sizeof(int)) { return 3; }
  if (sizeof(through<long *>::type) != sizeof(char)) { return 4; }
  if (named::which != 1) { return 5; }
  return 0;
}
