#include "lowir_validate.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "lowir_text.h"

namespace lowir_model {

namespace {

enum SymbolKind
{
  SK_GLOBAL,
  SK_FUNCTION
};

struct SymbolFacts
{
  SymbolKind kind = SK_GLOBAL;
  bool defined = false;
  bool thread_local_storage = false;
  const FunctionDeclaration * declaration = 0;
  const Function * definition = 0;
};

typedef std::unordered_map<std::string, SymbolFacts> SymbolTable;

void fail(const std::string & message)
{
  throw ParseError(message);
}

void add_symbol(SymbolTable & symbols, const std::string & name, const SymbolFacts & facts)
{
  if(symbols.find(name) != symbols.end()) {
    fail("duplicate top-level symbol '@" + name + "'");
  }
  symbols[name] = facts;
}

SymbolTable build_symbol_table(const Program & program)
{
  SymbolTable symbols;
  for(std::size_t i = 0; i < program.global_declarations.size(); ++i) {
    const GlobalDeclaration & declaration = program.global_declarations[i];
    SymbolFacts facts;
    facts.kind = SK_GLOBAL;
    facts.thread_local_storage = declaration.storage == GSM_THREAD_LOCAL;
    add_symbol(symbols, declaration.name, facts);
  }
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i) {
    const FunctionDeclaration & declaration = program.function_declarations[i];
    SymbolFacts facts;
    facts.kind = SK_FUNCTION;
    facts.declaration = &declaration;
    add_symbol(symbols, declaration.name, facts);
  }
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    const GlobalDefinition & global = program.globals[i];
    SymbolFacts facts;
    facts.kind = SK_GLOBAL;
    facts.defined = true;
    facts.thread_local_storage = global.storage == GSM_THREAD_LOCAL;
    add_symbol(symbols, global.name, facts);
  }
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    const Function & function = program.functions[i];
    SymbolFacts facts;
    facts.kind = SK_FUNCTION;
    facts.defined = true;
    facts.definition = &function;
    add_symbol(symbols, function.name, facts);
  }
  return symbols;
}

void check_singleton_roles(const Program & program)
{
  std::unordered_set<int> seen;
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    SymbolRole role = program.globals[i].metadata.role;
    if(role != SR_NONE && !seen.insert(static_cast<int>(role)).second) {
      fail("more than one symbol owns the same LowIR role");
    }
  }
  for(std::size_t i = 0; i < program.global_declarations.size(); ++i) {
    SymbolRole role = program.global_declarations[i].metadata.role;
    if(role != SR_NONE && !seen.insert(static_cast<int>(role)).second) {
      fail("more than one symbol owns the same LowIR role");
    }
  }
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    SymbolRole role = program.functions[i].metadata.role;
    if(role != SR_NONE && !seen.insert(static_cast<int>(role)).second) {
      fail("more than one symbol owns the same LowIR role");
    }
  }
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i) {
    SymbolRole role = program.function_declarations[i].metadata.role;
    if(role != SR_NONE && !seen.insert(static_cast<int>(role)).second) {
      fail("more than one symbol owns the same LowIR role");
    }
  }
}

void check_tls_wrapper(const SymbolTable & symbols,
                       std::unordered_set<std::string> & wrapped,
                       const SymbolMetadata & metadata)
{
  if(metadata.tls_for_symbol.empty()) {
    return;
  }
  SymbolTable::const_iterator found = symbols.find(metadata.tls_for_symbol);
  if(found == symbols.end() || found->second.kind != SK_GLOBAL ||
     !found->second.thread_local_storage) {
    fail("tls_for target '@" + metadata.tls_for_symbol +
         "' does not name a thread-local global");
  }
  if(!wrapped.insert(metadata.tls_for_symbol).second) {
    fail("more than one tls_for wrapper names '@" + metadata.tls_for_symbol + "'");
  }
}

