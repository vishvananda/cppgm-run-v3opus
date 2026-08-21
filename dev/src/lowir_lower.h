#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "lowir_model.h"
#include "sema_facts.h"
#include "type_model.h"

struct DumpNode;
struct SemaEntity;
class LowirFunctionLowering;
class SourcePositionTable;
class AddressTable;

// 12.4p8 and 15.2p2: how many destructions a handler still writes out as the
// destructions they are.  Each of them needs the ones behind it as its own
// cleanup, which is n(n+1)/2 calls; beyond this the same order is written as a
// chain of n blocks, each running one destruction and entering the next,
// because past here the calls stop being a description of the objects and the
// order starts being it.  The destructor's own suffix and 15.2p2's handler in
// an ordinary body write the same chain, so they read one limit.
const std::size_t kUnwindSuffixLimit = 16;


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
		, storage_owed(nullptr)
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
	// 3.2p3: the declaration this lvalue names, where naming it was not yet a
	// use of it.  9.4.2p3's member is the one such name a body writes: a use
	// that reads nothing but the value is the value `held` carries and asks the
	// program for no storage at all, while one that reads the *place* - an
	// address taken, a reference bound, an array decayed - is 3.2p2's use and
	// asks 14.7.1p1 for the definition that lays the storage out.  Null
	// wherever naming the entity already asked, which is every other lvalue.
	const SemaEntity* storage_owed;
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
		, designated(false)
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
	// 12.6.2p2 and 9.5p1: whether the member above is the subobject an
	// initialization designated rather than one a use of the name reached,
	// which is what says the object an anonymous union declared is a step of
	// its own - see `member_storage`.
	bool designated;
	// 8.5.1p1: the object is the element these subscripts name inside the array
	// the two members above reach - one entry per dimension the walk has
	// stepped through, from the outside in, which is how 5.2.1p1 would write
	// them.  Empty where the object is the whole thing the storage holds.  The
	// chain rather than a cursor is what lets the element be named again
	// wherever it is asked for, which 15.2p2's handler needs and which a
	// namespace-scope array initialized one element at a time already did.
	std::vector<ElementStep> elements;
};

// The internal LowIR spelling of a qualified name: the regions it is declared
// in written with `__` where the source writes `::`, and every other character
// an identifier cannot hold written as one `_`.  Dropping those characters
// instead would leave two names one symbol: 12.4p1's `~C` and 12.1's `C` are two
// functions of one class with one qualified name.
std::string flatten_symbol_name(const std::string& name);

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
	// produced `unit` and are read, never changed.  `positions` says where in
	// the source each terminal of that unit was written, which is what names a
	// declaration no program spells a name for; it is the token stream's and
	// may be null where the caller kept none.
	// `addresses` is 5.19p2's own answer about an initializer of pointer type -
	// *which* object it designates - which is the one thing no walk of the
	// lines below a declaration can work out for itself, because a constant of
	// pointer type holds an interned identifier and not a number.
	void add_unit(const DumpNode& unit, TypeTable& types,
	              const SourcePositionTable* positions = nullptr,
	              const AddressTable* addresses = nullptr);

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
	// 14.7.1p1 and 14.7.1p6: whether any of the initializations the body holds
	// is one this unit owes an entry for.  A definition the program itself
	// wrote owes one however little it does, and a definition an instantiation
	// made owes one only where something runs - so a unit whose one
	// namespace-scope object is an instantiated static data member of trivial
	// type writes no body at all.
	bool startup_owed_;
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
	// 3.2p3 and the ABI: which name the program gives a class's virtual
	// function table, keyed by the object-file name that identifies the table
	// across units.  A unit holding the definition of the class's key function
	// writes the table under the class's own name; one that does not names a
	// table another unit owes.  Which of those the *program* has is no fact any
	// one unit holds - the unit that owns the table can be read after the ones
	// that use it - so each unit records what it did and `finish` settles it.
	std::unordered_map<std::string, std::string> vtable_owned_;
	std::unordered_map<std::string, std::string> vtable_external_;
	// The names a unit wrote for a table the program turns out to own, as the
	// name it has to become.  Empty for every program whose units agree, which
	// is every program written as one translation unit.
	void settle_vtable_names();
	// 3.2p1 and 3.5p9: a name with external linkage is one entity of the
	// program, and a declaration of it says only that some unit defines it.
	// A unit reading a name it does not define writes that declaration before
	// the unit that *does* define it has been read, so the program can hold
	// both - and one entity is one top-level entry, so the declaration goes.
	void settle_external_declarations();

	friend class LowirFunctionLowering;
	friend class LowirUnitLowering;
};

// One translation unit, lowered.
class LowirUnitLowering
{
public:
	LowirUnitLowering(TypeTable& types, LowirProgramBuilder& builder,
	                  const SourcePositionTable* positions = nullptr,
	                  const AddressTable* addresses = nullptr);
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
	// 5.2.2p4 and 8.5.3: what one declared parameter is at the boundary.  A
	// reference is the address of what it was bound to, a class the ABI cannot
	// carry as bytes is the address of the object the caller built, and every
	// other type is the value it is.  It is the same answer for the declaration,
	// the definition and a call through a pointer, so it is written once.
	void describe_parameter(TypeId written, lowir_model::Parameter& parameter);
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

