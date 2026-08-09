// N3485 focus: 14.7.2 [temp.explicit] an explicit instantiation names a
// specialization, so a class-name no template-argument-list follows is not one.

struct plain_class {
};

template class plain_class;

int main()
{
  return 0;
}
