#include "sema_builtin.h"

#include <stdexcept>
#include <string>

#include "ast_model.h"
#include "sema_analyzer.h"

namespace
{

// 3.4.3.2p1: 1.4p8 puts what the implementation provides in the global
// namespace, so `::__builtin_strlen` names the one `__builtin_strlen` names.
// No other nested-name-specifier does: a name the implementation reserves is
// the global namespace's alone, and one written under any other region names
// what that region declares or nothing.
std::string global_name(const std::string& written)
{
	return written.compare(0, 2, "::") == 0 ? written.substr(2) : written;
}

}

BuiltinReading::BuiltinReading(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
{}

// 1.4p8: the functions this implementation reserves a name for, as the type
// each has and what a backend may assume of a call of one.  A program declares
// none of them, so a use of the name is what declares the one it names - and
// the declaration is an ordinary one, which is what lets 13.3, 4.10's argument
// conversions and the lowering read it the way they read any other function.
SemaEntity* BuiltinReading::reserved(const std::string& written,
                                     std::vector<SemaEntity*>* found)
{
	TypeTable& types = analyzer_.types_;
	const std::string name = global_name(written);
	const TypeId voided = types.fundamental(FT_VOID);
	const TypeId size = types.fundamental(FT_UNSIGNED_LONG_INT);
	const TypeId any = types.pointer_to(voided);
	const TypeId source = types.pointer_to(types.qualified(voided, kCvConst));
	const TypeId text = types.pointer_to(
		types.qualified(types.fundamental(FT_CHAR), kCvConst));
	std::vector<TypeId> parameters;
	TypeId result = voided;
	unsigned char which = kNotBuiltin;
	if (name == "__builtin_memcpy" || name == "__builtin_memmove")
	{
		// 17.6.5.6: the copies of `<cstring>`, which take the storage to write,
		// the storage to read and how many bytes, and give back the first.
		which = name == "__builtin_memcpy" ? kBuiltinMemcpy : kBuiltinMemmove;
		result = any;
		parameters.push_back(any);
		parameters.push_back(source);
		parameters.push_back(size);
	}
	else if (name == "__builtin_strlen")
	{
		which = kBuiltinStrlen;
		result = size;
		parameters.push_back(text);
	}
	else if (name == "__builtin_unreachable")
	{
		which = kBuiltinUnreachable;
	}
	else if (name == "__builtin_expect")
	{
		// 1.4p8: the branch hint, which takes the value a condition came to and
		// the value it is expected to take and hands the first of them back.
		// Both are `long`, so 4.5 and 4.7 bring an operand of any other
		// integral type to it the way an ordinary declaration's parameter does.
		which = kBuiltinExpect;
		const TypeId counted = types.fundamental(FT_LONG_INT);
		result = counted;
		parameters.push_back(counted);
		parameters.push_back(counted);
	}
	else
	{
		return nullptr;
	}
	SemaContext global;
	global.scope = &analyzer_.model_.global();
	global.dump = analyzer_.model_.global().dump;
	SemaEntity& entity = analyzer_.declare_function(
		name, types.function_of(result, parameters, false), global, false);
	entity.builtin = which;
	// 14.6.4.2p1 with 1.4p8: the use is what declares this, so `bind` numbered
	// it among the declarations the program's own regions made - and a use
	// written inside a template would then have declared it *after* the pattern
	// that wrote it, which no second reading of that pattern could find.  A
	// name the implementation reserves stands before the unit rather than at
	// whichever use first named it, which is what no number at all says.
	entity.declared_serial = 0;
	// 15.4p14 and 17.6.5.12: none of the functions the implementation provides
	// under a reserved name propagates an exception, which is the same fact a
	// program writing `noexcept` states of its own - so it is written on the
	// declaration and read where any other declaration's is.
	entity.nonthrowing = true;
	entity.wrote_exception_specification = true;
	if (found != nullptr)
	{
		found->push_back(&entity);
	}
	return &entity;
}

// 3.7.4.1p2 and 3.7.4.2p2: the four allocation and deallocation functions the
// implementation declares in the global namespace of every translation unit.
// They are ordinary declarations, so 13.3 chooses among them beside whatever
// the program declared, a definition the program writes redeclares the one of
// them it matches, and the lowering writes nothing for the ones no use reaches.
// 3.7.4.2p3 gives the two deallocation functions a non-throwing
// exception-specification, which is what 5.3.4p15 reads of them.
void BuiltinReading::declare_allocation_functions(Scope& where)
{
	TypeTable& types = analyzer_.types_;
	const TypeId size = types.fundamental(FT_UNSIGNED_LONG_INT);
	const TypeId storage = types.pointer_to(types.fundamental(FT_VOID));
	SemaContext global;
	global.scope = &where;
	global.dump = where.dump;
	static const struct
	{
		const char* name;
		unsigned char builtin;
		bool allocates;
	} kReserved[] = {
		{"operatornew", kBuiltinOperatorNew, true},
		{"operatornew[]", kBuiltinOperatorNewArray, true},
		{"operatordelete", kBuiltinOperatorDelete, false},
		{"operatordelete[]", kBuiltinOperatorDeleteArray, false},
	};
	for (std::size_t index = 0;
	     index < sizeof(kReserved) / sizeof(kReserved[0]); ++index)
	{
		std::vector<TypeId> parameters;
		parameters.push_back(kReserved[index].allocates ? size : storage);
		SemaEntity& entity = analyzer_.declare_function(
			kReserved[index].name,
			types.function_of(kReserved[index].allocates
			                      ? storage
			                      : types.fundamental(FT_VOID),
			                  parameters, false),
			global, false);
		entity.builtin = kReserved[index].builtin;
		entity.nonthrowing = !kReserved[index].allocates;
		entity.wrote_exception_specification = true;
	}
}

bool BuiltinReading::answers(const std::string& written)
{
	const std::string name = global_name(written);
	return name == "__builtin_constant_p" || name == "__builtin_abort" ||
		name == "__builtin_invoke";
}

// 5.19, 20.8.2 and the course builtins: `__builtin_constant_p` answers whether
// its operand is one of the constants the translation propagates,
// `__builtin_invoke` is whichever call its operands make, and
// `__builtin_abort` is a zero-argument call PA12 recognises without lowering.
bool BuiltinReading::call(const std::string& written, const AstNode* list,
                          const SemaContext& ctx, DumpNode& parent,
                          AnalyzedValue& out)
{
	TypeTable& types = analyzer_.types_;
	const std::string name = global_name(written);
	if (name == "__builtin_constant_p")
	{
		bool known = false;
		if (list != nullptr && !list->children.empty())
		{
			DumpNode scratch;
			try
			{
				analyzer_.evaluate(*list->children[0], ctx);
				known = true;
			}
			catch (const NotConstant&)
			{
				known = false;
			}
			// The operand is unevaluated but still has to name what it names.
			if (!known)
			{
				analyzer_.probe_expression(*list->children[0], ctx, scratch);
			}
		}
		out = AnalyzedValue();
		out.type = types.fundamental(FT_INT);
		out.spelled = out.type;
		out.constant = true;
		out.value = known ? 1 : 0;
		out.what = "literal";
		out.payload = known ? "1" : "0";
		out.node = &analyzer_.model_.open_node(
			parent,
			analyzer_.spell(out.what, ValueCategory::PRValue, out.type,
			                out.payload));
		return true;
	}
	if (name == "__builtin_invoke")
	{
		out = invoke(list, ctx, parent);
		return true;
	}
	if (name != "__builtin_abort")
	{
		return false;
	}
	const TypeId type = types.function_of(types.fundamental(FT_VOID),
	                                      std::vector<TypeId>(), false);
	out = AnalyzedValue();
	out.type = types.fundamental(FT_VOID);
	out.spelled = out.type;
	out.what = "call-expression";
	out.node = &analyzer_.model_.open_node(
		parent,
		analyzer_.spell(out.what, ValueCategory::PRValue, out.type,
		                out.payload));
	analyzer_.model_.open_node(
		*out.node, "callee __builtin_abort " + types.description(type));
	return true;
}

// 20.8.2p1 [func.require]: `INVOKE(f, t1, ..., tN)` is `f(t1, ..., tN)` for
// every `f` that is not a pointer to member, which is the whole of what a
// detector writing `decltype(__builtin_invoke(declval<Args>()...))` asks: a
// callable of class type reaches 13.5.4p1's `operator()`, a function or a
// pointer to one reaches 5.2.2p1, and the conversions 13.3 makes of the
// arguments are the ones the chosen declaration's parameters ask for.
AnalyzedValue BuiltinReading::invoke(const AstNode* list,
                                     const SemaContext& ctx, DumpNode& parent)
{
	DumpNode& line = analyzer_.model_.open_node(parent, std::string());
	// 14.5.3p4: the operands are the arguments the call wrote read the way any
	// other call's are, so a clause written `pattern...` stands for one operand
	// per element of the run its packs are bound to.  Which of them is the
	// callable is a fact of the *expanded* run and not of what was written -
	// `__builtin_invoke(declval<Args>()...)` names no operand at all until the
	// run `Args` is bound to is known.
	std::vector<AnalyzedValue> operands;
	InitializerClauses written(list, analyzer_, ctx);
	for (; !written.spent(); ++written.at)
	{
		operands.push_back(
			analyzer_.argument_expression(written.next(), written.in(ctx), line));
	}
	if (operands.empty())
	{
		throw std::runtime_error("__builtin_invoke is written with no callable "
		                         "operand");
	}
	AnalyzedValue target = operands.front();
	if (target.braced != nullptr)
	{
		// 8.5.4p1: a braced-init-list is no expression, so it denotes nothing
		// a call may be made of - and 20.8.2p1's `f` is an expression.
		throw std::runtime_error("the callable operand of __builtin_invoke is "
		                         "written as a braced-init-list");
	}
	if (analyzer_.types_.kind(analyzer_.types_.strip_cv(target.type)) ==
	    TypeKind::MemberPointer)
	{
		// 20.8.2p1's first four bullets are the pointer-to-member arms, whose
		// readings are 5.5's `(t1.*f)(t2...)` and `t1.*f` - the two expressions
		// that give a pointer to member a meaning.  Neither has a reader at
		// this milestone, whose boundary leaves member-pointer behaviour to the
		// later support, so what such an `f` names here is a refusal 14.8.2p8
		// drops a candidate on rather than a result type invented from the
		// pointer alone.
		throw std::runtime_error("__builtin_invoke is written with a pointer to "
		                         "member, which this milestone has no reading "
		                         "of");
	}
	std::vector<AnalyzedValue> arguments(operands.begin() + 1, operands.end());
	return analyzer_.call_value(line, target, arguments, ctx);
}
