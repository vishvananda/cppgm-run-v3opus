// An invocation is located against the source file it was read from, so one
// that would leave that file behind is refused rather than answered wrongly.
#define WHERE(x) x __LINE__
#include "course/pa5/invocation-head.h"
1)
