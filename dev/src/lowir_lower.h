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
		, named(false)
		, field(nullptr)
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
	// Whether the operand is the place a name stands for rather than a pointer
	// a value already holds.  A slot and a global say so by what they are; a
	// subobject reached through `.` or `->` is a temporary holding its address
	// and still a place the source named, which is where 4.2 converts.
	bool named;
	// 9.6p2: the bit-field this lvalue names, whose storage is a run of bits
	// inside the unit `operand` addresses rather than the unit itself.  Null
	// for every other lvalue, which names whole storage; where it is set, a
	// read masks what it loads and a write puts the other bits back.
	const SemaEntity* field;
};

// The three shapes a value written into a bit-field is put there with.
//
// 9.6p2 leaves the bits of a storage unit that the field does not own alone,
// which costs a read-modify-write - except for the first initialization to
// reach a unit, which owns every bit of it and may take the whole unit.  The
// initializer and the assignment forms write the same instructions in the two
// orders the references write them in.
enum class BitFieldWrite
{
	FirstInitializer,
	Initializer,
	Assignment
};

// The object a subobject is named from.
//
// It is either a place the function already holds - a slot, a global, a pointer
// temporary - or the resolved expression that names the object a member is part
// of, together with that member.  The second form is read again for each
// subobject, because naming a subobject from the object is the one description
// of where it is that does not depend on how the walk reached it, and a member
// of the object being constructed is reached through `this` every time.
struct LowObject
{
	// 8.5.1p1 and 5.2.1p1: one subscript of the walk down to the object, held
	// as the array it steps through and which of its elements it reached.
	struct ElementStep
	{
		ElementStep(TypeId stepped, unsigned long long reached)
			: array(stepped)
			, index(reached)
		{}

		TypeId array;
		unsigned long long index;
	};

	LowObject()
		: written(nullptr)
		, member(nullptr)
		, addressed(false)
	{}

	lowir_model::Operand storage;
	const DumpNode* written;
	const SemaEntity* member;
	// The address of the object where whoever opened it already had to name
	// one - the declaration of an object of class type is worth its address, so
	// the initialization writes into the address the declaration computed
	// rather than computing a second one for the same storage.
	lowir_model::Operand address;
	bool addressed;
	// 8.5.1p1: the object is the element these subscripts name inside the array
	// the two members above reach - one entry per dimension the walk has
	// stepped through, from the outside in, which is how 5.2.1p1 would write
	// them.  Empty where the object is the whole thing the storage holds.  The
	// chain rather than a cursor is what lets the element be named again
	// wherever it is asked for, which 15.2p2's handler needs and which a
	// namespace-scope array initialized one element at a time already did.
	std::vector<ElementStep> elements;
};

// The symbols one LowIR program names, which outlive the translation unit that
// first named them: 3.5 makes a name with external linkage one entity across
// units, so a second unit that declares it again must reach the same symbol.
class LowirSymbolTable
{
public:
	// The internal LowIR symbol of a namespace-scope declaration, which is its
	// qualified name with `::` written as `__`.  13.1 lets one name have
	// several declarations, and 13.5 lets two operator functions of one region
	// flatten to one name and take the same parameter types, so the second and
	// later functions of one base name are distinguished by a suffix rather
	// than colliding.  `identity` is what tells two functions apart: it is the
	// name the object file gives each, so a declaration of one function in a
	// second translation unit reaches the symbol the first one named.
	std::string function_symbol(const SemaEntity& entity,
	                            const std::string& identity);
	static std::string object_symbol(const SemaEntity& entity);

private:
	// Which function each base name has already given a symbol to, as the
	// order it was first named in.
	std::unordered_map<std::string,
	                   std::unordered_map<std::string, std::size_t> > overloads_;
};

// The counters a generated body carries between the translation units that add
// to it.  3.6.2p2 makes one initialization function of the whole program, so
// its blocks, temporaries and generated slots keep rising across the units that
// fill it rather than restarting - and colliding - at each one.
struct GeneratedBody
{
	GeneratedBody()
		: temps(0)
		, blocks(0)
		, slots(0)
	{}

	unsigned temps;
	unsigned blocks;
	unsigned slots;
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
	// The top-level entries this program has already written, so a repeated
	// definition is rejected by one probe rather than by a walk of everything
	// written so far.
	std::unordered_set<std::string> emitted_functions_;
	std::unordered_set<std::string> emitted_globals_;
	// 3.6.2p2: the actions the objects with no constant initializer need before
	// the program starts, as the one function that runs them in order.
	lowir_model::Function startup_;
	bool has_startup_;
	GeneratedBody startup_body_;
	// 3.6.3p1: the destructors of the objects with static storage duration, as
	// the one function that runs them when the program ends.
	lowir_model::Function shutdown_;
	bool has_shutdown_;
	GeneratedBody shutdown_body_;
	// 2.14.5p8: the global holding the code units of each distinct string
	// literal.  A literal is one array object of the program however many
	// translation units write it, so the map outlives any one of them.
	std::unordered_map<std::string, std::string> strings_;
	// 3.7.2p2: the body one thread runs for a thread-local object of this unit,
	// keyed by the name the object file gives the object.  That name and not
	// the declaration is what identifies the object: 3.1p2 lets a program
	// declare one variable as often as it likes, and a use written against an
	// `extern` declaration reaches the same object the definition lays out.  A
	// use of the name calls the body, so the map is what turns a use into that
	// call in one probe; a thread-local this unit runs nothing for - one
	// another unit defines, or one whose value the image holds and whose
	// lifetime ends with nothing - is absent, and a use of it costs the same
	// probe and no call.
	std::unordered_map<std::string, std::string> thread_initializers_;

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
	// 6.6.3p2: the boundary a function returning `returned` has.  A class the
	// ABI hands back through storage the caller named takes that destination as
	// a parameter standing before every one the declaration wrote, and returns
	// nothing; every other return type is the value it is.  Both halves are one
	// answer, written here once so a declaration, a definition and a call
	// through a pointer cannot disagree about the same function.
	void open_signature(TypeId returned,
	                    std::vector<lowir_model::Parameter>& params,
	                    lowir_model::LowType& result);
	bool is_signed(TypeId type);
	// The register width of a scalar type, in bytes.
	unsigned long long width(TypeId type);
	// 3.9.1p1 and 4.7p2: `bits` as an object of `type` holds them, which is
	// what a value of that type is worth to everything that reads it.
	unsigned long long narrowed(TypeId type, unsigned long long bits);

