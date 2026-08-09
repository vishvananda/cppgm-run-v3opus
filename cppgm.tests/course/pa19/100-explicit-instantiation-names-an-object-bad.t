// N3485 focus: 14.7.2 [temp.explicit] the declaration an explicit instantiation
// stands on shall name a specialization, so one that declares an object of a
// fundamental type names none.

int slot;

template int slot;

int main()
{
  return 0;
}
