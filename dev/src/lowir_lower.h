#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "lowir_model.h"
#include "sema_facts.h"
#include "type_model.h"

struct DumpNode;
struct SemaEntity;
class LowirFunctionLowering;

// The PA15 source-to-LowIR lowering.
//
// The input is the resolved procedural tree the PA12 analysis leaves behind:
// one node per statement, declaration and expression, each carrying the typed
// facts of `SemaFact`.  Nothing here re-resolves a name, re-reads a type out of
// syntax, or parses the text that tree also spells.  The output is a typed
// `lowir_model::Program`, which `lowir_write.cpp` alone turns into LowIR text.
//
// One walk of one function emits its blocks in source order, and each
// expression is lowered once, bottom up, into the temporaries that already
// hold its operands.  Nothing is visited twice and nothing is rewritten after
// it is emitted, so the cost of a function is linear in the resolved nodes it
// holds.

// What one lowered expression is: the LowIR operand that carries it, and the
// C++ type that says what the operand means.  An lvalue's operand designates
// its storage - a slot, a global, or a pointer temporary - so reading it is a
// `load` and taking its address is at most an `addr`.
struct LowValue
{
	LowValue()
		: type(kNoType)
		, lvalue(false)
		, has_held(false)
		, constant(false)
		, value(0)
		, unnamed(false)
	{}

	lowir_model::Operand operand;
	TypeId type;
	bool lvalue;
	// The value an lvalue was just given, when the expression that produced it
	// also produced it as a value: an assignment and a prefix `++` both name
	// the object they wrote and hold what they wrote into it, so reading them
	// back costs no second load.
	bool has_held;
	lowir_model::Operand held;
	// 5.19: the value the translation knows, which is what lets a widening of
	// an immediate be written as the immediate it widens to.
	bool constant;
	unsigned long long value;
	// 2.14.5p8: an array object no declaration named, which is what a string
	// literal is.  `unary decay` marks where a declared entity becomes a
	// pointer view of itself, and there is no declaration here to mark.
	bool unnamed;
};

// The symbols one LowIR program names, which outlive the translation unit that
// first named them: 3.5 makes a name with external linkage one entity across
// units, so a second unit that declares it again must reach the same symbol.
class LowirSymbolTable
{
public:
	// The internal LowIR symbol of a namespace-scope declaration, which is its
	// qualified name with `::` written as `__`.  13.1 lets one name have
	// several declarations, so the second and later signatures of one symbol
	// are distinguished by a suffix rather than colliding.
	std::string function_symbol(const SemaEntity& entity,
	                            const std::string& signature);
	static std::string object_symbol(const SemaEntity& entity);

private:
	// The signatures already given a symbol under each base name, in the order
	// they were first named.
	std::unordered_map<std::string, std::vector<std::string> > overloads_;
};

// The LowIR program built from one or more translation units.
class LowirProgramBuilder
{
public:
	LowirProgramBuilder();

	// Lowers one analysed translation unit into the program being built.
	// `types` answers what a type is; both belong to the analysis that
	// produced `unit` and are read, never changed.
	void add_unit(const DumpNode& unit, TypeTable& types);

	// Closes what the program as a whole owns, which is the initialization
	// 3.6.2 gives the objects no constant initializes.
	void finish();

	const lowir_model::Program& program() const { return program_; }

private:
	lowir_model::Program program_;
	LowirSymbolTable symbols_;
	// The symbols this program already defines or declares, so a second
	// translation unit that declares the same entity adds nothing.
	std::unordered_set<std::string> defined_;
	std::unordered_set<std::string> declared_;
	// 3.6.2p2: the actions the objects with no constant initializer need before
	// the program starts, as the one function that runs them in order.
	lowir_model::Function startup_;
	bool has_startup_;

	friend class LowirFunctionLowering;
	friend class LowirUnitLowering;
};

// One translation unit, lowered.
class LowirUnitLowering
{
public:
	LowirUnitLowering(TypeTable& types, LowirProgramBuilder& builder);
	~LowirUnitLowering();

	void run(const DumpNode& unit);

