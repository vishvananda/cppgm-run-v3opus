// 14.5.3p4: a parameter pack whose run holds no elements declares no place, so
// the function type a constructor template's specialization has takes no
// argument for it.  14.7.1p1 reads 12.1p1's pattern again to build that
// specialization a body, and the reading that does it is the one that gives the
// declaration its type - so the count has to be the same one both times, or the
// second naming of one specialization finds a declaration no call of it fits.
struct plain { int held; };
struct parenthesized
{
  plain value;
  template<class... A> parenthesized(A&&... a)
    : value(static_cast<A&&>(a)...) {}
};
struct counted
{
  int value;
  template<class... A> counted(A&&... a) : value(static_cast<A&&>(a)...) {}
};
int main()
{
  parenthesized first;
  parenthesized second;
  counted third;
  counted fourth;
  return first.value.held + second.value.held + third.value + fourth.value;
}
