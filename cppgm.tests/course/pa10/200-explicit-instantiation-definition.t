// N3485 focus: 14.7.2 [temp.explicit] 14.7.2p1's explicit instantiation
// definition is the same target `extern template` writes with one keyword
// fewer, and what that keyword says - whether this unit owes the definitions -
// is a fact about the whole declaration rather than a terminal inside it.

template<class T>
struct box
{
  T held;
};

template struct box<int>;

template<class T>
void carried(T);

template void carried<int>(int);
