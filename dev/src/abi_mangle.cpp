// Itanium C++ ABI name encoder for the typed ABI fact model.
//
// The encoder consumes only typed facts. `abimangle` builds them from fact
// text; later compiler stages build the same records from semantic state and
// call `mangle_target` directly.

#include "abi_mangle.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

using namespace std;

namespace abi_mangle {

namespace {

const size_t MAX_ENCODE_DEPTH = 4000;

void fail(const string & message)
{
  throw logic_error("abi-mangle: " + message);
}

struct WordCode
{
  const char * word;
  const char * code;
};

const WordCode BUILTIN_TYPE_CODES[] = {
  {"void", "v"}, {"wchar", "w"}, {"wchar_t", "w"}, {"bool", "b"},
  {"char", "c"}, {"schar", "a"}, {"uchar", "h"}, {"short", "s"},
  {"ushort", "t"}, {"int", "i"}, {"uint", "j"}, {"long", "l"},
  {"ulong", "m"}, {"longlong", "x"}, {"ulonglong", "y"}, {"int128", "n"},
  {"uint128", "o"}, {"float", "f"}, {"double", "d"}, {"longdouble", "e"},
  {"float128", "g"}, {"ellipsis", "z"}, {"nullptr", "Dn"}, {"auto", "Da"},
  {"decltype-auto", "Dc"}, {"half", "Dh"}, {"char16", "Ds"},
  {"char16_t", "Ds"}, {"char32", "Di"}, {"char32_t", "Di"},
  {"complex-float", "Cf"}, {"complex-double", "Cd"},
  {"complex-longdouble", "Ce"},
};

const WordCode CTOR_DTOR_CODES[] = {
  {"constructor-complete", "C1"}, {"constructor-base", "C2"},
  {"constructor-allocating", "C3"}, {"constructor-inheriting", "C4"},
  {"destructor-deleting", "D0"}, {"destructor-complete", "D1"},
  {"destructor-base", "D2"},
};

// Operators whose Itanium terminal does not depend on unary/binary use.
const WordCode FIXED_OPERATOR_CODES[] = {
  {"new", "nw"}, {"new-array", "na"}, {"delete", "dl"},
  {"delete-array", "da"}, {"unary-plus", "ps"}, {"unary-minus", "ng"},
  {"address-of", "ad"}, {"deref", "de"}, {"bit-not", "co"},
  {"complement", "co"}, {"binary-plus", "pl"}, {"binary-minus", "mi"},
  {"multiply", "ml"}, {"bit-and", "an"}, {"divide", "dv"},
  {"remainder", "rm"}, {"bit-or", "or"}, {"bit-xor", "eo"},
  {"assign", "aS"}, {"plus-assign", "pL"}, {"minus-assign", "mI"},
  {"multiply-assign", "mL"}, {"divide-assign", "dV"},
  {"remainder-assign", "rM"}, {"and-assign", "aN"}, {"or-assign", "oR"},
  {"xor-assign", "eO"}, {"lshift", "ls"}, {"rshift", "rs"},
  {"lshift-assign", "lS"}, {"rshift-assign", "rS"}, {"eq", "eq"},
  {"equal", "eq"}, {"ne", "ne"}, {"not-equal", "ne"}, {"lt", "lt"},
  {"gt", "gt"}, {"le", "le"}, {"ge", "ge"}, {"logical-and", "aa"},
  {"logical-or", "oo"}, {"logical-not", "nt"}, {"increment", "pp"},
  {"decrement", "mm"}, {"comma", "cm"}, {"member-pointer", "pm"},
  {"arrow-star", "pm"}, {"arrow", "pt"}, {"call", "cl"}, {"index", "ix"},
  {"subscript", "ix"}, {"conditional", "qu"}, {"sizeof", "sz"},
  {"alignof", "az"},
};

bool lookup_code(const WordCode * table, size_t count, const string & word,
                 string & code)
{
  for(size_t i = 0; i < count; ++i) {
    if(word == table[i].word) {
      code = table[i].code;
      return true;
    }
  }
  return false;
}

bool builtin_type_code(const string & word, string & code)
{
  const size_t count = sizeof(BUILTIN_TYPE_CODES) / sizeof(WordCode);
  return lookup_code(BUILTIN_TYPE_CODES, count, word, code);
}

// Bit width of a normalized unsigned builtin word, or 0 when the word does not
// name an unsigned builtin type.
unsigned unsigned_builtin_width(const string & word)
{
  if(word == "uchar" || word == "bool") { return 8; }
  if(word == "ushort") { return 16; }
  if(word == "uint") { return 32; }
  if(word == "ulong" || word == "ulonglong") { return 64; }
  return 0;
}

string decimal(unsigned long long value)
{
  ostringstream stream;
  stream << value;
  return stream.str();
}

// Itanium spells a negative literal as `n` plus its magnitude, and reads a
// negative stored value for an unsigned type as that type's bit pattern.
string integral_literal_digits(const string & type_word, long long value)
{
  const unsigned width = unsigned_builtin_width(type_word);
  if(width != 0) {
    unsigned long long bits = (unsigned long long)value;
    if(width < 64) { bits &= ((unsigned long long)1 << width) - 1; }
    return decimal(bits);
  }
  if(value < 0) {
    // Negating LLONG_MIN overflows, so take the magnitude unsigned.
    const unsigned long long magnitude = -(unsigned long long)value;
    return "n" + decimal(magnitude);
  }
  return decimal((unsigned long long)value);
}

string source_name(const string & text)
{
  if(text.empty()) { fail("empty source name"); }
  return decimal(text.size()) + text;
}

// `T_`, `T0_`, `T1_`, ... for template parameter index 0, 1, 2, ...
string indexed_code(const string & prefix, size_t index)
{
  if(index == 0) { return prefix + "_"; }
  return prefix + decimal(index - 1) + "_";
}

vector<string> split_qualified_name(const string & name)
{
  vector<string> parts;
  size_t start = 0;
  while(start <= name.size()) {
    const size_t stop = name.find("::", start);
    if(stop == string::npos) {
      parts.push_back(name.substr(start));
      break;
    }
    parts.push_back(name.substr(start, stop - start));
    start = stop + 2;
  }
  // A leading `::` produces an empty root component; drop it.
  if(!parts.empty() && parts.front().empty()) { parts.erase(parts.begin()); }
  for(size_t i = 0; i < parts.size(); ++i) {
    if(parts[i].empty()) { fail("malformed qualified name '" + name + "'"); }
  }
  if(parts.empty()) { fail("empty qualified name"); }
  return parts;
}

string abi_tag_suffix(const vector<string> & tags)
{
  vector<string> sorted = tags;
  sort(sorted.begin(), sorted.end());
  sorted.erase(unique(sorted.begin(), sorted.end()), sorted.end());
  string text;
  for(size_t i = 0; i < sorted.size(); ++i) {
    text += "B" + source_name(sorted[i]);
  }
  return text;
}

// `<call-offset>`: `h <offset> _` for a fixed adjustment, `v <offset> _
// <vcall-offset> _` when the adjustment is loaded from the vtable.
string call_offset(bool virtual_offset, long long fixed, long long vcall)
{
  string text = virtual_offset ? "v" : "h";
  text += integral_literal_digits("longlong", fixed) + "_";
  if(virtual_offset) {
    text += integral_literal_digits("longlong", vcall) + "_";
  }
  return text;
}

}  // namespace

namespace {

// One mangled name's encoding state. Substitution slots are keyed on the
// canonical expansion of the candidate, so equivalent spellings of the same
// fact share a slot while encoding-significant differences do not.
class Encoder
{
public:
  explicit Encoder(const AbiDefinitionMap & definitions) : defs(definitions) {}