	// 3.5p9: the name the object file gives `entity` under one of 12.1's entry
	// points, encoded once per pair however often it is asked for.
	const std::string& object_symbol_of(const SemaEntity& entity,
	                                    unsigned variant);
	// 7.1.2p4 and 14.7.1p1: whether the definition this unit holds is one every
	// unit that needs it holds too.  An inline definition is the program's
	// rather than this unit's, and so is one an instantiation made: the
	// template is what the program declared, and each unit that names a
	// specialization reads that definition for itself.  The three questions
	// that follow from it - what the object file binds the symbol as, which of
	// 12.1's entry points the definition owes, and whether 3.2p3 waits for a
	// use before writing it - are one question and are asked here.
	bool shared_definition(const SemaEntity& entity);
	// Whether this unit writes the base-object entry of `entity` as a definition
	// of its own, which is what a constructor or destructor a complete object
	// and a base subobject both asked for needs.
	bool writes_base_entry(const SemaEntity& entity);
	// 12.1 and 10.1p4: whether the ABI's two entry points of `entity` do the
	// same thing.  They do wherever no base subobject of the class stands
	// somewhere a complete object of it does not - which is every class with no
	// virtual base, where the complete-object body is named twice rather than
	// written twice.  A class with one owes a base-object entry that builds no
	// shared subobject, and that entry is a different function.
	bool shares_base_entry(const SemaEntity& entity);
	// 2.14.5p8: the global holding the code units of a string literal, made
	// once per distinct literal.  The literal is an array object with static
	// storage duration and no name a program can write, so the program holds
	// it under one of its own.
	std::string string_literal(const std::string& data, TypeId array);
	// 2.14.5p8: that object where 8.5.2p1 copied the literal into an *element*
	// of an array, which is the one place the copy is all that names it.
	void kept_string_object(const DumpNode& node, TypeId element);
	// 2.14.5p8: those objects for every literal written inside an expression
	// the lowering does not read.  The literal has static storage duration
	// because the program wrote it, not because anything evaluates the
	// expression it stands in, so an initialization that comes to nothing
	// leaves the objects standing all the same.
	void kept_string_objects(const DumpNode& node);
	// 3.7.2p2 and 3.6.3p3: the runtime entries an object destroyed at the end
	// of its thread needs - the function the destruction is handed to, and the
	// handle that says which loaded image the pair belongs to.  Each is
	// declared the first time one is asked for and named by one probe after
	// that, so a program with no such object writes neither.
	const std::string& thread_atexit_symbol();
	const std::string& image_handle_symbol();
	// 3.6.3p3: the same runtime entry for an object destroyed when the program
	// ends rather than when a thread does, which 3.7.1p3's block-scope object
	// is handed to where 6.7p4 initializes it.
	const std::string& atexit_symbol();
	// 3.7.1p3 and 3.6.2: the storage such an object asks the image for, laid out
	// the first time this unit reaches its declaration, and the initializer
	// clause 6.7p4 leaves for the body to run - null where the image is the
	// whole of it.
	const DumpNode* local_static_definition(const DumpNode& node);
	// 6.7p4: the flag beside that storage which says whether this run of the
	// program has been through the declaration.
	const std::string& local_static_guard(const SemaEntity& entity);
	// 3.7.2p2 and 3.6.3p1: the action that ends the lifetime of the object a
	// declaration declared, which stands under the declaration where no point
	// of the program the declaration was written at is where it runs.
	static const DumpNode* declared_destruction(const DumpNode& node);
	// 12.4p8: the one function a runtime that ends a lifetime is handed - the
	// destructor itself, or a body of the program's own where the object is an
	// array and one call would end only its first element.
	const std::string& destruction_entry(const DumpNode& node,
	                                     const std::string& object);
	// 3.7.2p2: the body that initializes one thread's copy of `entity`, or null
	// where this unit gives the object none - which is every object but a
	// thread-local one this unit initializes with code.  A use of the name has
	// to run it before it reads the object, because no point in the program
	// stands before every thread that names one.
	const std::string* thread_initializer_of(const SemaEntity& entity);
	// Names `entity` in the program as a declaration, for a use of a function
	// or object this unit does not define.  A use of a function whose definition
	// belongs to every unit that needs one also asks for that definition here.
	//
	// 3.2p3: `used` says whether naming it is a use of it.  A name of 9.4.2p3's
	// member that reads nothing but the value is not, and asks the program for
	// no definition; every other name is, and the declaration line is written
	// either way because the *name* is what the line stands for.
	void declare_entity(const SemaEntity& entity, bool used = true);
	// 10.3p10 and the ABI: the internal symbol of `owner`'s virtual function
	// table, which a constructor or destructor of the class writes into the
	// object.  The table is emitted here the first time one is asked for -
	// or declared, where the class's key function says another unit owes the
	// program the definition - so a program that creates no polymorphic object
	// holds no table at all.
	const std::string& vtable_symbol(const SemaEntity& owner);

	// 3.2p2 and 3.5p4: the definition a use named even though this lowering
	// wrote no call of it.  A definition every unit that needs one may hold is
	// nobody's to owe where no call survives; one with internal linkage is this
	// unit's alone, so the object file holds it wherever an initialization
	// named it.
	void owe_internal_definition(const SemaEntity& entity);
	// 3.2p2 and 8.4.2p1: the same question where the initialization is 3.6.2p2's
	// image.  `read` is whether working that image out went through the
	// definition the standard gives a constructor the class declared - which is
	// what makes that definition this unit's.
	void owe_folded_construction(const SemaEntity& constructor, bool read);
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
	// `stored` says the text is the operand of `global @x : T = v`, which names
	// the whole object's storage rather than one clause of its image.
	bool image_value(const DumpNode& node, TypeId type, std::string& text,
	                 bool stored = false);
	// 2.14.4 and `lowir.md`: one floating value spelled at the width of the
	// storage that holds it.  It is one owner because a spelling that carries
	// the suffix of another width would be a value of that width, so the
	// digits of the image, of a body's immediate and of 8.5p7's zero all come
	// through here rather than each carrying a suffix of its own.
	std::string spell_floating(TypeId type, const std::string& written);
	// 8.5p7 and 8.5.4p3: the zero this translation makes, spelled from the type
	// alone - `0.0` with the storage's suffix, 4.10p1's null pointer, or the
	// digits of an integral zero.  It is what an item, and a body's immediate,
	// carries where no clause of the program stands for the value.
	std::string made_zero(TypeId type);

private:
	// 2.14.4: the digits of the floating constant a *clause* of an aggregate
	// initializer was written with, which is what an item of a structured image
	// carries.
	bool floating_image(const DumpNode& node, std::string& text);
	// 3.6.2p2 over a scalar object of floating type: what its initializer came
	// to, which is a value and not a spelling - so a conversion inside it
	// rounds and a folded call answers at all.
	bool folded_real(const DumpNode& node, long double& value);
	// The definitions of this unit, gathered before any of it is lowered.
	void collect_definitions(const DumpNode& node);
	// 3.7.2p2: the definitions with thread storage duration, lowered before any
	// body is, so that a use of one written before its definition runs what
	// initializes it as a use written after it does.
	void thread_definitions(const DumpNode& node);
	void declaration(const DumpNode& node);
	void global_variable(const DumpNode& node);
	// 3.6.2: the image half of such a definition - what the object's storage
	// holds before the program starts - and the initializer clause left for the
	// program to run, or null where the image is the whole initialization.
	// `stored` says the object is one a *declaration at namespace scope* named,
	// whose one operand names its whole storage and spells a value at the width
	// that holds it.  3.7.1p3's object a block declared is the other: it is one
	// the body would have written, so its image spells what the body spells -
	// the clause as the program wrote it, and a made zero the made way.
	const DumpNode* global_image(lowir_model::GlobalDefinition& global,
	                             const DumpNode& node, TypeId type,
	                             bool stored = true);
	void function_definition(const DumpNode& node);
	// 3.6.2 and 8.5p6: the constant a namespace-scope object is initialized
	// with, as the data the global definition holds.
	// False when the initializer names no value or address the translation
	// knows, which 3.6.2p2 makes an action rather than data.
	bool global_initializer(lowir_model::GlobalDefinition& global,
	                        const DumpNode& node, TypeId type, bool stored);
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
	// 4.10p1: the item a subobject of pointer type whose value is zero takes,
	// which is its storage rather than the integer a null pointer constant is
	// written as.  Every other item is left as it stands.
	void null_pointer_item(lowir_model::GlobalDefinition::DataItem& item,
	                       TypeId type, unsigned long long size);
	// 3.6.2p2: whether a clause of a structured image holds a call, which is
	// work the program runs and no item the translation lays out.  Asked of
	// every clause of every object 5.2.2p1 *builds* - a class's subobjects, a
	// constructor's member initializations and an array's elements alike.
	bool runs_a_call(const DumpNode& node) const;
	// 3.6.2p2: the image a `constructor-action` leaves the object holding,
	// where the constructor's own definition is nothing but 12.6.2's member
	// initializations of values the translation knows once its parameters hold
	// the call's arguments.  False for every other constructor, which leaves
	// the object to be built before the program runs.
	// 5.2.2p4: one argument of the call a `constructor-action` is, as the place
	// it stands in holds it - the node, and what 3.6.2p2 then asks of it.
	//
	// A constructor carries one place into as many members as it likes, and both
	// questions are walks of the argument's whole expression: whether it runs a
	// call, and what value it comes to.  So each is answered once and kept here -
	// the first with the place, the second the first time a member of that type
	// asks, because what an argument comes to is one value spelled at the type of
	// the subobject it initializes.  `Point(int v) : x(v), y(v) {}` reads one
	// expression once however many members it feeds, where asking again per
	// member is that expression's size times their number.
	struct BoundArgument
	{
		BoundArgument()
			: value(nullptr), runs_a_call(false)
		{
		}

