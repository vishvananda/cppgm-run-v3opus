// N3485 focus: 14.7.2 [temp.explicit] an explicit instantiation of a function
// template names the specialization 14.8.1's argument list makes, and asks this
// unit for its definition where no use of it stands.

template<class T>
T carried(T value)
{
  return value;
}

template int carried<int>(int);

int main()
{
  return 0;
}