	TypeTable& types() { return types_; }
	lowir_model::Program& program() { return program_; }
	LowirSymbolTable& symbols() { return symbols_; }
	// The internal LowIR symbol of a namespace-scope object.
	const std::string& global_symbol(const SemaEntity& entity);
	// The internal LowIR symbol a call of `entity` names.  12.1 and 12.4 give a
	// constructor and a destructor two entry points, so a call on a base class
	// subobject names the base-object one wherever a complete object asked for
	// the other; everywhere else the one entry this unit wrote is the one name.
	const std::string& function_symbol(const SemaEntity& entity,
	                                   bool base_subobject = false);
	// 12.1 and 12.4: which of the ABI's two entry points a constructor or a
	// destructor stands under here, which is what this unit ran it as.
	unsigned abi_variant(const SemaEntity& entity);
	// Whether this unit writes the base-object entry of `entity` as a definition
	// of its own, which is what a constructor or destructor a complete object
	// and a base subobject both asked for needs.
	static bool writes_base_entry(const SemaEntity& entity);
	// 2.14.5p8: the global holding the code units of a string literal, made
	// once per distinct literal.  The literal is an array object with static
	// storage duration and no name a program can write, so the program holds
	// it under one of its own.
	std::string string_literal(const std::string& data, TypeId array);
	// 3.7.2p2 and 3.6.3p3: the runtime entries an object destroyed at the end
	// of its thread needs - the function the destruction is handed to, and the
	// handle that says which loaded image the pair belongs to.  Each is
	// declared the first time one is asked for and named by one probe after
	// that, so a program with no such object writes neither.
	const std::string& thread_atexit_symbol();
	const std::string& image_handle_symbol();
	// 3.7.2p2: the body that initializes one thread's copy of `entity`, or null
	// where this unit gives the object none - which is every object but a
	// thread-local one this unit initializes with code.  A use of the name has
	// to run it before it reads the object, because no point in the program
	// stands before every thread that names one.
	const std::string* thread_initializer_of(const SemaEntity& entity);
	// Names `entity` in the program as a declaration, for a use of a function
	// or object this unit does not define.  A use of a function whose definition
	// belongs to every unit that needs one also asks for that definition here.
	void declare_entity(const SemaEntity& entity);
	// 3.2p2 and 3.5p4: the definition a use named even though this lowering
	// wrote no call of it.  A definition every unit that needs one may hold is
	// nobody's to owe where no call survives; one with internal linkage is this
	// unit's alone, so the object file holds it wherever an initialization
	// named it.
	void owe_internal_definition(const SemaEntity& entity);
	// 12.1p5: whether running `constructor` on an object writes an instruction.
	// A constructor whose definition the program wrote is answered by that
	// definition alone - an empty body writes nothing - and one the standard
	// gave it by what its bases and members come to, which is the same question
	// asked of each of them.
	bool construction_writes_nothing(const SemaEntity& constructor);
	// 3.2p2: the definitions a construction this lowering did not write still
	// named.  An elided body would have built each of its subobjects, so what
	// it would have called is owed even though no call of it is written; a
	// subobject whose own construction is elided passes the question on.
	void owe_elided_construction(const SemaEntity& constructor,
	                             unsigned depth = 0);
	// The same for a call that names one of the ABI's two entry points and not
	// the other.  `declare_entity` owes both wherever the analysis saw a
	// complete object and a base subobject each ask for the function; a call
	// this lowering writes knows which of the two it wrote, so it says so and
	// the unit owes that one name.
	void declare_call_target(const SemaEntity& entity, bool base_entry);
	// Records what the metadata of a namespace-scope symbol says: 3.5p3 whose
	// program may reach it, 7.5p1 what language linkage it was declared with,
	// and 3.5p9 the name the object file gives it, which is written only where
	// it differs from the internal LowIR symbol `symbol`.
	// `base_entry` says this symbol is the base-object entry of a constructor or
	// a destructor whose other entry the unit writes too.
	void describe_symbol(const SemaEntity& entity,
	                     lowir_model::SymbolMetadata& metadata,
	                     const std::string& symbol, bool base_entry = false);
	// 5.19 over the resolved tree: the value an initializer is worth, as the
	// bits an object of its own type holds.  3.6.2 initializes a namespace
	// scope object with data rather than with code, so the data has to be
	// known here; the same fold answers a constant an initializer clause is.
	bool folded(const DumpNode& node, unsigned long long& bits);
	// 3.6.2p2: the constant one item of the program image holds, spelled at the
	// type of the storage.  An integral value is what the fold above works out;
	// 2.14.4's floating one is a value no integer of this translation holds, so
	// it reaches the image as the digits the program wrote and the suffix the
	// storage asks for.
	bool image_value(const DumpNode& node, TypeId type, std::string& text);
	// 2.14.4 and `lowir.md`: one floating value spelled at the width of the
	// storage that holds it.  It is one owner because a spelling that carries
	// the suffix of another width would be a value of that width, so the
	// digits of the image, of a body's immediate and of 8.5p7's zero all come
	// through here rather than each carrying a suffix of its own.
	std::string spell_floating(TypeId type, const std::string& written);

private:
	// 2.14.4: the digits of the floating constant an initializer is worth, and
	// one floating value spelled at the width the storage has.
	bool floating_image(const DumpNode& node, std::string& text);
	// The definitions of this unit, gathered before any of it is lowered.
	void collect_definitions(const DumpNode& node);
	// 3.7.2p2: the definitions with thread storage duration, lowered before any
	// body is, so that a use of one written before its definition runs what
	// initializes it as a use written after it does.
	void thread_definitions(const DumpNode& node);
	void declaration(const DumpNode& node);
	void global_variable(const DumpNode& node);
	void function_definition(const DumpNode& node);
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
	// holds, with the elements no clause reached left zero.  False where a
	// clause names no value the translation knows, which 3.6.2p2 makes an
	// action over the whole array rather than data.
	bool global_array_initializer(lowir_model::GlobalDefinition& global,
	                              const DumpNode* node, TypeId type);
	// 8.5.1 and 3.6.2p2 over a namespace-scope aggregate: the subobjects the
	// analysis resolved, as the items its storage holds.  One pass in
	// declaration order, which is the order the subobjects are laid out in, so
	// the padding between two of them is what the offsets say.  False when a
	// clause names no value the translation knows, which leaves the whole
	// object to be initialized by code.
	bool global_aggregate_initializer(lowir_model::GlobalDefinition& global,
	                                  const DumpNode& node, TypeId type);
	bool global_subobjects(lowir_model::GlobalDefinition& global,
	                       const DumpNode& node, unsigned long long base,
	                       unsigned long long& at);
	// 3.6.2p2: the image a `constructor-action` leaves the object holding,
	// where the constructor's own definition is nothing but 12.6.2's member
	// initializations of values the translation knows once its parameters hold
	// the call's arguments.  False for every other constructor, which leaves
	// the object to be built before the program runs.
	bool global_constructed(lowir_model::GlobalDefinition& global,
	                        const DumpNode& action, unsigned long long base,
	                        unsigned long long& at);
	// `bytes` of zero, added to the items when there are any to add.
	static void add_zero_item(lowir_model::GlobalDefinition& global,
	                          unsigned long long bytes);
	// The literal `bits` of `type` is written as, signed when the type is.
	std::string spell_value(TypeId type, unsigned long long bits);
	// The declaration of `entity` as a function, without a body - one per name
	// a call of it can write, which for a constructor or a destructor both a
	// complete object and a base subobject asked for is both of the ABI's
	// entry points.
	void add_function_declaration(const SemaEntity& entity);
	void add_function_declaration(const SemaEntity& entity, bool base_entry);
	// 1.4p8: what a call of a function the implementation reserves a name for
	// may be assumed to do, which is a fact of that function and not of the
	// call.  Nothing at all for a declaration the program wrote.
	static void describe_builtin(const SemaEntity& entity,
	                             lowir_model::FunctionDeclaration& declaration);
	// 7.1.2p4 and 12.1p5: a definition no one translation unit owns is in the
	// program only where the program uses it, so a use asks for it here.  The
	// definition is taken off the deferred map as it is asked for, which makes
	// one use of a name lower it and every later use cost a probe.
	void demand_definition(const SemaEntity& entity);
	void demand_definition_by_id(std::uint32_t entity);
	// 3.2p2 and 3.2p3: the functions the resolved tree names anywhere, which is
	// what odr-uses them - a body this unit does not write still uses what it
	// calls.  They are gathered in the order the tree names them and asked for
	// once every use the unit itself wrote has been answered, so a definition a
	// written body asked for still stands where that body asked for it.
	void demand_referenced(const DumpNode& node);
	std::vector<std::uint32_t> referenced_;
	// Lowers the definitions asked for so far, and the ones lowering those asks
	// for.  It runs between top level declarations and never inside one, so no
	// `lowir_model::Function&` of a body being lowered is alive when
	// `program_.functions` grows.
	void drain_demanded();