		// 3.6.2p2: what the argument is worth as the storage of an object of one
		// type holds it, and whether the translation knows that at all.
		struct Image
		{
			Image()
				: held(false), known(false)
			{
			}

			bool held;
			bool known;
			std::string text;
		};

		const DumpNode* value;
		bool runs_a_call;
		std::unordered_map<TypeId, Image> images;
	};
	typedef std::unordered_map<std::uint32_t, BoundArgument> BoundArguments;

	// `outer`, where given, is the frame this construction stands inside -
	// 12.6.2p2's mem-initializer of a base names the enclosing constructor's own
	// parameters, so an argument that is one of them is worth what the call one
	// level up passed for it and not what its own name says.
	bool global_constructed(lowir_model::GlobalDefinition& global,
	                        const DumpNode& action, unsigned long long base,
	                        unsigned long long& at,
	                        const BoundArguments* outer = nullptr);
	// 5.19p2: the symbol and addend the *fold's* own answer names, where the
	// initializer came to an address this walk cannot re-read off the lines -
	// a conditional that chose between two of them, another constant pointer's
	// value, 2.14.5p8's literal.  False where the object it designates is one
	// no image may name.
	bool folded_address(std::uint32_t held, std::string& symbol,
	                    long long& addend);
	// 12.6.2p2: which member of class type the constructor call a
	// mem-initializer wrote builds, which is the object that call stands on.
	static const SemaEntity* constructed_member(const DumpNode& action);
	// 10p1 and 9.2p13: the byte the class a constructor belongs to laid its
	// direct base subobject of this type out at, which is where that base's own
	// construction writes its image.
	bool base_subobject_offset(const SemaEntity& constructor, TypeId base,
	                           unsigned long long& out) const;
	// 3.6.2p2 with 9.4.2p3: the image an object holds where the declaration that
	// wrote its initializer is not the one that defines it - a static data
	// member, whose class wrote the brace-or-equal-initializer and whose
	// definition outside the class writes none.  What the object holds is then a
	// fact of the *member*, which is the constant the analysis folded it to:
	// `SemaEntity::value` is the identifier of the interned list its subobjects
	// came to, and this walks that list against the layout the storage has.
	// One item per scalar subobject and one pass over the object, so an array of
	// n elements costs n items and no re-reading of anything.
	bool constant_image(lowir_model::GlobalDefinition& global, TypeId type,
	                    unsigned long long bits, long double real,
	                    unsigned long long base, unsigned long long& at);
	// The one item a scalar subobject of such a constant takes, at the type its
	// storage has - 3.9.1p8's two kinds of arithmetic value spelled the way
	// every other item of the image spells them.
	void constant_item(lowir_model::GlobalDefinition& global, TypeId type,
	                   unsigned long long bits, long double real);
	std::string constant_text(TypeId type, unsigned long long bits,
	                          long double real, bool stored = false);
	// Whether a constant of `type` carries the *value* an object of it holds,
	// which is what the image can spell without reading the initializer again.
	bool valued_type(TypeId type) const;
	// `bytes` of zero, added to the items when there are any to add.
	static void add_zero_item(lowir_model::GlobalDefinition& global,
	                          unsigned long long bytes);
	// 8.5.1p7: `count` elements of `element` that no clause reached, as the
	// items value-initializing each of them leaves.
	void add_zero_elements(lowir_model::GlobalDefinition& global, TypeId element,
	                       unsigned long long count, unsigned long long stride);
	// 8.5p7 over one object of `type` at `base`, with `at` where the image
	// stands: the items value-initializing it leaves.  False where a subobject
	// of it holds what no item names.
	bool zero_object_items(lowir_model::GlobalDefinition& global, TypeId type,
	                       unsigned long long base, unsigned long long& at,
	                       unsigned depth);
	// The literal `bits` of `type` is written as, signed when the type is - and
	// signed when the *LowIR* type is, for the one operand that names an
	// object's whole storage rather than a clause the program wrote.
	std::string spell_value(TypeId type, unsigned long long bits,
	                        bool stored = false);
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
	// 3.6.2p2 and 12.1p11: whether the image of a namespace-scope object holds
	// what constructing it comes to, which it does where the whole object is the
	// vpointer the standard's own default constructor writes.
	bool vpointer_image(const SemaEntity& built, TypeId type);
	// 12.4 and 5.3.5p3: this unit owes the destructor's deleting entry, whether
	// a table it writes named the slot or the definition it holds is one no
	// other unit may define.
	void owe_deleting_entry(const SemaEntity& entity);
	void demand_definition_by_id(std::uint32_t entity);
	// 3.2p2 and 3.2p3: the functions the resolved tree names anywhere, which is
	// what odr-uses them - a body this unit does not write still uses what it
	// calls.  They are gathered in the order the tree names them and asked for
	// once every use the unit itself wrote has been answered, so a definition a
	// written body asked for still stands where that body asked for it.
	// `running` says the walk stands inside a body the program carries out,
	// which is what tells a call that runs from one 3.6.2p2 folded into the
	// image.
	void demand_referenced(const DumpNode& node, bool running = false,
	                       bool befriended = false);
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
	// Where the startup body stands - how many blocks it has and how much the
	// last of them holds - which is what says whether the initialization just
	// added to it runs anything.
	std::pair<std::size_t, std::size_t> startup_mark() const;
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
	// 3.6.3p1: the destructor of an object with static storage duration, added
	// to the program's one shutdown function.
	void static_destructor(const DumpNode& node);
	// 3.7.1p3: the symbol the image gives an object a block declared `static`,
	// which the program spells no name that reaches - so it is made of the
	// function whose body declared it and where in that body it stands.
	std::string local_static_symbol(const SemaEntity& entity);
	std::string local_static_owner(const SemaEntity& owner);
	std::string local_static_place(const SemaEntity& entity,
	                               const SemaEntity* owner);
	// 3.5p3 and 7.1.2p4: whether the definition that declared the object is
	// one every unit may hold, which is what makes the object one object of
	// the whole program rather than one of this unit's.
	bool local_static_shared(const SemaEntity* owner);
	lowir_model::SymbolBindingMode local_static_binding(
		const SemaEntity& entity);
	// 3.6.2p1: whether the initializer denotes an object the image lays out,
	// which is what makes the binding of a reference a constant initialization.
	bool static_bound(const DumpNode& node);

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
	// 6.7p4: the flag of each block-scope `static` this unit has named, so that
	// the body reaching one declaration twice - a `for` around it, an unwind
	// path past it - names one flag.
	std::unordered_map<std::uint32_t, std::string> local_static_guards_;
	// 7.1.2p4: how many of the block-scope statics of one shared definition
	// this unit has already named, which is what a translation unit can agree
	// with another one on where the terminals it read cannot.
	std::unordered_map<std::string, unsigned> local_static_places_;
	// Where in the source this unit's terminals were written, which is what
	// two units reading one shared definition do agree on.  Null where the
	// caller kept no such record, and then the counter above stands in.
	const SourcePositionTable* positions_;
	// 5.19p2: the pool the identifier a constant of pointer type carries
	// indexes into.  Null where the caller kept none, and then an image is owed
	// only where the lines below the declaration spell the address themselves.
	const AddressTable* addresses_;
	// 12.4p8: the body that ends every element of one array, against the symbol
	// the array was laid out under.
	std::unordered_map<std::string, std::string> destruction_entries_;
	// The bodies this unit generated while another body was being lowered,
	// which is where the list `program_.functions` is has a reference into it.
	std::vector<lowir_model::Function> pending_functions_;
	void write_pending_functions();
	// The base-object entry's own symbol, for the constructors and destructors a
	// base subobject asked for after a complete object already had.  It is kept
	// apart from `entity_symbols_` because one declaration then stands under two
	// names, and which of them a call writes is a fact about the call.
	std::unordered_map<std::uint32_t, std::string> base_entry_symbols_;
	// 3.5p9: the *object file's* name for each of those, keyed by the
	// declaration and which of 12.1's entry points is being named.  The
	// encoding of one name is proportional to what it spells - 9.8p1's local
	// name repeats the whole encoding of the function whose body declared the
	// class - so a member asked about twice is encoded once here rather than
	// once per question.
	std::unordered_map<std::uint64_t, std::string> object_symbols_;
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

