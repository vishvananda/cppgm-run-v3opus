int main() {
  unsigned long (*measure)(const char*) = __builtin_strlen;
  unsigned long (*again)(const char*) = &::__builtin_strlen;
  return (int)measure("abc") - (int)again("abc");
}
