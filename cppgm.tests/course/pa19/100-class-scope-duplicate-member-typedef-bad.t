// N3485 focus: 9.2 [class.mem] a member shall not be declared twice in the
// member-specification, which 7.1.3 lets a namespace do for one type.

typedef int size_type;
typedef int size_type;

struct box {
  typedef int size_type;
  typedef int size_type;
};

box value;