	// 3.6.2p2: the action a namespace-scope object with no constant initializer
	// needs, added to the program's one startup function.
	void dynamic_initializer(const SemaEntity& entity, const DumpNode& node,
	                         TypeId type);
	// 3.7.2p2: the ABI's access surface for one object with thread storage
	// duration, which is a function declaration naming the global it reaches.
	// `object` is the name the object file gives that function, empty for a
	// global this program made up and no other unit can name.
	void thread_wrapper(const std::string& symbol, const std::string& object,
	                    lowir_model::SymbolBindingMode binding);
	// 3.7.2p2: what one thread runs for a thread-local object - the
	// initialization 3.6.2p2 does not settle and 12.4p11's handing of the
	// destruction to the runtime - as the per-object body a thread runs rather
	// than as an action of the program's one startup function.  Either of the
	// two may be null, and the caller writes no body where both are.
	void thread_initializer(const std::string& symbol, const DumpNode* node,
	                        TypeId type, const DumpNode* destruction);
	// 3.7.2p2: the bodies those definitions asked for, written once every
	// thread-local definition of the unit has said whether it asked for one -
	// so that a body naming another object of its own thread runs what
	// initializes that one, wherever the two are written.
	void write_thread_bodies();
	// 3.7.2p2: the flag of the same thread that says a thread has run what one
	// thread-local object asked it to.
	static std::string guard_of(const std::string& symbol);
	// 3.7.2p2: the action that ends the lifetime of the object a declaration
	// declared, which stands under the declaration for a thread-local object
	// because no point of the program is where it runs.
	static const DumpNode* thread_destruction(const DumpNode& node);
	// 3.6.3p1: the destructor of an object with static storage duration, added
	// to the program's one shutdown function.
	void static_destructor(const DumpNode& node);

	// One thread-local definition's outstanding body: the object it fills, the
	// initialization 3.6.2p2 left to run and the destruction 12.4p11 hands to
	// the runtime, either of which may be absent.
	struct ThreadBody
	{
		std::string symbol;
		std::string body;
		const DumpNode* initialization;
		TypeId type;
		const DumpNode* destruction;
	};
	std::vector<ThreadBody> thread_bodies_;
	// The object whose body is being written, if any.  3.2p2: filling an
	// object's storage is not a use of the name that has to be filled first, so
	// this one body is the one a use inside it must not call.
	std::string writing_thread_body_;

