// N3485 focus: 14.7.2 [temp.explicit] an explicit instantiation definition is
// one of only those members that have been defined at the point of
// instantiation, so the out-of-class definition written below it is left to
// 3.2p3's use.

template<class T>
struct late_box
{
  int early() const;
  int late() const;
};

template<class T>
int late_box<T>::early() const
{
  return 1;
}

template class late_box<int>;

template<class T>
int late_box<T>::late() const
{
  return 2;
}

int main()
{
  return 0;
}
