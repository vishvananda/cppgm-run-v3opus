// N3485 focus: 13.5.6 [over.ref] the process that repeats until a pointer comes back
struct value { int held; };

// 13.5.6p1's process "repeats until an `operator->` is found that returns a
// pointer", and this one hands the arrow back the class it was written on - so
// no step of it ever reaches one.  The fold reads such a chain as much as the
// expression layer does, and both have to stop.
struct cyc {
  constexpr cyc() { }
  constexpr cyc operator->() const { return cyc(); }
};

int main() {
  static_assert(cyc()->held == 0, "a chain that hands itself back");
  return 0;
}
