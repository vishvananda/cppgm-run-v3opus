#include "lowir_cy86.h"

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "lowir_text.h"
#include "lowir_validate.h"

namespace lowir_cy86 {

using lowir_model::Block;
using lowir_model::Function;
using lowir_model::FunctionDeclaration;
using lowir_model::GlobalDefinition;
using lowir_model::Instruction;
using lowir_model::LowType;
using lowir_model::Operand;
using lowir_model::Parameter;
using lowir_model::ParseError;
using lowir_model::Program;
using lowir_model::TypeFacts;

namespace {

const char kArgRegisters[4] = { 'x', 'y', 'z', 't' };
const std::size_t kScratchSlots = 4;
const std::size_t kScratchSlotBytes = 16;
const char * const kEhTopGlobal = "__cppgm_eh_top";
const char * const kEhValueGlobal = "__cppgm_eh_value";
const char * const kEhUnhandledFunction = "__cppgm_eh_unhandled";

std::string num(long long value)
{
  std::ostringstream out;
  out << value;
  return out.str();
}

std::string unum(std::size_t value)
{
  std::ostringstream out;
  out << value;
  return out.str();
}

std::string reg(char base, int width)
{
  return std::string(1, base) + num(width);
}

TypeFacts facts_of(const LowType & type)
{
  TypeFacts facts;
  if(!lowir_model::describe_low_type(type, facts)) {
    throw ParseError("unsupported LowIR type '" + type.text + "'");
  }
  return facts;
}

int scalar_width(const LowType & type)
{
  const TypeFacts facts = facts_of(type);
  return facts.width_bits == 0 ? 64 : facts.width_bits;
}

bool is_float_type(const LowType & type)
{
  return facts_of(type).category == TypeFacts::TC_FLOAT;
}

bool is_signed_narrow(const LowType & type)
{
  const TypeFacts facts = facts_of(type);
  return facts.category == TypeFacts::TC_INTEGER && facts.is_signed && facts.width_bits < 64;
}

std::size_t frame_cell_size(const LowType & type)
{
  const TypeFacts facts = facts_of(type);
  std::size_t size = facts.size < 8 ? 8 : facts.size;
  return (size + 7) & ~static_cast<std::size_t>(7);
}

LowType make_type(const char * text)
{
  LowType type;
  type.text = text;
  return type;
}

struct FrameEntity
{
  std::size_t offset = 0;
  LowType type;
  bool memory_class = false;
};

// Program-wide symbol facts shared by every function emitter.
struct SymbolIndex
{
  std::unordered_set<std::string> functions;
  std::unordered_set<std::string> globals;
  std::unordered_map<std::string, const Function *> definitions;
  std::unordered_map<std::string, const FunctionDeclaration *> declarations;
  std::string eh_top_label;
  std::string eh_value_label;
  std::string eh_unhandled_label;

  bool is_function(const std::string & name) const
  {
    return functions.find(name) != functions.end();
  }

  std::string label_for(const std::string & name) const
  {
    return is_function(name) ? ("fn__" + name) : ("g__" + name);
  }
};

bool uses_exceptions(const Instruction & instruction)
{
  switch(instruction.kind) {
    case Instruction::IK_EH_TRY:
    case Instruction::IK_EH_CLEANUP:
    case Instruction::IK_EH_CLEANUP_CLAUSE:
    case Instruction::IK_EH_CATCH:
    case Instruction::IK_EH_FILTER:
    case Instruction::IK_EH_CATCH_ALL:
    case Instruction::IK_EH_END:
    case Instruction::IK_THROW:
    case Instruction::IK_EXCEPTION:
    case Instruction::IK_EXCEPTION_SELECTOR:
    case Instruction::IK_RESUME:
      return true;
    default:
      return false;
  }
}

LowType destination_type(const Instruction & instruction)
{
  switch(instruction.kind) {
    case Instruction::IK_CMP:
    case Instruction::IK_ATOMIC_COMPARE_EXCHANGE:
    case Instruction::IK_EXCEPTION_SELECTOR:
      return make_type("i64");
    case Instruction::IK_ADDR:
    case Instruction::IK_INDEX:
    case Instruction::IK_STACK_ALLOC:
      return make_type("ptr");
    default:
      return instruction.type;
  }
}

std::size_t next_chunk_bytes(std::size_t remaining)
{
  if(remaining >= 8) { return 8; }
  if(remaining >= 4) { return 4; }
  if(remaining >= 2) { return 2; }
  return 1;
}

// ---------------------------------------------------------------------------
// Per-function emission
// ---------------------------------------------------------------------------

class FunctionEmitter
{
public:
  FunctionEmitter(const Function & function,
                  const SymbolIndex & symbols,
                  std::string & out,
                  long long & label_counter)
    : function_(function), symbols_(symbols), out_(out), label_counter_(label_counter)
  {}

  void run();

private:
  void build_layout();
  void emit_prologue();
  void emit_incoming_parameter(std::size_t index,
                               const FrameEntity & entity,
                               std::size_t & stack_index);
  void emit_epilogue();

  void emit(const std::string & text) { out_ += '\t'; out_ += text; out_ += ";\n"; }
  void emit_label(const std::string & text) { out_ += text; out_ += ":\n"; }

  const FrameEntity & temp(const std::string & name) const;
  const FrameEntity & slot(const std::string & name) const;
  std::string frame_mem(std::size_t offset) const;
  std::string scratch_mem(std::size_t index, std::size_t byte_offset) const;
  std::string block_label(const std::string & label) const;
  std::string dest_mem() const;

  LowType operand_type(const Operand & operand) const;
  bool is_address_form(const LowType & expected, const Operand & operand) const;
  void materialize(char base, const LowType & expected, const Operand & operand);
  void materialize_address(char base, const Operand & operand);
  void materialize_storage_pointer(char base, const Operand & operand);
  void load_scalar(char base, const std::string & mem, int width);
  void sign_extend_to_64(char base, const LowType & type);
  void store_from_register(char base, const LowType & type, const std::string & mem);

  void f80_literal_to_scratch(std::size_t index, const std::string & literal);
  void f80_pad_scratch(std::size_t index);
  void f80_operand_to_scratch(std::size_t index, const Operand & operand);
  void f80_storage_to_scratch(std::size_t index, const Operand & operand);
  void f80_pointer_to_scratch(std::size_t index, char pointer);
  void f80_scratch_to_frame(std::size_t index, std::size_t offset);
  void f80_scratch_to_pointer(std::size_t index, char pointer);
  void block_copy(std::size_t bytes, char destination, char source);