	// -- 10.3p10 and the ABI: the polymorphic object model's own data --------

	// 3.4.3: whether a qualified name reaches `owner` from the namespace it
	// stands under, which is what lets the internal spelling of its table be
	// that name.  A class a function declares is reached by no such name, and
	// two functions can declare two of one spelling, so those are spelled by
	// the encoding of the type instead.
	static bool named_from_namespace_scope(const SemaEntity& owner);
	// The internal spelling of the class - `class_B`, `struct_N__S`, or the
	// encoded type for one no qualified name reaches.
	std::string class_tag(const SemaEntity& owner);
	// 5.2.8: the type-information record of `owner`, and the string it names
	// the type by, emitted on demand.  A base class has one wherever a class
	// below it does, whether or not the base itself dispatches.
	const std::string& rtti_symbol(const SemaEntity& owner);
	// The ABI's own type-information class vtables, which every record points
	// into and no unit defines.
	const std::string& external_rtti_vtable(unsigned bases);
	// 10.4p2: the ABI's stand-in for a slot whose final overrider is pure,
	// declared with the signature of the first such slot the unit emits.
	const std::string& pure_virtual_symbol(const SemaEntity& member);
	// The slots of `owner`'s table, written into the global `into`.
	void write_vtable_slots(const SemaEntity& owner,
	                        lowir_model::GlobalDefinition& into);
	// 12.4 and 5.3.5p3: the deleting entry over a destructor's definition,
	// which is that definition with the deallocation added to 12.4p8's suffix.
	void deleting_definition(const SemaEntity& entity);
	// The tables this unit has named, so one class's is emitted once.
	std::unordered_map<std::uint32_t, std::string> vtable_symbols_;
	std::unordered_map<std::uint32_t, std::string> rtti_symbols_;
	std::string pure_virtual_;
	// The destructors a table has named the deleting entry of and that are not
	// written yet, and the ones already asked for - the table and the
	// definition can be reached in either order, so the ask is recorded rather
	// than acted on where it is made.
	std::vector<const SemaEntity*> deleting_owed_;
	std::unordered_set<std::uint32_t> deleting_entries_;
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
	// `function-definition` of the resolved tree.  `variant` says which of the
	// ABI's entry points this body stands under, which for `kDeletingObjectAbi`
	// adds 5.3.5p3's deallocation to what the destructor's own definition does.
	void run(const DumpNode& node, TypeId type,
	         unsigned variant = 0 /* kCompleteObjectAbi */);

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
	// 3.7.1p3 and 6.7p4: the declaration of an object a block declared
	// `static`, as the body that reaches it runs it - the flag that says
	// whether this run of the program has been through the declaration, and the
	// initialization and hand-off of the destruction the first run does.
	void local_static_variable(const DumpNode& node);
	// 3.6.3p3 and 3.7.2p2: the end of an object's lifetime, handed where the
	// object was begun to the runtime that knows when it comes.
	void hand_to_runtime(const std::string& runtime, const std::string& object,
	                     const lowir_model::Operand& address,
	                     const DumpNode& node);
	// 8.5: what one declaration writes into the storage it was given.  The
	// address is the one already in hand where the caller had to compute it -
	// 3.6.3p3's hand-off of a block-scope `static` to the runtime is that
	// caller - and null where the storage is the whole of what it named.
	void initialize_declared(const DumpNode& node,
	                         const lowir_model::Operand& storage,
	                         const lowir_model::Operand* address_in_hand);
	// The storage and the initialization one declaration writes, which is the
	// whole of it but 3.8p1's beginning of a lifetime.
	void declare_local(const DumpNode& node);
	// 6.4p4: the condition of a selection or iteration statement, lowered as
	// the branch it is rather than as a value that is then tested.
	void branch_on_condition(const DumpNode& node, const std::string& on_true,
	                         const std::string& on_false);
	// The same over the expression the condition wrote, which 5.14 and 5.15
	// lower as their own control flow.
	void branch_on_value(const DumpNode& node, const std::string& on_true,
	                     const std::string& on_false);
	// The same, answering which edge the condition already stands for: 1 for
	// the true one, -1 for the false one and 0 where a terminator was written.
	// A folded edge is left for the caller to jump to, because a condition that
	// is a literal is one the statement around it jumps for.
	int branch_or_fold(const DumpNode& node, const std::string& on_true,
	                   const std::string& on_false);
	// 6.4p4 and 5.19: the edge a condition that *is* a literal already stands
	// for, asked of the expression and emitting nothing - so an operand of `&&`
	// or `||` that stands for one is known to before a block is reserved for the
	// operand beside it.
	int folded_edge(const DumpNode& node) const;
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
	//
	// `designated` is 12.6.2p2's question rather than 5.2.5p1's: see the
	// definition for what the two answer differently about 9.5p1's object.
	lowir_model::Operand member_storage(const DumpNode& object,
	                                    const SemaEntity& member,
	                                    bool bound = false,
	                                    bool designated = false);
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
	// 10p1 and 4.10p3: the address of a base subobject, and the same address
	// asked of a pointer that may hold 4.10p1's null pointer value.
	// 5.2.9p11's cast to a derived class is the same step back, which
	// `downward` is what says.
	lowir_model::Operand base_step(const lowir_model::Operand& from,
	                               unsigned long long offset,
	                               bool downward = false);
	lowir_model::Operand null_preserving_base_step(
		const lowir_model::Operand& from, unsigned long long offset,
		bool downward = false);
	void store_pointer(const lowir_model::Operand& value,
	                   const lowir_model::Operand& storage);
	// 12.1p5: the constructor call an object of class type is initialized by,
	// on the address of that object.  `always` says the object has no other
	// mark of its lifetime beginning, so even a constructor that does nothing
	// is the call it is - which is what a temporary no declaration named needs
	// and what an object a declaration named does not.
	// `storage` says what stands at `address` is storage 5.3.4 obtained rather
	// than an object of the function's own, which is what 8.5p7's zero covers
	// there: the whole extent the allocation asked for, and not only the bytes
	// an object of the class holds.
	// `standing_alone` says the object being built is 12.2p1's own temporary,
	// standing in storage of the function's that nothing else named: the step
	// that builds one is no place a region left pending for the objects already
	// standing belongs inside.
	void constructor_call(const lowir_model::Operand& address,
	                      const DumpNode& node, bool always = false,
	                      TypeId zeroed = kNoType, bool storage = false,
	                      bool standing_alone = false);
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
	// 12.8p28: the one step `node` holds, run over every element of the array
	// member it walks.  The step names the arrays and the walk names the
	// element, so the assignment of an array member is written once here for
	// however many elements the member holds - one step per element while a
	// reader still wants to count them, and one loop past that.
	void array_transfer(const DumpNode& node);
	// 12.8p15 and p28: the element of the array this line names that the walk
	// standing over it has reached, which is what every reader of that line
	// inside one step of an array member's transfer is given.
	LowValue walked_element(const DumpNode& node);
	// Whether such a walk is standing and this line is one of the arrays it
	// walks - a name or a member access whose type is an array, which inside
	// one of those steps is only ever the object being written into or the
	// object being read from.
	bool walks_this_array(const DumpNode& node) const;
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
	// 5.16p3: the conditional an initializer reads its object out of, where
	// that conditional selects among objects that already stand somewhere.
	// Only one of the two exists on each path, so a destination the
	// translation named is filled on the path that chose the object it reads
	// rather than once at the end over an address both paths wrote.  A
	// destination the *program* declared is filled where its declaration
	// stands, which is why this is asked at the hand-off and nowhere else.
	const DumpNode* selecting_conditional(const DumpNode& node, TypeId type);
	// Whether the initializer was written over the arms of such a conditional,
	// which is what says `place_class_object` has nothing further to write.
	bool place_over_conditional(const lowir_model::Operand& destination,
	                            TypeId type, const DumpNode& node);
	// The node standing in for `node` while an arm of a conditional is being
	// written, which is that arm.
	const DumpNode& selected(const DumpNode& node) const;
	// `place_class_object` with 5.16p3's selection already asked, which is what
	// every step below the hand-off is: the cast an initialization was written
	// under is one step of the same hand-off and not a second destination.
	LowValue place_created_object(const lowir_model::Operand& destination,
	                              TypeId type, const DumpNode& node);
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
	// 5.2.3p3 and 12.2p1: the same for an *array* prvalue, whose elements the
	// clauses standing under it initialize where they stand.
	LowValue array_object_slot(const DumpNode& node, TypeId type,
	                           const char* prefix);
	// 5.18p1 and 5.2.9p4: whether the array prvalue this node writes is one a
	// discarding of the open full-expression names.
	bool names_a_discarded_array(const DumpNode& node) const;
	// 5.2.2p4: one argument of a call standing where a parameter of class type
	// is, which is the storage that parameter object occupies.
	lowir_model::Operand class_argument(const DumpNode& node, TypeId type);
	// 8.3.5p5: the same for the one parameter whose type is an array, which
	// carries the address of an array object of the caller's own.
	lowir_model::Operand array_argument(const DumpNode& node, TypeId type);
	// Which array object that storage is opened for - the by-value parameter
	// 8.3.5p5 leaves carrying one, or 8.5.3p5's temporary where a
	// braced-init-list stands at a reference to an array.  kNoType where the
	// argument names an array that already stands somewhere.
	TypeId passed_array(TypeId parameter, const DumpNode& argument);
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
	// 12.8p31: the automatic object of the function's own returned type that an
	// id-expression standing in a return names, and nothing otherwise.
	const SemaEntity* returned_name(const DumpNode& node);
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
	// The same over the declaration 5.3.5p9 chose rather than over a line the
	// analysis wrote, which is what the ABI's deleting entry has: no
	// delete-expression stands under it.
	void deallocation_call(const SemaEntity& release,
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
	// 12.8p15 and 9p6: whether the copy this initializer comes to carries no
	// byte - `node` is worth an object of the destination's own class, and the
	// class has no non-static data member and no base subobject to read out of
	// it.  Asked *before* the initializer is read, because what the copy would
	// have carried is what says whether the object it reads is made at all.
	bool copies_no_byte(const DumpNode& node, TypeId type);
	// The same question asked of a `constructor-action` 13.3 answered with
	// 12.8p15's own transfer, whose one operand is what the copy would read.
	bool transfers_no_byte(const DumpNode& action, TypeId type);
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
	// object.  `prefix` is what asked: an argument, a return.  `addressed` is
	// 5.2.2p4's other half - whether what the place asking takes is the address
	// of that storage rather than the bytes standing in it.
	lowir_model::Operand class_value_slot(const DumpNode& node,
	                                      const LowValue& value, TypeId type,
	                                      const char* prefix,
	                                      bool addressed = false);
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
	// 8.5p7 over the elements of an array none of which an initializer reached:
	// each is value-initialized where it stands, named from the array it is an
	// element of - so an element that is itself an array is that same walk one
	// dimension in, and the addresses read as the dimensions the declarator
	// wrote rather than as one flat run of bytes.
	void zero_elements(const lowir_model::Operand& address, TypeId type);
	// Whether that walk is what the zero of `type` is written as.  A scalar is
	// one store and an array is the question asked of its own element; a class
	// is neither - what its zero comes to is the storage it holds - so it is
	// left to the span.
	bool zeroed_elementwise(TypeId type);
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
			, release(nullptr)
		{}

		const DumpNode* action;
		bool element;
		unsigned long long index;
		// How many elements this one entry destroys: zero for the single object
		// or single element every other entry is, and the whole bound for an
		// array written as the loop 12.4p8's order is.
		unsigned long long count;
		// 5.3.5p3: the deallocation function this entry is a call of, for the
		// one entry of the ABI's deleting destructor that ends no lifetime.  It
		// stands last, so every subobject is gone before the storage is - and
		// 15.2p2 makes it the step every handler of the ones before it ends
		// with, which is what the same list already writes for a destruction.
		const SemaEntity* release;
	};
	// The destructions after the body, with the handler each of them needs for
	// the subobjects still standing behind it.
	void destructor_epilogue();
	// One entry of that list, which is a destruction for every entry but the
	// deallocation the ABI's deleting entry ends with.
	void epilogue_step(const LowDestruction& step,
	                   const std::string* cleanup = nullptr,
	                   bool suffix = false);
	// Reads that list off the definition and answers where the body ends among
	// its children.
	std::size_t collect_epilogue(const DumpNode& node);
	// 12.1p11 and 12.4p11: the store that makes the object being built or torn
	// down an object of `owner` - written after the base subobject was built and
	// before this class's members are, and at the head of what a destructor
	// does, because between those two points 10.3p12 dispatches on this class.
	void write_vpointer(const SemaEntity& owner);
	// 5.3.5p3 and 12.5p4: the call the ABI's deleting entry ends with, which
	// gives the storage of the complete object back to the function 5.3.5p9
	// chose in the class the destructor belongs to.
	void deleting_release_call(const SemaEntity& release, TypeId destroyed);
	// 10.3p12: the address of the final overrider the object standing at
	// `object` has for the table entry `slot`, read out of that object's own
	// vpointer.  Which class the object turns out to be is what the vpointer
	// says, so nothing here names one.
	lowir_model::Operand dispatch_slot(const lowir_model::Operand& object,
	                                   unsigned slot);

