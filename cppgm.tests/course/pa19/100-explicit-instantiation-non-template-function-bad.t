// N3485 focus: 14.7.2 [temp.explicit] the declaration an explicit instantiation
// stands on shall name a specialization, so a function no template declares is
// not one an explicit instantiation may be written over.

int plain(int value);

template int plain(int);

int main()
{
  return 0;
}