  void emit_instruction(const Instruction & instruction);
  void emit_value_instruction(const Instruction & instruction);
  void emit_load(const Instruction & instruction);
  void emit_store(const Instruction & instruction);
  void emit_index(const Instruction & instruction);
  void emit_atomic_instruction(const Instruction & instruction);
  void emit_compare_exchange(const Instruction & instruction);
  void emit_unary(const Instruction & instruction);
  void emit_binary(const Instruction & instruction);
  void emit_compare(const Instruction & instruction);
  void emit_convert(const Instruction & instruction);
  void emit_convert_source(const Instruction & instruction);
  void emit_call(const Instruction & instruction);
  void emit_bulk_instruction(const Instruction & instruction);
  void emit_exception_instruction(const Instruction & instruction);
  void emit_terminator(const Instruction & instruction);
  void emit_switch(const Instruction & instruction);
  void emit_unwind();
  void emit_return(const Instruction & instruction);
  void emit_argument(std::size_t index, const LowType & type, const Operand & operand);
  void store_scalar_result(const LowType & type) { store_from_register('x', type, dest_mem()); }

  const Function & function_;
  const SymbolIndex & symbols_;
  std::string & out_;
  long long & label_counter_;

  std::unordered_map<std::string, FrameEntity> temps_;
  std::unordered_map<std::string, FrameEntity> slots_;
  std::vector<FrameEntity> incoming_;
  std::size_t frame_size_ = 0;
  std::size_t scratch_base_ = 0;
  bool has_scratch_ = false;
  bool indirect_return_ = false;
  std::size_t return_pointer_offset_ = 0;
  const Instruction * current_ = 0;
};

const FrameEntity & FunctionEmitter::temp(const std::string & name) const
{
  std::unordered_map<std::string, FrameEntity>::const_iterator found = temps_.find(name);
  if(found == temps_.end()) {
    throw ParseError("undefined temporary '%" + name + "'");
  }
  return found->second;
}

const FrameEntity & FunctionEmitter::slot(const std::string & name) const
{
  std::unordered_map<std::string, FrameEntity>::const_iterator found = slots_.find(name);
  if(found == slots_.end()) {
    throw ParseError("undefined stack slot '$" + name + "'");
  }
  return found->second;
}

std::string FunctionEmitter::frame_mem(std::size_t offset) const
{
  if(offset == 0) {
    return "[bp]";
  }
  return "[bp-" + unum(offset) + "]";
}

std::string FunctionEmitter::scratch_mem(std::size_t index, std::size_t byte_offset) const
{
  const std::size_t base = scratch_base_ + kScratchSlotBytes * (index + 1);
  return frame_mem(base - byte_offset);
}

std::string FunctionEmitter::block_label(const std::string & label) const
{
  return "fn__" + function_.name + "__" + label;
}

std::string FunctionEmitter::dest_mem() const
{
  return frame_mem(temp(current_->dest).offset);
}

LowType FunctionEmitter::operand_type(const Operand & operand) const
{
  if(operand.kind == Operand::OP_TEMP) {
    return temp(operand.text).type;
  }
  if(operand.kind == Operand::OP_SLOT || operand.kind == Operand::OP_GLOBAL) {
    return make_type("ptr");
  }
  return make_type("i64");
}

bool FunctionEmitter::is_address_form(const LowType & expected, const Operand & operand) const
{
  if(operand.kind == Operand::OP_SLOT || operand.kind == Operand::OP_GLOBAL) {
    return true;
  }
  if(operand.kind == Operand::OP_TEMP && lowir_model::is_memory_class_type(expected)) {
    return temp(operand.text).memory_class;
  }
  return false;
}

void FunctionEmitter::load_scalar(char base, const std::string & mem, int width)
{
  if(width < 32) {
    emit("move64 " + reg(base, 64) + " 0");
  }
  emit("move" + num(width) + " " + reg(base, width) + " " + mem);
}

void FunctionEmitter::sign_extend_to_64(char base, const LowType & type)
{
  if(!is_signed_narrow(type)) {
    return;
  }
  const int shift = 64 - scalar_width(type);
  emit("move8 t8 " + num(shift));
  emit("lshift64 " + reg(base, 64) + " " + reg(base, 64) + " t8");
  emit("srshift64 " + reg(base, 64) + " " + reg(base, 64) + " t8");
}

void FunctionEmitter::store_from_register(char base, const LowType & type, const std::string & mem)
{
  const int width = scalar_width(type);
  emit("move" + num(width) + " " + mem + " " + reg(base, width));
}

void FunctionEmitter::materialize_address(char base, const Operand & operand)
{
  switch(operand.kind) {
    case Operand::OP_TEMP: {
      const FrameEntity & entity = temp(operand.text);
      if(entity.memory_class) {
        emit("isub64 " + reg(base, 64) + " bp " + unum(entity.offset));
      } else {
        emit("move64 " + reg(base, 64) + " " + frame_mem(entity.offset));
      }
      return;
    }
    case Operand::OP_SLOT:
      emit("isub64 " + reg(base, 64) + " bp " + unum(slot(operand.text).offset));
      return;
    case Operand::OP_GLOBAL:
      emit("move64 " + reg(base, 64) + " " + symbols_.label_for(operand.text));
      return;
    default:
      emit("move64 " + reg(base, 64) + " " + operand.text);
      return;
  }
}

// A `<storage>` operand always names a location: a temporary holds the address
// in its own frame cell, while a slot or global names the location directly.
void FunctionEmitter::materialize_storage_pointer(char base, const Operand & operand)
{
  if(operand.kind == Operand::OP_TEMP) {
    emit("move64 " + reg(base, 64) + " " + frame_mem(temp(operand.text).offset));
    return;
  }
  materialize_address(base, operand);
}

void FunctionEmitter::materialize(char base, const LowType & expected, const Operand & operand)
{
  if(lowir_model::is_memory_class_type(expected)) {
    materialize_address(base, operand);
    return;
  }
  const int width = scalar_width(expected);
  switch(operand.kind) {
    case Operand::OP_TEMP:
      load_scalar(base, frame_mem(temp(operand.text).offset), width);
      return;
    case Operand::OP_SLOT:
      emit("isub64 " + reg(base, 64) + " bp " + unum(slot(operand.text).offset));
      return;
    case Operand::OP_GLOBAL:
      emit("move64 " + reg(base, 64) + " " + symbols_.label_for(operand.text));
      return;
    default: {
      const int literal_width = is_float_type(expected) ? width : 64;
      emit("move" + num(literal_width) + " " + reg(base, literal_width) + " " + operand.text);
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// 80-bit float helpers
// ---------------------------------------------------------------------------

void FunctionEmitter::f80_pad_scratch(std::size_t index)
{
  emit("move64 z64 0");
  emit("move32 " + scratch_mem(index, 10) + " z32");
  emit("move16 " + scratch_mem(index, 14) + " z16");
}

void FunctionEmitter::f80_literal_to_scratch(std::size_t index, const std::string & literal)
{
  emit("move80 " + scratch_mem(index, 0) + " " + literal);
  f80_pad_scratch(index);
}

void FunctionEmitter::f80_pointer_to_scratch(std::size_t index, char pointer)
{
  emit("move64 z64 [" + reg(pointer, 64) + "]");
  emit("move64 " + scratch_mem(index, 0) + " z64");
  emit("move64 z64 [" + reg(pointer, 64) + "+8]");
  emit("move64 " + scratch_mem(index, 8) + " z64");
}

void FunctionEmitter::f80_operand_to_scratch(std::size_t index, const Operand & operand)
{
  if(operand.kind == Operand::OP_INTEGER || operand.kind == Operand::OP_FLOAT) {
    f80_literal_to_scratch(index, operand.text);
    return;
  }
  materialize_address('x', operand);
  f80_pointer_to_scratch(index, 'x');
}

void FunctionEmitter::f80_storage_to_scratch(std::size_t index, const Operand & operand)
{
  materialize_storage_pointer('x', operand);
  f80_pointer_to_scratch(index, 'x');
}

void FunctionEmitter::f80_scratch_to_frame(std::size_t index, std::size_t offset)
{
  emit("move64 z64 " + scratch_mem(index, 0));
  emit("move64 " + frame_mem(offset) + " z64");
  emit("move64 z64 " + scratch_mem(index, 8));
  emit("move64 " + frame_mem(offset - 8) + " z64");
}

void FunctionEmitter::f80_scratch_to_pointer(std::size_t index, char pointer)
{
  emit("move64 z64 " + scratch_mem(index, 0));
  emit("move64 [" + reg(pointer, 64) + "] z64");
  emit("move64 z64 " + scratch_mem(index, 8));
  emit("move64 [" + reg(pointer, 64) + "+8] z64");
}

void FunctionEmitter::block_copy(std::size_t bytes, char destination, char source)
{
  std::size_t done = 0;
  std::size_t previous = 0;
  while(done < bytes) {
    const std::size_t chunk = next_chunk_bytes(bytes - done);
    const int width = static_cast<int>(chunk) * 8;
    if(done != 0) {
      emit("iadd64 " + reg(destination, 64) + " " + reg(destination, 64) + " " + unum(previous));
      emit("iadd64 " + reg(source, 64) + " " + reg(source, 64) + " " + unum(previous));
    }
    emit("move" + num(width) + " " + reg('z', width) + " [" + reg(source, 64) + "]");
    emit("move" + num(width) + " [" + reg(destination, 64) + "] " + reg('z', width));
    previous = chunk;
    done += chunk;
  }
}

// ---------------------------------------------------------------------------
// Frame layout
// ---------------------------------------------------------------------------

void FunctionEmitter::build_layout()
{
  std::size_t offset = 0;
  bool needs_scratch = false;
  if(lowir_model::is_memory_class_type(function_.return_type)) {
    indirect_return_ = true;
    offset += 8;
    return_pointer_offset_ = offset;
    FrameEntity entity;
    entity.offset = offset;
    entity.type = make_type("ptr");
    incoming_.push_back(entity);
  }
  if(function_.return_type.text == "f80") {
    needs_scratch = true;
  }
  for(std::size_t i = 0; i < function_.params.size(); ++i) {
    const Parameter & param = function_.params[i];
    FrameEntity entity;
    offset += frame_cell_size(param.type);
    entity.offset = offset;
    entity.type = param.type;
    entity.memory_class = lowir_model::is_memory_class_type(param.type);
    temps_[param.name] = entity;
    incoming_.push_back(entity);
    if(param.type.text == "f80") {
      needs_scratch = true;
    }
  }
  for(std::size_t i = 0; i < function_.slots.size(); ++i) {
    FrameEntity entity;
    offset += frame_cell_size(function_.slots[i].second);
    entity.offset = offset;
    entity.type = function_.slots[i].second;
    entity.memory_class = lowir_model::is_memory_class_type(entity.type);
    slots_[function_.slots[i].first] = entity;
    if(entity.type.text == "f80") {
      needs_scratch = true;
    }
  }
  for(std::size_t b = 0; b < function_.blocks.size(); ++b) {
    const std::vector<Instruction> & instructions = function_.blocks[b].instructions;
    for(std::size_t i = 0; i < instructions.size(); ++i) {
      const Instruction & instruction = instructions[i];
      if(instruction.kind == Instruction::IK_CONVERT ||
         instruction.type.text == "f80" || instruction.source_type.text == "f80") {
        needs_scratch = true;
      }
      if(instruction.dest.empty() || temps_.find(instruction.dest) != temps_.end()) {
        continue;
      }
      FrameEntity entity;
      entity.type = destination_type(instruction);
      offset += frame_cell_size(entity.type);
      entity.offset = offset;
      entity.memory_class = lowir_model::is_memory_class_type(entity.type);
      temps_[instruction.dest] = entity;
    }
  }
  scratch_base_ = offset;
  has_scratch_ = needs_scratch;
  frame_size_ = offset + (needs_scratch ? kScratchSlots * kScratchSlotBytes : 0);
}

void FunctionEmitter::emit_incoming_parameter(std::size_t index,
                                              const FrameEntity & entity,
                                              std::size_t & stack_index)
{
  if(index < 4) {
    const char source = kArgRegisters[index];
    if(!entity.memory_class) {
      emit("move64 " + frame_mem(entity.offset) + " " + reg(source, 64));
      return;
    }
    emit("move64 x64 " + reg(source, 64));
  } else {
    const std::size_t displacement = 16 + 8 * stack_index;
    ++stack_index;
    emit("move64 x64 [bp+" + unum(displacement) + "]");
    if(!entity.memory_class) {
      emit("move64 " + frame_mem(entity.offset) + " x64");
      return;
    }
  }
  const std::size_t bytes = facts_of(entity.type).size;
  std::size_t done = 0;
  while(done < bytes) {
    const std::size_t chunk = next_chunk_bytes(bytes - done);
    const int width = static_cast<int>(chunk) * 8;
    const std::string source_mem = done == 0 ? "[x64]" : ("[x64+" + unum(done) + "]");
    emit("move" + num(width) + " " + reg('z', width) + " " + source_mem);
    emit("move" + num(width) + " " + frame_mem(entity.offset - done) + " " + reg('z', width));
    done += chunk;
  }
}

void FunctionEmitter::emit_prologue()
{
  emit_label("fn__" + function_.name);
  emit("isub64 sp sp 8");
  emit("move64 [sp] bp");
  emit("move64 bp sp");
  if(frame_size_ != 0) {
    emit("isub64 sp sp " + unum(frame_size_));
  }
  std::size_t stack_index = 0;
  for(std::size_t i = 0; i < incoming_.size(); ++i) {
    emit_incoming_parameter(i, incoming_[i], stack_index);
  }
}

void FunctionEmitter::emit_epilogue()
{
  emit_label("fn__" + function_.name + "__epilogue");
  emit("move64 sp bp");
  emit("move64 bp [sp]");
  emit("iadd64 sp sp 8");
  emit("ret");
}

void FunctionEmitter::run()
{
  build_layout();
  emit_prologue();
  for(std::size_t b = 0; b < function_.blocks.size(); ++b) {
    const Block & block = function_.blocks[b];
    emit_label(block_label(block.label));
    for(std::size_t i = 0; i < block.instructions.size(); ++i) {
      emit_instruction(block.instructions[i]);
    }
  }
  emit_epilogue();
}

// ---------------------------------------------------------------------------
// Instruction selection
// ---------------------------------------------------------------------------

std::string binary_opcode(const std::string & op, const LowType & type, int width)
{
  const bool fp = is_float_type(type);
  const std::string suffix = num(width);
  if(op == "add") { return (fp ? std::string("fadd") : std::string("iadd")) + suffix; }
  if(op == "sub") { return (fp ? std::string("fsub") : std::string("isub")) + suffix; }
  if(op == "mul") { return (fp ? std::string("fmul") : std::string("smul")) + suffix; }
  if(op == "div") { return (fp ? std::string("fdiv") : std::string("sdiv")) + suffix; }
  if(op == "mod") { return "smod" + suffix; }
  if(op == "udiv") { return "udiv" + suffix; }
  if(op == "umod") { return "umod" + suffix; }
  if(op == "and") { return "and" + suffix; }
  if(op == "or") { return "or" + suffix; }
  if(op == "xor") { return "xor" + suffix; }
  if(op == "shl") { return "lshift" + suffix; }
  if(op == "shr") { return "srshift" + suffix; }
  if(op == "ushr") { return "urshift" + suffix; }
  throw ParseError("binary operator '" + op + "' is not available for type '" + type.text + "'");
}

std::string compare_opcode(const std::string & pred, const LowType & type, int width)
{
  const TypeFacts type_facts = facts_of(type);
  const bool fp = type_facts.category == TypeFacts::TC_FLOAT;
  const bool unsigned_default = type_facts.category == TypeFacts::TC_POINTER;
  std::string base;
  if(pred == "eq") { base = fp ? "feq" : "ieq"; }
  else if(pred == "ne") { base = fp ? "fne" : "ine"; }
  else if(pred == "lt") { base = fp ? "flt" : (unsigned_default ? "ult" : "slt"); }
  else if(pred == "le") { base = fp ? "fle" : (unsigned_default ? "ule" : "sle"); }
  else if(pred == "gt") { base = fp ? "fgt" : (unsigned_default ? "ugt" : "sgt"); }
  else if(pred == "ge") { base = fp ? "fge" : (unsigned_default ? "uge" : "sge"); }
  else if(pred == "ult") { base = fp ? "flt" : "ult"; }
  else if(pred == "ule") { base = fp ? "fle" : "ule"; }
  else if(pred == "ugt") { base = fp ? "fgt" : "ugt"; }
  else if(pred == "uge") { base = fp ? "fge" : "uge"; }
  else { throw ParseError("unknown compare predicate '" + pred + "'"); }
  return base + num(width);
}

void FunctionEmitter::emit_instruction(const Instruction & instruction)
{
  current_ = &instruction;
  switch(instruction.kind) {
    case Instruction::IK_CONST:
    case Instruction::IK_COPY:
    case Instruction::IK_ADDR:
      emit_value_instruction(instruction);
      return;
    case Instruction::IK_LOAD:
      emit_load(instruction);
      return;
    case Instruction::IK_STORE:
      emit_store(instruction);
      return;
    case Instruction::IK_INDEX:
      emit_index(instruction);
      return;
    case Instruction::IK_ATOMIC_LOAD:
    case Instruction::IK_ATOMIC_STORE:
    case Instruction::IK_ATOMIC_EXCHANGE:
    case Instruction::IK_ATOMIC_ADD_FETCH:
      emit_atomic_instruction(instruction);
      return;
    case Instruction::IK_ATOMIC_COMPARE_EXCHANGE:
      emit_compare_exchange(instruction);
      return;
    case Instruction::IK_ATOMIC_THREAD_FENCE:
    case Instruction::IK_ATOMIC_SIGNAL_FENCE:
      return;
    case Instruction::IK_UNARY:
      emit_unary(instruction);
      return;
    case Instruction::IK_BINARY:
      emit_binary(instruction);
      return;
    case Instruction::IK_CMP:
      emit_compare(instruction);
      return;
    case Instruction::IK_CONVERT:
      emit_convert(instruction);
      return;
    case Instruction::IK_CALL:
      emit_call(instruction);
      return;
    case Instruction::IK_COPYOBJ:
    case Instruction::IK_ZEROINIT:
    case Instruction::IK_STACK_ALLOC:
      emit_bulk_instruction(instruction);
      return;
    case Instruction::IK_JUMP:
    case Instruction::IK_BRANCH:
    case Instruction::IK_SWITCH:
    case Instruction::IK_RETURN:
    case Instruction::IK_THROW:
    case Instruction::IK_RESUME:
      emit_terminator(instruction);
      return;
    default:
      emit_exception_instruction(instruction);
      return;
  }
}

void FunctionEmitter::emit_value_instruction(const Instruction & instruction)
{
  if(instruction.kind == Instruction::IK_ADDR) {
    materialize_address('x', instruction.first);
    emit("move64 " + dest_mem() + " x64");
    return;
  }
  const LowType & type = instruction.type;
  if(type.text == "f80") {
    f80_operand_to_scratch(0, instruction.first);
    f80_scratch_to_frame(0, temp(instruction.dest).offset);
    return;
  }
  if(lowir_model::is_memory_class_type(type)) {
    materialize_address('y', instruction.first);
    emit("isub64 x64 bp " + unum(temp(instruction.dest).offset));
    block_copy(facts_of(type).size, 'x', 'y');
    return;
  }
  materialize('x', type, instruction.first);
  store_scalar_result(type);
}

void FunctionEmitter::emit_load(const Instruction & instruction)
{
  const LowType & type = instruction.type;
  if(type.text == "f80") {
    f80_storage_to_scratch(0, instruction.first);
    f80_scratch_to_frame(0, temp(instruction.dest).offset);
    return;
  }
  if(lowir_model::is_memory_class_type(type)) {
    materialize_storage_pointer('y', instruction.first);
    emit("isub64 x64 bp " + unum(temp(instruction.dest).offset));
    block_copy(facts_of(type).size, 'x', 'y');
    return;
  }
  std::string mem;
  if(instruction.first.kind == Operand::OP_SLOT) {
    mem = frame_mem(slot(instruction.first.text).offset);
  } else if(instruction.first.kind == Operand::OP_GLOBAL) {
    mem = "[" + symbols_.label_for(instruction.first.text) + "]";
  } else {
    // A sub-32-bit load zero-extends by clearing the whole destination
    // register first, so an address held in that same register would be lost.
    const char pointer = scalar_width(type) < 32 ? 'y' : 'x';
    materialize_storage_pointer(pointer, instruction.first);
    mem = "[" + reg(pointer, 64) + "]";
  }
  load_scalar('x', mem, scalar_width(type));
  sign_extend_to_64('x', type);
  store_scalar_result(type);
}

void FunctionEmitter::emit_store(const Instruction & instruction)
{
  const LowType & type = instruction.type;
  if(type.text == "f80") {
    f80_operand_to_scratch(0, instruction.first);
    materialize_storage_pointer('x', instruction.second);
    f80_scratch_to_pointer(0, 'x');
    return;
  }
  if(lowir_model::is_memory_class_type(type)) {
    materialize_storage_pointer('x', instruction.second);
    materialize_address('y', instruction.first);
    block_copy(facts_of(type).size, 'x', 'y');
    return;
  }
  materialize('x', type, instruction.first);
  const int width = scalar_width(type);
  if(instruction.second.kind == Operand::OP_SLOT) {
    emit("move" + num(width) + " " + frame_mem(slot(instruction.second.text).offset) +
         " " + reg('x', width));
    return;
  }
  if(instruction.second.kind == Operand::OP_GLOBAL) {
    emit("move" + num(width) + " [" + symbols_.label_for(instruction.second.text) + "] " +
         reg('x', width));
    return;
  }
  materialize_address('y', instruction.second);
  emit("move" + num(width) + " [y64] " + reg('x', width));
}

void FunctionEmitter::emit_index(const Instruction & instruction)
{
  materialize('y', make_type("ptr"), instruction.first);
  materialize('x', make_type("i64"), instruction.second);
  const std::size_t element = facts_of(instruction.type).size;
  if(element != 1) {
    emit("move64 z64 " + unum(element));
    emit("smul64 x64 x64 z64");
  }
  emit("iadd64 x64 y64 x64");
  emit("move64 " + dest_mem() + " x64");
}

void FunctionEmitter::emit_atomic_instruction(const Instruction & instruction)
{
  const LowType & type = instruction.type;
  const int width = scalar_width(type);
  if(instruction.kind == Instruction::IK_ATOMIC_STORE) {
    materialize('y', make_type("ptr"), instruction.second);
    materialize('x', type, instruction.first);
    emit("move" + num(width) + " [y64] " + reg('x', width));
    return;
  }
  materialize('y', make_type("ptr"), instruction.first);
  if(instruction.kind == Instruction::IK_ATOMIC_LOAD) {
    load_scalar('x', "[y64]", width);
    sign_extend_to_64('x', type);
    store_scalar_result(type);
    return;
  }
  if(instruction.kind == Instruction::IK_ATOMIC_EXCHANGE) {
    materialize('x', type, instruction.second);
    load_scalar('t', "[y64]", width);
    emit("move" + num(width) + " [y64] " + reg('x', width));
    emit("move64 x64 0");
    emit("move" + num(width) + " " + reg('x', width) + " " + reg('t', width));
    store_scalar_result(type);
    return;
  }
  load_scalar('x', "[y64]", width);
  materialize('z', type, instruction.second);
  emit("iadd" + num(width) + " " + reg('x', width) + " " + reg('x', width) + " " +
       reg('z', width));
  emit("move" + num(width) + " [y64] " + reg('x', width));
  store_scalar_result(type);
}

void FunctionEmitter::emit_compare_exchange(const Instruction & instruction)
{
  const LowType & type = instruction.type;
  const int width = scalar_width(type);
  const long long success = label_counter_++;
  const long long done = label_counter_++;
  materialize('y', make_type("ptr"), instruction.first);
  materialize('z', make_type("ptr"), instruction.second);
  load_scalar('t', "[y64]", width);
  load_scalar('x', "[z64]", width);
  emit("ieq" + num(width) + " x8 " + reg('t', width) + " " + reg('x', width));
  emit("jumpif x8 __atomic_cmpxchg_success__" + num(success));
  emit("move" + num(width) + " [z64] " + reg('t', width));
  emit("move64 x64 0");
  emit("move64 " + dest_mem() + " x64");
  emit("jump __atomic_cmpxchg_end__" + num(done));
  emit_label("__atomic_cmpxchg_success__" + num(success));
  materialize('x', type, instruction.third);
  emit("move" + num(width) + " [y64] " + reg('x', width));
  emit("move64 x64 1");
  emit("move64 " + dest_mem() + " x64");
  emit_label("__atomic_cmpxchg_end__" + num(done));
}

void FunctionEmitter::emit_unary(const Instruction & instruction)
{
  const LowType & type = instruction.type;
  if(type.text == "f80") {
    if(instruction.op != "neg") {
      throw ParseError("'unary " + instruction.op + "' is not available for f80");
    }
    f80_operand_to_scratch(0, instruction.first);
    f80_literal_to_scratch(1, "0.0L");
    emit("fsub80 " + scratch_mem(2, 0) + " " + scratch_mem(1, 0) + " " + scratch_mem(0, 0));
    f80_pad_scratch(2);
    f80_scratch_to_frame(2, temp(instruction.dest).offset);
    return;
  }
  const int width = scalar_width(type);
  materialize('x', type, instruction.first);
  if(instruction.op == "neg") {
    if(is_float_type(type)) {
      emit("move" + num(width) + " " + reg('y', width) + " 0");
      emit("fsub" + num(width) + " " + reg('x', width) + " " + reg('y', width) + " " +
           reg('x', width));
    } else {
      emit("move64 y64 0");
      emit("isub" + num(width) + " " + reg('x', width) + " " + reg('y', width) + " " +
           reg('x', width));
    }
  } else if(instruction.op == "not") {
    emit("ieq" + num(width) + " z8 " + reg('x', width) + " 0");
    emit("move64 x64 0");
    emit("move8 x8 z8");
  } else if(instruction.op == "bitnot") {
    emit("not" + num(width) + " " + reg('x', width) + " " + reg('x', width));
  } else if(instruction.op == "bswap") {
    emit("bswap" + num(width) + " " + reg('x', width) + " " + reg('x', width));
  }
  store_scalar_result(type);
}

void FunctionEmitter::emit_binary(const Instruction & instruction)
{
  const LowType & type = instruction.type;
  if(type.text == "f80") {
    f80_operand_to_scratch(0, instruction.first);
    f80_operand_to_scratch(1, instruction.second);
    emit(binary_opcode(instruction.op, type, 80) + " " + scratch_mem(2, 0) + " " +
         scratch_mem(0, 0) + " " + scratch_mem(1, 0));
    f80_pad_scratch(2);
    f80_scratch_to_frame(2, temp(instruction.dest).offset);
    return;
  }
  const int width = scalar_width(type);
  materialize('y', type, instruction.first);
  materialize('x', type, instruction.second);
  const bool shift = instruction.op == "shl" || instruction.op == "shr" ||
                     instruction.op == "ushr";
  if(shift) {
    emit("move64 z64 x64");
    emit("move8 x8 z8");
    emit(binary_opcode(instruction.op, type, width) + " " + reg('x', width) + " " +
         reg('y', width) + " x8");
  } else {
    emit(binary_opcode(instruction.op, type, width) + " " + reg('x', width) + " " +
         reg('y', width) + " " + reg('x', width));
  }
  store_scalar_result(type);
}

void FunctionEmitter::emit_compare(const Instruction & instruction)
{
  const LowType & type = instruction.type;
  if(type.text == "f80") {
    f80_operand_to_scratch(0, instruction.first);
    f80_operand_to_scratch(1, instruction.second);
    emit(compare_opcode(instruction.op, type, 80) + " z8 " + scratch_mem(0, 0) + " " +
         scratch_mem(1, 0));
  } else {
    const int width = scalar_width(type);
    materialize('y', type, instruction.first);
    materialize('x', type, instruction.second);
    emit(compare_opcode(instruction.op, type, width) + " z8 " + reg('y', width) + " " +
         reg('x', width));
  }
  emit("move64 x64 0");
  emit("move8 x8 z8");
  emit("move64 " + dest_mem() + " x64");
}

void FunctionEmitter::emit_convert_source(const Instruction & instruction)
{
  const LowType & source = instruction.source_type;
  if(source.text == "f80") {
    f80_operand_to_scratch(0, instruction.first);
    return;
  }
  const int width = scalar_width(source);
  materialize('x', source, instruction.first);
  std::string opcode;
  if(is_float_type(source)) {
    opcode = "f" + num(width) + "convf80";
  } else if(instruction.op == "uitofp") {
    opcode = "u" + num(width) + "convf80";
  } else {
    opcode = "s" + num(width) + "convf80";
  }
  emit(opcode + " " + scratch_mem(0, 0) + " " + reg('x', width));
  f80_pad_scratch(0);
}

void FunctionEmitter::emit_convert(const Instruction & instruction)
{
  const LowType & destination = instruction.type;
  const LowType & source = instruction.source_type;
  const bool integral = !is_float_type(destination) && !is_float_type(source);
  if(integral) {
    materialize('x', source, instruction.first);
    if(instruction.op == "sext") {
      const int shift = 64 - scalar_width(source);
      if(shift > 0) {
        emit("move8 t8 " + num(shift));
        emit("lshift64 x64 x64 t8");
        emit("srshift64 x64 x64 t8");
      }
    }
    store_scalar_result(destination);
    return;
  }
  emit_convert_source(instruction);
  if(destination.text == "f80") {
    f80_scratch_to_frame(0, temp(instruction.dest).offset);
    return;
  }
  const int width = scalar_width(destination);
  std::string opcode;
  if(is_float_type(destination)) {
    opcode = "f80convf" + num(width);
  } else if(instruction.op == "fptoui") {
    opcode = "f80convu" + num(width);
  } else {
    opcode = "f80convs" + num(width);
  }
  emit(opcode + " " + dest_mem() + " " + scratch_mem(0, 0));
}

void FunctionEmitter::emit_argument(std::size_t index,
                                    const LowType & type,
                                    const Operand & operand)
{
  const bool address = is_address_form(type, operand);
  if(index < 4) {
    const char target = kArgRegisters[index];
    if(address) {
      materialize('x', type, operand);
      emit("move64 " + reg(target, 64) + " x64");
    } else {
      materialize(target, type, operand);
    }
    return;
  }
  const std::size_t slot_offset = 8 * (index - 4);
  const std::string mem = slot_offset == 0 ? "[sp]" : ("[sp+" + unum(slot_offset) + "]");
  materialize('x', type, operand);
  const int width =
    (address || lowir_model::is_memory_class_type(type)) ? 64 : scalar_width(type);
  emit("move64 " + mem + " 0");
  emit("move" + num(width) + " " + mem + " " + reg('x', width));
}

void FunctionEmitter::emit_call(const Instruction & instruction)
{
  const bool direct = instruction.first.kind == Operand::OP_GLOBAL &&
                      symbols_.is_function(instruction.first.text);
  const bool memory_result = !instruction.call_returns_void &&
                             lowir_model::is_memory_class_type(instruction.type);
  const std::vector<Parameter> * params = 0;
  if(instruction.has_call_signature) {
    params = &instruction.call_params;
  } else if(direct) {
    std::unordered_map<std::string, const Function *>::const_iterator definition =
      symbols_.definitions.find(instruction.first.text);
    if(definition != symbols_.definitions.end()) {
      params = &definition->second->params;
    } else {
      std::unordered_map<std::string, const FunctionDeclaration *>::const_iterator declared =
        symbols_.declarations.find(instruction.first.text);
      if(declared != symbols_.declarations.end()) {
        params = &declared->second->params;
      }
    }
  }
  const std::size_t total = instruction.args.size() + (memory_result ? 1 : 0);
  const std::size_t stack_args = total > 4 ? total - 4 : 0;
  std::size_t pop = 0;
  if(!direct) {
    materialize('x', make_type("ptr"), instruction.first);
    emit("isub64 sp sp 8");
    emit("move64 [sp] x64");
    pop += 8;
  }
  if(stack_args != 0) {
    emit("isub64 sp sp " + unum(8 * stack_args));
    pop += 8 * stack_args;
  }
  std::size_t index = 0;
  if(memory_result) {
    Operand hidden;
    hidden.kind = Operand::OP_TEMP;
    hidden.text = instruction.dest;
    emit_argument(index, instruction.type, hidden);
    ++index;
  }
  for(std::size_t i = 0; i < instruction.args.size(); ++i, ++index) {
    const LowType type = (params != 0 && i < params->size())
      ? (*params)[i].type : operand_type(instruction.args[i]);
    emit_argument(index, type, instruction.args[i]);
  }
  if(direct) {
    emit("call fn__" + instruction.first.text);
  } else if(stack_args != 0) {
    emit("call [sp+" + unum(8 * stack_args) + "]");
  } else {
    emit("call [sp]");
  }
  if(pop != 0) {
    emit("iadd64 sp sp " + unum(pop));
  }
  if(!instruction.call_returns_void && !memory_result) {
    store_scalar_result(instruction.type);
  }
}

void FunctionEmitter::emit_bulk_instruction(const Instruction & instruction)
{
  if(instruction.kind == Instruction::IK_STACK_ALLOC) {
    emit("isub64 sp sp " + unum(instruction.byte_count));
    emit("move64 x64 sp");
    emit("move64 " + dest_mem() + " x64");
    return;
  }
  if(instruction.kind == Instruction::IK_COPYOBJ) {
    materialize_address('x', instruction.second);
    materialize_address('y', instruction.first);
    block_copy(instruction.byte_count, 'x', 'y');
    return;
  }
  materialize_address('x', instruction.first);
  emit("move64 z64 0");
  std::size_t done = 0;
  std::size_t previous = 0;
  while(done < instruction.byte_count) {
    const std::size_t chunk = next_chunk_bytes(instruction.byte_count - done);
    const int width = static_cast<int>(chunk) * 8;
    if(done != 0) {
      emit("iadd64 x64 x64 " + unum(previous));
    }
    emit("move" + num(width) + " [x64] " + reg('z', width));
    previous = chunk;
    done += chunk;
  }
}

void FunctionEmitter::emit_exception_instruction(const Instruction & instruction)
{
  if(instruction.kind == Instruction::IK_EH_TRY ||
     instruction.kind == Instruction::IK_EH_CLEANUP) {
    emit("isub64 sp sp 32");
    emit("move64 z64 [" + symbols_.eh_top_label + "]");
    emit("move64 [sp] z64");
    emit("move64 z64 " + block_label(instruction.first.text));
    emit("move64 [sp+8] z64");
    emit("move64 [sp+16] bp");
    emit("move64 z64 sp");
    emit("iadd64 z64 z64 32");
    emit("move64 [sp+24] z64");
    emit("move64 z64 sp");
    emit("move64 [" + symbols_.eh_top_label + "] z64");
    return;
  }
  if(instruction.kind == Instruction::IK_EH_END) {
    emit("move64 x64 [" + symbols_.eh_top_label + "]");
    emit("move64 y64 [x64]");
    emit("move64 [" + symbols_.eh_top_label + "] y64");
    emit("move64 sp x64");
    emit("iadd64 sp sp 32");
    return;
  }
  if(instruction.kind == Instruction::IK_EXCEPTION) {
    load_scalar('x', "[" + symbols_.eh_value_label + "]", scalar_width(instruction.type));
    sign_extend_to_64('x', instruction.type);
    store_scalar_result(instruction.type);
    return;
  }
  if(instruction.kind == Instruction::IK_EXCEPTION_SELECTOR) {
    emit("move64 x64 0");
    emit("move64 " + dest_mem() + " x64");
  }
}

void FunctionEmitter::emit_unwind()
{
  const long long handler = label_counter_++;
  const long long unhandled = label_counter_++;
  emit("move64 x64 [" + symbols_.eh_top_label + "]");
  emit("ieq64 z8 x64 0");
  emit("jumpif z8 __eh_unhandled__" + num(unhandled));
  emit_label("__eh_handler__" + num(handler));
  emit("move64 y64 [x64]");
  emit("move64 [" + symbols_.eh_top_label + "] y64");
  emit("move64 z64 [x64+8]");
  emit("move64 bp [x64+16]");
  emit("move64 sp [x64+24]");
  emit("jump z64");
  emit_label("__eh_unhandled__" + num(unhandled));
  emit("move64 x64 [" + symbols_.eh_value_label + "]");
  emit("call " + symbols_.eh_unhandled_label);
  emit("syscall1 t64 60 x64");
  out_ += "\n";
}

void FunctionEmitter::emit_switch(const Instruction & instruction)
{
  LowType type = operand_type(instruction.first);
  if(lowir_model::is_memory_class_type(type)) {
    type = make_type("i64");
  }
  const int width = scalar_width(type);
  materialize('x', type, instruction.first);
  for(std::size_t i = 0; i < instruction.switch_cases.size(); ++i) {
    materialize('t', type, instruction.switch_cases[i].value);
    emit("ieq" + num(width) + " z8 " + reg('x', width) + " " + reg('t', width));
    emit("jumpif z8 " + block_label(instruction.switch_cases[i].label));
  }
  emit("jump " + block_label(instruction.second.text));
}

void FunctionEmitter::emit_return(const Instruction & instruction)
{
  const std::string epilogue = "fn__" + function_.name + "__epilogue";
  if(instruction.type.text == "void") {
    emit("jump " + epilogue);
    return;
  }
  if(instruction.type.text == "f80") {
    f80_operand_to_scratch(0, instruction.first);
    emit("move64 x64 " + frame_mem(return_pointer_offset_));
    f80_scratch_to_pointer(0, 'x');
    emit("jump " + epilogue);
    return;
  }
  if(lowir_model::is_memory_class_type(instruction.type)) {
    materialize_address('x', instruction.first);
    emit("move64 y64 " + frame_mem(return_pointer_offset_));
    const std::size_t bytes = facts_of(instruction.type).size;
    std::size_t done = 0;
    while(done < bytes) {
      const std::size_t chunk = next_chunk_bytes(bytes - done);
      const int width = static_cast<int>(chunk) * 8;
      const std::string suffix = done == 0 ? std::string("]") : ("+" + unum(done) + "]");
      emit("move" + num(width) + " " + reg('z', width) + " [x64" + suffix);
      emit("move" + num(width) + " [y64" + suffix + " " + reg('z', width));
      done += chunk;
    }
    emit("jump " + epilogue);
    return;
  }
  materialize('x', instruction.type, instruction.first);
  emit("jump " + epilogue);
}

void FunctionEmitter::emit_terminator(const Instruction & instruction)
{
  if(instruction.kind == Instruction::IK_JUMP) {
    emit("jump " + block_label(instruction.first.text));
    return;
  }
  if(instruction.kind == Instruction::IK_BRANCH) {
    LowType type = operand_type(instruction.first);
    if(lowir_model::is_memory_class_type(type)) {
      type = make_type("i64");
    }
    const int width = scalar_width(type);
    materialize('x', type, instruction.first);
    emit("ieq" + num(width) + " z8 " + reg('x', width) + " 0");
    emit("jumpif z8 " + block_label(instruction.third.text));
    emit("jump " + block_label(instruction.second.text));
    return;
  }
  if(instruction.kind == Instruction::IK_SWITCH) {
    emit_switch(instruction);
    return;
  }
  if(instruction.kind == Instruction::IK_RETURN) {
    emit_return(instruction);
    return;
  }
  if(instruction.kind == Instruction::IK_THROW) {
    materialize('x', instruction.type, instruction.first);
    emit("move64 [" + symbols_.eh_value_label + "] x64");
  }
  emit_unwind();
}

// ---------------------------------------------------------------------------
// Static data
// ---------------------------------------------------------------------------

void emit_f80_data(const std::string & literal, std::string & out)
{
  const long double value = strtold(literal.c_str(), 0);
  unsigned char bytes[sizeof(long double)];
  memset(bytes, 0, sizeof(bytes));
  memcpy(bytes, &value, sizeof(long double));
  long long low = 0;
  unsigned short high = 0;
  if(sizeof(long double) >= 10) {
    memcpy(&low, bytes, 8);
    memcpy(&high, bytes + 8, 2);
  }
  out += "\tdata64 " + num(low) + ";\n";
  out += "\tdata16 " + unum(high) + ";\n";
  for(int i = 0; i < 6; ++i) {
    out += "\tdata8 0;\n";
  }
}

void emit_zero_bytes(std::size_t count, std::string & out)
{
  for(std::size_t i = 0; i < count; ++i) {
    out += "\tdata8 0;\n";
  }
}

void align_data(std::size_t alignment, std::size_t & offset, std::string & out)
{
  while(alignment != 0 && offset % alignment != 0) {
    out += "\tdata8 0;\n";
    ++offset;
  }
}

std::string address_data_text(const SymbolIndex & symbols,
                              const std::string & symbol,
                              long long addend)
{
  const std::string label = symbols.label_for(symbol);
  if(addend == 0) {
    return label;
  }
  if(addend > 0) {
    return "(" + label + "+" + num(addend) + ")";
  }
  return "(" + label + "-" + num(-addend) + ")";
}

void emit_structured_global(const GlobalDefinition & global,
                            const SymbolIndex & symbols,
                            std::string & out)
{
  std::size_t offset = 0;
  for(std::size_t i = 0; i < global.data_items.size(); ++i) {
    const GlobalDefinition::DataItem & item = global.data_items[i];
    if(item.kind == GlobalDefinition::DataItem::ITEM_ZERO) {
      emit_zero_bytes(item.zero_bytes, out);
      offset += item.zero_bytes;
      continue;
    }
    if(item.kind == GlobalDefinition::DataItem::ITEM_ADDR) {
      align_data(8, offset, out);
      out += "\tdata64 " + address_data_text(symbols, item.symbol, item.addr_addend) + ";\n";
      offset += 8;
      continue;
    }
    const TypeFacts item_facts = facts_of(item.type);
    align_data(item_facts.alignment, offset, out);
    if(item_facts.width_bits == 80) {
      emit_f80_data(item.literal_operand.text, out);
    } else {
      out += "\tdata" + num(item_facts.width_bits) + " " + item.literal_operand.text + ";\n";
    }
    offset += item_facts.size;
  }
}

void emit_global_definition(const GlobalDefinition & global,
                            const SymbolIndex & symbols,
                            std::string & out)
{
  out += "g__" + global.name + ":\n";
  if(global.structured) {
    emit_structured_global(global, symbols, out);
    return;
  }
  const TypeFacts global_facts = facts_of(global.type);
  if(global.init_kind == GlobalDefinition::INIT_ADDR) {
    out += "\tdata64 " +
      address_data_text(symbols, global.init_operand.text, global.addr_addend) + ";\n";
    return;
  }
  if(global_facts.width_bits == 80) {
    emit_f80_data(global.init_kind == GlobalDefinition::INIT_ZERO
                    ? std::string("0.0L") : global.init_operand.text, out);
    return;
  }
  if(global_facts.category == TypeFacts::TC_OBJECT) {
    emit_zero_bytes(global_facts.size, out);
    return;
  }
  const std::string text = global.init_kind == GlobalDefinition::INIT_ZERO
    ? std::string("0") : global.init_operand.text;
  out += "\tdata" + num(global_facts.width_bits) + " " + text + ";\n";
}

// ---------------------------------------------------------------------------
// Program assembly
// ---------------------------------------------------------------------------

SymbolIndex build_symbol_index(const Program & program)
{
  SymbolIndex symbols;
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i) {
    symbols.functions.insert(program.function_declarations[i].name);
    symbols.declarations[program.function_declarations[i].name] =
      &program.function_declarations[i];
  }
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    symbols.functions.insert(program.functions[i].name);
    symbols.definitions[program.functions[i].name] = &program.functions[i];
  }
  for(std::size_t i = 0; i < program.global_declarations.size(); ++i) {
    symbols.globals.insert(program.global_declarations[i].name);
  }
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    symbols.globals.insert(program.globals[i].name);
  }
  return symbols;
}

std::string role_owner_global(const Program & program, lowir_model::SymbolRole role)
{
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    if(program.globals[i].metadata.role == role) {
      return program.globals[i].name;
    }
  }
  for(std::size_t i = 0; i < program.global_declarations.size(); ++i) {
    if(program.global_declarations[i].metadata.role == role) {
      return program.global_declarations[i].name;
    }
  }
  return std::string();
}

std::string role_owner_function(const Program & program, lowir_model::SymbolRole role)
{
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    if(program.functions[i].metadata.role == role) {
      return program.functions[i].name;
    }
  }
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i) {
    if(program.function_declarations[i].metadata.role == role) {
      return program.function_declarations[i].name;
    }
  }
  return std::string();
}

