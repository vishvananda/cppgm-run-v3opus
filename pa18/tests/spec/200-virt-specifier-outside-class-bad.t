// VALIDATION: compile-fail
// N3485 focus: 9.2 [class.mem] virt-specifier placement
// Expected: a function that is no member of a class dispatches on nothing, so
// a virt-specifier written on one is ill formed wherever it stands.

void f() final;