	// The LowIR type of a C++ type: a reference and a decayed array or
	// function are a pointer, an enumeration is its underlying integer, and an
	// object with no register form is its storage span.
	lowir_model::LowType low_type(TypeId type);
	bool is_signed(TypeId type);
	// The register width of a scalar type, in bytes.
	unsigned long long width(TypeId type);

	TypeTable& types() { return types_; }
	lowir_model::Program& program() { return program_; }
	LowirSymbolTable& symbols() { return symbols_; }
	// The internal LowIR symbol of a namespace-scope object.
	std::string global_symbol(const SemaEntity& entity);
	std::string function_symbol(const SemaEntity& entity);
	// 2.14.5p8: the global holding the code units of a string literal, made
	// once per distinct literal.  The literal is an array object with static
	// storage duration and no name a program can write, so the program holds
	// it under one of its own.
	std::string string_literal(const std::string& data, TypeId array);
	// Names `entity` in the program as a declaration, for a use of a function
	// or object this unit does not define.
	void declare_entity(const SemaEntity& entity);
	// Records what the metadata of a namespace-scope symbol says.
	void describe_symbol(const SemaEntity& entity,
	                     lowir_model::SymbolMetadata& metadata);
	// 5.19 over the resolved tree: the value an initializer is worth, as the
	// bits an object of its own type holds.  3.6.2 initializes a namespace
	// scope object with data rather than with code, so the data has to be
	// known here; the same fold answers a constant an initializer clause is.
	bool folded(const DumpNode& node, unsigned long long& bits);

private:
	// The definitions of this unit, gathered before any of it is lowered.
	void collect_definitions(const DumpNode& node);
	void declaration(const DumpNode& node);
	void global_variable(const DumpNode& node);
	void function_definition(const DumpNode& node);
	void function_declaration(const DumpNode& node);
	// 3.6.2 and 8.5p6: the constant a namespace-scope object is initialized
	// with, as the data the global definition holds.
	// False when the initializer names no value or address the translation
	// knows, which 3.6.2p2 makes an action rather than data.
	bool global_initializer(lowir_model::GlobalDefinition& global,
	                        const DumpNode& node, TypeId type);
	// 3.6.2 and 5.19: the address a constant initializer names, as the symbol
	// it is an address in and the byte offset into it.  False when the
	// initializer names no address the translation can work out.
	bool global_address(const DumpNode& node, std::string& symbol,
	                    long long& addend);
	// 8.5.1 over a namespace-scope array: the clauses as the items its storage
	// holds, with the elements no clause reached left zero.
	void global_array_initializer(lowir_model::GlobalDefinition& global,
	                              const DumpNode* node, TypeId type);
	// The literal `bits` of `type` is written as, signed when the type is.
	std::string spell_value(TypeId type, unsigned long long bits);
	// The declaration of `entity` as a function, without a body.
	void add_function_declaration(const SemaEntity& entity);

	// 3.6.2p2: the action a namespace-scope object with no constant initializer
	// needs, added to the program's one startup function.
	void dynamic_initializer(const SemaEntity& entity, const DumpNode& node,
	                         TypeId type);

	TypeTable& types_;
	LowirProgramBuilder& builder_;
	lowir_model::Program& program_;
	LowirSymbolTable& symbols_;
	std::unordered_set<std::string>& defined_;
	std::unordered_set<std::string>& declared_;
	// The walk of the startup function, opened by the first object that needs
	// it, so the actions of one unit are emitted into one body in order.
	LowirFunctionLowering* startup_;
	// The global each distinct string literal was given, so one literal
	// written twice is one array object.
	std::unordered_map<std::string, std::string> strings_;
};

// One function body, lowered.
//
// The block and temporary counters, the slots and the jump targets are all
// facts about one function, so they live for exactly as long as one does.
class LowirFunctionLowering
{
public:
	LowirFunctionLowering(LowirUnitLowering& unit, lowir_model::Function& out);

	// Lowers the parameters and body written under `node`, which is a
	// `function-definition` of the resolved tree.
	void run(const DumpNode& node, TypeId type);

