// N3485 focus: 14.7.2 [temp.explicit] the declaration an explicit instantiation
// stands on shall name a specialization, so a simple-declaration with no
// declarator at all names none.

template int;

int main()
{
  return 0;
}