	TypeTable& types_;
	LowirProgramBuilder& builder_;
	lowir_model::Program& program_;
	LowirSymbolTable& symbols_;
	std::unordered_set<std::string>& defined_;
	std::unordered_set<std::string>& declared_;
	// The walk of the startup function, opened by the first object that needs
	// it, so the actions of one unit are emitted into one body in order.
	LowirFunctionLowering* startup_;
	// The walk of the shutdown function, opened by the first object that needs
	// one, for the same reason.
	LowirFunctionLowering* shutdown_;
	// The global each distinct string literal was given, so one literal
	// written twice is one array object.  It belongs to the program, which is
	// what makes two units that write one literal name one object.
	std::unordered_map<std::string, std::string>& strings_;
	// The symbol of every namespace-scope entity this unit has already asked
	// for, so a name used many times is flattened and signed once.
	std::unordered_map<std::uint32_t, std::string> entity_symbols_;
	// The base-object entry's own symbol, for the constructors and destructors a
	// base subobject asked for after a complete object already had.  It is kept
	// apart from `entity_symbols_` because one declaration then stands under two
	// names, and which of them a call writes is a fact about the call.
	std::unordered_map<std::uint32_t, std::string> base_entry_symbols_;
	// 3.2p3 and 7.1.2p4: the definitions this unit holds that belong to every
	// unit needing one rather than to this one - an inline function, a member
	// function defined in its class, and the constructor 12.1p5 gives a class -
	// keyed by the entity each defines.  A definition the program never uses is
	// not part of the program, so none of these is lowered until a use asks.
	std::unordered_map<std::uint32_t, const DumpNode*> deferred_;
	// Every definition this unit holds, keyed by the entity it defines, kept
	// whether or not the definition has been lowered yet.  3.6.2p2 reads what a
	// constructor does to fold one call of it into the image of an object, and
	// n objects built by one constructor ask n times, so the index outlives the
	// one-shot walk `deferred_` is.
	std::unordered_map<std::uint32_t, const DumpNode*> bodies_;
	// 12.1p5 read as a question about what runs rather than about what was
	// written: whether constructing an object by this constructor writes an
	// instruction at all.  It is asked of the definition the unit already
	// holds, and one class asks it of its bases and members, so the answer is
	// kept - `kEmptyUnknown` until the first reader, `kEmptyOpen` while the
	// walk that is answering it stands inside.
	std::unordered_map<std::uint32_t, unsigned char> empty_construction_;
	// Those a use has asked for and that are not lowered yet.
	std::vector<const DumpNode*> demanded_;
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
	// or the one a previous translation unit left open is taken up again, and
	// actions are added to it.  3.6.2p2 initialization is the one such body
	// PA15 generates, and the program owns it, so `state` carries what one unit
	// has used to the next.
	void open_generated(const GeneratedBody& state);
	void add_initialization(const lowir_model::Operand& storage, TypeId type,
	                        const DumpNode& node);
	// 3.7.2p2: what one thread-local object asks the thread that reaches it to
	// run, guarded by a flag of the same thread so that a thread runs it at
	// most once.  `node` is the initialization, or null where 3.6.2p2 settled
	// it and the image already holds the value; `destruction` is the action
	// that ends the object's lifetime, or null where its class ends one with
	// nothing to run: this thread ends when nothing in the program does, so
	// what ends the object is handed to the runtime here rather than written at
	// a point of the program.
	void add_thread_initialization(const std::string& guard,
	                               const lowir_model::Operand& storage,
	                               TypeId type, const DumpNode* node,
	                               const DumpNode* destruction);
	// 3.6.3p1: one object's destruction, added to the generated body.
	void add_destruction(const DumpNode& node);
	void suspend_generated(GeneratedBody& state) const;

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
	// 6.1p1 and 6.6.4p1: the block a label names, which is one block of the
	// function however many places name it and whether the label was written
	// before or after them.
	const std::string& goto_label(const std::string& name);
	// Whether the enclosing switch can reach a label written inside `node`.
	static bool holds_label(const DumpNode& node);
	void declaration_statement(const DumpNode& node);
	void local_variable(const DumpNode& node);
	// 6.4p4: the condition of a selection or iteration statement, lowered as
	// the branch it is rather than as a value that is then tested.
	void branch_on_condition(const DumpNode& node, const std::string& on_true,
	                         const std::string& on_false);
	// The same over the expression the condition wrote, which 5.14 and 5.15
	// lower as their own control flow.
	void branch_on_value(const DumpNode& node, const std::string& on_true,
	                     const std::string& on_false);
	// 3.8p1 and 12.2p3: whether any object's lifetime ends where this construct
	// does, which is what makes the edges out of it places that end one.
	static bool ends_temporaries(const DumpNode& node);
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
	// 5.2.5p1: `E1.E2`, `E1->E2` and a member named with no object expression,
	// which all reach the member through the object the operand denotes.
	LowValue member_expression(const DumpNode& node);
	// 9.2p13: where one member of the object `object` names begins, which is
	// the storage a use of the member reads through - and, for a member of
	// reference type, the storage of the pointer it holds rather than of the
	// object that pointer names.
	lowir_model::Operand member_storage(const DumpNode& object,
	                                    const SemaEntity& member,
	                                    bool bound = false);
	// 12.6.2: one member of the object a constructor is initializing.
	void member_initialization(const DumpNode& node);
	// 12.8p15 and p28: one step of a value transfer the standard defines that
	// carries storage rather than naming a subobject.
	void storage_transfer(const DumpNode& node);
	// 9.2p13: the storage a given number of bytes into the object at `at`.
	lowir_model::Operand at_offset(const lowir_model::Operand& at,
	                               unsigned long long offset);
	lowir_model::Operand field_at(const lowir_model::Operand& at,
	                              unsigned long long offset);
	// 9.6p2 and 4.5p3: the value a bit-field lvalue holds, which is the unit it
	// sits in read at the field's own promoted type, shifted down to the bottom
	// and masked to the width the declaration wrote.
	lowir_model::Operand read_bit_field(const LowValue& value);
	// 8.5.1 and 12.6.2: `held` written into the bit-field a subobject path
	// names.  The unit is addressed from the object again for each step the
	// write takes, which is how the references write it and what keeps the
	// description of the place independent of the value being put there.
	void initialize_bit_field(const SemaEntity& field, const LowObject& object,
	                          const std::vector<const DumpNode*>& path,
	                          unsigned long long unit,
	                          const lowir_model::Operand& held, TypeId type);
	// 5.17 and 5.3.2: `held` written into the field `target` names, whose unit
	// the caller already holds the address of.
	void assign_bit_field(const LowValue& target,
	                      const lowir_model::Operand& held, TypeId type);
	// The field's bits moved up to where they belong in the unit, which is what
	// both writes put there.  `first` gives the mask and the value the order
	// 8.5.1's write writes them in rather than 5.17's.
	lowir_model::Operand placed_bits(const SemaEntity& field,
	                                 const lowir_model::Operand& held,
	                                 TypeId type, bool initializer);
	// 9.6p2: whether the field owns every bit of the unit read at `type`, which
	// leaves both of the masks nothing to say.
	bool fills_unit(const SemaEntity& field, TypeId type);
	// 4.5p1: one operation on values of `type` written as the source would have
	// written it, at the type 4.5 promotes the operands to.
	lowir_model::Operand field_binary(const char* op, TypeId type,
	                                  const lowir_model::Operand& left,
	                                  const lowir_model::Operand& right);
	// 4.5p1: one operand of such an operation, at the promoted type.
	lowir_model::Operand promoted_operand(const lowir_model::Operand& held,
	                                      TypeId from, TypeId to);
	// 8.5p14: the value an initializer gives the object it initializes, which
	// for a constant is the value it produces at that object's own type.
	lowir_model::Operand initializer_value(const LowValue& value, TypeId target);
	// 8.5p7 and 9.6p2: whether an initialization that takes the whole of
	// [first, last) would be writing over bytes this initialization has already
	// put something in.  The subobjects of one object are initialized in the
	// order 9.2p13 laid them out, so how far the initialization has reached is
	// one number rather than a record of everything it wrote.  Asking moves
	// that number on, because asking is what a write asks.
	bool claims_storage(unsigned long long first, unsigned long long last);
	// 9.2p13: how far into the object being initialized a subobject path has
	// reached, which is what tells two storage units of one object apart.
	unsigned long long subobject_offset(
		const std::vector<const DumpNode*>& path) const;
	// 4.10p3 and 10p1: the base class subobject of the object an operand
	// denotes, as the storage of that object at the place its class gave the
	// base.
	LowValue base_conversion(const DumpNode& node);
	// 12.1p5: the constructor call an object of class type is initialized by,
	// on the address of that object.  `always` says the object has no other
	// mark of its lifetime beginning, so even a constructor that does nothing
	// is the call it is - which is what a temporary no declaration named needs
	// and what an object a declaration named does not.
	void constructor_call(const lowir_model::Operand& address,
	                      const DumpNode& node, bool always = false,
	                      TypeId zeroed = kNoType);
	// 12.6p1 and 12.4p8: the action `node` names, run on every element of the
	// array it names rather than on one object.  The object the action names is
	// read again for each element, because where the element is, is where the
	// array is plus the elements before it, and that is one description however
	// the walk reached the array.
	void array_lifecycle(const DumpNode& node, bool construct);
	// The same action written as the loop 12.6p1's order is, for an array with
	// more elements than a reader wants to count.  The array is named once and
	// the element is the index the loop carries, so what the output holds is
	// what the source wrote rather than what the bound multiplies it by.
	void array_lifecycle_loop(const DumpNode& node, bool construct,
	                          const DumpNode& named, unsigned long long total,
	                          TypeId element);
	// 12.4p8 over the first `count` elements of the array at `base`, destroyed
	// in the reverse of the order they were created in.  `count` is a value
	// rather than a number, because 15.2p2 asks for the elements an exception
	// left standing and only the loop's own index knows how many those are.
	// `cleanup` is the handler each of those destructions stands under, which
	// 12.4p8's suffix has and the other three callers do not.
	// `blocks` and `counter` name the blocks and the index this loop writes,
	// which 5.3.5p2's array delete spells for itself: a loop the same shape in
	// two places is still two places, and the output names each after what it
	// is a step of.
	void destroy_array_loop(const lowir_model::Operand& base,
	                        const lowir_model::Operand& count,
	                        unsigned long long stride,
	                        const SemaEntity& destructor, bool base_subobject,
	                        bool guarded, const std::string* after,
	                        const char* blocks = "array_dtor",
	                        const char* counter = "array_dtor_index");
	// The address of the element at `index` of the array at `base`, counted in
	// the bytes an element occupies - which is what LowIR indexes an element
	// with no register width by.
	lowir_model::Operand element_at_value(const lowir_model::Operand& base,
	                                      const lowir_model::Operand& index,
	                                      unsigned long long stride);
	// 12.4p1: the destructor of the class the constructor `entity` belongs to,
	// or null where that class ends a lifetime with nothing.
	const SemaEntity* subobject_destructor(const SemaEntity& constructor) const;
	// 5.2.1p1: one element of the array whose address is `array`, which is the
	// array read as a pointer to its first element and moved by whole elements.
	lowir_model::Operand array_element(const lowir_model::Operand& array,
	                                   TypeId array_type,
	                                   unsigned long long index);
	// One dimension of that walk, from a pointer already standing at the first
	// element of `array_type`.
	lowir_model::Operand element_step(const lowir_model::Operand& cursor,
	                                  TypeId array_type,
	                                  unsigned long long index);
	// The same step named by the element's own type, which is what a subobject
	// path holds where 8.5.1p1 says which element a clause reached.
	lowir_model::Operand element_of_step(const lowir_model::Operand& cursor,
	                                     TypeId element,
	                                     unsigned long long index);
	// 8.3.4p1: the dimensions of an array type, outermost first, and how many
	// objects it holds altogether.
	unsigned long long array_dimensions(TypeId type,
	                                    std::vector<TypeId>& dimensions,
	                                    std::vector<unsigned long long>& bounds);
	// The address of the object at `flat` among them, with the last dimension
	// moving fastest - which is the order the storage lays them out in.
	lowir_model::Operand element_at(
		const lowir_model::Operand& array,
		const std::vector<TypeId>& dimensions,
		const std::vector<unsigned long long>& bounds, unsigned long long flat);
	// 8.5p7 over an array: every element value-initialized, written as the
	// elements they are while there are few enough of them for that to be a
	// description of the array.
	void value_initialize_array(const LowObject& object, TypeId type);
	// 12.2p1: the storage a prvalue of class type stands in, given to it here
	// and constructed here.  The object is named once however many readers the
	// temporary has, so the slot is made the first time it is reached.
	LowValue temporary_object(const DumpNode& node,
	                          const lowir_model::Operand* into = nullptr);
	// 12.8p31 and 8.5p14: the object standing at `destination`, initialized by
	// `node`.  An initializer that creates an object of its own - a temporary
	// the program wrote, a call that returns one, a conditional whose arms each
	// do - creates it there and no copy stands between the two; every other
	// initializer is read for the object it names and copied.  The destination
	// is named before the initializer runs, because it is what the
	// initialization is about.
	LowValue place_class_object(const lowir_model::Operand& destination,
	                            TypeId type, const DumpNode& node);
	// Whether `node` is an initializer that creates its object, which is what
	// says the destination is where that object is built.
	bool creates_object(const DumpNode& node, TypeId type);
	// 12.2p1: storage of the function's own for an object of class type no
	// declaration named, and the address of it.  The slot is opened before
	// whatever fills it runs, because the object standing in it is what that
	// initialization is about.  `storage` takes the slot itself, which is what
	// 5.2.2p4 passes an object of class type as.
	lowir_model::Operand open_object_slot(TypeId type, const char* prefix,
	                                      lowir_model::Operand* storage = nullptr);
	// 12.2p1: the storage a class prvalue an initialization did not name a
	// destination for is given - one slot of the function, named after what
	// asked for it and named before the initializer that fills it runs.
	LowValue class_object_slot(const DumpNode& node, TypeId type,
	                           const char* prefix);
	// 5.2.2p4: one argument of a call standing where a parameter of class type
	// is, which is the storage that parameter object occupies.
	lowir_model::Operand class_argument(const DumpNode& node, TypeId type);
	// 5p11: an expression whose value is discarded, where the expression is
	// worth an object of class type it created.  The object still stands in
	// storage of the function's, which is what the discarding gives it here.
	bool discarded_class_object(const DumpNode& node);
	// 12.2p1: whether the expression is worth an object of class type standing
	// in no storage of the function's, which is what needs some given to it.
	bool stands_in_no_storage(const DumpNode& node);
	// 12.8p31: the local object every `return` of this function copies into the
	// returned object, where there is one such object and the copy is all that
	// stands between it and the destination the caller named.  The two may then
	// be one object: the declaration is given the destination as its storage and
	// the copy is not written at all.  Null where no local answers that, which
	// is every function whose returns name different objects, whose returned
	// object is not one a declaration of the outermost block named, or that
	// leaves a lifetime behind on the way out.
	const SemaEntity* return_slot_local(const DumpNode& definition);
	// The object one `return` copies into the returned object, or null where
	// what it returns is not the copy of a named object.
	const SemaEntity* returned_local(const DumpNode& node);
	// 5.3.4: the storage 3.7.4.1's allocation function returned, and the
	// object 8.5p16 creates in it.  The address is the one the call produced,
	// so nothing here allocates a slot and the object is built at a value
	// rather than at a name.
	LowValue new_expression(const DumpNode& node);
	// 5.3.4p1: the array form, which asks the allocation function for every
	// element at once, writes the count 5.3.5p2 will read in front of them,
	// and gives 12.6p1's construction to each of them in one loop.
	LowValue array_new_expression(const DumpNode& node);
	// 5.3.5: a delete-expression, which is 12.4p3's end of a lifetime and
	// 3.7.4.2's return of the storage, over the object the operand points to.
	LowValue delete_expression(const DumpNode& node);
	// 3.7.4.2p2: the call that gives `storage` back, made through the
	// deallocation function `callee` names.  12.5p4's two-parameter form is
	// handed the size of the object as well, which is a number of the type.
	void deallocation_call(const DumpNode& callee,
	                       const lowir_model::Operand& storage,
	                       unsigned long long bytes);
	// 5.3.4p1 and the ABI: how many elements the array standing at `data`
	// holds, read from the count in front of them or written as the number the
	// translation knows.
	lowir_model::Operand array_element_count(const DumpNode& node,
	                                         const lowir_model::Operand& bytes,
	                                         unsigned long long stride);
	// 8.5p7 over an array: every byte of `bytes` bytes standing at `data` set
	// to zero, written as the loop the count is a value makes it.
	void zero_storage_loop(const lowir_model::Operand& data,
	                       const lowir_model::Operand& bytes);
	// 8.5p7 over the elements a new-expression's array form created: the zero
	// of the storage they stand in, over an extent the translation knows where
	// 5.3.4p6's count is one it knows, and over the bytes the call was asked
	// for where it is not.
	void zero_new_elements(const DumpNode& node,
	                       const lowir_model::Operand& data,
	                       const lowir_model::Operand& bytes,
	                       unsigned long long stride, TypeId element,
	                       bool cookie);
	// 12.6p1 over the elements a new-expression created, with 15.2p2's handler
	// around them: `storage` is what the allocation handed back, `data` where
	// the elements begin, and `release` the deallocation the handler makes.
	void construct_array_new_run(const DumpNode& node, const DumpNode& action,
	                             const DumpNode* release,
	                             const lowir_model::Operand& storage,
	                             const lowir_model::Operand& data,
	                             const lowir_model::Operand& count,
	                             unsigned long long stride, TypeId element);
	// 12.8p15: one class object copied into another, which for a class that
	// holds nothing moves nothing at all.  `stored` says the source is the
	// value a call handed back rather than the storage an object stands in,
	// which is what leaves 9p6's class with a byte to write after all: what is
	// written is the returned value going into storage, and not the bytes of
	// one object read out of another.
	void copy_class_object(const lowir_model::Operand& destination,
	                       const lowir_model::Operand& source, TypeId type,
	                       bool stored = false);
	// 12.8p15: the bytes of one object of class type written into the storage
	// of another, which is what a copy the standard defines comes to.
	void copy_object_storage(const lowir_model::Operand& destination,
	                         const lowir_model::Operand& source, TypeId type,
	                         bool stored = false);
	// Whether what the value is worth is the object itself rather than storage
	// it stands in, which is what a call returning a small class hands back.
	bool holds_class_value(const LowValue& value);
	// 5.2.2p4: what the call is passed for one argument.  For every type but a
	// class this is the conversion the parameter asks for; for a class it is
	// the storage the copy was made in, and 12.8p31 lets a prvalue argument be
	// created in that storage rather than copied into it.
	lowir_model::Operand argument_operand(const DumpNode& node,
	                                      const LowValue& value,
	                                      TypeId parameter);
	// The same for the argument at `at` of a call whose declaration is
	// `parameters`, which for one past the last of them is 5.2.2p7's argument
	// matched by the ellipsis, passed under the default argument promotions.
	// A call the program wrote and a constructor call the object model wrote
	// pass an argument the same way.
	lowir_model::Operand passed_operand(const DumpNode& node,
	                                    const LowValue& value,
	                                    const std::vector<TypeId>& parameters,
	                                    std::size_t at);
	// 12.8p15 and 12.8p31: the storage a copy of one class object is made in
	// where the place that asked for the copy owns storage of its own, and the
	// storage a prvalue already stands in where 12.8p31 lets the two be one
	// object.  `prefix` is what asked: an argument, a return.
	lowir_model::Operand class_value_slot(const DumpNode& node,
	                                      const LowValue& value, TypeId type,
	                                      const char* prefix);
	// 12.2p1: the storage a class prvalue that stands in none of its own is
	// given, which is one slot of the function holding a copy of it.
	lowir_model::Operand materialized_class_value(const LowValue& value);
	// The object a copy of a class value is made from: the storage it stands
	// in, or the value itself where a call handed it back without any.
	lowir_model::Operand class_copy_source(const LowValue& value);
	// 8.5p5: the zero of an object of class type, which is the zero of the
	// bytes it occupies.  They are written as the widest stores that fit, and
	// as one `zeroinit` where there are more of them than a reader wants to
	// count; a class that holds nothing has none and is written nothing.
	void zero_object(const lowir_model::Operand& address, TypeId type);
	// 8.5p6's zero over a span of storage, written as the widest stores the
	// bytes take and as one `zeroinit` past the point where that stops being
	// the shorter of the two.
	void zero_span(const lowir_model::Operand& address, unsigned long long size,
	               unsigned long long align);
	// 12.4p3: the destructor call the end of an object's lifetime is.
	void destructor_call(const DumpNode& node);
	// One call of that destructor: on the object the action names, or on the
	// element at `index` of the array it names.  12.4p8 makes an array as many
	// ends of a lifetime as it has elements, and 15.2p2 asks about each of them
	// separately, so the step rather than the action is the unit.
	// `count` past zero makes the step the whole array, destroyed by a loop.
	// `suffix` says the step stands in 12.4p8's suffix, where a destruction
	// that throws leaves the ones behind it standing - which is `cleanup`, or
	// 15.1p2's resume for the last step of all.
	void destruction_step(const DumpNode& node, bool element,
	                      unsigned long long index,
	                      unsigned long long count = 0,
	                      const std::string* cleanup = nullptr,
	                      bool suffix = false);
	// How many of those steps an action is: one, or one per element.
	unsigned long long destruction_steps(const DumpNode& node);
	// 3.8p1: the destructor actions a statement carries for the blocks control
	// leaves through it - the jump out of them, or the end of the region the
	// statement itself opened.
	void leave_blocks(const DumpNode& node);