	// 15.2p2: one subobject the constructor being lowered has already built,
	// held as the instructions that named it rather than as the address they
	// produced.  The handler stands in a block of its own, and a temporary of
	// the block the construction stands in is not one that block may name, so
	// the subobject is named there the way the step that built it named it.
	struct LowUnwind
	{
		LowUnwind()
			: destructor(nullptr)
			, object(nullptr)
			, base_subobject(false)
			, elements(0)
			, stride(0)
		{}

		const SemaEntity* destructor;
		// 3.8p1: the object this entry ends, where a declaration or a prvalue
		// named one - which is what says the entry goes away again when the
		// program's own end of that lifetime is written.  Null for 12.6.2's
		// subobjects, which no name reaches and whose entries the whole
		// constructor holds.
		const SemaEntity* object;
		bool base_subobject;
		std::vector<lowir_model::Instruction> address;
		lowir_model::Operand at;
		// 12.6p1 and 3.8p1: the object is an *array*, so what the handler owes
		// it is every one of these elements and not the one `at` names - the
		// same walk the end of its block writes, written out below
		// `kArrayLoopLimit` and as one loop past it.  Zero is the one object
		// `at` names, which is every entry that is not an array.
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
			, live(0)
			, chained(false)
			, ended(0)
			, block(0)
			, at(0)
		{}