	// A body with no source declaration behind it: an entry block is opened,
	// actions are added to it, and the whole is closed as a `void` function.
	// 3.6.2p2 initialization is the one such body PA15 generates.
	void open_generated();
	void add_initialization(const lowir_model::Operand& storage, TypeId type,
	                        const DumpNode& node);
	void close_generated();

private:
	// Statements.
	void statement(const DumpNode& node);
	void compound_statement(const DumpNode& node);
	void expression_statement(const DumpNode& node);
	void return_statement(const DumpNode& node);
	void if_statement(const DumpNode& node);
	void while_statement(const DumpNode& node);
	void do_statement(const DumpNode& node);
	void for_statement(const DumpNode& node);
	void switch_statement(const DumpNode& node);
	void case_statement(const DumpNode& node);
	// Whether the enclosing switch can reach a label written inside `node`.
	static bool holds_label(const DumpNode& node);
	void declaration_statement(const DumpNode& node);
	void local_variable(const DumpNode& node);
	// 6.4p4: the condition of a selection or iteration statement, lowered as
	// the branch it is rather than as a value that is then tested.
	void branch_on_condition(const DumpNode& node, const std::string& on_true,
	                         const std::string& on_false);
	// 6.4p3: what a condition is worth, which for a condition-declaration is
	// the object it declared and whose lifetime the statement holds.
	LowValue condition_value(const DumpNode& node);
	// The children of a statement node that are its substatements, which for a
	// selection statement are the arms it wrote.
	void arm(const DumpNode& node);

	// Expressions.
	//
	// `as_object` says that the caller needs the object an lvalue names rather
	// than the value it holds, which is what separates the two shapes a
	// conditional expression is lowered into: one that chooses a value, and one
	// that chooses an object for the address of it to be taken.
	LowValue expression(const DumpNode& node, bool as_object = false);
	LowValue literal(const DumpNode& node);
	LowValue id_expression(const DumpNode& node);
	LowValue call_expression(const DumpNode& node);
	LowValue unary_expression(const DumpNode& node);
	LowValue increment_expression(const DumpNode& node, bool postfix);
	LowValue binary_expression(const DumpNode& node);
	LowValue logical_expression(const DumpNode& node);
	LowValue assignment_expression(const DumpNode& node);
	LowValue conditional_expression(const DumpNode& node, bool as_object);
	// 5.16 where the value is thrown away: the arms are still alternatives and
	// still run, but neither has a value for a slot to hold.
	void discarded_conditional(const DumpNode& node);
	// 5.14p1 and 5.15p1: whether the left operand alone decides a short
	// circuit, which is what keeps the right one - and every symbol it names -
	// out of the program entirely.
	static bool decided_logical(const DumpNode& node, unsigned long long& value);
	LowValue subscript_expression(const DumpNode& node);
	LowValue cast_expression(const DumpNode& node, bool as_object);
	// 5.6 to 5.13: the value a built-in binary operator computes from two
	// operands already lowered, which a compound assignment needs as well as
	// the operator written on its own.
	lowir_model::Operand binary_operation(unsigned op, const LowValue& left,
	                                      const LowValue& right, TypeId common);
	// 5.7p1: pointer arithmetic, whose scale is the element the pointer
	// addresses rather than a byte.
	lowir_model::Operand pointer_operation(unsigned op, const LowValue& left,
	                                       const LowValue& right, TypeId common,
	                                       TypeId result);
	// `address` advanced by `count` elements of `element`, forward or back.
	// LowIR indexes bytes here because the scale is what the source said and
	// not what the pointer's own type would imply after a conversion.
	lowir_model::Operand scaled_advance(const lowir_model::Operand& address,
	                                    const LowValue& count, TypeId element,
	                                    bool forward);
	// 5.17p7: the built-in operator a compound assignment is written from.
	static unsigned written_operator(unsigned op);
	// 8.5: initialising storage of `type` from the resolved initializer
	// `node`, which is what a variable, a return, an argument and a simple
	// assignment all do.
	void initialize(const lowir_model::Operand& storage, TypeId type,
	                const DumpNode& node);
	void initialize_array(const lowir_model::Operand& storage, TypeId type,
	                      const DumpNode& node);