	// 15.2p2: what a destructor still owes when control leaves its own body -
	// the destruction of every base and member subobject, each of which may
	// itself throw and leave the rest still to run.  The list is the trailing
	// `destructor-action` children of the definition, flattened to one entry
	// per call, and it is written at every point control leaves the body.
	struct LowDestruction
	{
		LowDestruction()
			: action(nullptr)
			, element(false)
			, index(0)
			, count(0)
		{}

		const DumpNode* action;
		bool element;
		unsigned long long index;
		// How many elements this one entry destroys: zero for the single object
		// or single element every other entry is, and the whole bound for an
		// array written as the loop 12.4p8's order is.
		unsigned long long count;
	};
	// The destructions after the body, with the handler each of them needs for
	// the subobjects still standing behind it.
	void destructor_epilogue();
	// Reads that list off the definition and answers where the body ends among
	// its children.
	std::size_t collect_epilogue(const DumpNode& node);

	// 15.2p2: one subobject the constructor being lowered has already built,
	// held as the instructions that named it rather than as the address they
	// produced.  The handler stands in a block of its own, and a temporary of
	// the block the construction stands in is not one that block may name, so
	// the subobject is named there the way the step that built it named it.
	struct LowUnwind
	{
		LowUnwind()
			: destructor(nullptr)
			, base_subobject(false)
			, elements(0)
			, stride(0)
		{}

