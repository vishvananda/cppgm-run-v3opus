#include "lowir_text.h"

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace lowir_model {

namespace {

struct ScalarTypeEntry
{
  const char * name;
  TypeFacts::Category category;
  bool is_signed;
  std::size_t size;
  std::size_t alignment;
  int width_bits;
};

const ScalarTypeEntry kScalarTypes[] = {
  { "void", TypeFacts::TC_VOID, false, 0, 1, 0 },
  { "i1", TypeFacts::TC_INTEGER, false, 1, 1, 8 },  // a truth value has no sign
  { "i8", TypeFacts::TC_INTEGER, true, 1, 1, 8 },
  { "u8", TypeFacts::TC_INTEGER, false, 1, 1, 8 },
  { "i16", TypeFacts::TC_INTEGER, true, 2, 2, 16 },
  { "u16", TypeFacts::TC_INTEGER, false, 2, 2, 16 },
  { "i32", TypeFacts::TC_INTEGER, true, 4, 4, 32 },
  { "u32", TypeFacts::TC_INTEGER, false, 4, 4, 32 },
  { "i64", TypeFacts::TC_INTEGER, true, 8, 8, 64 },
  { "f32", TypeFacts::TC_FLOAT, true, 4, 4, 32 },
  { "f64", TypeFacts::TC_FLOAT, true, 8, 8, 64 },
  { "f80", TypeFacts::TC_FLOAT, true, 16, 16, 80 },
  { "ptr", TypeFacts::TC_POINTER, false, 8, 8, 64 }
};

bool parse_object_dimensions(const std::string & text,
                             std::size_t & bytes,
                             std::size_t & alignment)
{
  std::size_t split = text.find('x');
  if(split == std::string::npos || split == 0 || split + 1 >= text.size()) {
    return false;
  }
  bytes = 0;
  alignment = 0;
  for(std::size_t i = 0; i < split; ++i) {
    if(!isdigit(static_cast<unsigned char>(text[i]))) {
      return false;
    }
    bytes = bytes * 10 + static_cast<std::size_t>(text[i] - '0');
  }
  for(std::size_t i = split + 1; i < text.size(); ++i) {
    if(!isdigit(static_cast<unsigned char>(text[i]))) {
      return false;
    }
    alignment = alignment * 10 + static_cast<std::size_t>(text[i] - '0');
  }
  if(bytes == 0 || alignment == 0) {
    return false;
  }
  return (alignment & (alignment - 1)) == 0;
}

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------

struct Token
{
  enum Kind
  {
    TK_END,
    TK_IDENT,
    TK_NUMBER,
    TK_GLOBAL,
    TK_TEMP,
    TK_SLOT,
    TK_BLOCK,
    TK_PUNCT,
    TK_DBG
  };

  Kind kind = TK_END;
  std::string text;  // spelling, or the source-file field of a !dbg location
  long long dbg_line = 0;
  long long dbg_column = 0;
  std::size_t line = 1;
};

bool is_name_char(char c)
{
  return isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '$';
}

bool is_ident_start(char c)
{
  return isalpha(static_cast<unsigned char>(c)) || c == '_';
}

// The lexer hands out one token at a time. The parser needs a two-token window
// and never looks further back, so the token stream is never materialized: a
// LowIR program's tokens outweigh its source text several times over.
class Lexer
{
public:
  Lexer(const std::string & text, const std::string & source_name)
    : text_(text), source_name_(source_name)
  {}

  Token next();

private:
  void skip_trivia();
  void scan_sigil(Token & token, Token::Kind kind);
  void scan_number(Token & token);
  void scan_ident(Token & token);
  void scan_debug_location(Token & token);
  void scan_punct(Token & token);
  long long read_debug_integer();
  void fail(const std::string & message) const;

  const std::string & text_;
  const std::string & source_name_;
  std::size_t pos_ = 0;
  std::size_t line_ = 1;
};

void Lexer::fail(const std::string & message) const
{
  std::ostringstream out;
  out << source_name_ << ":" << line_ << ": " << message;
  throw ParseError(out.str());
}

void Lexer::skip_trivia()
{
  while(pos_ < text_.size()) {
    char c = text_[pos_];
    if(c == '\n') {
      ++line_;
      ++pos_;
    } else if(isspace(static_cast<unsigned char>(c))) {
      ++pos_;
    } else if(c == '#' || (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/')) {
      while(pos_ < text_.size() && text_[pos_] != '\n') {
        ++pos_;
      }
    } else {
      return;
    }
  }
}

void Lexer::scan_sigil(Token & token, Token::Kind kind)
{
  token.kind = kind;
  ++pos_;
  const std::size_t begin = pos_;
  while(pos_ < text_.size() && is_name_char(text_[pos_])) {
    ++pos_;
  }
  if(pos_ == begin) {
    fail("empty LowIR symbol name");
  }
  token.text.assign(text_, begin, pos_ - begin);
}

void Lexer::scan_number(Token & token)
{
  token.kind = Token::TK_NUMBER;
  const std::size_t begin = pos_;
  while(pos_ < text_.size()) {
    char c = text_[pos_];
    if(!is_name_char(c)) {
      break;
    }
    ++pos_;
    bool exponent = (c == 'e' || c == 'E' || c == 'p' || c == 'P');
    if(exponent && pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
      ++pos_;
    }
  }
  token.text.assign(text_, begin, pos_ - begin);
}

void Lexer::scan_ident(Token & token)
{
  token.kind = Token::TK_IDENT;
  const std::size_t begin = pos_;
  while(pos_ < text_.size() && is_name_char(text_[pos_])) {
    ++pos_;
  }
  token.text.assign(text_, begin, pos_ - begin);
}

long long Lexer::read_debug_integer()
{
  while(pos_ < text_.size() && isspace(static_cast<unsigned char>(text_[pos_]))) {
    ++pos_;
  }
  bool negative = false;
  if(pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) {
    negative = text_[pos_] == '-';
    ++pos_;
  }
  std::string digits;
  while(pos_ < text_.size() && isdigit(static_cast<unsigned char>(text_[pos_]))) {
    digits += text_[pos_];
    ++pos_;
  }
  if(digits.empty()) {
    fail("expected a number in !dbg location");
  }
  long long value = 0;
  std::istringstream in(digits);
  in >> value;
  return negative ? -value : value;
}

void Lexer::scan_debug_location(Token & token)
{
  token.kind = Token::TK_DBG;
  ++pos_;  // '!'
  std::string keyword;
  while(pos_ < text_.size() && is_name_char(text_[pos_])) {
    keyword += text_[pos_];
    ++pos_;
  }
  if(keyword != "dbg") {
    fail("unknown LowIR metadata marker '!" + keyword + "'");
  }
  if(pos_ >= text_.size() || text_[pos_] != '(') {
    fail("expected '(' after !dbg");
  }
  ++pos_;
  std::string file;
  while(pos_ < text_.size() && text_[pos_] != ',' && text_[pos_] != ')' && text_[pos_] != '\n') {
    file += text_[pos_];
    ++pos_;
  }
  while(!file.empty() && isspace(static_cast<unsigned char>(file[file.size() - 1]))) {
    file.erase(file.size() - 1);
  }
  std::size_t lead = 0;
  while(lead < file.size() && isspace(static_cast<unsigned char>(file[lead]))) {
    ++lead;
  }
  file = file.substr(lead);
  if(file.empty()) {
    fail("!dbg location requires a source file token");
  }
  if(pos_ >= text_.size() || text_[pos_] != ',') {
    fail("expected ',' in !dbg location");
  }
  ++pos_;
  token.text = file;
  token.dbg_line = read_debug_integer();
  while(pos_ < text_.size() && isspace(static_cast<unsigned char>(text_[pos_]))) {
    ++pos_;
  }
  if(pos_ >= text_.size() || text_[pos_] != ',') {
    fail("expected ',' in !dbg location");
  }
  ++pos_;
  token.dbg_column = read_debug_integer();
  while(pos_ < text_.size() && isspace(static_cast<unsigned char>(text_[pos_]))) {
    ++pos_;
  }
  if(pos_ >= text_.size() || text_[pos_] != ')') {
    fail("expected ')' in !dbg location");
  }
  ++pos_;
}

void Lexer::scan_punct(Token & token)
{
  token.kind = Token::TK_PUNCT;
  char c = text_[pos_];
  if(c == '-' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '>') {
    token.text = "->";
    pos_ += 2;
    return;
  }
  static const char kSingles[] = ":,()[]{}=+-<>";
  if(c == '\0' || strchr(kSingles, c) == 0) {
    fail(std::string("unexpected character '") + c + "' in LowIR text");
  }
  token.text.assign(1, c);
  ++pos_;
}

Token Lexer::next()
{
  skip_trivia();
  Token token;
  token.line = line_;
  if(pos_ >= text_.size()) {
    token.kind = Token::TK_END;
    return token;
  }
  char c = text_[pos_];
  if(c == '@') {
    scan_sigil(token, Token::TK_GLOBAL);
  } else if(c == '%') {
    scan_sigil(token, Token::TK_TEMP);
  } else if(c == '$') {
    scan_sigil(token, Token::TK_SLOT);
  } else if(c == '^') {
    scan_sigil(token, Token::TK_BLOCK);
  } else if(c == '!') {
    scan_debug_location(token);
  } else if(isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && pos_ + 1 < text_.size() &&
             isdigit(static_cast<unsigned char>(text_[pos_ + 1])))) {
    scan_number(token);
  } else if(is_ident_start(c)) {
    scan_ident(token);
  } else {
    scan_punct(token);
  }
  return token;
}

// ---------------------------------------------------------------------------
// Metadata vocabulary
// ---------------------------------------------------------------------------

struct MetaItem
{
  std::string key;
  std::string value;
  bool value_is_symbol = false;
  std::size_t line = 0;
};

bool lookup_role(const std::string & value, SymbolRole & role, bool & is_function_role)
{
  struct Entry { const char * name; SymbolRole role; bool function_role; };
  static const Entry kRoles[] = {
    { "entry", SR_ENTRY, true },
    { "init", SR_INIT, true },
    { "fini", SR_FINI, true },
    { "eh_unhandled", SR_EH_UNHANDLED, true },
    { "eh_allocate_exception", SR_EH_ALLOCATE_EXCEPTION, true },
    { "eh_begin_catch", SR_EH_BEGIN_CATCH, true },
    { "eh_call_unexpected", SR_EH_CALL_UNEXPECTED, true },
    { "eh_current_exception_type", SR_EH_CURRENT_EXCEPTION_TYPE, true },
    { "eh_end_catch", SR_EH_END_CATCH, true },
    { "eh_rethrow", SR_EH_RETHROW, true },
    { "eh_throw", SR_EH_THROW, true },
    { "eh_personality", SR_EH_PERSONALITY, true },
    { "eh_resume", SR_EH_RESUME, true },
    { "eh_top", SR_EH_TOP, false },
    { "eh_value", SR_EH_VALUE, false },
    { "eh_type", SR_EH_TYPE, false }
  };
  for(std::size_t i = 0; i < sizeof(kRoles) / sizeof(kRoles[0]); ++i) {
    if(value == kRoles[i].name) {
      role = kRoles[i].role;
      is_function_role = kRoles[i].function_role;
      return true;
    }
  }
  return false;
}

bool apply_boundary_item(const MetaItem & item, FunctionBoundaryMetadata & boundary)
{
  if(item.key == "arity") {
    if(item.value == "fixed") { boundary.arity = CAM_FIXED; }
    else if(item.value == "variadic") { boundary.arity = CAM_VARIADIC; }
    else if(item.value == "prototype_relaxed") { boundary.arity = CAM_PROTOTYPE_RELAXED; }
    else { throw ParseError("invalid arity metadata value '" + item.value + "'"); }
    return true;
  }
  if(item.key == "effects") {
    if(item.value == "readnone") { boundary.effects = CFXM_READNONE; }
    else if(item.value == "readonly") { boundary.effects = CFXM_READONLY; }
    else if(item.value == "readwrite") { boundary.effects = CFXM_READWRITE; }
    else { throw ParseError("invalid effects metadata value '" + item.value + "'"); }
    return true;
  }
  if(item.key == "unwind") {
    if(item.value == "may") { boundary.unwind = CUM_MAY; }
    else if(item.value == "no") { boundary.unwind = CUM_NO; }
    else { throw ParseError("invalid unwind metadata value '" + item.value + "'"); }
    return true;
  }
  if(item.key == "return") {
    if(item.value == "returns") { boundary.returns = CRM_RETURNS; }
    else if(item.value == "noreturn") { boundary.returns = CRM_NORETURN; }
    else { throw ParseError("invalid return metadata value '" + item.value + "'"); }
    return true;
  }
  return false;
}

bool apply_flag_item(const MetaItem & item, SymbolMetadata & metadata, bool is_function)
{
  bool * flag = 0;
  bool functions_only = false;
  if(item.key == "keep_alias") { flag = &metadata.keep_internal_alias; }
  else if(item.key == "prefer_local") { flag = &metadata.prefer_local_object_binding; }
  else if(item.key == "trivial_lifecycle") {
    flag = &metadata.object_trivial_lifecycle;
    functions_only = true;
  } else if(item.key == "force_inline") {
    flag = &metadata.force_inline;
    functions_only = true;
  }
  if(flag == 0) {
    return false;
  }
  if(functions_only && !is_function) {
    throw ParseError("'" + item.key + "' metadata is only valid on functions");
  }
  if(item.value == "yes") { *flag = true; }
  else if(item.value == "no") { *flag = false; }
  else { throw ParseError("invalid '" + item.key + "' metadata value '" + item.value + "'"); }
  return true;
}

void apply_symbol_item(const MetaItem & item,
                       SymbolMetadata & metadata,
                       GlobalStorageMode * storage,
                       bool is_function)
{
  if(item.key == "role") {
    SymbolRole role = SR_NONE;
    bool function_role = false;
    if(!lookup_role(item.value, role, function_role)) {
      throw ParseError("invalid role metadata value '" + item.value + "'");
    }
    if(function_role != is_function) {
      throw ParseError("role '" + item.value + "' is not valid on this top-level kind");
    }
    metadata.role = role;
    return;
  }
  if(item.key == "linkage") {
    if(item.value == "c") { metadata.linkage = LLM_C; }
    else if(item.value == "cpp") { metadata.linkage = LLM_CPP; }
    else { throw ParseError("invalid linkage metadata value '" + item.value + "'"); }
    return;
  }
  if(item.key == "binding") {
    if(item.value == "internal") { metadata.binding = SBM_INTERNAL; }
    else if(item.value == "strong") { metadata.binding = SBM_STRONG; }
    else if(item.value == "weak") { metadata.binding = SBM_WEAK; }
    else { throw ParseError("invalid binding metadata value '" + item.value + "'"); }
    return;
  }
  if(item.key == "object") {
    if(item.value.empty()) {
      throw ParseError("empty object metadata value");
    }
    metadata.object_symbol = item.value;
    return;
  }
  if(item.key == "storage") {
    if(is_function) {
      throw ParseError("'storage' metadata is only valid on globals");
    }
    if(item.value == "readonly") { *storage = GSM_READONLY; }
    else if(item.value == "thread_local") { *storage = GSM_THREAD_LOCAL; }
    else { throw ParseError("invalid storage metadata value '" + item.value + "'"); }
    return;
  }
  if(item.key == "tls_for") {
    if(!is_function) {
      throw ParseError("'tls_for' metadata is only valid on functions");
    }
    if(!item.value_is_symbol) {
      throw ParseError("'tls_for' metadata requires a global name");
    }
    metadata.tls_for_symbol = item.value;
    return;
  }
  if(apply_flag_item(item, metadata, is_function)) {
    return;
  }
  throw ParseError("unknown metadata key '" + item.key + "'");
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

class Parser
{
public:
  Parser(Lexer & lexer, const std::string & source_name)
    : lexer_(lexer), source_name_(source_name)
  {
    window_[0] = lexer_.next();
    window_[1] = lexer_.next();
  }

  void parse_into(Program & program);

private:
  const Token & cur() const { return window_[0]; }
  const Token & peek(std::size_t n) const { return window_[n]; }
  void advance()
  {
    if(window_[0].kind == Token::TK_END) {
      return;
    }
    window_[0] = std::move(window_[1]);
    window_[1] = lexer_.next();
  }
  bool at_punct(const char * spelling) const
  {
    return cur().kind == Token::TK_PUNCT && cur().text == spelling;
  }
  bool at_ident(const char * spelling) const
  {
    return cur().kind == Token::TK_IDENT && cur().text == spelling;
  }
  bool accept_punct(const char * spelling)
  {
    if(!at_punct(spelling)) { return false; }
    advance();
    return true;
  }
  bool accept_ident(const char * spelling)
  {
    if(!at_ident(spelling)) { return false; }
    advance();
    return true;
  }
  void expect_punct(const char * spelling);
  std::string expect_ident();
  std::string expect_sigil(Token::Kind kind, const char * what);

  [[noreturn]] void fail(const std::string & message) const;

  LowType parse_type();
  std::vector<MetaItem> parse_metadata();
  void apply_metadata(const std::vector<MetaItem> & items,
                      SymbolMetadata & metadata,
                      GlobalStorageMode * storage,
                      FunctionBoundaryMetadata * boundary,
                      bool is_function);
  std::vector<Parameter> parse_parameters();
  ParameterMetadata parse_parameter_metadata();
  InstructionDebugLocation parse_optional_debug_location();

  void parse_global_declaration(Program & program);
  void parse_function_declaration(Program & program);
  void parse_global_definition(Program & program);
  void parse_structured_items(GlobalDefinition & global);
  void parse_function_definition(Program & program);
  void parse_object_alias(Program & program);

  void parse_function_body(Function & function);
  Instruction parse_instruction();
  Instruction parse_assignment();
  Instruction parse_rvalue(const std::string & dest);
  Instruction parse_statement();
  void parse_call_tail(Instruction & instruction);
  void parse_call_signature(Instruction & instruction);
  Operand parse_value();
  Operand parse_storage();
  Operand parse_literal();
  Operand parse_block_operand();
  long long parse_integer_literal();
  void parse_object_span(Instruction & instruction);

  Lexer & lexer_;
  const std::string & source_name_;
  Token window_[2];
};

void Parser::fail(const std::string & message) const
{
  std::ostringstream out;
  out << source_name_ << ":" << cur().line << ": " << message;
  throw ParseError(out.str());
}

void Parser::expect_punct(const char * spelling)
{
  if(!at_punct(spelling)) {
    fail(std::string("expected '") + spelling + "'");
  }
  advance();
}

std::string Parser::expect_ident()
{
  if(cur().kind != Token::TK_IDENT) {
    fail("expected an identifier");
  }
  std::string text = cur().text;
  advance();
  return text;
}

std::string Parser::expect_sigil(Token::Kind kind, const char * what)
{
  if(cur().kind != kind) {
    fail(std::string("expected ") + what);
  }
  std::string text = cur().text;
  advance();
  return text;
}

LowType Parser::parse_type()
{
  if(cur().kind != Token::TK_IDENT) {
    fail("expected a LowIR type");
  }
  std::string name = cur().text;
  if(name == "obj") {
    advance();
    expect_punct("<");
    if(cur().kind != Token::TK_NUMBER) {
      fail("expected <bytes>x<align> in obj type");
    }
    std::string dims = cur().text;
    std::size_t bytes = 0;
    std::size_t alignment = 0;
    if(!parse_object_dimensions(dims, bytes, alignment)) {
      fail("invalid obj type dimensions '" + dims + "'");
    }
    advance();
    expect_punct(">");
    LowType type;
    type.text = "obj<" + dims + ">";
    return type;
  }
  TypeFacts facts;
  LowType type;
  type.text = name;
  if(!describe_low_type(type, facts)) {
    fail("unknown LowIR type '" + name + "'");
  }
  advance();
  return type;
}

std::vector<MetaItem> Parser::parse_metadata()
{
  std::vector<MetaItem> items;
  while(at_punct("[")) {
    advance();
    if(at_punct("]")) {
      fail("empty metadata bracket");
    }
    for(;;) {
      MetaItem item;
      item.line = cur().line;
      item.key = expect_ident();
      expect_punct("=");
      if(cur().kind == Token::TK_GLOBAL) {
        item.value = cur().text;
        item.value_is_symbol = true;
        advance();
      } else if(cur().kind == Token::TK_IDENT || cur().kind == Token::TK_NUMBER) {
        item.value = cur().text;
        advance();
      } else {
        fail("expected a metadata value");
      }
      items.push_back(item);
      if(!accept_punct(",")) {
        break;
      }
    }
    expect_punct("]");
  }
  return items;
}

void Parser::apply_metadata(const std::vector<MetaItem> & items,
                            SymbolMetadata & metadata,
                            GlobalStorageMode * storage,
                            FunctionBoundaryMetadata * boundary,
                            bool is_function)
{
  for(std::size_t i = 0; i < items.size(); ++i) {
    try {
      if(boundary != 0 && apply_boundary_item(items[i], *boundary)) {
        continue;
      }
      apply_symbol_item(items[i], metadata, storage, is_function);
    } catch(const ParseError & error) {
      std::ostringstream out;
      out << source_name_ << ":" << items[i].line << ": " << error.what();
      throw ParseError(out.str());
    }
  }
}

ParameterMetadata Parser::parse_parameter_metadata()
{
  ParameterMetadata metadata;
  std::vector<MetaItem> items = parse_metadata();
  for(std::size_t i = 0; i < items.size(); ++i) {
    const std::string & key = items[i].key;
    const std::string & value = items[i].value;
    if(key == "pass") {
      if(value == "direct") { metadata.passing = PPM_DIRECT; }
      else if(value == "indirect_result") { metadata.passing = PPM_INDIRECT_RESULT; }
      else if(value == "by_address") { metadata.passing = PPM_BY_ADDRESS; }
      else if(value == "reference") { metadata.passing = PPM_REFERENCE; }
      else if(value == "decay") { metadata.passing = PPM_DECAY; }
      else { fail("invalid pass metadata value '" + value + "'"); }
    } else if(key == "capture") {
      if(value == "nocapture") { metadata.capture = PCM_NOCAPTURE; }
      else if(value == "maycapture") { metadata.capture = PCM_MAYCAPTURE; }
      else { fail("invalid capture metadata value '" + value + "'"); }
    } else if(key == "access") {
      if(value == "none") { metadata.access = PAM_NONE; }
      else if(value == "read") { metadata.access = PAM_READ; }
      else if(value == "write") { metadata.access = PAM_WRITE; }
      else if(value == "readwrite") { metadata.access = PAM_READWRITE; }
      else { fail("invalid access metadata value '" + value + "'"); }
    } else if(key == "alias") {
      if(value == "noalias") { metadata.alias = PALM_NOALIAS; }
      else { fail("invalid alias metadata value '" + value + "'"); }
    } else {
      fail("unknown parameter metadata key '" + key + "'");
    }
  }
  return metadata;
}

std::vector<Parameter> Parser::parse_parameters()
{
  std::vector<Parameter> params;
  expect_punct("(");
  if(accept_punct(")")) {
    return params;
  }
  for(;;) {
    Parameter param;
    param.name = expect_sigil(Token::TK_TEMP, "a parameter name");
    expect_punct(":");
    param.type = parse_type();
    param.metadata = parse_parameter_metadata();
    params.push_back(param);
    if(!accept_punct(",")) {
      break;
    }
  }
  expect_punct(")");
  return params;
}

InstructionDebugLocation Parser::parse_optional_debug_location()
{
  InstructionDebugLocation location;
  if(cur().kind != Token::TK_DBG) {
    return location;
  }
  if(cur().dbg_line <= 0 || cur().dbg_column <= 0) {
    fail("!dbg location requires positive line and column values");
  }
  location.file = cur().text;
  location.line = static_cast<std::size_t>(cur().dbg_line);
  location.column = static_cast<std::size_t>(cur().dbg_column);
  advance();
  return location;
}

void Parser::parse_global_declaration(Program & program)
{
  GlobalDeclaration declaration;
  declaration.name = expect_sigil(Token::TK_GLOBAL, "a global name");
  if(accept_ident("readonly")) { declaration.storage = GSM_READONLY; }
  else if(accept_ident("thread_local")) { declaration.storage = GSM_THREAD_LOCAL; }
  if(accept_punct(":")) {
    declaration.has_type = true;
    declaration.type = parse_type();
  }
  apply_metadata(parse_metadata(), declaration.metadata, &declaration.storage, 0, false);
  program.global_declarations.push_back(std::move(declaration));
}

void Parser::parse_function_declaration(Program & program)
{
  FunctionDeclaration declaration;
  declaration.name = expect_sigil(Token::TK_GLOBAL, "a function name");
  declaration.params = parse_parameters();
  expect_punct("->");
  declaration.return_type = parse_type();
  apply_metadata(parse_metadata(), declaration.metadata, 0, &declaration.boundary, true);
  program.function_declarations.push_back(std::move(declaration));
}

void Parser::parse_structured_items(GlobalDefinition & global)
{
  expect_punct("{");
  if(at_punct("}")) {
    fail("structured global requires at least one data item");
  }
  while(!at_punct("}")) {
    GlobalDefinition::DataItem item;
    if(accept_ident("zero")) {
      item.kind = GlobalDefinition::DataItem::ITEM_ZERO;
      long long bytes = parse_integer_literal();
      if(bytes < 0) {
        fail("zero data item requires a non-negative byte count");
      }
      item.zero_bytes = static_cast<std::size_t>(bytes);
    } else {
      item.type = parse_type();
      if(at_ident("addr")) {
        advance();
        item.kind = GlobalDefinition::DataItem::ITEM_ADDR;
        item.symbol = expect_sigil(Token::TK_GLOBAL, "an address initializer symbol");
        if(accept_punct("+")) { item.addr_addend = parse_integer_literal(); }
        else if(accept_punct("-")) { item.addr_addend = -parse_integer_literal(); }
      } else {
        item.kind = GlobalDefinition::DataItem::ITEM_INTEGER;
        item.literal_operand = parse_literal();
      }
    }
    global.data_items.push_back(item);
  }
  expect_punct("}");
}

void Parser::parse_global_definition(Program & program)
{
  GlobalDefinition global;
  global.name = expect_sigil(Token::TK_GLOBAL, "a global name");
  if(accept_ident("readonly")) { global.storage = GSM_READONLY; }
  else if(accept_ident("thread_local")) { global.storage = GSM_THREAD_LOCAL; }
  bool has_type = accept_punct(":");
  if(has_type) {
    global.type = parse_type();
  }
  apply_metadata(parse_metadata(), global.metadata, &global.storage, 0, false);
  expect_punct("=");
  if(at_punct("{")) {
    global.structured = true;
    parse_structured_items(global);
    program.globals.push_back(std::move(global));
    return;
  }
  if(!has_type) {
    fail("scalar global definition requires an explicit type");
  }
  if(accept_ident("zero")) {
    global.init_kind = GlobalDefinition::INIT_ZERO;
  } else if(accept_ident("addr")) {
    global.init_kind = GlobalDefinition::INIT_ADDR;
    global.init_operand.kind = Operand::OP_GLOBAL;
    global.init_operand.text = expect_sigil(Token::TK_GLOBAL, "an address initializer symbol");
    if(accept_punct("+")) { global.addr_addend = parse_integer_literal(); }
    else if(accept_punct("-")) { global.addr_addend = -parse_integer_literal(); }
  } else {
    global.init_kind = GlobalDefinition::INIT_INTEGER;
    global.init_operand = parse_literal();
  }
  program.globals.push_back(std::move(global));
}

void Parser::parse_object_alias(Program & program)
{
  if(!accept_ident("object")) {
    fail("expected 'object' after 'alias'");
  }
  ObjectAlias alias;
  alias.object_symbol = expect_ident();
  expect_punct("=");
  alias.target = expect_sigil(Token::TK_GLOBAL, "an alias target symbol");
  program.object_aliases.push_back(alias);
}

void Parser::parse_function_definition(Program & program)
{
  Function function;
  function.name = expect_sigil(Token::TK_GLOBAL, "a function name");
  function.params = parse_parameters();
  expect_punct("->");
  function.return_type = parse_type();
  apply_metadata(parse_metadata(), function.metadata, 0, &function.boundary, true);
  function.debug_location = parse_optional_debug_location();
  expect_punct("{");
  parse_function_body(function);
  expect_punct("}");
  program.functions.push_back(std::move(function));
}

void Parser::parse_function_body(Function & function)
{
  while(!at_punct("}") && cur().kind != Token::TK_END) {
    if(at_ident("slot")) {
      advance();
      std::pair<std::string, LowType> slot;
      slot.first = expect_sigil(Token::TK_SLOT, "a slot name");
      expect_punct(":");
      slot.second = parse_type();
      function.slots.push_back(slot);
      continue;
    }
    if(at_ident("block")) {
      advance();
      Block block;
      block.label = expect_sigil(Token::TK_BLOCK, "a block label");
      expect_punct(":");
      function.blocks.push_back(std::move(block));
      continue;
    }
    Instruction instruction = parse_instruction();
    if(function.blocks.empty()) {
      fail("instruction appears before the first block");
    }
    function.blocks[function.blocks.size() - 1].instructions.push_back(std::move(instruction));
  }
}

Instruction Parser::parse_instruction()
{
  Instruction instruction;
  if(cur().kind == Token::TK_TEMP && peek(1).kind == Token::TK_PUNCT && peek(1).text == "=") {
    instruction = parse_assignment();
  } else {
    instruction = parse_statement();
  }
  instruction.debug_location = parse_optional_debug_location();
  return instruction;
}

Instruction Parser::parse_assignment()
{
  std::string dest = cur().text;
  advance();
  expect_punct("=");
  return parse_rvalue(dest);
}

long long Parser::parse_integer_literal()
{
  bool negative = accept_punct("-");
  if(cur().kind != Token::TK_NUMBER) {
    fail("expected an integer literal");
  }
  const std::string text = cur().text;
  advance();
  char * end = 0;
  const long long value = strtoll(text.c_str(), &end, 0);
  if(end == text.c_str() || *end != '\0') {
    fail("invalid integer literal '" + text + "'");
  }
  return negative ? -value : value;
}

Operand Parser::parse_literal()
{
  Operand operand;
  operand.kind = Operand::OP_INTEGER;
  bool negative = accept_punct("-");
  if(cur().kind == Token::TK_NUMBER) {
    operand.text = (negative ? "-" : "") + cur().text;
    advance();
    return operand;
  }
  if(cur().kind == Token::TK_IDENT && !negative) {
    if(cur().text == "nullptr" || cur().text == "false") {
      operand.text = "0";
      advance();
      return operand;
    }
    if(cur().text == "true") {
      operand.text = "1";
      advance();
      return operand;
    }
  }
  fail("expected a scalar literal");
}

Operand Parser::parse_value()
{
  Operand operand;
  if(cur().kind == Token::TK_TEMP) {
    operand.kind = Operand::OP_TEMP;
    operand.text = cur().text;
    advance();
    return operand;
  }
  if(cur().kind == Token::TK_SLOT) {
    operand.kind = Operand::OP_SLOT;
    operand.text = cur().text;
    advance();
    return operand;
  }
  if(cur().kind == Token::TK_GLOBAL) {
    operand.kind = Operand::OP_GLOBAL;
    operand.text = cur().text;
    advance();
    return operand;
  }
  return parse_literal();
}

Operand Parser::parse_storage()
{
  if(cur().kind != Token::TK_TEMP && cur().kind != Token::TK_SLOT &&
     cur().kind != Token::TK_GLOBAL) {
    fail("expected a storage operand");
  }
  return parse_value();
}

Operand Parser::parse_block_operand()
{
  Operand operand;
  operand.kind = Operand::OP_LABEL;
  operand.text = expect_sigil(Token::TK_BLOCK, "a block label");
  return operand;
}

void Parser::parse_object_span(Instruction & instruction)
{
  if(cur().kind != Token::TK_NUMBER) {
    fail("expected a <bytes>x<align> span");
  }
  std::size_t bytes = 0;
  std::size_t alignment = 0;
  if(!parse_object_dimensions(cur().text, bytes, alignment)) {
    fail("invalid <bytes>x<align> span '" + cur().text + "'");
  }
  instruction.byte_count = bytes;
  instruction.byte_alignment = alignment;
  advance();
}

void Parser::parse_call_signature(Instruction & instruction)
{
  instruction.has_call_signature = true;
  instruction.call_params = parse_parameters();
  expect_punct("->");
  instruction.call_return_type = parse_type();
  std::vector<MetaItem> items = parse_metadata();
  for(std::size_t i = 0; i < items.size(); ++i) {
    if(!apply_boundary_item(items[i], instruction.call_boundary)) {
      std::ostringstream out;
      out << source_name_ << ":" << items[i].line
          << ": symbol-boundary metadata key '" << items[i].key
          << "' is not valid on a call signature";
      throw ParseError(out.str());
    }
  }
}

void Parser::parse_call_tail(Instruction & instruction)
{
  if(cur().kind == Token::TK_GLOBAL) {
    instruction.first.kind = Operand::OP_GLOBAL;
  } else if(cur().kind == Token::TK_TEMP) {
    instruction.first.kind = Operand::OP_TEMP;
  } else {
    fail("expected a call target");
  }
  instruction.first.text = cur().text;
  advance();
  expect_punct("(");
  if(!accept_punct(")")) {
    for(;;) {
      instruction.args.push_back(parse_value());
      if(!accept_punct(",")) {
        break;
      }
    }
    expect_punct(")");
  }
  if(accept_ident("as")) {
    parse_call_signature(instruction);
  }
}

bool is_known_operator(const char * const * table, std::size_t count,
                       const std::string & spelling)
{
  for(std::size_t i = 0; i < count; ++i) {
    if(spelling == table[i]) {
      return true;
    }
  }
  return false;
}

IndexProjectionKind projection_from_items(const std::vector<MetaItem> & items,
                                          std::string & bad_key,
                                          std::string & bad_value)
{
  IndexProjectionKind projection = IPK_NONE;
  for(std::size_t i = 0; i < items.size(); ++i) {
    if(items[i].key != "projection") {
      bad_key = items[i].key;
      return IPK_NONE;
    }
    const std::string & value = items[i].value;
    if(value == "array_element") { projection = IPK_ARRAY_ELEMENT; }
    else if(value == "field") { projection = IPK_FIELD; }
    else if(value == "base_subobject") { projection = IPK_BASE_SUBOBJECT; }
    else if(value == "reference_field") { projection = IPK_REFERENCE_FIELD; }
    else { bad_value = value; return IPK_NONE; }
  }
  return projection;
}

Instruction Parser::parse_rvalue(const std::string & dest)
{
  static const char * kUnaryOps[] = { "neg", "not", "bitnot", "decay", "bswap" };
  static const char * kBinaryOps[] = {
    "add", "sub", "mul", "div", "mod", "udiv", "umod",
    "and", "or", "xor", "shl", "shr", "ushr"
  };
  static const char * kComparePredicates[] = {
    "eq", "ne", "lt", "le", "gt", "ge", "ult", "ule", "ugt", "uge"
  };
  static const char * kConversionOps[] = {
    "sext", "zext", "trunc", "sitofp", "uitofp", "fptosi", "fptoui", "fpext", "fptrunc"
  };

  Instruction instruction;
  instruction.dest = dest;
  const std::string keyword = expect_ident();

  if(keyword == "const" || keyword == "copy") {
    instruction.kind = keyword == "const" ? Instruction::IK_CONST : Instruction::IK_COPY;
    instruction.type = parse_type();
    instruction.first = keyword == "const" ? parse_literal() : parse_value();
    return instruction;
  }
  if(keyword == "addr") {
    instruction.kind = Instruction::IK_ADDR;
    if(cur().kind != Token::TK_SLOT && cur().kind != Token::TK_GLOBAL) {
      fail("expected an addressable operand");
    }
    instruction.first = parse_value();
    return instruction;
  }
  if(keyword == "load") {
    instruction.kind = Instruction::IK_LOAD;
    instruction.type = parse_type();
    instruction.first = parse_storage();
    return instruction;
  }
  if(keyword == "atomic_load") {
    instruction.kind = Instruction::IK_ATOMIC_LOAD;
    instruction.type = parse_type();
    instruction.first = parse_value();
    expect_punct(",");
    instruction.atomic_order = parse_integer_literal();
    return instruction;
  }
  if(keyword == "index") {
    instruction.kind = Instruction::IK_INDEX;
    instruction.type = parse_type();
    std::vector<MetaItem> items = parse_metadata();
    std::string bad_key;
    std::string bad_value;
    instruction.index_projection = projection_from_items(items, bad_key, bad_value);
    if(!bad_key.empty()) {
      fail("unknown index metadata key '" + bad_key + "'");
    }
    if(!bad_value.empty()) {
      fail("invalid projection metadata value '" + bad_value + "'");
    }
    instruction.first = parse_value();
    expect_punct(",");
    instruction.second = parse_value();
    return instruction;
  }
  if(keyword == "unary" || keyword == "binary" || keyword == "cmp") {
    instruction.op = expect_ident();
    if(keyword == "unary") {
      instruction.kind = Instruction::IK_UNARY;
      if(!is_known_operator(kUnaryOps, 5, instruction.op)) {
        fail("unknown unary operator '" + instruction.op + "'");
      }
    } else if(keyword == "binary") {
      instruction.kind = Instruction::IK_BINARY;
      if(!is_known_operator(kBinaryOps, 13, instruction.op)) {
        fail("unknown binary operator '" + instruction.op + "'");
      }
    } else {
      instruction.kind = Instruction::IK_CMP;
      if(!is_known_operator(kComparePredicates, 10, instruction.op)) {
        fail("unknown compare predicate '" + instruction.op + "'");
      }
    }
    instruction.type = parse_type();
    instruction.first = parse_value();
    if(keyword != "unary") {
      expect_punct(",");
      instruction.second = parse_value();
    }
    return instruction;
  }
  if(keyword == "convert") {
    instruction.kind = Instruction::IK_CONVERT;
    instruction.op = expect_ident();
    if(!is_known_operator(kConversionOps, 9, instruction.op)) {
      fail("unknown conversion operator '" + instruction.op + "'");
    }
    instruction.type = parse_type();
    instruction.source_type = parse_type();
    instruction.first = parse_value();
    return instruction;
  }
  if(keyword == "atomic_add_fetch" || keyword == "atomic_exchange") {
    instruction.kind = keyword == "atomic_add_fetch"
      ? Instruction::IK_ATOMIC_ADD_FETCH : Instruction::IK_ATOMIC_EXCHANGE;
    instruction.type = parse_type();
    instruction.first = parse_value();
    expect_punct(",");
    instruction.second = parse_value();
    expect_punct(",");
    instruction.atomic_order = parse_integer_literal();
    return instruction;
  }
  if(keyword == "atomic_compare_exchange") {
    instruction.kind = Instruction::IK_ATOMIC_COMPARE_EXCHANGE;
    instruction.type = parse_type();
    instruction.first = parse_value();
    expect_punct(",");
    instruction.second = parse_value();
    expect_punct(",");
    instruction.third = parse_value();
    expect_punct(",");
    instruction.atomic_order = parse_integer_literal();
    expect_punct(",");
    instruction.atomic_failure_order = parse_integer_literal();
    return instruction;
  }
  if(keyword == "call") {
    instruction.kind = Instruction::IK_CALL;
    instruction.type = parse_type();
    if(instruction.type.text == "void") {
      fail("a call assigned to a temporary must have a non-void result type");
    }
    parse_call_tail(instruction);
    return instruction;
  }
  if(keyword == "exception" || keyword == "exception_selector") {
    instruction.kind = keyword == "exception"
      ? Instruction::IK_EXCEPTION : Instruction::IK_EXCEPTION_SELECTOR;
    instruction.type = parse_type();
    if(instruction.type.text == "void") {
      fail("'" + keyword + "' requires a non-void type");
    }
    return instruction;
  }
  if(keyword == "stack_alloc") {
    instruction.kind = Instruction::IK_STACK_ALLOC;
    long long size = parse_integer_literal();
    if(size <= 0) {
      fail("stack_alloc requires a positive size");
    }
    instruction.byte_count = static_cast<std::size_t>(size);
    return instruction;
  }
  fail("unknown LowIR instruction '" + keyword + "'");
}

Instruction Parser::parse_statement()
{
  Instruction instruction;
  const std::string keyword = expect_ident();

  if(keyword == "store") {
    instruction.kind = Instruction::IK_STORE;
    instruction.type = parse_type();
    instruction.first = parse_value();
    expect_punct(",");
    instruction.second = parse_storage();
    return instruction;
  }
  if(keyword == "atomic_store") {
    instruction.kind = Instruction::IK_ATOMIC_STORE;
    instruction.type = parse_type();
    instruction.first = parse_value();
    expect_punct(",");
    instruction.second = parse_value();
    expect_punct(",");
    instruction.atomic_order = parse_integer_literal();
    return instruction;
  }
  if(keyword == "atomic_thread_fence" || keyword == "atomic_signal_fence") {
    instruction.kind = keyword == "atomic_thread_fence"
      ? Instruction::IK_ATOMIC_THREAD_FENCE : Instruction::IK_ATOMIC_SIGNAL_FENCE;
    instruction.atomic_order = parse_integer_literal();
    return instruction;
  }
  if(keyword == "call") {
    instruction.kind = Instruction::IK_CALL;
    instruction.type = parse_type();
    if(instruction.type.text != "void") {
      fail("a call statement must have a void result type");
    }
    instruction.call_returns_void = true;
    parse_call_tail(instruction);
    return instruction;
  }
  if(keyword == "copyobj" || keyword == "zeroinit") {
    instruction.kind = keyword == "copyobj"
      ? Instruction::IK_COPYOBJ : Instruction::IK_ZEROINIT;
    parse_object_span(instruction);
    instruction.first = parse_value();
    if(keyword == "copyobj") {
      expect_punct(",");
      instruction.second = parse_value();
    }
    return instruction;
  }
  if(keyword == "eh_try" || keyword == "eh_cleanup") {
    instruction.kind = keyword == "eh_try"
      ? Instruction::IK_EH_TRY : Instruction::IK_EH_CLEANUP;
    instruction.first = parse_block_operand();
    return instruction;
  }
  if(keyword == "eh_catch") {
    instruction.kind = Instruction::IK_EH_CATCH;
    instruction.first.kind = Operand::OP_GLOBAL;
    instruction.first.text = expect_sigil(Token::TK_GLOBAL, "an exception type symbol");
    return instruction;
  }
  if(keyword == "eh_filter") {
    instruction.kind = Instruction::IK_EH_FILTER;
    while(cur().kind == Token::TK_GLOBAL) {
      Operand operand;
      operand.kind = Operand::OP_GLOBAL;
      operand.text = cur().text;
      advance();
      instruction.args.push_back(operand);
      if(!accept_punct(",")) {
        break;
      }
    }
    return instruction;
  }
  if(keyword == "eh_catch_all") {
    instruction.kind = Instruction::IK_EH_CATCH_ALL;
    return instruction;
  }
  if(keyword == "eh_end") {
    instruction.kind = Instruction::IK_EH_END;
    return instruction;
  }
  if(keyword == "throw") {
    instruction.kind = Instruction::IK_THROW;
    instruction.type = parse_type();
    instruction.first = parse_value();
    return instruction;
  }
  if(keyword == "resume") {
    instruction.kind = Instruction::IK_RESUME;
    return instruction;
  }
  if(keyword == "jump") {
    instruction.kind = Instruction::IK_JUMP;
    instruction.first = parse_block_operand();
    return instruction;
  }
  if(keyword == "branch") {
    instruction.kind = Instruction::IK_BRANCH;
    instruction.first = parse_value();
    expect_punct(",");
    instruction.second = parse_block_operand();
    expect_punct(",");
    instruction.third = parse_block_operand();
    return instruction;
  }
  if(keyword == "switch") {
    instruction.kind = Instruction::IK_SWITCH;
    instruction.first = parse_value();
    expect_punct(",");
    instruction.second = parse_block_operand();
    while(accept_punct(",")) {
      SwitchCase arm;
      arm.value = parse_value();
      expect_punct(":");
      arm.label = expect_sigil(Token::TK_BLOCK, "a switch case block label");
      instruction.switch_cases.push_back(arm);
    }
    return instruction;
  }
  if(keyword == "return") {
    instruction.kind = Instruction::IK_RETURN;
    instruction.type = parse_type();
    if(instruction.type.text != "void") {
      instruction.first = parse_value();
    }
    return instruction;
  }
  fail("unknown LowIR instruction '" + keyword + "'");
}

void Parser::parse_into(Program & program)
{
  while(cur().kind != Token::TK_END) {
    if(accept_ident("declare")) {
      if(accept_ident("global")) { parse_global_declaration(program); }
      else if(accept_ident("function")) { parse_function_declaration(program); }
      else { fail("expected 'global' or 'function' after 'declare'"); }
    } else if(accept_ident("global")) {
      parse_global_definition(program);
    } else if(accept_ident("function")) {
      parse_function_definition(program);
    } else if(accept_ident("alias")) {
      parse_object_alias(program);
    } else {
      fail("expected a LowIR top-level item");
    }
  }
}

}  // namespace

bool describe_low_type(const LowType & type, TypeFacts & facts)
{
  for(std::size_t i = 0; i < sizeof(kScalarTypes) / sizeof(kScalarTypes[0]); ++i) {
    if(type.text == kScalarTypes[i].name) {
      facts.category = kScalarTypes[i].category;
      facts.is_signed = kScalarTypes[i].is_signed;
      facts.size = kScalarTypes[i].size;
      facts.alignment = kScalarTypes[i].alignment;
      facts.width_bits = kScalarTypes[i].width_bits;
      return true;
    }
  }
  if(type.text.compare(0, 4, "obj<") == 0 &&
     type.text.size() > 5 && type.text[type.text.size() - 1] == '>') {
    std::string dims = type.text.substr(4, type.text.size() - 5);
    std::size_t bytes = 0;
    std::size_t alignment = 0;
    if(!parse_object_dimensions(dims, bytes, alignment)) {
      return false;
    }
    facts.category = TypeFacts::TC_OBJECT;
    facts.is_signed = false;
    facts.size = bytes;
    facts.alignment = alignment;
    facts.width_bits = 0;
    return true;
  }
  return false;
}

bool is_memory_class_type(const LowType & type)
{
  TypeFacts facts;
  if(!describe_low_type(type, facts)) {
    return false;
  }
  return facts.category == TypeFacts::TC_OBJECT || facts.width_bits == 80;
}

LowirProgram parse_lowir_program_text(const std::string & text,
                                      const std::string & source_name)
{
  Program program;
  Lexer lexer(text, source_name);
  Parser parser(lexer, source_name);
  parser.parse_into(program);
  return program;
}

namespace {

std::string read_source_file(const std::string & path)
{
  std::ifstream in(path.c_str(), std::ios::binary);
  if(!in) {
    throw ParseError("cannot open LowIR source file '" + path + "'");
  }
  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  in.seekg(0, std::ios::beg);
  if(!in || size < 0) {
    throw ParseError("cannot read LowIR source file '" + path + "'");
  }
  std::string text;
  text.resize(static_cast<std::size_t>(size));
  if(size != 0) {
    in.read(&text[0], size);
    text.resize(static_cast<std::size_t>(in.gcount()));
  }
  return text;
}

}  // namespace

LowirProgram parse_lowir_program_files(const std::vector<std::string> & paths)
{
  Program program;
  for(std::size_t i = 0; i < paths.size(); ++i) {
    const std::string text = read_source_file(paths[i]);
    Lexer lexer(text, paths[i]);
    Parser parser(lexer, paths[i]);
    parser.parse_into(program);
  }
  return program;
}

}  // namespace lowir_model