		std::string dispatch;
		std::string end;
		bool fresh;
		// 15.2p2: the objects standing when the region opened, which is what
		// its handler owes.  They are taken at the open and not read off the
		// standing ones at the close because 12.2p3's end of a temporary's
		// lifetime is written inside the region that covered the temporary: an
		// exception out of the code before that end still has the object to
		// destroy, and by the close that object is no longer standing.
		std::size_t live;
		std::vector<LowUnwind> owed;
		// 12.6.2p10 again: whether this handler owes the one object the step
		// before it added plus everything that step's own handler owed, which
		// is what makes n standing objects n destructions and not n(n+1)/2 -
		// and what lets the region hold one entry rather than a copy of the
		// whole list.  The handler it enters is named here for the same reason
		// the list is: both are what was standing when the region opened.
		bool chained;
		std::string behind;
		// How many lifetimes the program had ended when the region opened.  A
		// region no end of a lifetime falls inside owes exactly the objects
		// still standing at its close, so it keeps no list of its own; only one
		// that does needs the list as it was, and there is one such list rather
		// than one per region.
		unsigned long long ended;
		// 15.2p2: where the `eh_try` went in, which is what says whether a
		// close asked to end the region where a step began would leave it
		// holding anything at all.
		std::size_t block;
		std::size_t at;
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
	// 15.2p2 in an ordinary body.  A handler stands around whatever is being
	// written whenever some object's lifetime is open and a call that may throw
	// is written under it: the objects are the ones a declaration or 12.2p1's
	// temporary began, the region begins where the step that made the call
	// began, and it ends where the set of live objects changes, where the
	// full-expression ends, or where the block does.
	//
	// The step a call belongs to, taken by the outermost call of an expression
	// so that a call written as its operand stands inside the same region.
	// Answers whether this caller took it.
	bool mark_call_step();
	void release_call_step(bool taken);
	// One call about to be written.  15.4p1's non-throwing call is not a place
	// an exception leaves by, so it needs no handler around it.
	// `returned_object` says the call is 12.8p15's transfer filling 6.6.3p2's
	// destination, which opens no region of its own: nothing of this function
	// stands after it, so a handler it needed would be one over objects the
	// caller is about to see destroyed anyway.  A region already open around
	// the full-expression still covers it.
	// `building` says the call is the construction of 12.2p1's own temporary:
	// the step that builds one is not where a region left pending for the code
	// after the last change of the live set belongs.
	void note_call(bool throwing, bool returned_object = false,
	               bool building = false);
	// 1.9p10: a full-expression opened and closed, which is where 12.2p3 ends
	// the temporaries it created and where the region around it ends.
	void open_full_expression();
	void close_full_expression();
	// The region now standing, closed - and, where objects are still alive
	// inside an open full-expression, a new one left pending for whatever is
	// written next.  A pending region that nothing follows is no region.
	void close_region();
	// The same, ending where the step now being written began: what that step
	// has already written moves out of the region and stands after it.
	void close_region_at_step(std::size_t step);
	void settle_pending_region();
	// 3.8p1: the object `node` begins a lifetime for joins the ones an
	// exception has to end, and the region around the step that built it is
	// written where a later call in the same full-expression would find it.
	void begin_object_lifetime(const DumpNode& node, const UnwindMark& mark,
	                           const lowir_model::Operand& at,
	                           const std::vector<lowir_model::Instruction>& address);
	// 3.8p1: the program's own end of that lifetime, which takes the object
	// back out of the set an exception would end.
	void end_object_lifetime(const DumpNode& node);
	// 15.2p2: whether a call stands under a handler, and whether lowering an
	// expression begins a lifetime something after it has to end.
	bool guarded_call(const DumpNode& node);
	bool begins_lifetime(const DumpNode& node);
	UnwindRegion open_unwind_region(const UnwindMark& mark);
	void close_unwind_region(const UnwindRegion& held);
	// 3.8p1: the elements of the object this entry ends, where its type is an
	// array - which is what makes its end one end per element.
	void array_entry(TypeId type, LowUnwind& live);
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
	// 12.2p3 and 5.16p1: the end of the temporaries the arm control took
	// created, written where that arm's own block ends.
	void end_conditional_arm(const DumpNode& node, unsigned arm);
	// 5.14p1 and 5.15p1: whether the left operand alone decides a short
	// circuit, which is what keeps the right one - and every symbol it names -
	// out of the program entirely.
	bool decided_logical(const DumpNode& node, unsigned long long& value) const;
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
	// 8.5.3p5 and 12.2p1: the address a reference's storage is written with -
	// the object the initializer named, or the temporary it binds where the
	// initializer was a value and not an object.
	lowir_model::Operand bound_address(const LowValue& value, TypeId bound);
	// Clause 4: `value` converted to `target`, as the one conversion the two
	// types call for.  `bound` is 12.2p1's name for the storage a reference
	// target gives the temporary it binds, which the place that asked for the
	// conversion is what says.
	lowir_model::Operand converted(const LowValue& value, TypeId target,
	                               const char* bound = "refarg");
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
	// 12.2p1: where an object no declaration named stands, and the slot one was
	// given.  These are the two writers of that, because 12.6p1's and 12.8p15's
	// element walks **run a step written once** for each element of an array:
	// the objects one element's run creates - the storage a by-value parameter
	// stands in, a temporary a default argument asked for - are that element's
	// own, so a run records what it placed and drops it at its end rather than
	// leaving it to answer the next element's run of the same step.
	void place_object(std::uint32_t entity, const lowir_model::Operand& at);
	void name_object(std::uint32_t entity, const std::string& slot);
	// One element's run of such a step.  `close_element_run` takes the value
	// `open_element_run` returned, which is where this run's own objects begin.
	std::size_t open_element_run();
	void close_element_run(std::size_t opened);
	// The storage a name denotes: the slot a local was given, or the global
	// symbol a namespace-scope object has.
	LowValue storage_of(const SemaEntity& entity);
	// 9.4.2p3: whether a name of this declaration reads a value rather than the
	// object 9.4.2p2's definition lays out, which is what makes the naming no
	// use of it under 3.2p3.
	bool folds_to_value(const SemaEntity& entity) const;

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
	// 6.6.3p2: where the caller named no destination, the returned object
	// stands in storage of the function's own - and it is *the* returned
	// object of the function, one however many returns write it, because no
	// two of them are ever standing at once.  The slot is opened where the
	// first return needs it and named again by every other.
	lowir_model::Operand returned_object_storage_;
	bool returned_object_open_;
	// 6.6.3p2 and 12.8p31: whether the construction about to be lowered is the
	// one that builds the object the return hands back.  The prvalue the return
	// wrote and that object are one, so what initializes the result object is
	// the constructor the elision left standing and not 8.5p7's zero the
	// prvalue would have held.  It is read and cleared by the one call it marks,
	// because an argument written inside that call builds an object of its own.
	bool returned_object_;
	// 12.6.2p2 and 9.5p1: whether the member-expression about to be lowered is
	// the subobject a constructor's own initialization designated rather than
	// one a use of the name reached.  A subobject of class type is built by a
	// call, so what names it is an ordinary member-expression under that call's
	// object - and the two questions part company over 9.5p1's object alone.
	// It is read and cleared by the one member-expression it marks, because the
	// object expression under that one names a member of its own.
	bool designated_subobject_;
	// 12.8p31: the one local object this function's returns copy into that
	// destination, which is then the object standing in it.
	const SemaEntity* return_slot_local_;
	std::unordered_map<std::uint32_t, std::string> slots_;
	// 8.3.5p5: the places of this function that carry an array as the address
	// of the caller's, which is what says a use of one is a read of that
	// address rather than of an object standing where the place does.  Only
	// 8.5.1p2's constructor of an aggregate declares one, so every other
	// function keeps this empty and pays nothing for it.
	std::unordered_set<std::uint32_t> decayed_arrays_;
	// 12.2p1 and 12.8p31: the address a temporary that was created in storage
	// the place asking for it already owned stands at.  Such a temporary has no
	// slot of its own, so what names it is the destination it was built in, and
	// every later reader of the same temporary reads that address.
	std::unordered_map<std::uint32_t, lowir_model::Operand> placed_;
	// The objects the element runs standing here created, this run's last.  A
	// run is the only thing that writes to it, so a function that walks no
	// array keeps it empty and pays nothing for it.
	std::vector<std::uint32_t> element_objects_;
	unsigned element_runs_;
	// 5.16p3: while one arm of a conditional is being written, that arm stands
	// where the conditional does for every reader of it.  The map is empty
	// everywhere else, so no expression pays for it, and an entry standing is
	// also what stops the distribution from finding the same conditional again.
	std::unordered_map<const DumpNode*, const DumpNode*> selected_arms_;
	// 12.8p15 and p28: while one element of an array member's transfer is being
	// written, every line of the step that names one of the arrays it walks
	// names the element the walk stands at.  The step is written once and run
	// per element, so the index is the only thing that tells two of them apart
	// - a number where the elements are written out, and the loop's own counter
	// where the bound is written as one.  Inactive everywhere else, so no
	// expression outside such a step pays anything for it.
	struct ElementWalk
	{
		ElementWalk()
			: active(false)
			, counted(true)
			, index(0)
		{}