		const SemaEntity* destructor;
		bool base_subobject;
		std::vector<lowir_model::Instruction> address;
		lowir_model::Operand at;
		// 12.6p1: the subobject is an array written as a loop, so what the
		// handler owes is that loop over this many elements rather than one
		// call.  Zero everywhere else, which is the one object `at` names.
		unsigned long long elements;
		unsigned long long stride;
	};
	// Where the code that builds one subobject begins.  Whether the step needs
	// a handler is known only once it has made its call, so the place is marked
	// before the step and the region is opened into it afterwards.
	//
	// `at` is where the instructions that name the subobject begin, which is
	// what the handler replays.  `at_call` says the region begins after them
	// rather than at them: where 8.5.1p1's clause reached the subobject, the
	// path to it is walked before the initialization the clause is, and it is
	// that initialization the region covers - where 12.6.2 and 12.6p1 name a
	// subobject, the naming is part of the step and stands inside it.
	struct UnwindMark
	{
		UnwindMark()
			: active(false)
			, at_call(false)
			, block(0)
			, at(0)
		{}

		bool active;
		bool at_call;
		std::size_t block;
		std::size_t at;
	};
	// The two blocks one such region stands between, and whether this step
	// wrote the handler or named one an earlier step wrote.
	struct UnwindRegion
	{
		UnwindRegion()
			: fresh(false)
		{}

