#include "lowir_model.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

// The LowIR text writer.
//
// `lowir_text.cpp` owns everything recoverable from LowIR source text; this
// owns the other direction, and is the only place that decides how a program
// in the typed model is spelled.  A producer builds `lowir_model::Program` and
// never writes a character of LowIR itself, so the canonical presentation of
// `pa13/lowir.md` - phase order, metadata order, block layout - is one
// description rather than one per producer.

namespace lowir_model {

namespace {

// The metadata keys of one entry, in the canonical order `pa13/lowir.md`
// writes them: what the entry is to the runtime first, then how it is called,
// then how it is named in an object file, then the storage it lives in.
class MetadataText
{
public:
	void add(const char* key, const std::string& value)
	{
		if (value.empty())
		{
			return;
		}
		if (!text_.empty())
		{
			text_ += ", ";
		}
		text_ += key;
		text_ += "=";
		text_ += value;
	}

	std::string result() const
	{
		return text_.empty() ? std::string() : " [" + text_ + "]";
	}

private:
	std::string text_;
};

const char* role_text(SymbolRole role)
{
	switch (role)
	{
	case SR_ENTRY: return "entry";
	case SR_INIT: return "init";
	case SR_FINI: return "fini";
	case SR_EH_TOP: return "eh_top";
	case SR_EH_VALUE: return "eh_value";
	case SR_EH_TYPE: return "eh_type";
	case SR_EH_UNHANDLED: return "eh_unhandled";
	case SR_EH_PERSONALITY: return "eh_personality";
	case SR_EH_RESUME: return "eh_resume";
	default: return "";
	}
}

const char* binding_text(SymbolBindingMode binding)
{
	switch (binding)
	{
	case SBM_INTERNAL: return "internal";
	case SBM_STRONG: return "strong";
	case SBM_WEAK: return "weak";
	default: return "";
	}
}

const char* storage_text(GlobalStorageMode storage)
{
	switch (storage)
	{
	case GSM_WRITABLE: return "writable";
	case GSM_READONLY: return "readonly";
	case GSM_THREAD_LOCAL: return "thread_local";
	default: return "";
	}
}

const char* projection_text(IndexProjectionKind projection)
{
	switch (projection)
	{
	case IPK_ARRAY_ELEMENT: return "array_element";
	case IPK_FIELD: return "field";
	case IPK_BASE_SUBOBJECT: return "base_subobject";
	case IPK_REFERENCE_FIELD: return "reference_field";
	default: return "";
	}
}

const char* passing_text(ParamPassingMode passing)
{
	switch (passing)
	{
	case PPM_INDIRECT_RESULT: return "indirect_result";
	case PPM_BY_ADDRESS: return "by_address";
	case PPM_REFERENCE: return "reference";
	case PPM_DECAY: return "decay";
	default: return "";
	}
}

void boundary_metadata(const FunctionBoundaryMetadata & boundary,
                       MetadataText & out)
{
	if (boundary.arity == CAM_VARIADIC)
	{
		out.add("arity", "variadic");
	}
	else if (boundary.arity == CAM_PROTOTYPE_RELAXED)
	{
		out.add("arity", "prototype_relaxed");
	}
	if (boundary.effects == CFXM_READNONE)
	{
		out.add("effects", "readnone");
	}
	else if (boundary.effects == CFXM_READONLY)
	{
		out.add("effects", "readonly");
	}
	else if (boundary.effects == CFXM_READWRITE)
	{
		out.add("effects", "readwrite");
	}
	if (boundary.unwind == CUM_NO)
	{
		out.add("unwind", "no");
	}
	else if (boundary.unwind == CUM_MAY)
	{
		out.add("unwind", "may");
	}
	if (boundary.returns == CRM_NORETURN)
	{
		out.add("return", "noreturn");
	}
}

void symbol_metadata(const SymbolMetadata & metadata, MetadataText & out)
{
	out.add("linkage", metadata.linkage == LLM_C
	                       ? "c"
	                       : (metadata.linkage == LLM_CPP ? "cpp" : ""));
	out.add("binding", binding_text(metadata.binding));
	out.add("object", metadata.object_symbol);
	out.add("tls_for", metadata.tls_for_symbol);
	if (metadata.keep_internal_alias)
	{
		out.add("keep_alias", "yes");
	}
	if (metadata.prefer_local_object_binding)
	{
		out.add("prefer_local", "yes");
	}
	if (metadata.object_trivial_lifecycle)
	{
		out.add("trivial_lifecycle", "yes");
	}
	if (metadata.force_inline)
	{
		out.add("force_inline", "yes");
	}
}

std::string parameter_text(const std::vector<Parameter> & params)
{
	std::string text;
	for (std::size_t index = 0; index < params.size(); ++index)
	{
		if (index != 0)
		{
			text += ", ";
		}
		text += "%" + params[index].name + " : " + params[index].type.text;
		MetadataText metadata;
		metadata.add("pass", passing_text(params[index].metadata.passing));
		if (params[index].metadata.capture == PCM_NOCAPTURE)
		{
			metadata.add("capture", "nocapture");
		}
		text += metadata.result();
	}
	return text;
}

std::string signature_text(const std::vector<Parameter> & params,
                           const LowType & result,
                           const FunctionBoundaryMetadata & boundary)
{
	MetadataText metadata;
	boundary_metadata(boundary, metadata);
	return "(" + parameter_text(params) + ") -> " + result.text +
	       metadata.result();
}

// An operand as LowIR writes it: the sigil that says what kind of storage or
// value it names, and the name the model holds without one.
std::string operand_text(const Operand & operand)
{
	switch (operand.kind)
	{
	case Operand::OP_TEMP: return "%" + operand.text;
	case Operand::OP_SLOT: return "$" + operand.text;
	case Operand::OP_GLOBAL: return "@" + operand.text;
	case Operand::OP_LABEL: return "^" + operand.text;
	default: break;
	}
	return operand.text;
}

std::string label_text(const Operand & operand)
{
	return "^" + operand.text;
}

std::string span_text(std::size_t bytes, std::size_t alignment)
{
	std::ostringstream out;
	out << bytes << "x" << alignment;
	return out.str();
}

std::string decimal(long long value)
{
	std::ostringstream out;
	out << value;
	return out.str();
}

void write_global_item(std::ostream & out,
                       const GlobalDefinition::DataItem & item)
{
	switch (item.kind)
	{
	case GlobalDefinition::DataItem::ITEM_ZERO:
		out << "  zero " << item.zero_bytes << "\n";
		return;
	case GlobalDefinition::DataItem::ITEM_ADDR:
		out << "  " << item.type.text << " addr @" << item.symbol;
		if (item.addr_addend != 0)
		{
			out << " + " << decimal(item.addr_addend);
		}
		out << "\n";
		return;
	default:
		break;
	}
	out << "  " << item.type.text << " " << item.literal_operand.text << "\n";
}

void write_global(std::ostream & out, const GlobalDefinition & global)
{
	MetadataText metadata;
	metadata.add("role", role_text(global.metadata.role));
	symbol_metadata(global.metadata, metadata);
	metadata.add("storage", storage_text(global.storage));
	out << "global @" << global.name;
	if (!global.structured)
	{
		out << " : " << global.type.text;
	}
	out << metadata.result() << " = ";
	if (global.structured)
	{
		out << "{\n";
		for (std::size_t index = 0; index < global.data_items.size(); ++index)
		{
			write_global_item(out, global.data_items[index]);
		}
		out << "}\n";
		return;
	}
	switch (global.init_kind)
	{
	case GlobalDefinition::INIT_ADDR:
		out << "addr @" << global.init_operand.text;
		if (global.addr_addend != 0)
		{
			out << " + " << decimal(global.addr_addend);
		}
		break;
	case GlobalDefinition::INIT_INTEGER:
		out << global.init_operand.text;
		break;
	default:
		out << "zero";
		break;
	}
	out << "\n";
}

void write_global_declaration(std::ostream & out,
                              const GlobalDeclaration & global)
{
	MetadataText metadata;
	metadata.add("role", role_text(global.metadata.role));
	symbol_metadata(global.metadata, metadata);
	metadata.add("storage", storage_text(global.storage));
	out << "declare global @" << global.name;
	if (global.has_type)
	{
		out << " : " << global.type.text;
	}
	out << metadata.result() << "\n";
}

void write_function_declaration(std::ostream & out,
                                const FunctionDeclaration & function)
{
	MetadataText metadata;
	metadata.add("role", role_text(function.metadata.role));
	boundary_metadata(function.boundary, metadata);
	symbol_metadata(function.metadata, metadata);
	out << "declare function @" << function.name << "("
	    << parameter_text(function.params) << ") -> "
	    << function.return_type.text << metadata.result() << "\n";
}

std::string call_text(const Instruction & instruction)
{
	std::string text = "call " + instruction.type.text + " " +
	                   operand_text(instruction.first) + "(";
	for (std::size_t index = 0; index < instruction.args.size(); ++index)
	{
		if (index != 0)
		{
			text += ", ";
		}
		text += operand_text(instruction.args[index]);
	}
	text += ")";
	if (instruction.has_call_signature)
	{
		text += " as " + signature_text(instruction.call_params,
		                                instruction.call_return_type,
		                                instruction.call_boundary);
	}
	return text;
}

std::string terminator_text(const Instruction & instruction)
{
	if (instruction.kind == Instruction::IK_JUMP)
	{
		return "jump " + label_text(instruction.first);
	}
	if (instruction.kind == Instruction::IK_BRANCH)
	{
		return "branch " + operand_text(instruction.first) + ", " +
		       label_text(instruction.second) + ", " +
		       label_text(instruction.third);
	}
	if (instruction.kind == Instruction::IK_RETURN)
	{
		return instruction.type.text == "void"
		           ? std::string("return void")
		           : "return " + instruction.type.text + " " +
		                 operand_text(instruction.first);
	}
	std::string text = "switch " + operand_text(instruction.first) + ", " +
	                   label_text(instruction.second);
	for (std::size_t index = 0; index < instruction.switch_cases.size(); ++index)
	{
		text += ", " + operand_text(instruction.switch_cases[index].value) +
		        ":^" + instruction.switch_cases[index].label;
	}
	return text;
}

// The body of one instruction, without the `%t = ` a producing one writes.
std::string instruction_text(const Instruction & instruction)
{
	switch (instruction.kind)
	{
	case Instruction::IK_CONST:
		return "const " + instruction.type.text + " " + operand_text(instruction.first);
	case Instruction::IK_COPY:
		return "copy " + instruction.type.text + " " + operand_text(instruction.first);
	case Instruction::IK_ADDR:
		return "addr " + operand_text(instruction.first);
	case Instruction::IK_LOAD:
		return "load " + instruction.type.text + " " + operand_text(instruction.first);
	case Instruction::IK_STORE:
		return "store " + instruction.type.text + " " +
		       operand_text(instruction.first) + ", " +
		       operand_text(instruction.second);
	case Instruction::IK_INDEX:
	{
		const std::string projection = projection_text(instruction.index_projection);
		return "index " + instruction.type.text +
		       (projection.empty() ? std::string()
		                           : " [projection=" + projection + "]") +
		       " " + operand_text(instruction.first) + ", " +
		       operand_text(instruction.second);
	}
	case Instruction::IK_UNARY:
		return "unary " + instruction.op + " " + instruction.type.text + " " +
		       operand_text(instruction.first);
	case Instruction::IK_BINARY:
		return "binary " + instruction.op + " " + instruction.type.text + " " +
		       operand_text(instruction.first) + ", " +
		       operand_text(instruction.second);
	case Instruction::IK_CMP:
		return "cmp " + instruction.op + " " + instruction.type.text + " " +
		       operand_text(instruction.first) + ", " +
		       operand_text(instruction.second);
	case Instruction::IK_CONVERT:
		return "convert " + instruction.op + " " + instruction.type.text + " " +
		       instruction.source_type.text + " " +
		       operand_text(instruction.first);
	case Instruction::IK_STACK_ALLOC:
		return "stack_alloc " + operand_text(instruction.first);
	case Instruction::IK_CALL:
		return call_text(instruction);
	case Instruction::IK_COPYOBJ:
		return "copyobj " +
		       span_text(instruction.byte_count, instruction.byte_alignment) +
		       " " + operand_text(instruction.first) + ", " +
		       operand_text(instruction.second);
	case Instruction::IK_ZEROINIT:
		return "zeroinit " +
		       span_text(instruction.byte_count, instruction.byte_alignment) +
		       " " + operand_text(instruction.first);
	case Instruction::IK_VA_START:
		return "va_start " + operand_text(instruction.first);
	case Instruction::IK_VA_ARG:
		return "va_arg " + instruction.type.text + " " +
		       operand_text(instruction.first);
	default:
		break;
	}
	return terminator_text(instruction);
}

void write_function(std::ostream & out, const Function & function)
{
	MetadataText metadata;
	metadata.add("role", role_text(function.metadata.role));
	boundary_metadata(function.boundary, metadata);
	symbol_metadata(function.metadata, metadata);
	out << "function @" << function.name << "("
	    << parameter_text(function.params) << ") -> "
	    << function.return_type.text << metadata.result() << " {\n";
	for (std::size_t index = 0; index < function.slots.size(); ++index)
	{
		out << "  slot $" << function.slots[index].first << " : "
		    << function.slots[index].second.text << "\n";
	}
	if (!function.slots.empty())
	{
		out << "\n";
	}
	for (std::size_t index = 0; index < function.blocks.size(); ++index)
	{
		const Block & block = function.blocks[index];
		if (index != 0)
		{
			out << "\n";
		}
		out << "  block ^" << block.label << ":\n";
		for (std::size_t at = 0; at < block.instructions.size(); ++at)
		{
			const Instruction & instruction = block.instructions[at];
			out << "    ";
			if (!instruction.dest.empty())
			{
				out << "%" << instruction.dest << " = ";
			}
			out << instruction_text(instruction) << "\n";
		}
	}
	out << "}\n";
}

}  // namespace

std::string serialize_lowir_program(const Program & program)
{
	std::ostringstream out;
	for (std::size_t index = 0; index < program.global_declarations.size();
	     ++index)
	{
		write_global_declaration(out, program.global_declarations[index]);
	}
	for (std::size_t index = 0; index < program.function_declarations.size();
	     ++index)
	{
		write_function_declaration(out, program.function_declarations[index]);
	}
	if (!program.global_declarations.empty() ||
	    !program.function_declarations.empty())
	{
		out << "\n";
	}
	for (std::size_t index = 0; index < program.globals.size(); ++index)
	{
		write_global(out, program.globals[index]);
	}
	if (!program.globals.empty())
	{
		out << "\n";
	}
	for (std::size_t index = 0; index < program.functions.size(); ++index)
	{
		write_function(out, program.functions[index]);
	}
	for (std::size_t index = 0; index < program.object_aliases.size(); ++index)
	{
		// The alias names an object-file symbol on the left and a symbol of this
		// program on the right, which the sigil is what says.
		out << "alias object " << program.object_aliases[index].object_symbol
		    << " = @" << program.object_aliases[index].target << "\n";
	}
	return out.str();
}

void write_lowir_program_file(const std::string & path, const Program & program)
{
	std::ofstream out(path.c_str());
	if (!out)
	{
		throw std::runtime_error("cannot write " + path);
	}
	out << serialize_lowir_program(program);
	if (!out)
	{
		throw std::runtime_error("cannot write " + path);
	}
}

}  // namespace lowir_model
