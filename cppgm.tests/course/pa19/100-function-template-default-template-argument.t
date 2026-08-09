// N3485 focus: 14.8.1p2 [temp.arg.explicit] and 14.1p9 [temp.param] a trailing
// template argument may be omitted where it can be deduced *or* obtained from a
// default template-argument, so a place no P/A pair reaches takes what its head
// wrote - read where the head declared its parameters, because 14.1p9 lets a
// default name the places before it.

template<class T, class U = long>
int widened(T step)
{
  U held = step;
  return static_cast<int>(held);
}

template<class A, class B = A>
int paired(A step)
{
  B held = step;
  return static_cast<int>(held);
}

template<class C, class D>
int written(D step)
{
  return static_cast<int>(step) + static_cast<int>(sizeof(C));
}

int main()
{
  return widened(20) + paired(20) + written<char>(1) - 42;
}
