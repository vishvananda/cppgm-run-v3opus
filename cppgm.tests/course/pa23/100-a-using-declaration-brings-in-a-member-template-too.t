// VALIDATION: run-pass
// 13.3.1p4's last sentence with 7.3.3p1: a function a using-declaration brought
// into a derived class is a member of *that* class for the type of its implicit
// object parameter, so the object a call names is the derived object and 11.2p5
// has no base-specifier to ask about - which is what lets a private base's
// member be reached through the class that published it.  14.5.2p1 gives the
// clause a second exit: what 13.3 chose may be a specialization the arguments
// made of a member template, and that specialization carries none of the
// using-declaration's own facts - the template it was made of does.

struct base
{
  int value;
  template<int I> int & at() { return value; }
  int plain() { return 7; }
  template<class T> int operator+(T) const { return 7; }
};

class holder : private base
{
public:
  holder() { at<0>() = 7; }
  using base::at;
  using base::plain;
  using base::operator+;
};

int main()
{
  holder h;
  return h.at<0>() == 7 && h.plain() == 7 && (h + 1) == 7 ? 0 : 1;
}