void check_program_symbols(const Program & program, const SymbolTable & symbols)
{
  std::unordered_set<std::string> wrapped;
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i) {
    check_tls_wrapper(symbols, wrapped, program.function_declarations[i].metadata);
  }
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    check_tls_wrapper(symbols, wrapped, program.functions[i].metadata);
  }

  std::unordered_set<std::string> alias_spellings;
  for(std::size_t i = 0; i < program.object_aliases.size(); ++i) {
    const ObjectAlias & alias = program.object_aliases[i];
    if(!alias_spellings.insert(alias.object_symbol).second) {
      fail("duplicate object alias spelling '" + alias.object_symbol + "'");
    }
    if(symbols.find(alias.target) == symbols.end()) {
      fail("object alias target '@" + alias.target + "' is not a top-level symbol");
    }
  }

  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    const GlobalDefinition & global = program.globals[i];
    if(global.init_kind == GlobalDefinition::INIT_ADDR && !global.structured &&
       symbols.find(global.init_operand.text) == symbols.end()) {
      fail("global '@" + global.name + "' takes the address of unknown symbol '@" +
           global.init_operand.text + "'");
    }
    for(std::size_t k = 0; k < global.data_items.size(); ++k) {
      const GlobalDefinition::DataItem & item = global.data_items[k];
      if(item.kind == GlobalDefinition::DataItem::ITEM_ADDR &&
         symbols.find(item.symbol) == symbols.end()) {
        fail("global '@" + global.name + "' takes the address of unknown symbol '@" +
             item.symbol + "'");
      }
    }
  }
}

void check_parameter_list(const std::vector<Parameter> & params,
                          const LowType & return_type,
                          const std::string & where)
{
  std::unordered_set<std::string> names;
  for(std::size_t i = 0; i < params.size(); ++i) {
    const Parameter & param = params[i];
    if(!names.insert(param.name).second) {
      fail("duplicate parameter name '%" + param.name + "' in " + where);
    }
    const bool is_pointer = param.type.text == "ptr";
    if(param.metadata.passing != PPM_DIRECT && !is_pointer) {
      fail("non-direct pass metadata requires parameter type ptr in " + where);
    }
    if(param.metadata.capture != PCM_DEFAULT && !is_pointer) {
      fail("capture metadata requires parameter type ptr in " + where);
    }
    if(param.metadata.access != PAM_DEFAULT && !is_pointer) {
      fail("access metadata requires parameter type ptr in " + where);
    }
    if(param.metadata.alias != PALM_DEFAULT && !is_pointer) {
      fail("alias metadata requires parameter type ptr in " + where);
    }
    if(param.metadata.passing == PPM_INDIRECT_RESULT) {
      if(i != 0) {
        fail("indirect_result must be the first parameter in " + where);
      }
      if(return_type.text != "void") {
        fail("indirect_result requires a void return type in " + where);
      }
    }
  }
}

bool is_terminator(const Instruction & instruction)
{
  switch(instruction.kind) {
    case Instruction::IK_JUMP:
    case Instruction::IK_BRANCH:
    case Instruction::IK_SWITCH:
    case Instruction::IK_RETURN:
    case Instruction::IK_THROW:
    case Instruction::IK_RESUME:
      return true;
    default:
      return false;
  }
}

void check_conversion(const Instruction & instruction, const std::string & where)
{
  TypeFacts destination;
  TypeFacts source;
  if(!describe_low_type(instruction.type, destination) ||
     !describe_low_type(instruction.source_type, source)) {
    fail("invalid conversion type in " + where);
  }
  const bool destination_int = destination.category == TypeFacts::TC_INTEGER;
  const bool source_int = source.category == TypeFacts::TC_INTEGER;
  const bool destination_float = destination.category == TypeFacts::TC_FLOAT;
  const bool source_float = source.category == TypeFacts::TC_FLOAT;
  const std::string & op = instruction.op;

  if(op == "sext" || op == "zext") {
    if(!destination_int || !source_int || destination.width_bits <= source.width_bits) {
      fail("'" + op + "' must widen an integer value in " + where);
    }
  } else if(op == "trunc") {
    if(!destination_int || !source_int || destination.width_bits >= source.width_bits) {
      fail("'trunc' must narrow an integer value in " + where);
    }
  } else if(op == "fpext") {
    if(!destination_float || !source_float || destination.width_bits <= source.width_bits) {
      fail("'fpext' must widen a floating value in " + where);
    }
  } else if(op == "fptrunc") {
    if(!destination_float || !source_float || destination.width_bits >= source.width_bits) {
      fail("'fptrunc' must narrow a floating value in " + where);
    }
  } else if(op == "sitofp" || op == "uitofp") {
    if(!destination_float || !source_int) {
      fail("'" + op + "' converts an integer value to a floating value in " + where);
    }
  } else if(op == "fptosi" || op == "fptoui") {
    if(!destination_int || !source_float) {
      fail("'" + op + "' converts a floating value to an integer value in " + where);
    }
  }
}

class FunctionChecker
{
public:
  FunctionChecker(const Function & function, const SymbolTable & symbols)
    : function_(function), symbols_(symbols), where_("function '@" + function.name + "'")
  {}

