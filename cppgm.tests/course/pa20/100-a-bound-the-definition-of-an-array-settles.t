// VALIDATION: compile-pass
// 3.9p7 and 8.3.4p3: an array of unknown bound is an incomplete type the
// definition of the object completes, so the bound that definition deduced
// from its list is what a `sizeof` written over the declaration reads.

struct deduced
{
  static unsigned char bytes[];
};

unsigned char deduced::bytes[] = {1, 2, 3, 4};

struct two
{
  static int first[];
  static int second[];
};

int two::first[] = {1};
int two::second[] = {1, 2, 3};

extern int loose[];
int loose[] = {1, 2, 3};

template<int N>
struct specialized
{
  static unsigned char bytes[];
};

template<>
unsigned char specialized<4>::bytes[] = {1, 2, 3, 4};

int main()
{
  return sizeof(deduced::bytes) == 4 && deduced::bytes[3] == 4 &&
         sizeof(two::first) + sizeof(two::second) == 4 * sizeof(int) &&
         sizeof(loose) / sizeof(int) == 3 &&
         sizeof(specialized<4>::bytes) == 4
    ? 0 : 1;
}
