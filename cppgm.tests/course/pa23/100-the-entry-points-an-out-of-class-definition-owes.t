// 9.3p2 with 14.7.1p1: a member defined outside the body of a class template is
// a definition this unit writes whole, so the object file owes both of the
// ABI's entry points for it - whichever of them the use that instantiated it
// happened to name.
template<int N> struct written_out
{
  int held[1];
  written_out();
};
template<int N> written_out<N>::written_out() : held{ N } {}
template<int N> struct written_in
{
  int held[1];
  written_in() : held{ N } {}
};
template<class T> struct derived_out : written_out<1> {};
template<class T> struct derived_in : written_in<1> {};
int main()
{
  derived_out<int> first;
  derived_in<int> second;
  return first.held[0] + second.held[0] - 2;
}
