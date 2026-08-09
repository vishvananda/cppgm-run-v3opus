// 12.8p15 and 8.3.2p1: a reference is not an object, so a subobject of
// reference type has no bytes for the transfer the standard defines to carry.
// What it gives that subobject is 8.5.3's initialization of a reference - the
// binding the source holds, read and written on its own - so the run of members
// carried as storage ends where the reference member stands and begins again
// after it.
struct RefRun {
  int first;
  int& bound;
  int last;

  RefRun(int a, int& r, int b) : first(a), bound(r), last(b) {}
  RefRun(RefRun&&) = default;
};

int main()
{
  int value = 5;
  RefRun source(1, value, 2);
  RefRun moved(static_cast<RefRun&&>(source));
  return moved.first + moved.bound + moved.last - 8;
}