		std::string dispatch;
		std::string end;
		bool fresh;
	};
	// 12.6p1 and 8.5.1p7: `count` consecutive elements standing at `base`, each
	// built by the one call `action` names, written as the loop that one call
	// is.  `mark` is where the step that names them began, which 15.2p2's
	// handler writes again.
	void construct_element_run(const DumpNode& action,
	                           const lowir_model::Operand& base,
	                           unsigned long long count,
	                           unsigned long long stride, TypeId element,
	                           const UnwindMark& mark);
	void mark_unwind_step(bool at_call = false);
	UnwindRegion open_unwind_region(const UnwindMark& mark);
	void close_unwind_region(const UnwindRegion& region);
	// The subobject `address` names, destroyed in a block of its own.
	void replay_unwind(const LowUnwind& live);
	// 15.2p2: the subobject this step built joins the ones an exception out of
	// a later step has to destroy, unless its class ends a lifetime with
	// nothing.
	void push_unwind(const SemaEntity& constructor,
	                 const std::vector<lowir_model::Instruction>& address,
	                 const lowir_model::Operand& at,
	                 unsigned long long elements = 0,
	                 unsigned long long stride = 0);
	// The handler-stack instructions those regions are written with.
	void emit_handler(bool cleanup, const std::string& label);
	void emit_handler_end();
	void emit_resume();
	// 5.2.2p10 and 6.6.3p2: the call, and the object it returns.  `into` is the
	// storage the caller of the call named for a returned object the ABI hands
	// back through a destination; where the call returns one and no place asked
	// for it, the function gives it storage of its own.
	// `keep` is 5.3.4p1's one addition: the array form of a new-expression
	// reads the bytes it asked the allocation function for again once the call
	// has returned - for the count it writes in front of the elements and for
	// 8.5p7's zero - so the value the first argument came to is stored in an
	// object of the function where it is computed, and this takes its name.
	LowValue call_expression(const DumpNode& node,
	                         const lowir_model::Operand* into = nullptr,
	                         std::string* keep = nullptr);
	LowValue unary_expression(const DumpNode& node);
	LowValue increment_expression(const DumpNode& node, bool postfix);
	LowValue binary_expression(const DumpNode& node);
	LowValue logical_expression(const DumpNode& node);
	LowValue assignment_expression(const DumpNode& node);
	LowValue conditional_expression(const DumpNode& node, bool as_object);
	// 5.16 and 12.2p1: a conditional whose result is a prvalue of class type,
	// which is an object the function holds and each arm writes its own into.
	LowValue conditional_object(const DumpNode& node,
	                            const lowir_model::Operand* into = nullptr);
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
	void initialize_into(const LowObject& object, TypeId type,
	                     const DumpNode& node);
	void initialize_array(const LowObject& object, TypeId type,
	                      const DumpNode& node);
	// 8.5.1: the subobjects the analysis said each clause reached, written in
	// order.  Each leaf names its subobject from the object again, because that
	// is the one description of where the subobject is that does not depend on
	// how the walk got there.
	void initialize_aggregate(const LowObject& object, TypeId type,
	                          const DumpNode& node);
	void initialize_subobject(const LowObject& object, const DumpNode& node,
	                          std::vector<const DumpNode*>& path);
	// The place a store into the object itself names, the address of that
	// object, and the address of the subobject `path` names within it.
	lowir_model::Operand object_storage(const LowObject& object);
	lowir_model::Operand object_address(const LowObject& object);
	lowir_model::Operand subobject_address(const LowObject& object,
	                                       const std::vector<const DumpNode*>& path);

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
	// 8.5p7: the value an object of scalar type is value-initialized with, which
	// for a pointer is 4.10p1's null pointer value and not the `0` a program
	// writing the null pointer constant wrote.
	lowir_model::Operand zero_operand(TypeId type);
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
	// `unnamed` is what a parameter the declaration left unnamed is called,
	// which is the implementation's own `arg0` for a function 1.4p8 reserved
	// and this translation's `__param0` for every other.
	std::string add_slot(const SemaEntity& entity, TypeId type,
	                     const char* unnamed = "__param");
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
	// 6.6.3p2: the destination the caller named for the returned object, where
	// this function is one that returns an object indirectly.  Every return of
	// the function creates its object there rather than in storage of its own.
	lowir_model::Operand result_object_;
	bool indirect_result_;
	// 12.8p31: the one local object this function's returns copy into that
	// destination, which is then the object standing in it.
	const SemaEntity* return_slot_local_;
	std::unordered_map<std::uint32_t, std::string> slots_;
	// 12.2p1 and 12.8p31: the address a temporary that was created in storage
	// the place asking for it already owned stands at.  Such a temporary has no
	// slot of its own, so what names it is the destination it was built in, and
	// every later reader of the same temporary reads that address.
	std::unordered_map<std::uint32_t, lowir_model::Operand> placed_;
	std::unordered_set<std::string> slot_names_;
	// 3.3.3p4: the suffix the last slot named after one identifier took, so the
	// next one starts from there rather than from the first suffix again.
	std::unordered_map<std::string, unsigned> slot_shadows_;
	// The names the function's own declarations already use, which a generated
	// temporary must not take.
	std::unordered_set<std::string> taken_;
	std::vector<std::string> breaks_;
	std::vector<std::string> continues_;
	// 8.5p7: how far into the object it is initializing each open
	// initialization has written, in bytes.  A bit-field whose storage unit is
	// past that may take the unit whole, because the bits beside its own are
	// ones the initialization is giving zero to; one whose unit reaches back
	// into what is already there has to leave those bytes alone.  The frames
	// nest because an element of an array and a member with a constructor are
	// each an initialization of an object of their own.
	std::vector<unsigned long long> written_through_;
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
	// The block each label of this function names.
	std::unordered_map<std::string, std::string> labels_;
	// 12.4p8: the destructions that follow this destructor's body, one entry
	// per call, and the two blocks 15.2p2's region around the body stands
	// between - the cleanup an exception out of it reaches, which destroys
	// every subobject, and the end the body's own exit reaches.  Both blocks
	// are reserved before the body is read, so the blocks the body opens are
	// numbered after them.  Empty for every function but a destructor with a
	// subobject to destroy.
	std::vector<LowDestruction> epilogue_;
	std::string destructor_cleanup_;
	std::string destructor_end_;
	// Whether the walk is inside 12.6.2's initialization of the subobjects of
	// the object a constructor was called on, which is the one place 15.2p2's
	// partly built object exists.
	bool unwinding_;
	UnwindMark unwind_mark_;
	std::vector<LowUnwind> unwind_live_;
	// The handler the last step was given and how many subobjects it destroys.
	// The list only grows, so a step needing the same destructions as the one
	// before it names that block again rather than writing a second copy.
	std::string unwind_dispatch_;
	std::size_t unwind_dispatch_live_;
};