  void encode_target(const AbiTargetRecord & target,
                     const vector<AbiFunctionRecord> & records);

  const string & text() const { return out; }

private:
  struct Mark
  {
    size_t out;
    size_t canon;
    size_t table;
  };

  enum ComponentKind
  {
    COMPONENT_STD,
    COMPONENT_SOURCE,
    COMPONENT_TEMPLATE,
    COMPONENT_CLOSURE,
    COMPONENT_LOCAL_CLASS
  };

  struct NameComponent
  {
    ComponentKind kind = COMPONENT_SOURCE;
    string name;
    vector<string> argument_refs;
    string standard_substitution;
    bool standard_includes_arguments = false;
    vector<AbiType> lambda_signature;
    string discriminator;
  };

  struct FunctionShape;

  // --- substitution table ------------------------------------------------
  Mark mark() const;
  void rollback(const Mark & point);
  void close_candidate(const Mark & point);
  void put(const string & fragment) { out += fragment; canon += fragment; }
  void put_out(const string & fragment) { out += fragment; }
  void put_substitution(size_t slot);

  // --- fact resolution ---------------------------------------------------
  const AbiDefinitionRecord * definition(const string & id,
                                         AbiDefinitionKind kind) const;
  const AbiType & resolve_type(const AbiType & type, AbiType & storage,
                               const AbiDefinitionRecord ** origin = 0) const;
  const AbiTemplateArgument & resolve_argument(const string & id) const;
  const AbiDependentExpression & resolve_expression(const string & id) const;
  const AbiLocalContext & resolve_context(const string & id) const;
  const AbiEntityFact & resolve_entity(const string & id) const;

  // --- types -------------------------------------------------------------
  void emit_type(const AbiType & type);
  void emit_type_body(const AbiType & type);
  void emit_cv_type(const AbiType & type);
  void emit_function_type(const AbiType & type);
  void emit_class_prefix_content(const AbiType & type);
  void emit_class_prefix(const AbiType & type);
  void emit_qualified_name(const string & name, const vector<string> & tags,
                           bool is_template,
                           const vector<string> & argument_refs);
  void emit_template_arguments(const vector<string> & argument_refs);
  void emit_array(const AbiType & type);
  void emit_local_type(const AbiType & type);
  size_t class_component_count(const AbiType & type) const;

  // --- template arguments and expressions --------------------------------
  void emit_argument(const string & id);
  void emit_argument_fact(const AbiTemplateArgument & argument);
  void emit_member_template_entity(const AbiTemplateArgument & argument);
  void emit_expression_ref(const string & id);
  void emit_expression(const AbiDependentExpression & expression);
  void emit_expression_operands(const AbiDependentExpression & expression);
  void emit_entity_symbol(const string & id);
  string entity_symbol(const AbiEntityFact & entity) const;

  // --- names and functions -----------------------------------------------
  void push_name_components(const string & name, FunctionShape & shape) const;
  void push_name_component(const NameComponent & component,
                           FunctionShape & shape) const;
  void build_shape(const AbiFunctionTarget & target,
                   const vector<AbiFunctionRecord> & records,
                   FunctionShape & shape) const;
  void apply_record(const AbiFunctionRecord & record,
                    FunctionShape & shape) const;
  void emit_function(const AbiFunctionTarget & target,
                     const vector<AbiFunctionRecord> & records);
  void emit_function_name(const FunctionShape & shape);
  void emit_component_chain(const FunctionShape & shape);
  void emit_unqualified_terminal(const FunctionShape & shape);
  void emit_context_prefix(const FunctionShape & shape);
  void emit_bare_function_type(const FunctionShape & shape);
  void emit_context(const string & id);
  void emit_lambda_signature(const vector<AbiType> & types);
  void emit_data_name(const string & qualified_name, bool internal_linkage);

  bool reuse_known_slot(const AbiDefinitionRecord * binder);
  void remember_key(const AbiDefinitionRecord * binder, const Mark & point);

  const AbiDefinitionMap & defs;
  string out;
  string canon;
  vector<string> table;
  unordered_map<string, size_t> index;
  // Canonical key of every definition binder this name has already encoded.
  // A repeated use names the same binder, so it needs no second structural
  // walk to discover that it repeats.
  map<const AbiDefinitionRecord *, string> known_keys;
  size_t depth = 0;