	// Values.
	//
	// 4.1 to 4.3: the operand as a value of its own type, which is where an
	// lvalue is read and an array or function becomes a pointer.  A built-in
	// operator reads both its operands before either is converted, so the two
	// steps are separate.
	LowValue as_value(const LowValue& value);
	//
	// 4.1: the value an lvalue holds, which is the operand itself when the
	// expression already produced one.
	lowir_model::Operand rvalue(const LowValue& value);
	// 4.2 and 4.3: the pointer an array or function decays to.
	lowir_model::Operand decay(const LowValue& value);
	// 5.3.1p3: the address of the object an lvalue names.
	lowir_model::Operand address_of(const LowValue& value);
	// Clause 4: `value` converted to `target`, as the one conversion the two
	// types call for.
	lowir_model::Operand converted(const LowValue& value, TypeId target);
	lowir_model::Operand convert_scalar(const lowir_model::Operand& operand,
	                                    TypeId from, TypeId to);
	// 4.12: the operand a branch tests, which for an integer is the value
	// itself and for a pointer or a floating value is its comparison with zero.
	lowir_model::Operand truth_for_branch(const LowValue& value);
	// 5.14 and 5.15: the canonical integer truth value of an operand, which is
	// what a short-circuit operator stores.
	lowir_model::Operand truth_value(const LowValue& value);

	// Emission.
	lowir_model::Operand temp();
	lowir_model::Operand emit(lowir_model::Instruction& instruction);
	void emit_void(lowir_model::Instruction& instruction);
	lowir_model::Operand load(const lowir_model::Operand& storage, TypeId type);
	void store(const lowir_model::Operand& value, const lowir_model::Operand& storage,
	           TypeId type);
	lowir_model::Operand literal_operand(TypeId type, unsigned long long bits);
	// A block of the function, opened and made current.  The label is reserved
	// where the construct that owns it starts, which is what makes the numbers
	// of a nested construct follow those of the one that holds it.
	std::string reserve_block(const char* prefix);
	void open_block(const std::string& label);
	void terminate(lowir_model::Instruction& instruction);
	void jump(const std::string& label);
	void branch(const lowir_model::Operand& condition, const std::string& on_true,
	            const std::string& on_false);
	bool terminated() const;
	// The slot a declaration is given, named after it and kept distinct from
	// every slot the function already has.
	std::string add_slot(const SemaEntity& entity, TypeId type);
	std::string add_generated_slot(const char* prefix, TypeId type);
	std::string add_generated_slot(const char* prefix,
	                               const lowir_model::LowType& type);
	// The storage a name denotes: the slot a local was given, or the global
	// symbol a namespace-scope object has.
	LowValue storage_of(const SemaEntity& entity);

	LowirUnitLowering& unit_;
	lowir_model::Function& out_;
	unsigned temps_;
	unsigned blocks_;
	unsigned generated_slots_;
	std::size_t current_;
	bool open_;
	TypeId returns_;
	std::unordered_map<std::uint32_t, std::string> slots_;
	std::unordered_set<std::string> slot_names_;
	// The names the function's own declarations already use, which a generated
	// temporary must not take.
	std::unordered_set<std::string> taken_;
	std::vector<std::string> breaks_;
	std::vector<std::string> continues_;
	// 6.4.2: the arms each enclosing switch has found so far, and the type its
	// condition chose at.  A label can stand anywhere inside the substatement,
	// so the arms are gathered while it is read and the dispatch is completed
	// once it has been.
	struct SwitchArms
	{
		SwitchArms()
			: selector(kNoType)
		{}

		TypeId selector;
		std::vector<lowir_model::SwitchCase> cases;
		std::string fallback;
		// The block each label the substatement wrote was given, keyed by the
		// node that wrote it.  6.4.2 lets a label stand anywhere inside the
		// substatement, and the blocks of one switch are numbered before any of
		// them is filled, so the labels are handed out before it is read.
		std::unordered_map<const DumpNode*, std::string> labels;
	};

	// Reserves a block for each label `node` writes for the switch on top of
	// the stack, in the order they are written.
	void reserve_case_labels(const DumpNode& node, SwitchArms& arms);

	std::vector<SwitchArms> switches_;
};