		bool active;
		// Whether the element is the number `index` or the value `cursor`
		// holds.
		bool counted;
		unsigned long long index;
		lowir_model::Operand cursor;
	};
	ElementWalk element_walk_;
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
	// 5.3.5p3 and 12.5p4: the class whose destructor this body is, which is
	// what says how much storage the ABI's deleting entry gives back.  No type
	// for every other function.
	TypeId destroyed_class_;
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
	// 15.2p2: the handler already written for each number of standing objects,
	// which is what lets a step needing exactly what an earlier one needed name
	// that block again.  The list is a prefix of the standing objects, so an
	// entry stays good while those objects do: a lifetime ending takes the
	// entries past it away, and one beginning leaves the ones before it alone.
	std::vector<std::string> dispatch_cache_;
	// 3.8p1: how many lifetimes the program has ended, and the objects the
	// program's own ends have taken out of the standing list since the region
	// now open opened - each with the place it stood at, which is what puts it
	// back where a handler wants it.  What the region owes is what stood when
	// it opened, and an end of a lifetime is one entry rather than a copy of
	// the whole list, so n ends inside n regions cost n and not n².
	unsigned long long ended_lifetimes_;
	std::vector<std::pair<std::size_t, LowUnwind> > ended_in_region_;
	// The region now standing around what is being written, in an ordinary
	// body: whether there is one, what it is, and whether the last close left
	// one for the next instruction to open.
	UnwindRegion region_;
	bool region_open_;
	bool region_pending_;
	// Whether a call has been written since the mark, which is what says the
	// step 15.2p2's handler would cover holds anything.  The second is the same
	// question asked of 15.4p1's calls alone: where no object stands yet, the
	// handler would end no lifetime at all, so it is written only where an
	// exception could reach it - and where one does stand, the step is covered
	// however its calls are specified.
	bool call_since_mark_;
	bool throwing_since_mark_;
	// Whether the region machinery is writing its own instructions, so that the
	// jump and the handler-stack instructions a close writes do not re-enter it.
	bool closing_region_;
	// 15.2p2: whether what is being written is the naming of storage a new
	// object will stand in, which is no place an exception could leave the
	// objects already standing - so a region left pending opens after it.
	bool naming_storage_;
	// 1.9p10: how many full-expressions are open, and how many calls that may
	// throw are still being lowered around the point being written.  A
	// temporary created where a call is still to be made is one that call may
	// throw past, which is what says the step that built it needs a handler.
	unsigned full_expressions_;
	// 5.18p1 and 5.2.9p4: the array prvalues a discarding of the open
	// full-expression names, found by the descent `discarded_class_object`
	// makes through the cast and the comma that hand an object on, and read
	// where the reading that lowers one asks what 8.5.3p5 names its storage
	// after.  A class temporary carries that name on the object the analysis
	// made and an array prvalue has none, so this is where it stands.
	std::vector<const DumpNode*> discarded_arrays_;
	unsigned pending_calls_;
	unsigned pending_throwing_calls_;
	// How deep the lowering stands inside the call that opened the current
	// step.  A call written as an operand of another belongs to the step that
	// one opened, so only the outermost of them marks where the step began.
	unsigned step_depth_;
	// The answer `begins_lifetime` gave for a node, so an expression whose
	// operands nest n deep is walked once rather than once per call in it.
	std::unordered_map<const DumpNode*, bool> begins_lifetime_;
};
