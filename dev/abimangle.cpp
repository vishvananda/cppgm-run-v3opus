// The PA14 `abimangle` tool: the line-oriented ABI fact reader and driver.
//
// This file owns the fact-text boundary only. The typed ABI model and the
// Itanium encoder live in dev/src/abi_mangle.{h,cpp} so that later compiler
// stages can build the same typed targets and call the encoder directly.

#include "abi_mangle.h"
#include "exceptions.h"

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace abi_mangle {

namespace {

void fail(const string & message)
{
  throw logic_error("abi-fact: " + message);
}

string to_decimal(size_t value)
{
  ostringstream stream;
  stream << value;
  return stream.str();
}

string to_signed_decimal(long long value)
{
  ostringstream stream;
  stream << value;
  return stream.str();
}

// Fact lines are split on single spaces so that an intentionally empty field,
// as in `name-source <empty> -`, survives.
vector<string> split_line_words(const string & line)
{
  string text = line;
  while(!text.empty() && (text[text.size() - 1] == '\r' ||
                          text[text.size() - 1] == ' ' ||
                          text[text.size() - 1] == '\t')) {
    text.erase(text.size() - 1);
  }
  vector<string> words;
  if(text.empty()) { return words; }
  size_t start = 0;
  while(true) {
    const size_t stop = text.find(' ', start);
    if(stop == string::npos) {
      words.push_back(text.substr(start));
      break;
    }
    words.push_back(text.substr(start, stop - start));
    start = stop + 1;
  }
  return words;
}

// The offset of the first `:` that separates two fact operands. A `::` scope
// separator never delimits operands.
size_t operand_colon(const string & word, size_t from)
{
  for(size_t i = from; i < word.size(); ++i) {
    if(word[i] != ':') { continue; }
    if(i + 1 < word.size() && word[i + 1] == ':') {
      ++i;
      continue;
    }
    return i;
  }
  return string::npos;
}

size_t parse_index(const string & word)
{
  if(word.empty()) { fail("empty ABI index"); }
  for(size_t i = 0; i < word.size(); ++i) {
    if(word[i] < '0' || word[i] > '9') {
      fail("malformed ABI index '" + word + "'");
    }
  }
  errno = 0;
  const unsigned long long value = strtoull(word.c_str(), 0, 10);
  if(errno != 0) { fail("ABI index '" + word + "' is out of range"); }
  return (size_t)value;
}

long long parse_signed(const string & word)
{
  if(word.empty()) { fail("empty integer"); }
  size_t start = (word[0] == '-' || word[0] == '+') ? 1 : 0;
  if(start >= word.size()) { fail("malformed integer '" + word + "'"); }
  for(size_t i = start; i < word.size(); ++i) {
    if(word[i] < '0' || word[i] > '9') {
      fail("malformed integer '" + word + "'");
    }
  }
  // The minimum signed value has no positive magnitude, so read the digits
  // unsigned and negate in the unsigned domain.
  errno = 0;
  const unsigned long long magnitude = strtoull(word.c_str() + start, 0, 10);
  if(errno != 0) { fail("integer '" + word + "' is out of range"); }
  if(word[0] == '-') { return (long long)(0ull - magnitude); }
  return (long long)magnitude;
}

bool parse_flag(const string & word)
{
  if(word == "yes" || word == "true" || word == "1") { return true; }
  if(word == "no" || word == "false" || word == "0" || word == "-") {
    return false;
  }
  fail("malformed flag '" + word + "'");
  return false;
}

// An array bound is a decimal extent, or `expr:<reference>` for a dependent
// one. The compact spelling has no room for the expression form because `:`
// already separates the bound from the element type.
AbiArrayBound parse_array_bound(const string & word, bool allow_expression)
{
  AbiArrayBound bound;
  if(allow_expression && word.compare(0, 5, "expr:") == 0) {
    bound.kind = ABI_ARRAY_BOUND_EXPRESSION;
    bound.value = word.substr(5);
    if(bound.value.empty()) { fail("array bound expression has no reference"); }
    return bound;
  }
  bound.kind = ABI_ARRAY_BOUND_VALUE;
  parse_index(word);
  bound.value = word;
  return bound;
}

AbiType builtin_or_named(const string & word)
{
  AbiType type;
  type.kind = ABI_TYPE_NAME_OR_REFERENCE;
  type.name = word;
  return type;
}

AbiType parse_compact_type(const string & word);

AbiType wrap_type(const string & head, const AbiType & inner)
{
  AbiType type;
  if(head == "ptr") {
    type.kind = ABI_TYPE_POINTER;
  } else if(head == "ref") {
    type.kind = ABI_TYPE_LVALUE_REFERENCE;
  } else if(head == "rref") {
    type.kind = ABI_TYPE_RVALUE_REFERENCE;
  } else if(head == "pack") {
    type.kind = ABI_TYPE_PACK_EXPANSION;
  } else if(head == "const" || head == "volatile") {
    type.kind = ABI_TYPE_CV;
    type.is_const = head == "const";
    type.is_volatile = head == "volatile";
  } else {
    fail("unknown type constructor '" + head + "'");
  }
  type.types.push_back(inner);
  return type;
}

bool is_type_wrapper(const string & head)
{
  return head == "ptr" || head == "ref" || head == "rref" || head == "pack" ||
         head == "const" || head == "volatile";
}

AbiType parse_compact_type(const string & word)
{
  const size_t colon = operand_colon(word, 0);
  if(colon == string::npos) { return builtin_or_named(word); }
  const string head = word.substr(0, colon);
  const string rest = word.substr(colon + 1);
  if(rest.empty()) { fail("type constructor '" + head + "' has no operand"); }
  if(is_type_wrapper(head)) { return wrap_type(head, parse_compact_type(rest)); }
  if(head == "named" || head == "name") {
    AbiType type;
    type.kind = ABI_TYPE_NAMED;
    type.name = rest;
    return type;
  }
  if(head == "array") {
    const size_t bound_end = operand_colon(rest, 0);
    if(bound_end == string::npos) { fail("array type has no element type"); }
    AbiType type;
    type.kind = ABI_TYPE_ARRAY;
    type.array_bound = parse_array_bound(rest.substr(0, bound_end), false);
    type.types.push_back(parse_compact_type(rest.substr(bound_end + 1)));
    return type;
  }
  if(head == "memberptr" || head == "member-pointer") {
    const size_t owner_end = operand_colon(rest, 0);
    if(owner_end == string::npos) { fail("member pointer has no member type"); }
    AbiType type;
    type.kind = ABI_TYPE_MEMBER_POINTER;
    type.types.push_back(parse_compact_type(rest.substr(0, owner_end)));
    type.types.push_back(parse_compact_type(rest.substr(owner_end + 1)));
    return type;
  }
  fail("unknown compact type form '" + word + "'");
  return AbiType();
}

const string & word_at(const vector<string> & words, size_t position,
                       const string & what)
{
  if(position >= words.size()) { fail("missing " + what); }
  return words[position];
}

vector<string> tail_words(const vector<string> & words, size_t position)
{
  vector<string> rest;
  for(size_t i = position; i < words.size(); ++i) { rest.push_back(words[i]); }
  return rest;
}

// A type fact written across the rest of a line. Single-word operands use the
// compact `:` syntax; the multiword heads below take structured operands.
AbiType parse_type_words(const vector<string> & words, size_t position);

AbiType parse_template_type(const vector<string> & words, size_t position,
                            const string & head)
{
  AbiType type;
  if(head == "template") {
    type.kind = ABI_TYPE_TEMPLATE_SPECIALIZATION;
    type.name = word_at(words, position, "template name");
    type.argument_refs = tail_words(words, position + 1);
    return type;
  }
  type.kind = ABI_TYPE_STD_TEMPLATE_SPECIALIZATION;
  type.standard_substitution = word_at(words, position, "standard substitution");
  type.standard_substitution_includes_arguments =
    parse_flag(word_at(words, position + 1, "standard substitution flag"));
  type.name = word_at(words, position + 2, "template name");
  type.argument_refs = tail_words(words, position + 3);
  return type;
}

AbiType parse_local_type_words(const vector<string> & words, size_t position,
                               const string & head)
{
  AbiType type;
  if(head == "lambda-closure") {
    type.kind = ABI_TYPE_LAMBDA_CLOSURE;
    type.context_ref = word_at(words, position, "lambda context");
    type.discriminator = word_at(words, position + 1, "lambda discriminator");
    parse_index(type.discriminator);
    for(size_t i = position + 2; i < words.size(); ++i) {
      type.types.push_back(parse_compact_type(words[i]));
    }
    return type;
  }
  type.kind = ABI_TYPE_LOCAL_TYPE;
  type.context_ref = word_at(words, position, "local context");
  type.name = word_at(words, position + 1, "local type name");
  type.discriminator = word_at(words, position + 2, "local discriminator");
  parse_index(type.discriminator);
  return type;
}

AbiType parse_parameterized_type(const vector<string> & words, size_t position,
                                 const string & head)
{
  AbiType type;
  if(head == "template-param" || head == "template-param-subst") {
    type.kind = ABI_TYPE_TEMPLATE_PARAMETER;
    type.index = parse_index(word_at(words, position, "template parameter index"));
    type.substitutable = head == "template-param-subst";
    return type;
  }
  if(head == "template-param-template") {
    type.kind = ABI_TYPE_TEMPLATE_PARAMETER_SPECIALIZATION;
    type.index = parse_index(word_at(words, position, "template parameter index"));
    type.argument_refs = tail_words(words, position + 1);
    return type;
  }
  if(head == "builtin-transform") {
    type.kind = ABI_TYPE_BUILTIN_TRANSFORM;
    type.name = word_at(words, position, "builtin transform name");
    type.types.push_back(parse_type_words(words, position + 1));
    return type;
  }
  if(head == "member" || head == "member-template") {
    type.kind = head == "member" ? ABI_TYPE_MEMBER
                                 : ABI_TYPE_MEMBER_TEMPLATE_SPECIALIZATION;
    type.types.push_back(
      parse_compact_type(word_at(words, position, "member owner")));
    type.name = word_at(words, position + 1, "member name");
    type.argument_refs = tail_words(words, position + 2);
    return type;
  }
  if(head == "namespace-lambda") {
    type.kind = ABI_TYPE_NAMESPACE_LAMBDA;
    type.name = word_at(words, position, "lambda source name");
    type.namespace_qualifiers = tail_words(words, position + 1);
    return type;
  }
  fail("unknown type fact '" + head + "'");
  return type;
}

AbiType parse_type_words(const vector<string> & words, size_t position)
{
  const string head = word_at(words, position, "type fact");
  if(is_type_wrapper(head)) {
    return wrap_type(head, parse_type_words(words, position + 1));
  }
  if(head == "name" || head == "named") {
    AbiType type;
    type.kind = ABI_TYPE_NAMED;
    type.name = word_at(words, position + 1, "type name");
    return type;
  }
  if(head == "tagged") {
    AbiType type =
      parse_compact_type(word_at(words, position + 1, "tagged type"));
    for(size_t i = position + 2; i < words.size(); ++i) {
      type.abi_tags.push_back(words[i]);
    }
    if(type.abi_tags.empty()) { fail("tagged type has no ABI tag"); }
    return type;
  }
  if(head == "vendor") {
    AbiType type;
    type.kind = ABI_TYPE_VENDOR_QUALIFIED;
    type.name = word_at(words, position + 1, "vendor qualifier");
    type.types.push_back(parse_type_words(words, position + 2));
    return type;
  }
  if(head == "array") {
    AbiType type;
    type.kind = ABI_TYPE_ARRAY;
    type.array_bound =
      parse_array_bound(word_at(words, position + 1, "array bound"), true);
    type.types.push_back(parse_type_words(words, position + 2));
    return type;
  }
  if(head == "member-pointer") {
    AbiType type;
    type.kind = ABI_TYPE_MEMBER_POINTER;
    type.types.push_back(
      parse_compact_type(word_at(words, position + 1, "member pointer owner")));
    type.types.push_back(parse_type_words(words, position + 2));
    return type;
  }
  // 8.3.5p7's ref-qualifier is part of the function type, and the head carries
  // it because the words after it are the result and the parameters and nothing
  // else - a function type written without one keeps the two spellings it had.
  if(head.compare(0, 13, "function-type") == 0 &&
     (head.size() == 13 || head[13] == '-')) {
    const string tail = head.substr(13);
    AbiType type;
    type.kind = ABI_TYPE_FUNCTION;
    type.variadic = tail.compare(0, 9, "-variadic") == 0;
    const string qualifier = tail.substr(type.variadic ? 9 : 0);
    type.function_lvalue_ref = qualifier == "-lvalue-ref";
    type.function_rvalue_ref = qualifier == "-rvalue-ref";
    if(!qualifier.empty() && !type.function_lvalue_ref &&
       !type.function_rvalue_ref) {
      fail("unknown function type fact " + head);
    }
    for(size_t i = position + 1; i < words.size(); ++i) {
      type.types.push_back(parse_compact_type(words[i]));
    }
    if(type.types.empty()) { fail("function type has no result type"); }
    return type;
  }
  if(head == "decltype") {
    AbiType type;
    type.kind = ABI_TYPE_DECLTYPE_EXPRESSION;
    type.expression_ref = word_at(words, position + 1, "decltype expression");
    return type;
  }
  if(head == "template" || head == "std-template") {
    return parse_template_type(words, position + 1, head);
  }
  if(head == "lambda-closure" || head == "local-type") {
    return parse_local_type_words(words, position + 1, head);
  }
  if(head == "template-param" || head == "template-param-subst" ||
     head == "template-param-template" || head == "builtin-transform" ||
     head == "member" || head == "member-template" ||
     head == "namespace-lambda") {
    return parse_parameterized_type(words, position + 1, head);
  }
  if(position + 1 != words.size()) {
    fail("trailing words after type fact '" + head + "'");
  }
  return parse_compact_type(head);
}

AbiType builtin_value_type(const string & word)
{
  AbiType type;
  type.kind = ABI_TYPE_BUILTIN;
  type.name = word;
  return type;
}

// --- template arguments ---------------------------------------------------

AbiTemplateArgument parse_entity_argument(const vector<string> & words,
                                          size_t position, const string & head)
{
  AbiTemplateArgument argument;
  if(head == "entity-address") {
    argument.kind = ABI_TEMPLATE_ARGUMENT_ENTITY;
    argument.address_of = true;
    argument.entity_ref = word_at(words, position, "entity reference");
    return argument;
  }
  if(head == "entity") {
    argument.kind = ABI_TEMPLATE_ARGUMENT_ENTITY;
    argument.entity_ref = word_at(words, position, "entity reference");
    return argument;
  }
  argument.kind = ABI_TEMPLATE_ARGUMENT_MEMBER_EXTERNAL_ENTITY;
  argument.address_of = true;
  // The fact file's own label for the member. The encoder builds the ABI name
  // from the typed owner, member name, qualifiers and parameters that follow,
  // so this spelling is carried for serialization only.
  argument.symbol = word_at(words, position, "external symbol");
  argument.owner_type =
    parse_compact_type(word_at(words, position + 1, "member owner"));
  argument.name = word_at(words, position + 2, "member name");
  argument.member_is_function =
    parse_flag(word_at(words, position + 3, "member function flag"));
  if(!argument.member_is_function) { return argument; }
  argument.member_function_const =
    parse_flag(word_at(words, position + 4, "member const flag"));
  argument.member_function_volatile =
    parse_flag(word_at(words, position + 5, "member volatile flag"));
  argument.member_function_lvalue_ref =
    parse_flag(word_at(words, position + 6, "member lvalue-ref flag"));
  argument.member_function_rvalue_ref =
    parse_flag(word_at(words, position + 7, "member rvalue-ref flag"));
  argument.member_function_variadic =
    parse_flag(word_at(words, position + 8, "member variadic flag"));
  for(size_t i = position + 9; i < words.size(); ++i) {
    argument.parameter_types.push_back(parse_compact_type(words[i]));
  }
  return argument;
}

AbiTemplateArgument parse_template_argument(const vector<string> & words,
                                            size_t position)
{
  const string head = word_at(words, position, "template argument fact");
  AbiTemplateArgument argument;
  if(head == "type") {
    argument.kind = ABI_TEMPLATE_ARGUMENT_TYPE;
    argument.type = parse_type_words(words, position + 1);
    return argument;
  }
  if(head == "value") {
    argument.kind = ABI_TEMPLATE_ARGUMENT_VALUE;
    argument.value_type =
      builtin_value_type(word_at(words, position + 1, "value type"));
    argument.value = parse_signed(word_at(words, position + 2, "value"));
    return argument;
  }
  if(head == "dependent-value") {
    argument.kind = ABI_TEMPLATE_ARGUMENT_DEPENDENT_VALUE;
    argument.type =
      parse_compact_type(word_at(words, position + 1, "declared type"));
    argument.value_type =
      builtin_value_type(word_at(words, position + 2, "value type"));
    argument.value = parse_signed(word_at(words, position + 3, "value"));
    return argument;
  }
  if(head == "expression") {
    argument.kind = ABI_TEMPLATE_ARGUMENT_EXPRESSION;
    argument.name = word_at(words, position + 1, "expression reference");
    return argument;
  }
  if(head == "template-entity") {
    argument.kind = ABI_TEMPLATE_ARGUMENT_TEMPLATE_ENTITY;
    argument.name = word_at(words, position + 1, "template name");
    return argument;
  }
  if(head == "member-template-entity") {
    argument.kind = ABI_TEMPLATE_ARGUMENT_MEMBER_TEMPLATE_ENTITY;
    argument.owner_type =
      parse_compact_type(word_at(words, position + 1, "member template owner"));
    argument.name = word_at(words, position + 2, "member template name");
    argument.substitution = position + 3 < words.size() ? words[position + 3] : "";
    return argument;
  }
  if(head == "template-param-template") {
    argument.kind = ABI_TEMPLATE_ARGUMENT_TEMPLATE_PARAMETER_TEMPLATE;
    argument.index =
      parse_index(word_at(words, position + 1, "template parameter index"));
    argument.argument_refs = tail_words(words, position + 2);
    return argument;
  }
  if(head == "pack") {
    argument.kind = ABI_TEMPLATE_ARGUMENT_PACK;
    argument.argument_refs = tail_words(words, position + 1);
    return argument;
  }
  if(head == "entity-address" || head == "entity" ||
     head == "member-external-address") {
    return parse_entity_argument(words, position + 1, head);
  }
  fail("unknown template argument fact '" + head + "'");
  return argument;
}

// --- dependent expressions ------------------------------------------------

AbiDependentExpression parse_named_expression(const vector<string> & words,
                                              size_t position,
                                              const string & head)
{
  AbiDependentExpression expression;
  if(head == "template-id") {
    expression.kind = ABI_EXPRESSION_TEMPLATE_ID;
    expression.text = word_at(words, position, "template name");
    expression.argument_refs = tail_words(words, position + 1);
    return expression;
  }
  if(head == "type-trait") {
    expression.kind = ABI_EXPRESSION_TYPE_TRAIT;
    expression.op = word_at(words, position, "type trait name");
    for(size_t i = position + 1; i < words.size(); ++i) {
      expression.type_arguments.push_back(parse_compact_type(words[i]));
    }
    return expression;
  }
  if(head == "sizeof-type") {
    expression.kind = ABI_EXPRESSION_SIZEOF_TYPE;
    expression.type = parse_compact_type(word_at(words, position, "type"));
    return expression;
  }
  if(head == "member") {
    expression.kind = ABI_EXPRESSION_MEMBER;
    expression.type = parse_compact_type(word_at(words, position, "member owner"));
    // The owner flag is the three answers `<unresolved-name>` has, and not
    // two: `no` for one qualifier level written bare, `nested` for more than
    // one, which the encoder wraps in `N`/`E`, and `yes` for a written form
    // whose owner was opened somewhere this record does not see and which
    // therefore spells the closing `E` alone.
    const string owner_flag = word_at(words, position + 1, "member owner flag");
    if(owner_flag == "nested") {
      expression.nested_member_owner = true;
    } else {
      expression.close_member_owner = parse_flag(owner_flag);
    }
    expression.text = word_at(words, position + 2, "member name");
    return expression;
  }
  if(head == "object-member") {
    expression.kind = ABI_EXPRESSION_OBJECT_MEMBER;
    expression.op = word_at(words, position, "member operator");
    expression.expression_refs.push_back(
      word_at(words, position + 1, "object expression"));
    expression.text = word_at(words, position + 2, "member name");
    expression.argument_refs = tail_words(words, position + 3);
    return expression;
  }
  if(head == "entity-reference") {
    expression.kind = ABI_EXPRESSION_ENTITY;
    expression.entity_ref = word_at(words, position, "entity reference");
    return expression;
  }
  if(head == "symbol-reference") {
    expression.kind = ABI_EXPRESSION_EXTERNAL_ENTITY;
    expression.text = word_at(words, position, "symbol");
    return expression;
  }
  fail("unknown dependent expression fact '" + head + "'");
  return expression;
}

AbiDependentExpression parse_expression(const vector<string> & words,
                                        size_t position)
{
  const string head = word_at(words, position, "expression fact");
  AbiDependentExpression expression;
  if(head == "template-param" || head == "template-param-subst") {
    expression.kind = ABI_EXPRESSION_TEMPLATE_PARAMETER;
    expression.index =
      parse_index(word_at(words, position + 1, "template parameter index"));
    expression.substitutable = head == "template-param-subst";
    return expression;
  }
  if(head == "function-param") {
    expression.kind = ABI_EXPRESSION_FUNCTION_PARAMETER;
    expression.index =
      parse_index(word_at(words, position + 1, "function parameter index"));
    return expression;
  }
  if(head == "literal") {
    expression.kind = ABI_EXPRESSION_LITERAL;
    if(position + 2 < words.size()) {
      expression.value_type = builtin_value_type(words[position + 1]);
      expression.value = parse_signed(words[position + 2]);
    } else {
      expression.value_type = builtin_value_type("int");
      expression.value = parse_signed(word_at(words, position + 1, "literal"));
    }
    return expression;
  }
  if(head == "unary" || head == "binary") {
    expression.kind =
      head == "unary" ? ABI_EXPRESSION_UNARY : ABI_EXPRESSION_BINARY;
    expression.op = word_at(words, position + 1, "operator");
    expression.expression_refs = tail_words(words, position + 2);
    return expression;
  }
  if(head == "conditional") {
    expression.kind = ABI_EXPRESSION_CONDITIONAL;
    expression.op = "qu";
    expression.expression_refs = tail_words(words, position + 1);
    return expression;
  }
  if(head == "pack") {
    expression.kind = ABI_EXPRESSION_PACK_EXPANSION;
    expression.expression_refs = tail_words(words, position + 1);
    return expression;
  }
  if(head == "call") {
    expression.kind = ABI_EXPRESSION_CALL;
    expression.expression_refs = tail_words(words, position + 1);
    return expression;
  }
  if(head == "cast" || head == "conversion") {
    expression.kind =
      head == "cast" ? ABI_EXPRESSION_CAST : ABI_EXPRESSION_CONVERSION;
    expression.op = word_at(words, position + 1, "cast operator");
    expression.type = parse_compact_type(word_at(words, position + 2, "type"));
    expression.expression_refs = tail_words(words, position + 3);
    return expression;
  }
  return parse_named_expression(words, position + 1, head);
}

// --- contexts and entities ------------------------------------------------

AbiFunctionTarget parse_function_target(const vector<string> & words,
                                        size_t position);

AbiLocalContext parse_context(const vector<string> & words, size_t position)
{
  AbiLocalContext context;
  const string head = word_at(words, position, "context fact");
  if(head == "raw") {
    context.kind = ABI_CONTEXT_RAW;
    context.fragment = word_at(words, position + 1, "context fragment");
    return context;
  }
  if(head != "function") { fail("unknown context fact '" + head + "'"); }
  context.kind = ABI_CONTEXT_FUNCTION;
  context.function = parse_function_target(words, position + 1);
  return context;
}

AbiEntityFact parse_entity(const vector<string> & words, size_t position)
{
  AbiEntityFact entity;
  const string head = word_at(words, position, "entity fact");
  if(head == "symbol") {
    entity.kind = ABI_ENTITY_FACT_SYMBOL;
    entity.qualified_name = word_at(words, position + 1, "symbol");
    return entity;
  }
  if(head == "variable" || head == "internal-variable") {
    entity.kind = ABI_ENTITY_FACT_VARIABLE;
    entity.internal_linkage = head == "internal-variable";
    entity.qualified_name = word_at(words, position + 1, "entity name");
    return entity;
  }
  if(head == "function" || head == "internal-function") {
    entity.kind = ABI_ENTITY_FACT_FUNCTION;
    entity.internal_linkage = head == "internal-function";
    entity.qualified_name = word_at(words, position + 1, "entity name");
    for(size_t i = position + 2; i < words.size(); ++i) {
      AbiFunctionPathOperand operand;
      operand.kind = ABI_FUNCTION_PATH_TYPE;
      operand.type = parse_compact_type(words[i]);
      entity.function.path_operands.push_back(operand);
    }
    return entity;
  }
  fail("unknown entity fact '" + head + "'");
  return entity;
}

// --- function targets -----------------------------------------------------

AbiFunctionTarget parse_function_target(const vector<string> & words,
                                        size_t position)
{
  AbiFunctionTarget target;
  const string head = word_at(words, position, "function fact");
  if(head == "encoding") {
    target.kind = ABI_FUNCTION_TARGET_ENCODING;
    return target;
  }
  if(head == "lambda") {
    target.kind = ABI_FUNCTION_TARGET_LAMBDA;
    target.context_ref = word_at(words, position + 1, "lambda context");
    target.discriminator = word_at(words, position + 2, "lambda discriminator");
    parse_index(target.discriminator);
    target.terminal = word_at(words, position + 3, "lambda terminal");
    for(size_t i = position + 4; i < words.size(); ++i) {
      target.signature_parameter_types.push_back(parse_compact_type(words[i]));
    }
    return target;
  }
  if(head == "local") {
    target.kind = ABI_FUNCTION_TARGET_LOCAL;
    target.context_ref = word_at(words, position + 1, "local context");
    target.source_name = word_at(words, position + 2, "local class name");
    target.terminal = word_at(words, position + 3, "local terminal");
    target.discriminator = word_at(words, position + 4, "local discriminator");
    parse_index(target.discriminator);
    return target;
  }
  if(head == "namespace-lambda") {
    target.kind = ABI_FUNCTION_TARGET_NAMESPACE_LAMBDA;
    target.source_name = word_at(words, position + 1, "lambda source name");
    target.terminal = word_at(words, position + 2, "lambda terminal");
    target.namespace_qualifiers = tail_words(words, position + 3);
    return target;
  }
  target.kind = ABI_FUNCTION_TARGET_PATH;
  size_t first_operand = position + 1;
  if(head == "path") {
    target.qualified_name = word_at(words, position + 1, "function name");
    first_operand = position + 2;
  } else {
    target.qualified_name = head;
  }
  for(size_t i = first_operand; i < words.size(); ++i) {
    AbiFunctionPathOperand operand;
    operand.kind = ABI_FUNCTION_PATH_TYPE;
    operand.argument_ref = words[i];
    operand.type = parse_compact_type(words[i]);
    target.path_operands.push_back(operand);
  }
  return target;
}

// --- function component records -------------------------------------------

AbiFunctionQualifier parse_qualifier(const string & word)
{
  if(word == "const") { return ABI_FUNCTION_QUALIFIER_CONST; }
  if(word == "volatile") { return ABI_FUNCTION_QUALIFIER_VOLATILE; }
  if(word == "lvalue-ref" || word == "ref") {
    return ABI_FUNCTION_QUALIFIER_LVALUE_REFERENCE;
  }
  if(word == "rvalue-ref" || word == "rref") {
    return ABI_FUNCTION_QUALIFIER_RVALUE_REFERENCE;
  }
  fail("unknown function qualifier '" + word + "'");
  return ABI_FUNCTION_QUALIFIER_CONST;
}

bool parse_name_record(const vector<string> & words, AbiFunctionRecord & record)
{
  const string & head = words[0];
  if(head == "name-std") {
    record.kind = ABI_FUNCTION_RECORD_NAME_STD;
    return true;
  }
  if(head == "name-source") {
    record.kind = ABI_FUNCTION_RECORD_NAME_SOURCE;
    record.name = words.size() > 1 ? words[1] : "";
    record.substitution = words.size() > 2 ? words[2] : "";
    return true;
  }
  if(head == "name-template") {
    record.kind = ABI_FUNCTION_RECORD_NAME_TEMPLATE;
    record.name = word_at(words, 1, "template component name");
    record.substitution = word_at(words, 2, "template prefix substitution");
    record.complete_substitution = word_at(words, 3, "template substitution");
    const string standard = word_at(words, 4, "standard substitution");
    if(standard != "-") { record.standard_substitution = standard; }
    record.standard_substitution_includes_arguments =
      parse_flag(word_at(words, 5, "standard substitution flag"));
    record.argument_refs = tail_words(words, 6);
    return true;
  }
  return false;
}

bool parse_context_record(const vector<string> & words,
                          AbiFunctionRecord & record)
{
  const string & head = words[0];
  if(head == "local-context") {
    record.kind = ABI_FUNCTION_RECORD_LOCAL_CONTEXT;
    record.context_ref = word_at(words, 1, "local context");
    record.source_name = word_at(words, 2, "local class name");
    record.discriminator = word_at(words, 3, "local discriminator");
    parse_index(record.discriminator);
    return true;
  }
  if(head == "lambda-context") {
    record.kind = ABI_FUNCTION_RECORD_LAMBDA_CONTEXT;
    record.context_ref = word_at(words, 1, "lambda context");
    record.discriminator = word_at(words, 2, "lambda discriminator");
    parse_index(record.discriminator);
    for(size_t i = 3; i < words.size(); ++i) {
      record.types.push_back(parse_compact_type(words[i]));
    }
    return true;
  }
  if(head == "namespace-lambda-context") {
    record.kind = ABI_FUNCTION_RECORD_NAMESPACE_LAMBDA_CONTEXT;
    record.source_name = word_at(words, 1, "lambda source name");
    record.namespace_qualifiers = tail_words(words, 2);
    return true;
  }
  return false;
}

bool parse_terminal_record(const vector<string> & words,
                           AbiFunctionRecord & record)
{
  const string & head = words[0];
  if(head == "terminal") {
    record.kind = ABI_FUNCTION_RECORD_TERMINAL;
    record.terminal = word_at(words, 1, "terminal");
    return true;
  }
  if(head == "terminal-source") {
    record.kind = ABI_FUNCTION_RECORD_TERMINAL_SOURCE;
    record.source_name = word_at(words, 1, "terminal source name");
    return true;
  }
  if(head == "operator-terminal") {
    record.kind = ABI_FUNCTION_RECORD_OPERATOR_TERMINAL;
    record.terminal = word_at(words, 1, "operator terminal");
    if(record.terminal == "literal") {
      record.literal_suffix = word_at(words, 2, "literal operator suffix");
    }
    return true;
  }
  if(head == "conversion-terminal") {
    record.kind = ABI_FUNCTION_RECORD_CONVERSION_TERMINAL;
    record.type = parse_type_words(words, 1);
    return true;
  }
  return false;
}

bool parse_function_record(const vector<string> & words,
                           AbiFunctionRecord & record)
{
  if(parse_name_record(words, record)) { return true; }
  if(parse_context_record(words, record)) { return true; }
  if(parse_terminal_record(words, record)) { return true; }
  const string & head = words[0];
  if(head == "abi-tag") {
    record.kind = ABI_FUNCTION_RECORD_ABI_TAG;
    record.name = word_at(words, 1, "ABI tag");
    return true;
  }
  if(head == "qualifier" || head == "function-qualifier") {
    record.kind = ABI_FUNCTION_RECORD_QUALIFIER;
    if(words.size() < 2) { fail("qualifier record has no qualifier"); }
    for(size_t i = 1; i < words.size(); ++i) {
      record.qualifiers.push_back(parse_qualifier(words[i]));
    }
    return true;
  }
  if(head == "function-template-arg") {
    record.kind = ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_ARGUMENT;
    record.argument_refs.push_back(word_at(words, 1, "template argument"));
    return true;
  }
  if(head == "function-template-prefix") {
    record.kind = ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_PREFIX;
    record.substitution = word_at(words, 1, "template prefix key");
    return true;
  }
  if(head == "param" || head == "result") {
    record.kind = head == "param" ? ABI_FUNCTION_RECORD_PARAMETER
                                  : ABI_FUNCTION_RECORD_RESULT;
    record.type = parse_type_words(words, 1);
    return true;
  }
  if(head == "variadic") {
    record.kind = ABI_FUNCTION_RECORD_VARIADIC;
    return true;
  }
  return false;
}

// --- targets --------------------------------------------------------------

void parse_thunk_target(const vector<string> & words, AbiTargetRecord & target)
{
  target.kind = ABI_TARGET_FACT_THUNK;
  target.this_adjust = parse_signed(word_at(words, 1, "thunk this adjustment"));
  size_t position = 2;
  const string & next = word_at(words, position, "thunk function");
  if(next == "virtual-result") {
    target.has_result_adjust = true;
    target.result_adjust_virtual = true;
    target.result_adjust =
      parse_signed(word_at(words, position + 1, "thunk result adjustment"));
    target.result_vcall_offset =
      parse_signed(word_at(words, position + 2, "thunk vcall offset"));
    position += 3;
  } else if(next != "function") {
    target.has_result_adjust = true;
    target.result_adjust = parse_signed(next);
    position += 1;
  }
  if(word_at(words, position, "thunk function") != "function") {
    fail("thunk target has no function");
  }
  target.function = parse_function_target(words, position + 1);
}

bool parse_target_record(const vector<string> & words, AbiTargetRecord & target)
{
  const string & head = words[0];
  if(head == "type") {
    target.kind = ABI_TARGET_FACT_TYPE;
    target.type = parse_type_words(words, 1);
    return true;
  }
  if(head == "function" || head == "c-function") {
    target.kind = ABI_TARGET_FACT_FUNCTION;
    target.c_linkage = head == "c-function";
    target.function = parse_function_target(words, 1);
    return true;
  }
  if(head == "variable" || head == "internal-variable") {
    target.kind = ABI_TARGET_FACT_VARIABLE;
    target.internal_linkage = head == "internal-variable";
    target.qualified_name = word_at(words, 1, "variable name");
    return true;
  }
  if(head == "typeinfo" || head == "typeinfo-name" || head == "vtable" ||
     head == "vtt") {
    target.kind = head == "vtable"
                    ? ABI_TARGET_FACT_VTABLE
                    : (head == "vtt" ? ABI_TARGET_FACT_VTT
                                     : (head == "typeinfo-name"
                                          ? ABI_TARGET_FACT_TYPEINFO_NAME
                                          : ABI_TARGET_FACT_TYPEINFO));
    target.type = parse_type_words(words, 1);
    return true;
  }
  if(head == "construction-vtable") {
    target.kind = ABI_TARGET_FACT_CONSTRUCTION_VTABLE;
    target.type = parse_compact_type(word_at(words, 1, "derived type"));
    target.base_offset =
      (unsigned long long)parse_index(word_at(words, 2, "base offset"));
    target.base_type = parse_type_words(words, 3);
    return true;
  }
  if(head == "tls-wrapper") {
    target.kind = ABI_TARGET_FACT_THREAD_LOCAL_WRAPPER;
    if(word_at(words, 1, "wrapped entity") != "variable") {
      fail("tls-wrapper target must wrap a variable");
    }
    target.qualified_name = word_at(words, 2, "variable name");
    return true;
  }
  if(head == "thunk") {
    parse_thunk_target(words, target);
    return true;
  }
  if(head == "virtual-base-thunk") {
    target.kind = ABI_TARGET_FACT_VIRTUAL_BASE_THUNK;
    target.vcall_offset = parse_signed(word_at(words, 1, "vcall offset"));
    if(word_at(words, 2, "thunk function") != "function") {
      fail("virtual-base-thunk target has no function");
    }
    target.function = parse_function_target(words, 3);
    return true;
  }
  return false;
}

bool parse_definition_record(const vector<string> & words,
                             AbiDefinitionRecord & definition)
{
  const string & head = words[0];
  definition.id = words.size() > 1 ? words[1] : "";
  if(head == "let-type") {
    definition.kind = ABI_DEFINITION_TYPE;
    definition.type = parse_type_words(words, 2);
    return true;
  }
  if(head == "let-arg") {
    definition.kind = ABI_DEFINITION_TEMPLATE_ARGUMENT;
    definition.template_argument = parse_template_argument(words, 2);
    return true;
  }
  if(head == "let-expr") {
    definition.kind = ABI_DEFINITION_EXPRESSION;
    definition.expression = parse_expression(words, 2);
    return true;
  }
  if(head == "let-context") {
    definition.kind = ABI_DEFINITION_CONTEXT;
    definition.context = parse_context(words, 2);
    return true;
  }
  if(head == "let-entity") {
    definition.kind = ABI_DEFINITION_ENTITY;
    definition.entity = parse_entity(words, 2);
    return true;
  }
  return false;
}

// --- canonical fact serialization -----------------------------------------

string serialize_compact_type(const AbiType & type);
vector<string> serialize_type(const AbiType & type);

string serialize_array_bound(const AbiArrayBound & bound)
{
  if(bound.kind == ABI_ARRAY_BOUND_EXPRESSION) { return "expr:" + bound.value; }
  return bound.value;
}

string join_words(const vector<string> & words)
{
  string text;
  for(size_t i = 0; i < words.size(); ++i) {
    if(i != 0) { text += " "; }
    text += words[i];
  }
  return text;
}

void append_words(vector<string> & words, const vector<string> & extra)
{
  words.insert(words.end(), extra.begin(), extra.end());
}

// The single-word compact spelling, for the operand positions that take one
// word. Structured facts belong in a `let-*` binder instead.
string serialize_compact_type(const AbiType & type)
{
  switch(type.kind) {
  case ABI_TYPE_NAME_OR_REFERENCE:
  case ABI_TYPE_BUILTIN:
    return type.name;
  case ABI_TYPE_NAMED:
    if(!type.abi_tags.empty()) { break; }
    return "named:" + type.name;
  case ABI_TYPE_POINTER:
    return "ptr:" + serialize_compact_type(type.types.at(0));
  case ABI_TYPE_LVALUE_REFERENCE:
    return "ref:" + serialize_compact_type(type.types.at(0));
  case ABI_TYPE_RVALUE_REFERENCE:
    return "rref:" + serialize_compact_type(type.types.at(0));
  case ABI_TYPE_PACK_EXPANSION:
    return "pack:" + serialize_compact_type(type.types.at(0));
  case ABI_TYPE_CV: {
    string text;
    if(type.is_const) { text += "const:"; }
    if(type.is_volatile) { text += "volatile:"; }
    return text + serialize_compact_type(type.types.at(0));
  }
  case ABI_TYPE_ARRAY:
    if(type.array_bound.kind == ABI_ARRAY_BOUND_EXPRESSION) { break; }
    return "array:" + type.array_bound.value + ":" +
           serialize_compact_type(type.types.at(0));
  case ABI_TYPE_MEMBER_POINTER:
    return "memberptr:" + serialize_compact_type(type.types.at(0)) + ":" +
           serialize_compact_type(type.types.at(1));
  default:
    break;
  }
  fail("ABI type fact has no single-word spelling");
  return "";
}

vector<string> serialize_wrapped_type(const AbiType & type, const string & head)
{
  vector<string> words;
  words.push_back(head);
  append_words(words, serialize_type(type.types.at(0)));
  return words;
}

vector<string> serialize_named_type(const AbiType & type)
{
  vector<string> words;
  if(!type.abi_tags.empty()) {
    words.push_back("tagged");
    words.push_back("named:" + type.name);
    append_words(words, type.abi_tags);
    return words;
  }
  words.push_back("named:" + type.name);
  return words;
}

vector<string> serialize_structured_type(const AbiType & type)
{
  vector<string> words;
  switch(type.kind) {
  case ABI_TYPE_TEMPLATE_SPECIALIZATION:
    words.push_back("template");
    words.push_back(type.name);
    append_words(words, type.argument_refs);
    return words;
  case ABI_TYPE_STD_TEMPLATE_SPECIALIZATION:
    words.push_back("std-template");
    words.push_back(type.standard_substitution);
    words.push_back(type.standard_substitution_includes_arguments ? "yes" : "no");
    words.push_back(type.name);
    append_words(words, type.argument_refs);
    return words;
  case ABI_TYPE_TEMPLATE_PARAMETER:
    words.push_back(type.substitutable ? "template-param-subst" : "template-param");
    words.push_back(to_decimal(type.index));
    return words;
  case ABI_TYPE_TEMPLATE_PARAMETER_SPECIALIZATION:
    words.push_back("template-param-template");
    words.push_back(to_decimal(type.index));
    append_words(words, type.argument_refs);
    return words;
  case ABI_TYPE_BUILTIN_TRANSFORM:
    words.push_back("builtin-transform");
    words.push_back(type.name);
    append_words(words, serialize_type(type.types.at(0)));
    return words;
  case ABI_TYPE_MEMBER:
  case ABI_TYPE_MEMBER_TEMPLATE_SPECIALIZATION:
    words.push_back(type.kind == ABI_TYPE_MEMBER ? "member" : "member-template");
    words.push_back(serialize_compact_type(type.types.at(0)));
    words.push_back(type.name);
    append_words(words, type.argument_refs);
    return words;
  default:
    break;
  }
  fail("unsupported ABI type fact in serialization");
  return words;
}

vector<string> serialize_local_type(const AbiType & type)
{
  vector<string> words;
  if(type.kind == ABI_TYPE_LAMBDA_CLOSURE) {
    words.push_back("lambda-closure");
    words.push_back(type.context_ref);
    words.push_back(type.discriminator);
    for(size_t i = 0; i < type.types.size(); ++i) {
      words.push_back(serialize_compact_type(type.types[i]));
    }
    return words;
  }
  if(type.kind == ABI_TYPE_NAMESPACE_LAMBDA) {
    words.push_back("namespace-lambda");
    words.push_back(type.name);
    append_words(words, type.namespace_qualifiers);
    return words;
  }
  words.push_back("local-type");
  words.push_back(type.context_ref);
  words.push_back(type.name);
  words.push_back(type.discriminator);
  return words;
}

vector<string> serialize_type(const AbiType & type)
{
  vector<string> words;
  switch(type.kind) {
  case ABI_TYPE_NAME_OR_REFERENCE:
  case ABI_TYPE_BUILTIN:
    words.push_back(type.name);
    return words;
  case ABI_TYPE_NAMED:
    return serialize_named_type(type);
  case ABI_TYPE_POINTER:
    return serialize_wrapped_type(type, "ptr");
  case ABI_TYPE_LVALUE_REFERENCE:
    return serialize_wrapped_type(type, "ref");
  case ABI_TYPE_RVALUE_REFERENCE:
    return serialize_wrapped_type(type, "rref");
  case ABI_TYPE_PACK_EXPANSION:
    return serialize_wrapped_type(type, "pack");
  case ABI_TYPE_CV:
    if(type.is_const && type.is_volatile) {
      words.push_back("const");
      words.push_back(serialize_compact_type(type));
      words.erase(words.begin());
      return words;
    }
    return serialize_wrapped_type(type, type.is_const ? "const" : "volatile");
  case ABI_TYPE_VENDOR_QUALIFIED:
    words.push_back("vendor");
    words.push_back(type.name);
    append_words(words, serialize_type(type.types.at(0)));
    return words;
  case ABI_TYPE_ARRAY:
    words.push_back("array");
    words.push_back(serialize_array_bound(type.array_bound));
    append_words(words, serialize_type(type.types.at(0)));
    return words;
  case ABI_TYPE_MEMBER_POINTER:
    words.push_back("member-pointer");
    words.push_back(serialize_compact_type(type.types.at(0)));
    append_words(words, serialize_type(type.types.at(1)));
    return words;
  case ABI_TYPE_FUNCTION:
    words.push_back(string("function-type") +
                    (type.variadic ? "-variadic" : "") +
                    (type.function_lvalue_ref
                       ? "-lvalue-ref"
                       : (type.function_rvalue_ref ? "-rvalue-ref" : "")));
    for(size_t i = 0; i < type.types.size(); ++i) {
      words.push_back(serialize_compact_type(type.types[i]));
    }
    return words;
  case ABI_TYPE_DECLTYPE_EXPRESSION:
    words.push_back("decltype");
    words.push_back(type.expression_ref);
    return words;
  case ABI_TYPE_LAMBDA_CLOSURE:
  case ABI_TYPE_LOCAL_TYPE:
  case ABI_TYPE_NAMESPACE_LAMBDA:
    return serialize_local_type(type);
  default:
    return serialize_structured_type(type);
  }
}

vector<string> serialize_argument(const AbiTemplateArgument & argument)
{
  vector<string> words;
  switch(argument.kind) {
  case ABI_TEMPLATE_ARGUMENT_TYPE:
    words.push_back("type");
    append_words(words, serialize_type(argument.type));
    return words;
  case ABI_TEMPLATE_ARGUMENT_VALUE:
  case ABI_TEMPLATE_ARGUMENT_UNTYPED_VALUE:
    words.push_back("value");
    words.push_back(argument.value_type.name);
    words.push_back(to_signed_decimal(argument.value));
    return words;
  case ABI_TEMPLATE_ARGUMENT_DEPENDENT_VALUE:
    words.push_back("dependent-value");
    words.push_back(serialize_compact_type(argument.type));
    words.push_back(argument.value_type.name);
    words.push_back(to_signed_decimal(argument.value));
    return words;
  case ABI_TEMPLATE_ARGUMENT_EXPRESSION:
    words.push_back("expression");
    words.push_back(argument.name);
    return words;
  case ABI_TEMPLATE_ARGUMENT_TEMPLATE_ENTITY:
    words.push_back("template-entity");
    words.push_back(argument.name);
    return words;
  case ABI_TEMPLATE_ARGUMENT_MEMBER_TEMPLATE_ENTITY:
    words.push_back("member-template-entity");
    words.push_back(serialize_compact_type(argument.owner_type));
    words.push_back(argument.name);
    words.push_back(argument.substitution);
    return words;
  case ABI_TEMPLATE_ARGUMENT_TEMPLATE_PARAMETER_TEMPLATE:
    words.push_back("template-param-template");
    words.push_back(to_decimal(argument.index));
    append_words(words, argument.argument_refs);
    return words;
  case ABI_TEMPLATE_ARGUMENT_ENTITY:
    words.push_back(argument.address_of ? "entity-address" : "entity");
    words.push_back(argument.entity_ref);
    return words;
  case ABI_TEMPLATE_ARGUMENT_MEMBER_EXTERNAL_ENTITY:
    words.push_back("member-external-address");
    words.push_back(argument.symbol);
    words.push_back(serialize_compact_type(argument.owner_type));
    words.push_back(argument.name);
    words.push_back(argument.member_is_function ? "yes" : "no");
    if(!argument.member_is_function) { return words; }
    words.push_back(argument.member_function_const ? "yes" : "no");
    words.push_back(argument.member_function_volatile ? "yes" : "no");
    words.push_back(argument.member_function_lvalue_ref ? "yes" : "no");
    words.push_back(argument.member_function_rvalue_ref ? "yes" : "no");
    words.push_back(argument.member_function_variadic ? "yes" : "no");
    for(size_t i = 0; i < argument.parameter_types.size(); ++i) {
      words.push_back(serialize_compact_type(argument.parameter_types[i]));
    }
    return words;
  case ABI_TEMPLATE_ARGUMENT_PACK:
    words.push_back("pack");
    append_words(words, argument.argument_refs);
    return words;
  }
  fail("unsupported template argument fact in serialization");
  return words;
}

vector<string> serialize_expression(const AbiDependentExpression & expression)
{
  vector<string> words;
  switch(expression.kind) {
  case ABI_EXPRESSION_TEMPLATE_PARAMETER:
    words.push_back(expression.substitutable ? "template-param-subst"
                                             : "template-param");
    words.push_back(to_decimal(expression.index));
    return words;
  case ABI_EXPRESSION_FUNCTION_PARAMETER:
    words.push_back("function-param");
    words.push_back(to_decimal(expression.index));
    return words;
  case ABI_EXPRESSION_LITERAL:
  case ABI_EXPRESSION_INTEGRAL_VALUE:
    words.push_back("literal");
    words.push_back(expression.value_type.name);
    words.push_back(to_signed_decimal(expression.value));
    return words;
  case ABI_EXPRESSION_UNARY:
  case ABI_EXPRESSION_BINARY:
    words.push_back(expression.kind == ABI_EXPRESSION_UNARY ? "unary" : "binary");
    words.push_back(expression.op);
    append_words(words, expression.expression_refs);
    return words;
  case ABI_EXPRESSION_CONDITIONAL:
    words.push_back("conditional");
    append_words(words, expression.expression_refs);
    return words;
  case ABI_EXPRESSION_PACK_EXPANSION:
    words.push_back("pack");
    append_words(words, expression.expression_refs);
    return words;
  case ABI_EXPRESSION_CALL:
    words.push_back("call");
    append_words(words, expression.expression_refs);
    return words;
  case ABI_EXPRESSION_CAST:
  case ABI_EXPRESSION_CONVERSION:
    words.push_back(expression.kind == ABI_EXPRESSION_CAST ? "cast" : "conversion");
    words.push_back(expression.op);
    words.push_back(serialize_compact_type(expression.type));
    append_words(words, expression.expression_refs);
    return words;
  case ABI_EXPRESSION_TEMPLATE_ID:
    words.push_back("template-id");
    words.push_back(expression.text);
    append_words(words, expression.argument_refs);
    return words;
  case ABI_EXPRESSION_TYPE_TRAIT:
    words.push_back("type-trait");
    words.push_back(expression.op);
    for(size_t i = 0; i < expression.type_arguments.size(); ++i) {
      words.push_back(serialize_compact_type(expression.type_arguments[i]));
    }
    return words;
  case ABI_EXPRESSION_SIZEOF_TYPE:
    words.push_back("sizeof-type");
    words.push_back(serialize_compact_type(expression.type));
    return words;
  case ABI_EXPRESSION_MEMBER:
    words.push_back("member");
    words.push_back(serialize_compact_type(expression.type));
    words.push_back(expression.nested_member_owner
                      ? "nested"
                      : (expression.close_member_owner ? "yes" : "no"));
    words.push_back(expression.text);
    return words;
  case ABI_EXPRESSION_OBJECT_MEMBER:
    words.push_back("object-member");
    words.push_back(expression.op);
    append_words(words, expression.expression_refs);
    words.push_back(expression.text);
    append_words(words, expression.argument_refs);
    return words;
  case ABI_EXPRESSION_ENTITY:
    words.push_back("entity-reference");
    words.push_back(expression.entity_ref);
    return words;
  case ABI_EXPRESSION_EXTERNAL_ENTITY:
    words.push_back("symbol-reference");
    words.push_back(expression.text);
    return words;
  }
  fail("unsupported dependent expression fact in serialization");
  return words;
}

vector<string> serialize_function_target(const AbiFunctionTarget & target)
{
  vector<string> words;
  switch(target.kind) {
  case ABI_FUNCTION_TARGET_ENCODING:
    words.push_back("encoding");
    return words;
  case ABI_FUNCTION_TARGET_LAMBDA:
    words.push_back("lambda");
    words.push_back(target.context_ref);
    words.push_back(target.discriminator);
    words.push_back(target.terminal);
    for(size_t i = 0; i < target.signature_parameter_types.size(); ++i) {
      words.push_back(serialize_compact_type(target.signature_parameter_types[i]));
    }
    return words;
  case ABI_FUNCTION_TARGET_LOCAL:
    words.push_back("local");
    words.push_back(target.context_ref);
    words.push_back(target.source_name);
    words.push_back(target.terminal);
    words.push_back(target.discriminator);
    return words;
  case ABI_FUNCTION_TARGET_NAMESPACE_LAMBDA:
    words.push_back("namespace-lambda");
    words.push_back(target.source_name);
    words.push_back(target.terminal);
    append_words(words, target.namespace_qualifiers);
    return words;
  case ABI_FUNCTION_TARGET_PATH:
    break;
  }
  words.push_back("path");
  words.push_back(target.qualified_name);
  for(size_t i = 0; i < target.path_operands.size(); ++i) {
    const AbiFunctionPathOperand & operand = target.path_operands[i];
    words.push_back(!operand.argument_ref.empty()
                      ? operand.argument_ref
                      : serialize_compact_type(operand.type));
  }
  return words;
}

vector<string> serialize_entity(const AbiEntityFact & entity)
{
  vector<string> words;
  if(entity.kind == ABI_ENTITY_FACT_SYMBOL) {
    words.push_back("symbol");
    words.push_back(entity.qualified_name);
    return words;
  }
  if(entity.kind == ABI_ENTITY_FACT_VARIABLE) {
    words.push_back(entity.internal_linkage ? "internal-variable" : "variable");
    words.push_back(entity.qualified_name);
    return words;
  }
  words.push_back(entity.internal_linkage ? "internal-function" : "function");
  words.push_back(entity.qualified_name);
  for(size_t i = 0; i < entity.function.path_operands.size(); ++i) {
    words.push_back(
      serialize_compact_type(entity.function.path_operands[i].type));
  }
  return words;
}

string serialize_qualifier(AbiFunctionQualifier qualifier)
{
  switch(qualifier) {
  case ABI_FUNCTION_QUALIFIER_CONST: return "const";
  case ABI_FUNCTION_QUALIFIER_VOLATILE: return "volatile";
  case ABI_FUNCTION_QUALIFIER_LVALUE_REFERENCE: return "lvalue-ref";
  case ABI_FUNCTION_QUALIFIER_RVALUE_REFERENCE: return "rvalue-ref";
  }
  fail("unsupported function qualifier in serialization");
  return "";
}

vector<string> serialize_name_record(const AbiFunctionRecord & record)
{
  vector<string> words;
  if(record.kind == ABI_FUNCTION_RECORD_NAME_STD) {
    words.push_back("name-std");
    return words;
  }
  if(record.kind == ABI_FUNCTION_RECORD_NAME_SOURCE) {
    words.push_back("name-source");
    words.push_back(record.name);
    words.push_back(record.substitution.empty() ? "-" : record.substitution);
    return words;
  }
  words.push_back("name-template");
  words.push_back(record.name);
  words.push_back(record.substitution.empty() ? "-" : record.substitution);
  words.push_back(record.complete_substitution.empty()
                    ? "-" : record.complete_substitution);
  words.push_back(record.standard_substitution.empty()
                    ? "-" : record.standard_substitution);
  words.push_back(record.standard_substitution_includes_arguments ? "yes" : "no");
  append_words(words, record.argument_refs);
  return words;
}

vector<string> serialize_function_record(const AbiFunctionRecord & record)
{
  vector<string> words;
  switch(record.kind) {
  case ABI_FUNCTION_RECORD_NAME_SOURCE:
  case ABI_FUNCTION_RECORD_NAME_STD:
  case ABI_FUNCTION_RECORD_NAME_TEMPLATE:
    return serialize_name_record(record);
  case ABI_FUNCTION_RECORD_LOCAL_CONTEXT:
    words.push_back("local-context");
    words.push_back(record.context_ref);
    words.push_back(record.source_name);
    words.push_back(record.discriminator);
    return words;
  case ABI_FUNCTION_RECORD_LAMBDA_CONTEXT:
    words.push_back("lambda-context");
    words.push_back(record.context_ref);
    words.push_back(record.discriminator);
    for(size_t i = 0; i < record.types.size(); ++i) {
      words.push_back(serialize_compact_type(record.types[i]));
    }
    return words;
  case ABI_FUNCTION_RECORD_NAMESPACE_LAMBDA_CONTEXT:
    words.push_back("namespace-lambda-context");
    words.push_back(record.source_name);
    append_words(words, record.namespace_qualifiers);
    return words;
  case ABI_FUNCTION_RECORD_TERMINAL:
    words.push_back("terminal");
    words.push_back(record.terminal);
    return words;
  case ABI_FUNCTION_RECORD_TERMINAL_SOURCE:
    words.push_back("terminal-source");
    words.push_back(record.source_name);
    return words;
  case ABI_FUNCTION_RECORD_OPERATOR_TERMINAL:
    words.push_back("operator-terminal");
    words.push_back(record.terminal);
    if(!record.literal_suffix.empty()) { words.push_back(record.literal_suffix); }
    return words;
  case ABI_FUNCTION_RECORD_CONVERSION_TERMINAL:
    words.push_back("conversion-terminal");
    append_words(words, serialize_type(record.type));
    return words;
  case ABI_FUNCTION_RECORD_ABI_TAG:
    words.push_back("abi-tag");
    words.push_back(record.name);
    return words;
  case ABI_FUNCTION_RECORD_QUALIFIER:
    words.push_back("function-qualifier");
    for(size_t i = 0; i < record.qualifiers.size(); ++i) {
      words.push_back(serialize_qualifier(record.qualifiers[i]));
    }
    return words;
  case ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_ARGUMENT:
    words.push_back("function-template-arg");
    append_words(words, record.argument_refs);
    return words;
  case ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_PREFIX:
    words.push_back("function-template-prefix");
    words.push_back(record.substitution);
    return words;
  case ABI_FUNCTION_RECORD_PARAMETER:
  case ABI_FUNCTION_RECORD_RESULT:
    words.push_back(record.kind == ABI_FUNCTION_RECORD_PARAMETER ? "param"
                                                                 : "result");
    append_words(words, serialize_type(record.type));
    return words;
  case ABI_FUNCTION_RECORD_VARIADIC:
    words.push_back("variadic");
    return words;
  }
  fail("unsupported function record fact in serialization");
  return words;
}

vector<string> serialize_definition(const AbiDefinitionRecord & definition)
{
  vector<string> words;
  switch(definition.kind) {
  case ABI_DEFINITION_TYPE:
    words.push_back("let-type");
    words.push_back(definition.id);
    append_words(words, serialize_type(definition.type));
    return words;
  case ABI_DEFINITION_TEMPLATE_ARGUMENT:
    words.push_back("let-arg");
    words.push_back(definition.id);
    append_words(words, serialize_argument(definition.template_argument));
    return words;
  case ABI_DEFINITION_EXPRESSION:
    words.push_back("let-expr");
    words.push_back(definition.id);
    append_words(words, serialize_expression(definition.expression));
    return words;
  case ABI_DEFINITION_CONTEXT:
    words.push_back("let-context");
    words.push_back(definition.id);
    if(definition.context.kind == ABI_CONTEXT_RAW) {
      words.push_back("raw");
      words.push_back(definition.context.fragment);
      return words;
    }
    words.push_back("function");
    append_words(words, serialize_function_target(definition.context.function));
    return words;
  case ABI_DEFINITION_ENTITY:
    words.push_back("let-entity");
    words.push_back(definition.id);
    append_words(words, serialize_entity(definition.entity));
    return words;
  }
  fail("unsupported definition fact in serialization");
  return words;
}

vector<string> serialize_target(const AbiTargetRecord & target)
{
  vector<string> words;
  switch(target.kind) {
  case ABI_TARGET_FACT_TYPE:
    words.push_back("type");
    append_words(words, serialize_type(target.type));
    return words;
  case ABI_TARGET_FACT_FUNCTION:
    if(target.c_linkage) {
      words.push_back("c-function");
      words.push_back(target.function.qualified_name);
      return words;
    }
    words.push_back("function");
    append_words(words, serialize_function_target(target.function));
    return words;
  case ABI_TARGET_FACT_VARIABLE:
    words.push_back(target.internal_linkage ? "internal-variable" : "variable");
    words.push_back(target.qualified_name);
    return words;
  case ABI_TARGET_FACT_TYPEINFO:
  case ABI_TARGET_FACT_TYPEINFO_NAME:
  case ABI_TARGET_FACT_VTABLE:
  case ABI_TARGET_FACT_VTT:
    words.push_back(
      target.kind == ABI_TARGET_FACT_VTABLE
        ? "vtable"
        : (target.kind == ABI_TARGET_FACT_VTT
             ? "vtt"
             : (target.kind == ABI_TARGET_FACT_TYPEINFO_NAME ? "typeinfo-name"
                                                             : "typeinfo")));
    append_words(words, serialize_type(target.type));
    return words;
  case ABI_TARGET_FACT_CONSTRUCTION_VTABLE:
    words.push_back("construction-vtable");
    words.push_back(serialize_compact_type(target.type));
    words.push_back(to_decimal((size_t)target.base_offset));
    append_words(words, serialize_type(target.base_type));
    return words;
  case ABI_TARGET_FACT_THREAD_LOCAL_WRAPPER:
    words.push_back("tls-wrapper");
    words.push_back("variable");
    words.push_back(target.qualified_name);
    return words;
  case ABI_TARGET_FACT_THUNK:
    words.push_back("thunk");
    words.push_back(to_signed_decimal(target.this_adjust));
    if(target.has_result_adjust && target.result_adjust_virtual) {
      words.push_back("virtual-result");
      words.push_back(to_signed_decimal(target.result_adjust));
      words.push_back(to_signed_decimal(target.result_vcall_offset));
    } else if(target.has_result_adjust) {
      words.push_back(to_signed_decimal(target.result_adjust));
    }
    words.push_back("function");
    append_words(words, serialize_function_target(target.function));
    return words;
  case ABI_TARGET_FACT_VIRTUAL_BASE_THUNK:
    words.push_back("virtual-base-thunk");
    words.push_back(to_signed_decimal(target.vcall_offset));
    words.push_back("function");
    append_words(words, serialize_function_target(target.function));
    return words;
  }
  fail("unsupported target fact in serialization");
  return words;
}

}  // namespace

AbiFactRecord parse_fact_record_words(const vector<string> & words)
{
  if(words.empty()) { fail("empty fact record"); }
  AbiFactRecord record;
  if(words[0].compare(0, 4, "let-") == 0) {
    record.kind = ABI_FACT_RECORD_DEFINITION;
    if(words.size() < 2 || words[1].empty()) {
      fail("definition record has no identifier");
    }
    if(!parse_definition_record(words, record.definition)) {
      fail("unknown definition record '" + words[0] + "'");
    }
    return record;
  }
  record.kind = ABI_FACT_RECORD_TARGET;
  if(parse_target_record(words, record.target)) { return record; }
  record.kind = ABI_FACT_RECORD_FUNCTION;
  if(parse_function_record(words, record.function)) { return record; }
  fail("unknown ABI fact record '" + words[0] + "'");
  return record;
}

string serialize_fact_file(const AbiFactFile & file)
{
  string text;
  for(size_t index = 0; index < file.cases.size(); ++index) {
    const AbiFactCase & fact_case = file.cases[index];
    if(index != 0) { text += "\n"; }
    if(!fact_case.label.empty()) { text += "case " + fact_case.label + "\n"; }
    for(size_t i = 0; i < fact_case.records.size(); ++i) {
      const AbiFactRecord & record = fact_case.records[i];
      if(record.kind == ABI_FACT_RECORD_DEFINITION) {
        text += join_words(serialize_definition(record.definition));
      } else if(record.kind == ABI_FACT_RECORD_TARGET) {
        text += join_words(serialize_target(record.target));
      } else {
        text += join_words(serialize_function_record(record.function));
      }
      text += "\n";
    }
  }
  return text;
}

AbiFactFile parse_fact_text(const string & text)
{
  AbiFactFile file;
  istringstream stream(text);
  string line;
  while(getline(stream, line)) {
    const vector<string> words = split_line_words(line);
    if(words.empty()) { continue; }
    if(words[0] == "case") {
      AbiFactCase fact_case;
      fact_case.label = words.size() > 1 ? words[1] : "";
      file.cases.push_back(fact_case);
      continue;
    }
    if(file.cases.empty()) { file.cases.push_back(AbiFactCase()); }
    file.cases.back().records.push_back(parse_fact_record_words(words));
  }
  if(file.cases.empty()) { fail("fact file has no case"); }
  return file;
}

namespace {

string read_fact_file(const string & path)
{
  ifstream in(path.c_str(), ios::binary);
  if(!in) { fail("unable to read '" + path + "'"); }
  ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

}  // namespace

string mangle_fact_files(const vector<string> & input_paths)
{
  string output;
  for(size_t i = 0; i < input_paths.size(); ++i) {
    output += mangle_fact_file(parse_fact_text(read_fact_file(input_paths[i])));
  }
  return output;
}

// The canonical fact spelling of the inputs, for diagnostics and for
// regenerating normalized fact files from a typed model.
string serialize_fact_files(const vector<string> & input_paths)
{
  string output;
  for(size_t i = 0; i < input_paths.size(); ++i) {
    output += serialize_fact_file(parse_fact_text(read_fact_file(input_paths[i])));
  }
  return output;
}

}  // namespace abi_mangle

namespace {

struct AbimangleInvocation
{
  string outfile;
  vector<string> inputs;
  bool emit_facts = false;
};

bool has_help_arg(int argc, char ** argv)
{
  for(int i = 1; i < argc; ++i) {
    const string arg = argv[i];
    if(arg == "--help" || arg == "-h") {
      return true;
    }
  }
  return false;
}

void print_help()
{
  cout << "usage: abimangle [--emit-facts] -o <outfile> <abi-facts-file>...\n";
}

AbimangleInvocation parse_invocation(int argc, char ** argv)
{
  AbimangleInvocation invocation;
  for(int i = 1; i < argc; ++i) {
    const string arg = argv[i];
    if(arg == "--emit-facts") {
      invocation.emit_facts = true;
      continue;
    }
    if(arg == "-o") {
      if(i + 1 >= argc) {
        throw logic_error("missing output file after -o");
      }
      invocation.outfile = argv[++i];
      continue;
    }
    invocation.inputs.push_back(arg);
  }
  if(invocation.outfile.empty() || invocation.inputs.empty()) {
    throw logic_error("invalid usage");
  }
  return invocation;
}

int run_abimangle(int argc, char ** argv)
{
  if(has_help_arg(argc, argv)) {
    print_help();
    return EXIT_SUCCESS;
  }
  const AbimangleInvocation invocation = parse_invocation(argc, argv);
  const string names = invocation.emit_facts
                         ? abi_mangle::serialize_fact_files(invocation.inputs)
                         : abi_mangle::mangle_fact_files(invocation.inputs);
  ofstream out(invocation.outfile.c_str());
  if(!out) {
    throw logic_error("unable to open output file '" + invocation.outfile + "'");
  }
  out << names;
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char ** argv)
{
  try {
    return run_abimangle(argc, argv);
  } catch(const NotImplementedException &) {
    cerr << "abimangle: not implemented\n";
    return EXIT_FAILURE;
  } catch(const exception & e) {
    cerr << "abimangle: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
}
