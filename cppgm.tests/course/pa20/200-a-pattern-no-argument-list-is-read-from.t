// N3485 focus: 14.5.5 [temp.class.spec] p1 with 14.5.5.1 [temp.class.spec.match]
// p1 - a partial specialization is a second body one argument list may be read
// from, so a declaration of one that cannot be read is not a declaration that
// may be left out: every list would then be read from the primary's body, which
// is a different program.  The pattern below writes one argument where the
// template it specializes declares two places, so it says nothing about any
// list - and no naming of the template is answered rather than every one of
// them being answered from the primary.
template<class T, class U>
struct pair_of {
  static const int which = 0;
};

template<class T, class U>
struct pair_of<T> {
  static const int which = 1;
};

int main()
{
  return pair_of<char, long>::which;
}
