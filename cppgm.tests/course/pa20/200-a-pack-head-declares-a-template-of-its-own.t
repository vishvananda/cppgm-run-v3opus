// N3485 focus: 14.5.6.1 [temp.over.link] p5 with 14.5.3 [temp.variadic] p1 -
// a place that binds a *run* is not the place a head that declared no pack
// wrote there, so `pick(T)` and `pick(Ts...)` declare two templates and not
// one declaration written twice.  14.5.6.2 [temp.func.order] p2 then chooses
// between them, and 14.8.2.4 [temp.deduct.partial] p9 leaves the head that
// wrote a place ahead of the head that wrote a run - so a call both of them
// reach is the first one's, and a call only the run fits is the second's.
template<class T> int pick(T) { return 1; }

template<class... Ts> int pick(Ts...) { return 2; }

template<class T, class U> int span(T, U) { return 3; }

template<class... Ts> int span(Ts...) { return 4; }

int main()
{
  if (pick(1) != 1) { return 1; }
  if (pick(1, 2) != 2) { return 2; }
  if (span(1, 2) != 3) { return 3; }
  if (span(1) != 4) { return 4; }
  if (span(1, 2, 3) != 4) { return 5; }
  return 0;
}
