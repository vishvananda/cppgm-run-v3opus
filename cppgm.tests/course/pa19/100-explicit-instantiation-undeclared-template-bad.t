// N3485 focus: 14.7.2 [temp.explicit] the elaborated-type-specifier of an
// explicit instantiation shall name a class template specialization, so a
// template-id over a name no template declares names nothing to instantiate.

template class missing_template<int>;

int main()
{
  return 0;
}
