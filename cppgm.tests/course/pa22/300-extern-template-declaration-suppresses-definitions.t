// VALIDATION: compile-pass
// N3485 focus: 14.7.2 [temp.explicit]
//
// p9's `extern template` names a specialization and p10 suppresses the implicit
// instantiation of it, so this unit writes a declaration of every definition
// another unit was said to hold: the function template's specialization, the
// member function a class template defines outside its class, and the storage
// 9.4.2p2's definition of a static data member lays out.  A member defined *in*
// its class is inline, which p10's note leaves where it was.

template<class T>
T twice(T x)
{
  return x + x;
}

template<class T>
struct holder
{
  int inside() { return 1; }
  int outside();
  static int count;
};

template<class T>
int holder<T>::outside()
{
  return 2;
}

template<class T>
int holder<T>::count = 3;

extern template int twice(int);
extern template struct holder<int>;

int main()
{
  holder<int> one;
  return twice(0) + one.inside() + one.outside() + holder<int>::count - 6;
}
