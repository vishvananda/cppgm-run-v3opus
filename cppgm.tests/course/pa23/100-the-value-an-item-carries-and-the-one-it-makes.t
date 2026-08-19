// 3.6.2p2 with 8.5.1p7: an item of the image stands for one clause of it and
// carries the digits that clause was written with, whatever width the subobject
// it fills has - where the *operand* that names a whole scalar object's storage
// is a value, spelled at the width that holds it.  A value no clause of the
// program stands for is the third: 8.5p7's zero of the subobject, spelled from
// its type alone, which is what an element or a member no clause reached holds
// and what `{}` at a scalar place comes to.
struct pair { int x; char y; };
pair uncovered[3] = { { 1, 'a' } };
float digits[3] = { 0, 1.5F };
double wide[2] = { 1.5f };
long double widest[2] = { 1.5 };
float made = {};
int *address = {};
int counted = {};
int main()
{
  return uncovered[2].x + uncovered[1].y + (int)digits[2] + (int)wide[1] +
         (int)widest[1] + (int)made + (address == 0 ? 0 : 1) + counted;
}
