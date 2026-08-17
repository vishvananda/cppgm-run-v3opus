// VALIDATION: compile-pass
// N3485 focus: 7.2 [dcl.enum], 14.6 [temp.res], 8.3.4 [dcl.array]
//
// 7.2p1 gives an enumerator that writes no constant-expression the value of the
// one before it plus one.  So what a reading of a template definition knows of
// such an enumerator is exactly what it knew of its predecessor: where 14.6p8
// stood a value in for that one, the arithmetic here went through and says just
// as little about the specialization.
//
// The stand-in is the whole point of the reading - `sizeof(T)` has no value
// where the pattern stands - and every place that turns a value into a decision
// has to ask whether one was stood in.  8.3.4p1's bound is where that shows:
// `check[width == 4 ? 1 : -1]` reading an enumerator the pattern computed from
// a stand-in takes the arm the stand-in chose and refuses the bound it made.
//
// An enumerator that writes its own constant-expression is settled again by it,
// whether or not the one before it was; and outside a template nothing stands
// in, so an enumerator list is the plain arithmetic it has always been.

template<class T>
struct sized
{
  // 14.6p8 at the first: how large a dependent type is, an argument list is
  // what says.  7.2p1 carries that answer to the two that write nothing.
  enum { first = sizeof(T), second, third };
  typedef char check_first[first == 4 ? 1 : -1];
  typedef char check_second[second == 5 ? 1 : -1];
  typedef char check_third[third == 6 ? 1 : -1];

  // 7.2p1 again, one enumerator further along: the successor of one that wrote
  // its own constant-expression is settled by that one and not by anything
  // before it.
  enum { restarted = 10, after_restart };
  typedef char check_restarted[after_restart == 11 ? 1 : -1];

  // 5.19p2 one name further along: the member's own initializer read the
  // enumerator the pattern could not compute, so the member holds no constant
  // the pattern knows either, and a bound that reads it waits with it.
  static constexpr unsigned doubled = second * 2;
  typedef char check_doubled[doubled == 10 ? 1 : -1];
};

// 7.2p1 where no argument list is waited on: the same list, settled where it
// stands, which is what an ordinary enumeration has always been.
enum plain { begins = 3, follows, follows_again };
typedef char check_plain[follows == 4 ? 1 : -1];
typedef char check_plain_again[follows_again == 5 ? 1 : -1];

typedef sized<int>::check_first first_check;
typedef sized<int>::check_second second_check;
typedef sized<int>::check_third third_check;
typedef sized<int>::check_restarted fourth_check;
typedef sized<int>::check_doubled fifth_check;

int main()
{
  return sizeof(first_check) + sizeof(second_check) + sizeof(third_check) +
         sizeof(fourth_check) + sizeof(fifth_check) + sizeof(check_plain) +
         sizeof(check_plain_again) == 7 ? 0 : 1;
}