  void run();

private:
  void collect_names();
  void check_block_structure();
  void check_instruction(const Instruction & instruction);
  void check_operand(const Operand & operand);
  void check_call(const Instruction & instruction);

  const Function & function_;
  const SymbolTable & symbols_;
  std::string where_;
  std::unordered_set<std::string> temps_;
  std::unordered_set<std::string> slots_;
  std::unordered_set<std::string> blocks_;
};

void FunctionChecker::collect_names()
{
  for(std::size_t i = 0; i < function_.params.size(); ++i) {
    temps_.insert(function_.params[i].name);
  }
  for(std::size_t i = 0; i < function_.slots.size(); ++i) {
    if(!slots_.insert(function_.slots[i].first).second) {
      fail("duplicate slot name '$" + function_.slots[i].first + "' in " + where_);
    }
    TypeFacts facts;
    if(!describe_low_type(function_.slots[i].second, facts) ||
       facts.category == TypeFacts::TC_VOID) {
      fail("slot '$" + function_.slots[i].first + "' has an invalid type in " + where_);
    }
  }
  for(std::size_t i = 0; i < function_.blocks.size(); ++i) {
    if(!blocks_.insert(function_.blocks[i].label).second) {
      fail("duplicate block label '^" + function_.blocks[i].label + "' in " + where_);
    }
    const std::vector<Instruction> & instructions = function_.blocks[i].instructions;
    for(std::size_t k = 0; k < instructions.size(); ++k) {
      if(!instructions[k].dest.empty()) {
        temps_.insert(instructions[k].dest);
      }
    }
  }
}

void FunctionChecker::check_block_structure()
{
  if(function_.blocks.empty()) {
    fail(where_ + " has no blocks");
  }
  for(std::size_t i = 0; i < function_.blocks.size(); ++i) {
    const Block & block = function_.blocks[i];
    if(block.instructions.empty()) {
      fail("block '^" + block.label + "' has no terminator in " + where_);
    }
    for(std::size_t k = 0; k + 1 < block.instructions.size(); ++k) {
      if(is_terminator(block.instructions[k])) {
        fail("instruction follows a terminator in block '^" + block.label + "' of " + where_);
      }
    }
    if(!is_terminator(block.instructions[block.instructions.size() - 1])) {
      fail("block '^" + block.label + "' has no terminator in " + where_);
    }
  }
}

void FunctionChecker::check_operand(const Operand & operand)
{
  switch(operand.kind) {
    case Operand::OP_TEMP:
      if(temps_.find(operand.text) == temps_.end()) {
        fail("undefined temporary '%" + operand.text + "' in " + where_);
      }
      break;
    case Operand::OP_SLOT:
      if(slots_.find(operand.text) == slots_.end()) {
        fail("undefined stack slot '$" + operand.text + "' in " + where_);
      }
      break;
    case Operand::OP_GLOBAL:
      if(symbols_.find(operand.text) == symbols_.end()) {
        fail("undefined top-level symbol '@" + operand.text + "' in " + where_);
      }
      break;
    case Operand::OP_LABEL:
      if(blocks_.find(operand.text) == blocks_.end()) {
        fail("undefined block target '^" + operand.text + "' in " + where_);
      }
      break;
    default:
      break;
  }
}

void FunctionChecker::check_call(const Instruction & instruction)
{
  bool direct = false;
  const FunctionDeclaration * declaration = 0;
  const Function * definition = 0;
  if(instruction.first.kind == Operand::OP_GLOBAL) {
    SymbolTable::const_iterator found = symbols_.find(instruction.first.text);
    if(found == symbols_.end()) {
      fail("call to undefined symbol '@" + instruction.first.text + "' in " + where_);
    }
    if(found->second.kind == SK_FUNCTION) {
      direct = true;
      declaration = found->second.declaration;
      definition = found->second.definition;
    }
  }
  if(!direct && !instruction.has_call_signature) {
    fail("indirect call requires an explicit 'as (...) -> ...' signature in " + where_);
  }
  if(instruction.has_call_signature) {
    check_parameter_list(instruction.call_params, instruction.call_return_type,
                         "call signature in " + where_);
  }
  if(!direct) {
    return;
  }
  const std::vector<Parameter> * params = 0;
  CallArityMode arity = CAM_FIXED;
  const LowType * return_type = 0;
  if(definition != 0) {
    params = &definition->params;
    arity = definition->boundary.arity;
    return_type = &definition->return_type;
  } else if(declaration != 0) {
    params = &declaration->params;
    arity = declaration->boundary.arity;
    return_type = &declaration->return_type;
  }
  if(params == 0) {
    return;
  }
  if(instruction.args.size() < params->size() ||
     (arity == CAM_FIXED && instruction.args.size() != params->size())) {
    fail("call to '@" + instruction.first.text + "' has the wrong argument count in " + where_);
  }
  if(return_type != 0 && instruction.type.text != return_type->text) {
    fail("call to '@" + instruction.first.text +
         "' disagrees with the declared return type in " + where_);
  }
}

