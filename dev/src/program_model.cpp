#include "program_model.h"

#include <cstring>
#include <ostream>
#include <utility>

namespace
{

// The mock representation of a function: the assignment's `array of 4 char`.
const char kFunctionStub[4] = {'f', 'u', 'n', 0};

void append_little_endian(std::string& out, unsigned long long value, std::size_t width)
{
	for (std::size_t index = 0; index < width; ++index)
	{
		out.push_back(static_cast<char>(value & 0xFF));
		value >>= 8;
	}
}

void append_bytes(std::string& out, const void* source, std::size_t width)
{
	const char* bytes = static_cast<const char*>(source);
	out.append(bytes, width);
}

void pad(std::ostream& out, unsigned long long count)
{
	char zeros[64] = {0};
	while (count != 0)
	{
		const unsigned long long chunk = count < sizeof(zeros) ? count : sizeof(zeros);
		out.write(zeros, static_cast<std::streamsize>(chunk));
		count -= chunk;
	}
}

}

Symbol::Symbol()
	: kind(SymbolKind::Variable)
	, id(0)
	, type(kNoType)
	, from_string(false)
	, initialized(false)
	, readable(false)
	, defined(false)
	, is_inline(false)
	, is_thread_local(false)
	, is_constexpr(false)
	, declarations(0)
	, unit(0)
	, offset(0)
{}

std::size_t ProgramImage::LinkKeyHash::operator()(const LinkKey& key) const
{
	std::uint64_t hash = 1469598103934665603ULL;
	const std::uint64_t words[3] = {key.scope, key.name, key.signature};
	for (std::size_t index = 0; index < 3; ++index)
	{
		hash = (hash ^ words[index]) * 1099511628211ULL;
	}
	return static_cast<std::size_t>(hash ^ (hash >> 32));
}

ProgramImage::ProgramImage(TypeTable& types)
	: types_(types)
	, unit_(0)
{}

ProgramImage::Scope ProgramImage::scope_of(const Namespace& space)
{
	// A namespace keeps its path once it has one, so the walk up stops at the
	// first enclosing namespace that has been asked about before.  Both the
	// walk up and the way back down are loops: a translation unit can nest
	// namespaces as deeply as it is long.
	pending_.clear();
	const Namespace* current = &space;
	while (scopes_.find(current) == scopes_.end() && current->parent() != nullptr)
	{
		pending_.push_back(current);
		current = current->parent();
	}

	Scope scope;
	scope.id = 1;
	scope.internal = false;
	const std::unordered_map<const Namespace*, Scope>::iterator found = scopes_.find(current);
	if (found != scopes_.end())
	{
		scope = found->second;
	}
	else
	{
		scopes_.insert(std::make_pair(current, scope));
	}

	while (!pending_.empty())
	{
		const Namespace* member = pending_.back();
		pending_.pop_back();
		Scope inner;
		inner.internal = scope.internal || member->name() == kNoName;
		// Nothing an unnamed namespace declares can be named from another
		// translation unit, so its path never has to be interned at all.
		inner.id = inner.internal ? 0 : intern_path(scope.id, member->name());
		scopes_.insert(std::make_pair(member, inner));
		scope = inner;
	}
	return scope;
}

std::uint32_t ProgramImage::intern_path(std::uint32_t enclosing, NameId name)
{
	const std::uint64_t edge = (static_cast<std::uint64_t>(enclosing) << 32) | name;
	const std::pair<std::unordered_map<std::uint64_t, std::uint32_t>::iterator, bool> entry =
		paths_.insert(
			std::make_pair(edge, static_cast<std::uint32_t>(paths_.size() + 2)));
	return entry.first->second;
}

void ProgramImage::begin_unit()
{
	++unit_;
	scopes_.clear();
}

Symbol& ProgramImage::create(SymbolKind kind, TypeId type)
{
	symbols_.push_back(Symbol());
	Symbol& symbol = symbols_.back();
	symbol.kind = kind;
	symbol.id = static_cast<std::uint32_t>(symbols_.size());
	symbol.type = type;
	return symbol;
}

Symbol& ProgramImage::declare(SymbolKind kind, const Namespace& home, NameId name,
                              TypeId type, bool internal)
{
	const Scope scope = scope_of(home);
	if (internal || scope.internal)
	{
		Symbol& symbol = create(kind, type);
		objects_.push_back(symbol.id);
		return symbol;
	}

	LinkKey key;
	key.scope = scope.id;
	key.name = name;
	key.signature = kind == SymbolKind::Function ? types_.signature(type) : 0;

	const std::pair<std::unordered_map<LinkKey, std::uint32_t, LinkKeyHash>::iterator,
	                bool> entry =
		linkage_.insert(std::make_pair(key, static_cast<std::uint32_t>(symbols_.size() + 1)));
	if (!entry.second)
	{
		return at(entry.first->second);
	}
	Symbol& symbol = create(kind, type);
	objects_.push_back(symbol.id);
	return symbol;
}

