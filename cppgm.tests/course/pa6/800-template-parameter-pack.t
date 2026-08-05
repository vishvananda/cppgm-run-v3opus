template<class T1...> class C1 { };
template<class ...T1> class C2 { };
template<template<class> class = E1::a1> class C3 { };
template<template<class> class T1 = C1> class C4 { };