void FunctionChecker::check_instruction(const Instruction & instruction)
{
  check_operand(instruction.first);
  check_operand(instruction.second);
  check_operand(instruction.third);
  for(std::size_t i = 0; i < instruction.args.size(); ++i) {
    check_operand(instruction.args[i]);
  }
  for(std::size_t i = 0; i < instruction.switch_cases.size(); ++i) {
    check_operand(instruction.switch_cases[i].value);
    if(blocks_.find(instruction.switch_cases[i].label) == blocks_.end()) {
      fail("undefined switch target '^" + instruction.switch_cases[i].label +
           "' in " + where_);
    }
  }
  if(instruction.kind == Instruction::IK_UNARY) {
    if(instruction.op == "decay" && instruction.type.text != "ptr") {
      fail("'unary decay' requires type ptr in " + where_);
    }
    if(instruction.op == "bswap" && instruction.type.text != "i16" &&
       instruction.type.text != "u16" && instruction.type.text != "i32" &&
       instruction.type.text != "u32" && instruction.type.text != "i64") {
      fail("'unary bswap' requires a 16, 32 or 64 bit integer type in " + where_);
    }
  }
  if(instruction.kind == Instruction::IK_CONVERT) {
    check_conversion(instruction, where_);
  }
  if(instruction.kind == Instruction::IK_CALL) {
    check_call(instruction);
  }
  if(instruction.kind == Instruction::IK_COPYOBJ || instruction.kind == Instruction::IK_ZEROINIT) {
    if(instruction.byte_count == 0 || instruction.byte_alignment == 0 ||
       (instruction.byte_alignment & (instruction.byte_alignment - 1)) != 0) {
      fail("bulk memory span requires positive bytes and power-of-two alignment in " + where_);
    }
  }
}

void FunctionChecker::run()
{
  check_parameter_list(function_.params, function_.return_type, where_);
  collect_names();
  check_block_structure();
  for(std::size_t i = 0; i < function_.blocks.size(); ++i) {
    const std::vector<Instruction> & instructions = function_.blocks[i].instructions;
    for(std::size_t k = 0; k < instructions.size(); ++k) {
      check_instruction(instructions[k]);
    }
  }
}

const Function * find_function_by_role(const Program & program, SymbolRole role)
{
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    if(program.functions[i].metadata.role == role) {
      return &program.functions[i];
    }
  }
  return 0;
}

const Function * find_function_by_name(const Program & program, const std::string & name)
{
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    if(program.functions[i].name == name && program.functions[i].metadata.role == SR_NONE) {
      return &program.functions[i];
    }
  }
  return 0;
}

}  // namespace

RuntimeRoles resolve_runtime_roles(const Program & program)
{
  RuntimeRoles roles;
  roles.entry = find_function_by_role(program, SR_ENTRY);
  if(roles.entry == 0) {
    roles.entry = find_function_by_name(program, "main");
  }
  roles.init = find_function_by_role(program, SR_INIT);
  if(roles.init == 0) {
    roles.init = find_function_by_name(program, "__cppgm_init");
  }
  roles.fini = find_function_by_role(program, SR_FINI);
  if(roles.fini == 0) {
    roles.fini = find_function_by_name(program, "__cppgm_fini");
  }
  return roles;
}

void validate_lowir_program(const Program & program)
{
  const SymbolTable symbols = build_symbol_table(program);
  check_singleton_roles(program);
  check_program_symbols(program, symbols);
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i) {
    const FunctionDeclaration & declaration = program.function_declarations[i];
    check_parameter_list(declaration.params, declaration.return_type,
                         "function declaration '@" + declaration.name + "'");
  }
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    FunctionChecker checker(program.functions[i], symbols);
    checker.run();
  }
  if(resolve_runtime_roles(program).entry == 0) {
    fail("LowIR program has no entry function");
  }
}

}  // namespace lowir_model
