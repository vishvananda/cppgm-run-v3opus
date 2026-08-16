// VALIDATION: compile-pass
// 14.5.1.3p1 with 3.4.1p8: an out-of-class definition of a static data member
// of a class template reads its initializer's names in the class too, under a
// head of the definition's own - so the member typedefs the class declares are
// what `value_type` and `difference_type` name there.

template<class V, class W>
struct block
{
  static const W value = 4;
};

template<class T>
struct deque
{
  typedef T value_type;
  typedef long difference_type;
  static const difference_type block_size;
};

template<class T>
const typename deque<T>::difference_type deque<T>::block_size =
    block<value_type, difference_type>::value;

template<class T>
struct sized
{
  typedef T element_type;
  typedef block<element_type, long> held;
  static const long width;
};

template<class T>
const long sized<T>::width = held::value;

int main()
{
  return deque<int>::block_size == 4 && sized<int>::width == 4 ? 0 : 1;
}