  friend struct DepthGuard;
};

struct DepthGuard
{
  size_t & depth;
  explicit DepthGuard(size_t & counter) : depth(counter)
  {
    if(++depth > MAX_ENCODE_DEPTH) { fail("ABI fact graph is too deep"); }
  }
  ~DepthGuard() { --depth; }
};

// --- substitution table --------------------------------------------------

Encoder::Mark Encoder::mark() const
{
  Mark point;
  point.out = out.size();
  point.canon = canon.size();
  point.table = table.size();
  return point;
}

void Encoder::rollback(const Mark & point)
{
  for(size_t slot = point.table; slot < table.size(); ++slot) {
    index.erase(table[slot]);
  }
  table.resize(point.table);
  out.resize(point.out);
  canon.resize(point.canon);
}

void Encoder::put_substitution(size_t slot)
{
  out += "S";
  if(slot > 0) {
    size_t value = slot - 1;
    string digits;
    do {
      const size_t digit = value % 36;
      digits += (char)(digit < 10 ? '0' + digit : 'A' + (digit - 10));
      value /= 36;
    } while(value != 0);
    reverse(digits.begin(), digits.end());
    out += digits;
  }
  out += "_";
  canon += table[slot];
}

// Close a speculatively encoded candidate: reuse an earlier slot when the
// canonical expansion repeats, otherwise register a new slot.
void Encoder::close_candidate(const Mark & point)
{
  const string key = canon.substr(point.canon);
  const unordered_map<string, size_t>::const_iterator found = index.find(key);
  if(found != index.end()) {
    if(found->second < point.table) {
      const size_t slot = found->second;
      rollback(point);
      put_substitution(slot);
    }
    return;
  }
  index[key] = table.size();
  table.push_back(key);
}

// A node encoded earlier in this name already has a canonical key, so its
// substitution slot can be found without re-encoding the structure.
bool Encoder::reuse_known_slot(const AbiDefinitionRecord * binder)
{
  const map<const AbiDefinitionRecord *, string>::const_iterator known =
    known_keys.find(binder);
  if(known == known_keys.end()) { return false; }
  const unordered_map<string, size_t>::const_iterator found =
    index.find(known->second);
  if(found == index.end()) { return false; }
  put_substitution(found->second);
  return true;
}

void Encoder::remember_key(const AbiDefinitionRecord * binder,
                           const Mark & point)
{
  known_keys[binder] = canon.substr(point.canon);
}

// --- fact resolution -----------------------------------------------------

const AbiDefinitionRecord * Encoder::definition(const string & id,
                                                AbiDefinitionKind kind) const
{
  const AbiDefinitionMap::const_iterator found = defs.find(id);
  if(found == defs.end() || found->second->kind != kind) { return 0; }
  return found->second;
}

const AbiType & Encoder::resolve_type(const AbiType & type, AbiType & storage,
                                      const AbiDefinitionRecord ** origin) const
{
  const AbiType * current = &type;
  vector<string> tags = type.abi_tags;
  size_t hops = 0;
  if(origin != 0) { *origin = 0; }
  while(current->kind == ABI_TYPE_NAME_OR_REFERENCE) {
    const AbiDefinitionRecord * record =
      definition(current->name, ABI_DEFINITION_TYPE);
    if(record == 0) { break; }
    if(++hops > MAX_ENCODE_DEPTH) { fail("cyclic type definition"); }
    if(origin != 0) { *origin = record; }
    current = &record->type;
    tags.insert(tags.end(), current->abi_tags.begin(), current->abi_tags.end());
  }
  if(current->kind == ABI_TYPE_NAME_OR_REFERENCE) {
    // An unbound word names a builtin type when it spells one, and otherwise
    // names a class type by its bare source name.
    string code;
    storage = AbiType();
    storage.kind = builtin_type_code(current->name, code) ? ABI_TYPE_BUILTIN
                                                          : ABI_TYPE_NAMED;
    storage.name = current->name;
    storage.abi_tags = tags;
    return storage;
  }
  if(tags.size() != current->abi_tags.size()) {
    storage = *current;
    storage.abi_tags = tags;
    return storage;
  }
  return *current;
}

const AbiTemplateArgument & Encoder::resolve_argument(const string & id) const
{
  const AbiDefinitionRecord * record =
    definition(id, ABI_DEFINITION_TEMPLATE_ARGUMENT);
  if(record == 0) { fail("undefined template argument '" + id + "'"); }
  return record->template_argument;
}

const AbiDependentExpression & Encoder::resolve_expression(
  const string & id) const
{
  const AbiDefinitionRecord * record =
    definition(id, ABI_DEFINITION_EXPRESSION);
  if(record == 0) { fail("undefined expression '" + id + "'"); }
  return record->expression;
}

const AbiLocalContext & Encoder::resolve_context(const string & id) const
{
  const AbiDefinitionRecord * record = definition(id, ABI_DEFINITION_CONTEXT);
  if(record == 0) { fail("undefined context '" + id + "'"); }
  return record->context;
}

const AbiEntityFact & Encoder::resolve_entity(const string & id) const
{
  const AbiDefinitionRecord * record = definition(id, ABI_DEFINITION_ENTITY);
  if(record == 0) { fail("undefined entity '" + id + "'"); }
  return record->entity;
}

// --- types ---------------------------------------------------------------

bool is_class_like(AbiTypeKind kind)
{
  switch(kind) {
  case ABI_TYPE_NAMED:
  case ABI_TYPE_TEMPLATE_SPECIALIZATION:
  case ABI_TYPE_STD_TEMPLATE_SPECIALIZATION:
  case ABI_TYPE_TEMPLATE_PARAMETER_SPECIALIZATION:
  case ABI_TYPE_MEMBER:
  case ABI_TYPE_MEMBER_TEMPLATE_SPECIALIZATION:
  case ABI_TYPE_BUILTIN_TRANSFORM:
  case ABI_TYPE_LOCAL_TYPE:
  case ABI_TYPE_LAMBDA_CLOSURE:
  case ABI_TYPE_NAMESPACE_LAMBDA:
  case ABI_TYPE_DECLTYPE_EXPRESSION:
    return true;
  default:
    return false;
  }
}

size_t Encoder::class_component_count(const AbiType & type) const
{
  AbiType storage;
  const AbiType & node = resolve_type(type, storage);
  switch(node.kind) {
  case ABI_TYPE_NAMED:
  case ABI_TYPE_TEMPLATE_SPECIALIZATION: {
    vector<string> parts = split_qualified_name(node.name);
    if(parts.front() == "std") { parts.erase(parts.begin()); }
    return parts.empty() ? 1 : parts.size();
  }
  case ABI_TYPE_MEMBER:
  case ABI_TYPE_MEMBER_TEMPLATE_SPECIALIZATION:
    if(node.types.empty()) { fail("member type without an owner"); }
    return class_component_count(node.types[0]) + 1;
  case ABI_TYPE_NAMESPACE_LAMBDA:
    return node.namespace_qualifiers.size() + 1;
  case ABI_TYPE_TEMPLATE_PARAMETER:
    return 1;
  default:
    return 1;
  }
}

void Encoder::emit_type(const AbiType & type)
{
  DepthGuard guard(depth);
  AbiType storage;
  const AbiDefinitionRecord * binder = 0;
  const AbiType & node = resolve_type(type, storage, &binder);
  if(node.kind == ABI_TYPE_BUILTIN) {
    string code;
    if(!builtin_type_code(node.name, code)) {
      fail("unknown builtin type '" + node.name + "'");
    }
    put(code);
    return;
  }
  if(node.kind == ABI_TYPE_TEMPLATE_PARAMETER && !node.substitutable) {
    put(indexed_code("T", node.index));
    return;
  }
  // A standard substitution that already covers its arguments is spelled by
  // its predefined code and never occupies a table slot.
  if(node.kind == ABI_TYPE_STD_TEMPLATE_SPECIALIZATION &&
     node.standard_substitution_includes_arguments) {
    put(node.standard_substitution);
    return;
  }
  const bool shared = binder != 0 && &node != &storage;
  if(shared && reuse_known_slot(binder)) { return; }
  const Mark point = mark();
  const bool nested = is_class_like(node.kind) && class_component_count(node) > 1;
  if(nested) { put_out("N"); }
  if(is_class_like(node.kind)) {
    emit_class_prefix_content(node);
  } else {
    emit_type_body(node);
  }
  if(nested) { put_out("E"); }
  if(shared) { remember_key(binder, point); }
  close_candidate(point);
}

void Encoder::emit_type_body(const AbiType & type)
{
  switch(type.kind) {
  case ABI_TYPE_POINTER:
    put("P");
    emit_type(type.types.at(0));
    return;
  case ABI_TYPE_LVALUE_REFERENCE:
    put("R");
    emit_type(type.types.at(0));
    return;
  case ABI_TYPE_RVALUE_REFERENCE:
    put("O");
    emit_type(type.types.at(0));
    return;
  case ABI_TYPE_CV:
    emit_cv_type(type);
    return;
  case ABI_TYPE_PACK_EXPANSION:
    put("Dp");
    emit_type(type.types.at(0));
    return;
  case ABI_TYPE_VENDOR_QUALIFIED:
    put("U" + source_name(type.name));
    emit_type(type.types.at(0));
    return;
  case ABI_TYPE_ARRAY:
    emit_array(type);
    return;
  case ABI_TYPE_FUNCTION:
    emit_function_type(type);
    return;
  case ABI_TYPE_MEMBER_POINTER:
    put("M");
    emit_type(type.types.at(0));
    emit_type(type.types.at(1));
    return;
  case ABI_TYPE_TEMPLATE_PARAMETER:
    put(indexed_code("T", type.index));
    return;
  default:
    fail("unsupported ABI type fact");
  }
}

// Adjacent cv wrappers describe one canonical cv-qualified type, so they are
// folded together and emitted in Itanium order.
void Encoder::emit_cv_type(const AbiType & type)
{
  bool is_const = false;
  bool is_volatile = false;
  const AbiType * node = &type;
  AbiType storage;
  size_t hops = 0;
  while(node->kind == ABI_TYPE_CV) {
    is_const = is_const || node->is_const;
    is_volatile = is_volatile || node->is_volatile;
    if(node->types.empty()) { fail("cv type without an operand"); }
    if(++hops > MAX_ENCODE_DEPTH) { fail("cyclic cv type"); }
    AbiType inner;
    const AbiType & next = resolve_type(node->types[0], inner);
    if(next.kind != ABI_TYPE_CV) {
      storage = next;
      node = &storage;
      break;
    }
    storage = next;
    node = &storage;
  }
  if(is_volatile) { put("V"); }
  if(is_const) { put("K"); }
  emit_type(*node);
}

void Encoder::emit_array(const AbiType & type)
{
  put("A");
  switch(type.array_bound.kind) {
  case ABI_ARRAY_BOUND_EXPRESSION:
    emit_expression_ref(type.array_bound.value);
    break;
  default:
    put(type.array_bound.value);
    break;
  }
  put("_");
  emit_type(type.types.at(0));
}

void Encoder::emit_function_type(const AbiType & type)
{
  put("F");
  if(type.types.empty()) { fail("function type without a result"); }
  emit_type(type.types[0]);
  if(type.types.size() == 1) {
    put("v");
  } else {
    for(size_t i = 1; i < type.types.size(); ++i) { emit_type(type.types[i]); }
  }
  if(type.variadic) { put("z"); }
  put("E");
}

void Encoder::emit_template_arguments(const vector<string> & argument_refs)
{
  put("I");
  for(size_t i = 0; i < argument_refs.size(); ++i) {
    emit_argument(argument_refs[i]);
  }
  put("E");
}

// Emit a class-like name in prefix form. The caller supplies `N`/`E` when the
// name has more than one component; those delimiters stay out of the canonical
// key so a class used as a type and as a prefix shares one substitution slot.
void Encoder::emit_class_prefix_content(const AbiType & type)
{
  DepthGuard guard(depth);
  const Mark base = mark();
  switch(type.kind) {
  case ABI_TYPE_NAMED:
    emit_qualified_name(type.name, type.abi_tags, false, vector<string>());
    return;
  case ABI_TYPE_TEMPLATE_SPECIALIZATION:
    emit_qualified_name(type.name, type.abi_tags, true, type.argument_refs);
    return;
  case ABI_TYPE_STD_TEMPLATE_SPECIALIZATION:
    put(type.standard_substitution);
    emit_template_arguments(type.argument_refs);
    return;
  case ABI_TYPE_TEMPLATE_PARAMETER_SPECIALIZATION:
    put(indexed_code("T", type.index));
    if(type.substitutable) { close_candidate(base); }
    emit_template_arguments(type.argument_refs);
    return;
  case ABI_TYPE_MEMBER:
    emit_class_prefix(type.types.at(0));
    put(source_name(type.name) + abi_tag_suffix(type.abi_tags));
    return;
  case ABI_TYPE_MEMBER_TEMPLATE_SPECIALIZATION:
    emit_class_prefix(type.types.at(0));
    put(source_name(type.name) + abi_tag_suffix(type.abi_tags));
    close_candidate(base);
    emit_template_arguments(type.argument_refs);
    return;
  case ABI_TYPE_BUILTIN_TRANSFORM:
    put("u" + source_name(type.name));
    put("I");
    for(size_t i = 0; i < type.types.size(); ++i) { emit_type(type.types[i]); }
    put("E");
    return;
  case ABI_TYPE_DECLTYPE_EXPRESSION:
    put("DT");
    emit_expression_ref(type.expression_ref);
    put("E");
    return;
  case ABI_TYPE_NAMESPACE_LAMBDA:
    for(size_t i = 0; i < type.namespace_qualifiers.size(); ++i) {
      put(source_name(type.namespace_qualifiers[i]));
      close_candidate(base);
    }
    put(source_name(type.name) + abi_tag_suffix(type.abi_tags));
    return;
  case ABI_TYPE_LOCAL_TYPE:
  case ABI_TYPE_LAMBDA_CLOSURE:
    emit_local_type(type);
    return;
  default:
    fail("unsupported class-like ABI type fact");
  }
}

void Encoder::emit_local_type(const AbiType & type)
{
  emit_context(type.context_ref);
  if(type.kind == ABI_TYPE_LAMBDA_CLOSURE) {
    put("Ul");
    emit_lambda_signature(type.types);
    put("E");
    put(type.discriminator + "_");
    return;
  }
  put(source_name(type.name) + abi_tag_suffix(type.abi_tags));
  if(!type.discriminator.empty() && type.discriminator != "0") {
    const long long value = atoll(type.discriminator.c_str());
    put("_" + decimal((unsigned long long)(value - 1)));
  }
}

void Encoder::emit_lambda_signature(const vector<AbiType> & types)
{
  if(types.empty()) {
    put("v");
    return;
  }
  for(size_t i = 0; i < types.size(); ++i) { emit_type(types[i]); }
}

void Encoder::emit_qualified_name(const string & name,
                                  const vector<string> & tags,
                                  bool is_template,
                                  const vector<string> & argument_refs)
{
  const Mark base = mark();
  vector<string> parts = split_qualified_name(name);
  if(parts.front() == "std") {
    put("St");
    parts.erase(parts.begin());
    if(parts.empty()) { fail("qualified name names only namespace std"); }
  }
  for(size_t i = 0; i + 1 < parts.size(); ++i) {
    put(source_name(parts[i]));
    close_candidate(base);
  }
  put(source_name(parts.back()) + abi_tag_suffix(tags));
  if(is_template) {
    close_candidate(base);
    emit_template_arguments(argument_refs);
  }
}

void Encoder::emit_class_prefix(const AbiType & type)
{
  DepthGuard guard(depth);
  AbiType storage;
  const AbiDefinitionRecord * binder = 0;
  const AbiType & node = resolve_type(type, storage, &binder);
  if(node.kind == ABI_TYPE_TEMPLATE_PARAMETER) {
    if(!node.substitutable) {
      put(indexed_code("T", node.index));
      return;
    }
    const Mark point = mark();
    put(indexed_code("T", node.index));
    close_candidate(point);
    return;
  }
  if(node.kind == ABI_TYPE_STD_TEMPLATE_SPECIALIZATION &&
     node.standard_substitution_includes_arguments) {
    put(node.standard_substitution);
    return;
  }
  if(!is_class_like(node.kind)) { fail("ABI type cannot be a name prefix"); }
  const bool shared = binder != 0 && &node != &storage;
  if(shared && reuse_known_slot(binder)) { return; }
  const Mark point = mark();
  emit_class_prefix_content(node);
  if(shared) { remember_key(binder, point); }
  close_candidate(point);
}

// --- template arguments --------------------------------------------------

void Encoder::emit_argument(const string & id)
{
  DepthGuard guard(depth);
  emit_argument_fact(resolve_argument(id));
}

void Encoder::emit_argument_fact(const AbiTemplateArgument & argument)
{
  switch(argument.kind) {
  case ABI_TEMPLATE_ARGUMENT_TYPE:
    emit_type(argument.type);
    return;
  case ABI_TEMPLATE_ARGUMENT_VALUE:
    put("L");
    emit_type(argument.value_type);
    put(integral_literal_digits(argument.value_type.name, argument.value));
    put("E");
    return;
  case ABI_TEMPLATE_ARGUMENT_DEPENDENT_VALUE:
    put("Tn");
    emit_type(argument.type);
    put("L");
    emit_type(argument.value_type);
    put(integral_literal_digits(argument.value_type.name, argument.value));
    put("E");
    return;
  case ABI_TEMPLATE_ARGUMENT_UNTYPED_VALUE:
    put("L");
    put(integral_literal_digits(argument.value_type.name, argument.value));
    put("E");
    return;
  case ABI_TEMPLATE_ARGUMENT_EXPRESSION:
    put("X");
    emit_expression_ref(argument.name);
    put("E");
    return;
  case ABI_TEMPLATE_ARGUMENT_TEMPLATE_ENTITY: {
    AbiType named;
    named.kind = ABI_TYPE_NAMED;
    named.name = argument.name;
    emit_type(named);
    return;
  }
  case ABI_TEMPLATE_ARGUMENT_MEMBER_TEMPLATE_ENTITY:
    emit_member_template_entity(argument);
    return;
  case ABI_TEMPLATE_ARGUMENT_TEMPLATE_PARAMETER_TEMPLATE:
    if(argument.argument_refs.empty()) {
      put(indexed_code("T", argument.index));
      return;
    }
    put(indexed_code("T", argument.index));
    emit_template_arguments(argument.argument_refs);
    return;
  case ABI_TEMPLATE_ARGUMENT_EXTERNAL_ENTITY:
  case ABI_TEMPLATE_ARGUMENT_MEMBER_EXTERNAL_ENTITY:
    put("X");
    if(argument.address_of) { put("ad"); }
    put("L" + argument.symbol + "E");
    put("E");
    return;
  case ABI_TEMPLATE_ARGUMENT_ENTITY:
    put("X");
    if(argument.address_of) { put("ad"); }
    emit_entity_symbol(argument.entity_ref);
    put("E");
    return;
  case ABI_TEMPLATE_ARGUMENT_PACK:
    put("J");
    for(size_t i = 0; i < argument.argument_refs.size(); ++i) {
      emit_argument(argument.argument_refs[i]);
    }
    put("E");
    return;
  }
  fail("unsupported template argument fact");
}

// A member template named as a template-template argument registers the
// owner's internal prefixes and its own member name, but neither the owner nor
// the whole entity: that is the ordering the reference names require.
void Encoder::emit_member_template_entity(const AbiTemplateArgument & argument)
{
  AbiType storage;
  const AbiType & owner = resolve_type(argument.owner_type, storage);
  put_out("N");
  emit_class_prefix_content(owner);
  const Mark point = mark();
  put(source_name(argument.name));
  close_candidate(point);
  put_out("E");
}

// --- dependent expressions -----------------------------------------------

void Encoder::emit_expression_ref(const string & id)
{
  DepthGuard guard(depth);
  emit_expression(resolve_expression(id));
}

void Encoder::emit_expression(const AbiDependentExpression & expression)
{
  switch(expression.kind) {
  case ABI_EXPRESSION_TEMPLATE_PARAMETER:
    if(!expression.substitutable) {
      put(indexed_code("T", expression.index));
      return;
    } else {
      const Mark point = mark();
      put(indexed_code("T", expression.index));
      close_candidate(point);
      return;
    }
  case ABI_EXPRESSION_FUNCTION_PARAMETER:
    put(indexed_code("fp", expression.index));
    return;
  case ABI_EXPRESSION_LITERAL:
    put("L");
    emit_type(expression.value_type);
    put(integral_literal_digits(expression.value_type.name, expression.value));
    put("E");
    return;
  case ABI_EXPRESSION_INTEGRAL_VALUE:
    put("L");
    emit_type(expression.value_type);
    put(integral_literal_digits(expression.value_type.name, expression.value));
    put("E");
    return;
  case ABI_EXPRESSION_EXTERNAL_ENTITY:
    put("L" + expression.text + "E");
    return;
  case ABI_EXPRESSION_ENTITY:
    emit_entity_symbol(expression.entity_ref);
    return;
  default:
    emit_expression_operands(expression);
    return;
  }
}

void Encoder::emit_expression_operands(const AbiDependentExpression & node)
{
  switch(node.kind) {
  case ABI_EXPRESSION_UNARY:
  case ABI_EXPRESSION_BINARY:
  case ABI_EXPRESSION_CONDITIONAL:
    put(node.op);
    for(size_t i = 0; i < node.expression_refs.size(); ++i) {
      emit_expression_ref(node.expression_refs[i]);
    }
    return;
  case ABI_EXPRESSION_PACK_EXPANSION:
    put("sp");
    emit_expression_ref(node.expression_refs.at(0));
    return;
  case ABI_EXPRESSION_CALL:
    put("cl");
    for(size_t i = 0; i < node.expression_refs.size(); ++i) {
      emit_expression_ref(node.expression_refs[i]);
    }
    put("E");
    return;
  case ABI_EXPRESSION_CAST:
  case ABI_EXPRESSION_CONVERSION:
    put(node.op);
    emit_type(node.type);
    for(size_t i = 0; i < node.expression_refs.size(); ++i) {
      emit_expression_ref(node.expression_refs[i]);
    }
    return;
  case ABI_EXPRESSION_TEMPLATE_ID:
    put(source_name(node.text));
    emit_template_arguments(node.argument_refs);
    return;
  case ABI_EXPRESSION_TYPE_TRAIT:
    put("u" + source_name(node.op));
    for(size_t i = 0; i < node.type_arguments.size(); ++i) {
      emit_type(node.type_arguments[i]);
    }
    put("E");
    return;
  case ABI_EXPRESSION_SIZEOF_TYPE:
    put("st");
    emit_type(node.type);
    return;
  case ABI_EXPRESSION_MEMBER:
    put("sr");
    emit_class_prefix(node.type);
    if(node.close_member_owner) { put("E"); }
    put(source_name(node.text));
    return;
  case ABI_EXPRESSION_OBJECT_MEMBER:
    put(node.op);
    emit_expression_ref(node.expression_refs.at(0));
    put(source_name(node.text));
    if(!node.argument_refs.empty()) {
      emit_template_arguments(node.argument_refs);
    }
    return;
  default:
    fail("unsupported dependent expression fact");
  }
}

// --- entities ------------------------------------------------------------

void Encoder::emit_entity_symbol(const string & id)
{
  const string symbol = entity_symbol(resolve_entity(id));
  put("L" + symbol + "E");
}

// A named entity is a complete mangled name with its own substitution table,
// so it is encoded by a nested encoder and spliced in as one token.
string Encoder::entity_symbol(const AbiEntityFact & entity) const
{
  if(entity.kind == ABI_ENTITY_FACT_SYMBOL) { return entity.qualified_name; }
  Encoder nested(defs);
  nested.put("_Z");
  nested.emit_data_name(entity.qualified_name, entity.internal_linkage);
  if(entity.kind == ABI_ENTITY_FACT_FUNCTION) {
    const vector<AbiFunctionPathOperand> & operands =
      entity.function.path_operands;
    if(operands.empty()) {
      nested.put("v");
    } else {
      for(size_t i = 0; i < operands.size(); ++i) {
        nested.emit_type(operands[i].type);
      }
    }
  }
  return nested.text();
}

// A data name: `<name>`, with the internal-linkage marker on the final
// component when the entity has internal linkage.
void Encoder::emit_data_name(const string & qualified_name,
                             bool internal_linkage)
{
  vector<string> parts = split_qualified_name(qualified_name);
  bool standard = false;
  if(parts.front() == "std") {
    standard = true;
    parts.erase(parts.begin());
    if(parts.empty()) { fail("qualified name names only namespace std"); }
  }
  const bool nested = parts.size() > 1;
  const string marker = internal_linkage ? "L" : "";
  if(nested) { put_out("N"); }
  const Mark base = mark();
  if(standard) { put("St"); }
  for(size_t i = 0; i + 1 < parts.size(); ++i) {
    put(source_name(parts[i]));
    close_candidate(base);
  }
  put(marker + source_name(parts.back()));
  if(nested) { put_out("E"); }
}

// --- function names ------------------------------------------------------

enum TerminalKind
{
  TERMINAL_NONE,
  TERMINAL_CTOR_DTOR,
  TERMINAL_SOURCE,
  TERMINAL_OPERATOR,
  TERMINAL_CONVERSION
};

struct Encoder::FunctionShape
{
  vector<NameComponent> components;
  bool has_name_component = false;
  size_t last_name_index = 0;
  bool local_wrapper = false;
  string context_ref;
  TerminalKind terminal_kind = TERMINAL_NONE;
  string terminal;
  string literal_suffix;
  AbiType conversion_type;
  vector<string> abi_tags;
  vector<AbiFunctionQualifier> qualifiers;
  vector<string> template_args;
  bool has_result = false;
  AbiType result;
  vector<AbiType> parameters;
  bool variadic = false;
};

void Encoder::emit_context(const string & id)
{
  DepthGuard guard(depth);
  const AbiLocalContext & context = resolve_context(id);
  if(context.kind == ABI_CONTEXT_RAW) {
    put(context.fragment);
    return;
  }
  put("Z");
  emit_function(context.function, vector<AbiFunctionRecord>());
  put("E");
}

void Encoder::apply_record(const AbiFunctionRecord & record,
                           FunctionShape & shape) const
{
  NameComponent component;
  switch(record.kind) {
  case ABI_FUNCTION_RECORD_NAME_STD:
    component.kind = COMPONENT_STD;
    push_name_component(component, shape);
    return;
  case ABI_FUNCTION_RECORD_NAME_SOURCE:
    component.kind = COMPONENT_SOURCE;
    component.name = record.name;
    push_name_component(component, shape);
    return;
  case ABI_FUNCTION_RECORD_NAME_TEMPLATE:
    component.kind = COMPONENT_TEMPLATE;
    component.name = record.name;
    component.argument_refs = record.argument_refs;
    component.standard_substitution = record.standard_substitution;
    component.standard_includes_arguments =
      record.standard_substitution_includes_arguments;
    push_name_component(component, shape);
    return;
  case ABI_FUNCTION_RECORD_LOCAL_CONTEXT:
    shape.local_wrapper = true;
    shape.context_ref = record.context_ref;
    component.kind = COMPONENT_LOCAL_CLASS;
    component.name = record.source_name;
    component.discriminator = record.discriminator;
    shape.components.push_back(component);
    return;
  case ABI_FUNCTION_RECORD_LAMBDA_CONTEXT:
    shape.local_wrapper = true;
    shape.context_ref = record.context_ref;
    component.kind = COMPONENT_CLOSURE;
    component.discriminator = record.discriminator;
    component.lambda_signature = record.types;
    shape.components.push_back(component);
    return;
  case ABI_FUNCTION_RECORD_NAMESPACE_LAMBDA_CONTEXT:
    for(size_t i = 0; i < record.namespace_qualifiers.size(); ++i) {
      NameComponent qualifier;
      qualifier.kind = COMPONENT_SOURCE;
      qualifier.name = record.namespace_qualifiers[i];
      shape.components.push_back(qualifier);
    }
    component.kind = COMPONENT_SOURCE;
    component.name = record.source_name;
    shape.components.push_back(component);
    return;
  case ABI_FUNCTION_RECORD_TERMINAL:
    shape.terminal_kind = TERMINAL_CTOR_DTOR;
    shape.terminal = record.terminal;
    return;
  case ABI_FUNCTION_RECORD_TERMINAL_SOURCE:
    shape.terminal_kind = TERMINAL_SOURCE;
    shape.terminal = record.source_name;
    return;
  case ABI_FUNCTION_RECORD_OPERATOR_TERMINAL:
    shape.terminal_kind = TERMINAL_OPERATOR;
    shape.terminal = record.terminal;
    shape.literal_suffix = record.literal_suffix;
    return;
  case ABI_FUNCTION_RECORD_CONVERSION_TERMINAL:
    shape.terminal_kind = TERMINAL_CONVERSION;
    shape.conversion_type = record.type;
    return;
  case ABI_FUNCTION_RECORD_ABI_TAG:
    shape.abi_tags.push_back(record.name);
    return;
  case ABI_FUNCTION_RECORD_QUALIFIER:
    shape.qualifiers.insert(shape.qualifiers.end(), record.qualifiers.begin(),
                            record.qualifiers.end());
    return;
  case ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_ARGUMENT:
    shape.template_args.insert(shape.template_args.end(),
                               record.argument_refs.begin(),
                               record.argument_refs.end());
    return;
  case ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_PREFIX:
    return;
  case ABI_FUNCTION_RECORD_PARAMETER:
    shape.parameters.push_back(record.type);
    return;
  case ABI_FUNCTION_RECORD_RESULT:
    shape.has_result = true;
    shape.result = record.type;
    return;
  case ABI_FUNCTION_RECORD_VARIADIC:
    shape.variadic = true;
    return;
  }
  fail("unsupported function record fact");
}

// Split a qualified function name into name components, folding a leading
// `std` into the standard namespace substitution.
void Encoder::push_name_components(const string & name,
                                   FunctionShape & shape) const
{
  vector<string> parts = split_qualified_name(name);
  if(parts.front() == "std") {
    NameComponent standard;
    standard.kind = COMPONENT_STD;
    push_name_component(standard, shape);
    parts.erase(parts.begin());
    if(parts.empty()) { fail("function name names only namespace std"); }
  }
  for(size_t i = 0; i < parts.size(); ++i) {
    NameComponent component;
    component.kind = COMPONENT_SOURCE;
    component.name = parts[i];
    push_name_component(component, shape);
  }
}

void Encoder::push_name_component(const NameComponent & component,
                                  FunctionShape & shape) const
{
  shape.last_name_index = shape.components.size();
  shape.has_name_component = true;
  shape.components.push_back(component);
}

void Encoder::build_shape(const AbiFunctionTarget & target,
                          const vector<AbiFunctionRecord> & records,
                          FunctionShape & shape) const
{
  NameComponent component;
  switch(target.kind) {
  case ABI_FUNCTION_TARGET_PATH:
  case ABI_FUNCTION_TARGET_ENCODING:
    if(target.kind == ABI_FUNCTION_TARGET_PATH) {
      push_name_components(target.qualified_name, shape);
    }
    break;
  case ABI_FUNCTION_TARGET_LOCAL:
    shape.local_wrapper = true;
    shape.context_ref = target.context_ref;
    component.kind = COMPONENT_LOCAL_CLASS;
    component.name = target.source_name;
    component.discriminator = target.discriminator;
    shape.components.push_back(component);
    break;
  case ABI_FUNCTION_TARGET_LAMBDA:
    shape.local_wrapper = true;
    shape.context_ref = target.context_ref;
    component.kind = COMPONENT_CLOSURE;
    component.discriminator = target.discriminator;
    component.lambda_signature = target.signature_parameter_types;
    shape.components.push_back(component);
    break;
  case ABI_FUNCTION_TARGET_NAMESPACE_LAMBDA:
    for(size_t i = 0; i < target.namespace_qualifiers.size(); ++i) {
      NameComponent qualifier;
      qualifier.kind = COMPONENT_SOURCE;
      qualifier.name = target.namespace_qualifiers[i];
      shape.components.push_back(qualifier);
    }
    component.kind = COMPONENT_SOURCE;
    component.name = target.source_name;
    shape.components.push_back(component);
    break;
  }
  if(!target.terminal.empty()) {
    string code;
    const size_t count = sizeof(CTOR_DTOR_CODES) / sizeof(WordCode);
    if(lookup_code(CTOR_DTOR_CODES, count, target.terminal, code)) {
      shape.terminal_kind = TERMINAL_CTOR_DTOR;
    } else {
      shape.terminal_kind = TERMINAL_OPERATOR;
    }
    shape.terminal = target.terminal;
  }
  // A path operand naming a template-argument binder is a template argument;
  // anything else is a parameter type.
  for(size_t i = 0; i < target.path_operands.size(); ++i) {
    const AbiFunctionPathOperand & operand = target.path_operands[i];
    if(!operand.argument_ref.empty() &&
       definition(operand.argument_ref, ABI_DEFINITION_TEMPLATE_ARGUMENT) != 0) {
      shape.template_args.push_back(operand.argument_ref);
    } else {
      shape.parameters.push_back(operand.type);
    }
  }
  for(size_t i = 0; i < records.size(); ++i) { apply_record(records[i], shape); }
  // The last name component is a placeholder when a terminal names the entity.
  if(shape.terminal_kind != TERMINAL_NONE && shape.has_name_component) {
    shape.components.erase(shape.components.begin() +
                           (ptrdiff_t)shape.last_name_index);
    shape.has_name_component = false;
  }
  // A local or lambda context with no terminal names its call operator.
  if(shape.terminal_kind == TERMINAL_NONE && shape.local_wrapper) {
    shape.terminal_kind = TERMINAL_OPERATOR;
    shape.terminal = "call";
  }
}

void Encoder::emit_function(const AbiFunctionTarget & target,
                            const vector<AbiFunctionRecord> & records)
{
  DepthGuard guard(depth);
  FunctionShape shape;
  build_shape(target, records, shape);
  emit_function_name(shape);
  emit_bare_function_type(shape);
}

string function_qualifier_codes(const vector<AbiFunctionQualifier> & quals)
{
  bool is_const = false;
  bool is_volatile = false;
  string reference;
  for(size_t i = 0; i < quals.size(); ++i) {
    switch(quals[i]) {
    case ABI_FUNCTION_QUALIFIER_CONST: is_const = true; break;
    case ABI_FUNCTION_QUALIFIER_VOLATILE: is_volatile = true; break;
    case ABI_FUNCTION_QUALIFIER_LVALUE_REFERENCE: reference = "R"; break;
    case ABI_FUNCTION_QUALIFIER_RVALUE_REFERENCE: reference = "O"; break;
    }
  }
  string text;
  if(is_volatile) { text += "V"; }
  if(is_const) { text += "K"; }
  return text + reference;
}

void Encoder::emit_function_name(const FunctionShape & shape)
{
  if(shape.local_wrapper) { emit_context(shape.context_ref); }
  size_t prefix_count = 0;
  for(size_t i = 0; i < shape.components.size(); ++i) {
    if(shape.components[i].kind != COMPONENT_STD) { ++prefix_count; }
  }
  const bool unqualified_is_component = shape.terminal_kind == TERMINAL_NONE;
  if(unqualified_is_component) {
    if(shape.components.empty()) { fail("function name without a component"); }
    --prefix_count;
  }
  const bool nested = prefix_count > 0;
  if(nested) {
    put_out("N");
    put_out(function_qualifier_codes(shape.qualifiers));
  }
  emit_component_chain(shape);
  if(nested) { put_out("E"); }
}

void Encoder::emit_component_chain(const FunctionShape & shape)
{
  const Mark base = mark();
  const bool unqualified_is_component = shape.terminal_kind == TERMINAL_NONE;
  const size_t last =
    unqualified_is_component ? shape.components.size() - 1 : shape.components.size();
  for(size_t i = 0; i < last; ++i) {
    const NameComponent & component = shape.components[i];
    switch(component.kind) {
    case COMPONENT_STD:
      put("St");
      continue;
    case COMPONENT_SOURCE:
      put(source_name(component.name));
      break;
    case COMPONENT_TEMPLATE:
      if(!component.standard_substitution.empty()) {
        put(component.standard_substitution);
        if(component.standard_includes_arguments) { continue; }
        emit_template_arguments(component.argument_refs);
      } else {
        put(source_name(component.name));
        close_candidate(base);
        emit_template_arguments(component.argument_refs);
      }
      break;
    case COMPONENT_CLOSURE:
      put("Ul");
      emit_lambda_signature(component.lambda_signature);
      put("E");
      put(component.discriminator + "_");
      break;
    case COMPONENT_LOCAL_CLASS:
      put(source_name(component.name));
      if(!component.discriminator.empty() && component.discriminator != "0") {
        const long long value = atoll(component.discriminator.c_str());
        put("_" + decimal((unsigned long long)(value - 1)));
      }
      break;
    }
    close_candidate(base);
  }
  if(unqualified_is_component) {
    const NameComponent & component = shape.components.back();
    if(component.kind == COMPONENT_TEMPLATE &&
       !component.standard_substitution.empty()) {
      put(component.standard_substitution);
      if(!component.standard_includes_arguments) {
        emit_template_arguments(component.argument_refs);
      }
    } else {
      put(source_name(component.name) + abi_tag_suffix(shape.abi_tags));
      if(component.kind == COMPONENT_TEMPLATE) {
        close_candidate(base);
        emit_template_arguments(component.argument_refs);
      }
    }
  } else {
    emit_unqualified_terminal(shape);
  }
  if(!shape.template_args.empty()) {
    close_candidate(base);
    emit_template_arguments(shape.template_args);
  }
}

// Choose an operator terminal, resolving the operators whose Itanium code
// depends on unary versus binary use from the function shape.
string operator_terminal_code(const string & name, bool member, size_t params)
{
  string code;
  const size_t count = sizeof(FIXED_OPERATOR_CODES) / sizeof(WordCode);
  if(lookup_code(FIXED_OPERATOR_CODES, count, name, code)) { return code; }
  const size_t arity = member ? params + 1 : params;
  if(name == "plus") { return arity >= 2 ? "pl" : "ps"; }
  if(name == "minus") { return arity >= 2 ? "mi" : "ng"; }
  if(name == "star") { return arity >= 2 ? "ml" : "de"; }
  if(name == "ampersand") { return arity >= 2 ? "an" : "ad"; }
  fail("unknown operator terminal '" + name + "'");
  return code;
}

void Encoder::emit_unqualified_terminal(const FunctionShape & shape)
{
  const string tags = abi_tag_suffix(shape.abi_tags);
  switch(shape.terminal_kind) {
  case TERMINAL_CTOR_DTOR: {
    string code;
    const size_t count = sizeof(CTOR_DTOR_CODES) / sizeof(WordCode);
    if(!lookup_code(CTOR_DTOR_CODES, count, shape.terminal, code)) {
      fail("unknown terminal '" + shape.terminal + "'");
    }
    put(code + tags);
    return;
  }
  case TERMINAL_SOURCE:
    put(source_name(shape.terminal) + tags);
    return;
  case TERMINAL_OPERATOR: {
    if(shape.terminal == "literal") {
      put("li" + source_name(shape.literal_suffix) + tags);
      return;
    }
    const string terminal = shape.terminal == "operator-call" ? "call" : shape.terminal;
    bool member = false;
    for(size_t i = 0; i < shape.components.size(); ++i) {
      if(shape.components[i].kind != COMPONENT_STD) { member = true; }
    }
    put(operator_terminal_code(terminal, member, shape.parameters.size()) + tags);
    return;
  }
  case TERMINAL_CONVERSION:
    put("cv");
    emit_type(shape.conversion_type);
    put(tags);
    return;
  default:
    fail("function terminal is missing");
  }
}

void Encoder::emit_bare_function_type(const FunctionShape & shape)
{
  // A conversion function encodes its conversion type as the name; the ABI
  // does not repeat it as a result type.
  if(shape.has_result && shape.terminal_kind != TERMINAL_CONVERSION) {
    emit_type(shape.result);
  }
  if(shape.parameters.empty()) {
    put("v");
  } else {
    for(size_t i = 0; i < shape.parameters.size(); ++i) {
      emit_type(shape.parameters[i]);
    }
  }
  if(shape.variadic) { put("z"); }
}

// --- targets -------------------------------------------------------------

void Encoder::encode_target(const AbiTargetRecord & target,
                            const vector<AbiFunctionRecord> & records)
{
  switch(target.kind) {
  case ABI_TARGET_FACT_TYPE:
    emit_type(target.type);
    return;
  case ABI_TARGET_FACT_FUNCTION:
    if(target.c_linkage) {
      put(target.function.qualified_name);
      return;
    }
    put("_Z");
    emit_function(target.function, records);
    return;
  case ABI_TARGET_FACT_VARIABLE:
    put("_Z");
    emit_data_name(target.qualified_name, false);
    return;
  case ABI_TARGET_FACT_TYPEINFO:
    put("_ZTI");
    emit_type(target.type);
    return;
  case ABI_TARGET_FACT_VTABLE:
    put("_ZTV");
    emit_type(target.type);
    return;
  case ABI_TARGET_FACT_VTT:
    put("_ZTT");
    emit_type(target.type);
    return;
  case ABI_TARGET_FACT_CONSTRUCTION_VTABLE:
    put("_ZTC");
    emit_type(target.type);
    put(decimal(target.base_offset) + "_");
    emit_type(target.base_type);
    return;
  case ABI_TARGET_FACT_THREAD_LOCAL_WRAPPER:
    put("_ZTW");
    emit_data_name(target.qualified_name, false);
    return;
  case ABI_TARGET_FACT_THUNK:
    put("_Z");
    if(target.has_result_adjust) {
      put("Tc");
      put(call_offset(false, target.this_adjust, 0));
      put(call_offset(target.result_adjust_virtual, target.result_adjust,
                      target.result_vcall_offset));
    } else {
      put("T");
      put(call_offset(false, target.this_adjust, 0));
    }
    emit_function(target.function, records);
    return;
  case ABI_TARGET_FACT_VIRTUAL_BASE_THUNK:
    put("_ZT");
    put(call_offset(true, 0, target.vcall_offset));
    emit_function(target.function, records);
    return;
  }
  fail("unsupported ABI target fact");
}

}  // namespace

// --- public entry points -------------------------------------------------

AbiDefinitionMap build_definition_map(const AbiFactCase & fact_case)
{
  AbiDefinitionMap definitions;
  for(size_t i = 0; i < fact_case.records.size(); ++i) {
    const AbiFactRecord & record = fact_case.records[i];
    if(record.kind != ABI_FACT_RECORD_DEFINITION) { continue; }
    const string & id = record.definition.id;
    if(definitions.count(id) != 0) {
      fail("definition '" + id + "' is defined more than once");
    }
    definitions[id] = &record.definition;
  }
  return definitions;
}

string mangle_target(const AbiTargetRecord & target,
                     const vector<AbiFunctionRecord> & records,
                     const AbiDefinitionMap & definitions)
{
  Encoder encoder(definitions);
  encoder.encode_target(target, records);
  return encoder.text();
}

string mangle_fact_case(const AbiFactCase & fact_case)
{
  const AbiDefinitionMap definitions = build_definition_map(fact_case);
  const AbiTargetRecord * target = 0;
  vector<AbiFunctionRecord> records;
  for(size_t i = 0; i < fact_case.records.size(); ++i) {
    const AbiFactRecord & record = fact_case.records[i];
    if(record.kind == ABI_FACT_RECORD_TARGET) {
      if(target != 0) { fail("more than one target in one case"); }
      target = &record.target;
    } else if(record.kind == ABI_FACT_RECORD_FUNCTION) {
      if(target == 0) { fail("function record before the case target"); }
      records.push_back(record.function);
    }
  }
  if(target == 0) { fail("case has no target"); }
  return mangle_target(*target, records, definitions);
}

string mangle_fact_file(const AbiFactFile & file)
{
  string text;
  for(size_t i = 0; i < file.cases.size(); ++i) {
    text += mangle_fact_case(file.cases[i]);
    text += "\n";
  }
  return text;
}

}  // namespace abi_mangle
