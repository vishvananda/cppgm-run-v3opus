#pragma pack(push, 4)
struct Outer {
  char a;
  double b;
};

#pragma pack(push, 1)
struct Inner {
  char a;
  double b;
};
#pragma pack(pop)

struct Restored {
  char a;
  double b;
};
#pragma pack(pop)

struct Natural {
  char a;
  double b;
};

int main()
{
  return sizeof(Outer) == 12 && sizeof(Inner) == 9 &&
         sizeof(Restored) == 12 && sizeof(Natural) == 16 &&
         alignof(Outer) == 4 && alignof(Inner) == 1 &&
         alignof(Natural) == 8
    ? 0 : 1;
}