bool program_uses_exceptions(const Program & program)
{
  for(std::size_t f = 0; f < program.functions.size(); ++f) {
    const Function & function = program.functions[f];
    for(std::size_t b = 0; b < function.blocks.size(); ++b) {
      const std::vector<Instruction> & instructions = function.blocks[b].instructions;
      for(std::size_t i = 0; i < instructions.size(); ++i) {
        if(uses_exceptions(instructions[i])) {
          return true;
        }
      }
    }
  }
  return false;
}

void emit_start_block(const Program & program, std::string & out)
{
  const lowir_model::RuntimeRoles roles = lowir_model::resolve_runtime_roles(program);
  out += "start:\n";
  out += "\tmove64 bp sp;\n";
  if(roles.init != 0) {
    out += "\tcall fn__" + roles.init->name + ";\n";
  }
  out += "\tcall fn__" + roles.entry->name + ";\n";
  if(roles.fini != 0) {
    out += "\tisub64 sp sp 8;\n";
    out += "\tmove64 [sp] x64;\n";
    out += "\tcall fn__" + roles.fini->name + ";\n";
    out += "\tmove64 x64 [sp];\n";
    out += "\tiadd64 sp sp 8;\n";
  }
  out += "\tsyscall1 t64 60 x64;\n";
}

}  // namespace

