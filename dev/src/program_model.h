#pragma once

#include <cstdint>
#include <deque>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <vector>

#include "entity_model.h"
#include "name_table.h"
#include "type_model.h"
#include "value_model.h"

// What an object of the program image is, which is also which of the three
// blocks of the output format it belongs to.
enum class SymbolKind
{
	Variable,
	Function,
	Temporary,
	StringLiteral
};

// One object of the PA8 mock program image.
//
// A symbol is the whole program's, not one translation unit's: two
// declarations of a name with external linkage in two units denote one object,
// and the object holds what either of them said about it.  Its constant
// initializer is kept as a value rather than as bytes, because a pointer's
// bytes are not decided until the image is laid out.
struct Symbol
{
	Symbol();

	SymbolKind kind;
	std::uint32_t id;
	TypeId type;
	// The constant initializer.  `bytes` is 8.5.2's character array copy of a
	// string literal, and `value` is every other object's.
	ConstValue value;
	std::string bytes;
	bool from_string;
	bool initialized;
	// 5.19p2: an lvalue-to-rvalue conversion in a constant expression may read
	// this object, because it is `constexpr` or a const integral object with a
	// constant initializer.
	bool readable;
	bool defined;
	bool is_inline;
	bool is_thread_local;
	bool is_constexpr;
	// How many declarations of the whole program have reached this object,
	// which is what says whether the next one is the first and so may still
	// decide what 7.1.1p1 makes every declaration agree on.
	std::uint32_t declarations;
	std::uint32_t unit;         // the translation unit that defined it
	unsigned long long offset;  // its place in the image, after layout
};

// The objects of the program, their identity across translation units, and
// their places in the image.
//
// 3.5 is what makes this program wide: a declaration with external linkage
// names the same entity in every translation unit that declares it, so the
// declaration is keyed on the namespace it is in, its name and, for a
// function, its signature.  A declaration with internal linkage is keyed on
// nothing and simply makes a new object, which is exactly what 3.5p3 says it
// means.
class ProgramImage
{
public:
	explicit ProgramImage(TypeTable& types);

	// The units are linked in command line order, which is the order the
	// output format appends what each of them first declares.  A namespace is
	// a declarative region of one unit and its object dies with it, so the
	// cache that is keyed on one is dropped here while the interned paths it
	// produced - which are what the whole program agrees on - are kept.
	void begin_unit();
	std::uint32_t unit() const { return unit_; }

	// The object a namespace scope declaration of `name` in `home` denotes,
	// created when the program first declares it.
	Symbol& declare(SymbolKind kind, const Namespace& home, NameId name, TypeId type,
	                bool internal);
	// 8.5.3p5: the temporary a reference bound to a prvalue is bound to.
	Symbol& add_temporary(TypeId type);
	// 2.14.5: the array object a string literal denotes.  `in_program` is
	// false for a literal written where translation only evaluates it - a
	// `static_assert` condition or an array bound - which the image holds
	// nothing for; the object still exists, because the expression has to be
	// analysed against it.
	Symbol& add_string_literal(TypeId type, const std::string& bytes, bool in_program);

	Symbol& at(std::uint32_t id) { return symbols_[id - 1]; }
	const Symbol& at(std::uint32_t id) const { return symbols_[id - 1]; }

	// Places every object and writes the image, magic bytes first.
	void write(std::ostream& out);

private:
	// What makes two declarations in two units declare one entity.
	struct LinkKey
	{
		std::uint32_t scope;
		NameId name;
		std::uint32_t signature;  // a function's parameter list, else zero

		bool operator==(const LinkKey& other) const
		{
			return scope == other.scope && name == other.name &&
				signature == other.signature;
		}
	};

	struct LinkKeyHash
	{
		std::size_t operator()(const LinkKey& key) const;
	};

	// A namespace as the whole program names it: the interned path of names
	// from the global namespace, and whether that path passes through an
	// unnamed namespace.
	struct Scope
	{
		std::uint32_t id;
		bool internal;
	};

	Scope scope_of(const Namespace& space);
	std::uint32_t intern_path(std::uint32_t enclosing, NameId name);
	Symbol& create(SymbolKind kind, TypeId type);
	unsigned long long size_of(const Symbol& symbol) const;
	unsigned long long align_of(const Symbol& symbol) const;
	void layout();
	void write_symbol(std::ostream& out, const Symbol& symbol) const;
	// The object representation of a scalar object's constant initializer,
	// with every address already relocated to its place in the image.
	void encode(const Symbol& symbol, std::string& out) const;

	TypeTable& types_;
	std::uint32_t unit_;
	std::deque<Symbol> symbols_;
	// The three blocks of the output format, each in the order the assignment
	// appends it: first declaration, first use, and token order.
	std::vector<std::uint32_t> objects_;
	std::vector<std::uint32_t> temporaries_;
	std::vector<std::uint32_t> literals_;
	std::unordered_map<LinkKey, std::uint32_t, LinkKeyHash> linkage_;
	std::unordered_map<const Namespace*, Scope> scopes_;
	// Interned namespace paths, keyed on the enclosing path and one name, so a
	// path costs one probe per namespace of the program rather than a walk per
	// declaration.
	std::unordered_map<std::uint64_t, std::uint32_t> paths_;
	// The namespaces between one whose path is wanted and the nearest one
	// already interned, kept between walks so that a walk allocates nothing.
	std::vector<const Namespace*> pending_;
};