Symbol& ProgramImage::add_temporary(TypeId type)
{
	Symbol& symbol = create(SymbolKind::Temporary, type);
	temporaries_.push_back(symbol.id);
	return symbol;
}

Symbol& ProgramImage::add_string_literal(TypeId type, const std::string& bytes,
                                         bool in_program)
{
	Symbol& symbol = create(SymbolKind::StringLiteral, type);
	symbol.bytes = bytes;
	symbol.from_string = true;
	symbol.initialized = true;
	symbol.defined = true;
	if (in_program)
	{
		literals_.push_back(symbol.id);
	}
	return symbol;
}

unsigned long long ProgramImage::size_of(const Symbol& symbol) const
{
	return symbol.kind == SymbolKind::Function ? 4 : types_.object_size(symbol.type);
}

unsigned long long ProgramImage::align_of(const Symbol& symbol) const
{
	return symbol.kind == SymbolKind::Function ? 4 : types_.object_align(symbol.type);
}

void ProgramImage::layout()
{
	// The three blocks in the order the assignment appends them, each object
	// at the next offset its alignment allows, counting from the magic bytes.
	const std::vector<std::uint32_t>* blocks[3] = {&objects_, &temporaries_, &literals_};
	unsigned long long cursor = 4;
	for (std::size_t block = 0; block < 3; ++block)
	{
		for (std::size_t index = 0; index < blocks[block]->size(); ++index)
		{
			Symbol& symbol = at((*blocks[block])[index]);
			const unsigned long long align = align_of(symbol);
			if (align != 0 && cursor % align != 0)
			{
				cursor += align - cursor % align;
			}
			symbol.offset = cursor;
			cursor += size_of(symbol);
		}
	}
}

void ProgramImage::encode(const Symbol& symbol, std::string& out) const
{
	const std::size_t width = static_cast<std::size_t>(size_of(symbol));
	switch (symbol.value.kind)
	{
	case ValueKind::Integral:
		append_little_endian(out, symbol.value.integral, width);
		return;

	case ValueKind::Address:
		append_little_endian(out, at(symbol.value.symbol).offset + symbol.value.addend, 8);
		return;

	case ValueKind::Floating:
		break;

	default:
		return;
	}

	// A floating object holds the host representation of its type, which the
	// course ABI shares.  `long double` is the x87 extended format, whose ten
	// bytes sit at the start of a sixteen byte object.
	if (width == sizeof(float))
	{
		const float value = static_cast<float>(symbol.value.floating);
		append_bytes(out, &value, sizeof(value));
		return;
	}
	if (width == sizeof(double))
	{
		const double value = static_cast<double>(symbol.value.floating);
		append_bytes(out, &value, sizeof(value));
		return;
	}
	const long double value = symbol.value.floating;
	append_bytes(out, &value, 10);
}

void ProgramImage::write_symbol(std::ostream& out, const Symbol& symbol) const
{
	const unsigned long long width = size_of(symbol);
	if (symbol.kind == SymbolKind::Function)
	{
		out.write(kFunctionStub, sizeof(kFunctionStub));
		return;
	}
	if (!symbol.initialized)
	{
		pad(out, width);
		return;
	}

	// 8.5.2p2: the elements a string literal does not reach are zero, and a
	// literal longer than the array it initializes was refused when it was
	// analysed.
	std::string scalar;
	if (!symbol.from_string)
	{
		encode(symbol, scalar);
	}
	const std::string& content = symbol.from_string ? symbol.bytes : scalar;
	const std::size_t written = content.size() < width ? content.size()
	                                                   : static_cast<std::size_t>(width);
	out.write(content.data(), static_cast<std::streamsize>(written));
	pad(out, width - written);
}

void ProgramImage::write(std::ostream& out)
{
	layout();

	// The magic bytes are the `array of 4 char` string literal "PA8".
	out.write("PA8\0", 4);

	const std::vector<std::uint32_t>* blocks[3] = {&objects_, &temporaries_, &literals_};
	unsigned long long cursor = 4;
	for (std::size_t block = 0; block < 3; ++block)
	{
		for (std::size_t index = 0; index < blocks[block]->size(); ++index)
		{
			const Symbol& symbol = at((*blocks[block])[index]);
			pad(out, symbol.offset - cursor);
			write_symbol(out, symbol);
			cursor = symbol.offset + size_of(symbol);
		}
	}
}
