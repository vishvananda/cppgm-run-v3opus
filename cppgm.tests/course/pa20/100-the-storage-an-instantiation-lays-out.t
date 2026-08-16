// VALIDATION: compile-pass
// 14.7.1p1 with 3.2p3: the definition written outside a class template is one
// no unit wrote for any one argument list, so the storage it lays out belongs
// to the program where the program reaches it - exactly as the body of a
// member function of the same specialization does.  An object of the class is
// one such reach, and so is a name that takes the member's address; a member
// 9.4.2p3 makes a constant, read only for its value, is not.
//
// 14.7.3p1's `template<>` is written out for one argument list and is this
// unit's own definition however little of the unit reaches it.

template<class T, T V>
struct source
{
  static const T least = V;
};

template<class T, T V>
const T source<T, V>::least;

struct traits : source<int, -100>
{
};

template<int N>
struct box
{
  static const int value = N;
};

// No object of `source<int, -100>` is laid out and nothing takes the address
// of its member, so this unit lays out no storage for it.
typedef box<traits::least> folded;

template<class T, T V>
struct held
{
  static const T kept = V;
};

template<class T, T V>
const T held<T, V>::kept;

// An object of the class is what reaches every static data member it declares.
held<int, 7> object_of_it;

template<class T, T V>
struct addressed
{
  static const T taken = V;
};

template<class T, T V>
const T addressed<T, V>::taken;

const int * const pointed = &addressed<int, 9>::taken;

template<int N>
struct written
{
  static unsigned char bytes[];
};

template<>
unsigned char written<4>::bytes[] = {1, 2, 3, 4};

int main()
{
  return folded::value == -100 && sizeof(written<4>::bytes) == 4 &&
         *pointed == 9 && held<int, 7>::kept == 7
    ? 0 : 1;
}