std::string emit_cy86_program(const Program & program)
{
  SymbolIndex symbols = build_symbol_index(program);
  const bool exceptions = program_uses_exceptions(program);

  std::string top = role_owner_global(program, lowir_model::SR_EH_TOP);
  std::string value = role_owner_global(program, lowir_model::SR_EH_VALUE);
  std::string unhandled = role_owner_function(program, lowir_model::SR_EH_UNHANDLED);
  const bool synthesize_top = top.empty();
  const bool synthesize_value = value.empty();
  const bool synthesize_unhandled = unhandled.empty();
  if(synthesize_top) { top = kEhTopGlobal; }
  if(synthesize_value) { value = kEhValueGlobal; }
  if(synthesize_unhandled) { unhandled = kEhUnhandledFunction; }
  symbols.eh_top_label = "g__" + top;
  symbols.eh_value_label = "g__" + value;
  symbols.eh_unhandled_label = "fn__" + unhandled;

  std::string out;
  long long label_counter = 0;
  emit_start_block(program, out);
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    out += "\n";
    FunctionEmitter emitter(program.functions[i], symbols, out, label_counter);
    emitter.run();
  }
  if(exceptions && synthesize_unhandled) {
    out += "\n";
    out += symbols.eh_unhandled_label + ":\n";
    out += "\tsyscall1 t64 60 x64;\n";
  }
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    out += "\n";
    emit_global_definition(program.globals[i], symbols, out);
  }
  if(exceptions && synthesize_top) {
    out += "\n";
    out += symbols.eh_top_label + ":\n\tdata64 0;\n";
  }
  if(exceptions && synthesize_value) {
    out += "\n";
    out += symbols.eh_value_label + ":\n\tdata64 0;\n";
  }
  return out;
}

}  // namespace lowir_cy86
