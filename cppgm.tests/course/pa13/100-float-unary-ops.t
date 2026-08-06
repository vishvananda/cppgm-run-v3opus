# `unary not` on a floating operand is a floating zero test, and the zero a
# floating `neg` subtracts from is spelled as a floating immediate.
function @main() -> i64 {
  block ^entry:
    %f = const f32 6.5f
    %d = const f64 6.5
    %not_f = unary not f32 %f
    %not_d = unary not f64 %d
    %neg_f = unary neg f32 %f
    %neg_d = unary neg f64 %d
    %if = convert fptosi i64 f32 %neg_f
    %id = convert fptosi i64 f64 %neg_d
    %sum = binary add i64 %if, %id
    return i64 %sum
}
