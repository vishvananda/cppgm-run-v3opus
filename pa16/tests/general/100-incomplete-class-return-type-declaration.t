class held;
held make_held();
void take_held(held&);

typedef held (*maker)();
maker made = &make_held;
void (*taker)(held&) = &take_held;
