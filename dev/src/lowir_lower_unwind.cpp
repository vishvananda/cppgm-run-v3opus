#include "lowir_lower.h"

#include <sstream>
#include <stdexcept>

#include "sema_scope.h"

// 15.2p2 in a function body: what an exception thrown while objects stand has
// to end before it leaves.
//
// The objects are the ones 3.8p1 gave a lifetime - a declaration's, 12.2p1's
// temporary, and 12.6.2's subobject of the object a constructor is building -
// and they are one list, in the order they began.  A handler is a region over
// the code between two changes of that list: it opens where the step that made
// a throwing call began, it owes what stood when it opened, and it closes where
// the set changes, where 1.9p10's full-expression ends, or where the block
// does.  12.6.2p10's chain is what keeps n standing objects at n destructions
// and not n(n+1)/2, and the handler already written for a set is named again
// rather than written twice.
//
// Nothing here reads syntax or a type: the analysis wrote the destructor each
// lifetime ends in on the node that began it, and this is one walk of what the
// body has already emitted.

namespace {

using lowir_model::Instruction;
using lowir_model::Operand;

std::string decimal(unsigned long long value)
{
	std::ostringstream out;
	out << value;
	return out.str();
}

Operand named_operand(Operand::Kind kind, const std::string& text)
{
	Operand operand;
	operand.kind = kind;
	operand.text = text;
	return operand;
}

}  // namespace

// 15.2p2: the place one subobject's construction begins.  Whether a handler
// stands around it is known only once the step has said whether it made a call,
// so the place is remembered and the region is written into it afterwards.
void LowirFunctionLowering::mark_unwind_step(bool at_call)
{
	if (!unwinding_)
	{
		return;
	}
	unwind_mark_.active = true;
	unwind_mark_.at_call = at_call;
	unwind_mark_.block = current_;
	unwind_mark_.at = out_.blocks[current_].instructions.size();
	call_since_mark_ = false;
}

// 15.2p2: the step one call belongs to.  A call written as the operand of
// another - an argument, the object a member call is made on - stands inside
// the step the outer call opened, because what the handler owes is everything
// the outer call has already brought into being.  So the outermost call of an
// expression takes the mark and the ones under it find it taken.
bool LowirFunctionLowering::mark_call_step()
{
	const bool take = step_depth_ == 0;
	++step_depth_;
	if (!take)
	{
		return false;
	}
	unwind_mark_.active = true;
	unwind_mark_.at_call = false;
	unwind_mark_.block = current_;
	unwind_mark_.at = out_.blocks[current_].instructions.size();
	call_since_mark_ = false;
	return true;
}

void LowirFunctionLowering::release_call_step(bool)
{
	if (step_depth_ != 0)
	{
		--step_depth_;
	}
}

// 15.2p2 and 15.4p1: a call about to be written.  Where objects stand whose
// lifetimes an exception out of it would have to end, the handler that ends
// them is opened where this step began.
void LowirFunctionLowering::note_call(bool throwing)
{
	if (!throwing)
	{
		return;
	}
	call_since_mark_ = true;
	settle_pending_region();
	if (region_open_ || unwind_live_.empty() || !unwind_mark_.active ||
	    unwind_mark_.block != current_)
	{
		return;
	}
	region_ = open_unwind_region(unwind_mark_);
	region_open_ = true;
}

// The region a close left for whatever is written next.  A close that nothing
// follows leaves no region at all, which is what keeps a handler out of the end
// of a full-expression that has nothing more to do.
void LowirFunctionLowering::settle_pending_region()
{
	if (!region_pending_ || closing_region_)
	{
		return;
	}
	region_pending_ = false;
	if (unwind_live_.empty() || full_expressions_ == 0)
	{
		return;
	}
	UnwindMark here;
	here.active = true;
	here.block = current_;
	here.at = out_.blocks[current_].instructions.size();
	region_ = open_unwind_region(here);
	region_open_ = true;
}

void LowirFunctionLowering::close_region()
{
	region_pending_ = false;
	if (!region_open_)
	{
		return;
	}
	region_open_ = false;
	closing_region_ = true;
	close_unwind_region(region_);
	closing_region_ = false;
	region_ = UnwindRegion();
}

void LowirFunctionLowering::open_full_expression()
{
	++full_expressions_;
}

// 12.2p3: the full-expression is over, so the objects it created have been
// destroyed and the handler that stood around it comes off.
void LowirFunctionLowering::close_full_expression()
{
	if (full_expressions_ != 0)
	{
		--full_expressions_;
	}
	close_region();
}

// 3.8p1 and 15.2p2: the object `node` began a lifetime for joins the ones an
// exception has to end.  The set has changed, so the handler that stood around
// what came before it ends and a new one covers what comes after; where none
// stood, the step that built the object gets one of its own exactly where a
// call still to be made in this full-expression could throw past it.
void LowirFunctionLowering::begin_object_lifetime(
	const DumpNode& node, const UnwindMark& mark, const Operand& at,
	const std::vector<Instruction>& address)
{
	if (node.fact.destruction == nullptr)
	{
		return;
	}
	if (!region_open_ && call_since_mark_ && pending_calls_ != 0 &&
	    mark.active && mark.block == current_)
	{
		// A call still to be written in this full-expression is a place the
		// object could be left standing by, and the step that built it is what
		// the handler for that has to cover.
		region_ = open_unwind_region(mark);
		region_open_ = true;
	}
	const bool covered = region_open_;
	close_region();
	LowUnwind live;
	live.destructor = node.fact.destruction;
	live.object = node.fact.object != nullptr ? node.fact.object
	                                          : node.fact.entity;
	live.base_subobject = false;
	live.address = address;
	live.at = at;
	unwind_live_.push_back(live);
	// Where a handler already stood, the rest of the full-expression stands
	// under one too: the set it owes has changed, so the region it stood in
	// ends and another begins.  Where none stood, nothing yet asks for one.
	region_pending_ = covered && full_expressions_ != 0;
	// The step that built the object is over and the next one begins here, so
	// a handler the code after it needs stands where that code does.
	unwind_mark_.active = true;
	unwind_mark_.at_call = false;
	unwind_mark_.block = current_;
	unwind_mark_.at = out_.blocks[current_].instructions.size();
	call_since_mark_ = false;
}

// 3.8p1: the program wrote the end of this object's lifetime, so an exception
// after it no longer owes one.
void LowirFunctionLowering::end_object_lifetime(const DumpNode& node)
{
	const SemaEntity* const object =
		node.children.empty() ? nullptr : node.children[0]->fact.entity;
	if (object == nullptr)
	{
		return;
	}
	for (std::size_t index = unwind_live_.size(); index-- > 0;)
	{
		if (unwind_live_[index].object == object)
		{
			// The region now open covers this end of a lifetime, so what it
			// owes is what stood before it rather than what stands after - so
			// the one entry leaving the list is kept, with the place it stood
			// at, and the list itself is never copied.
			ended_in_region_.push_back(
				std::make_pair(index, unwind_live_[index]));
			++ended_lifetimes_;
			unwind_live_.erase(unwind_live_.begin() +
			                   static_cast<std::ptrdiff_t>(index));
			// Every handler written for more objects than stand below this one
			// destroys an object that no longer stands, so those are no longer
			// blocks a later step may name.
			if (dispatch_cache_.size() > index + 1)
			{
				dispatch_cache_.resize(index + 1);
			}
			return;
		}
	}
}

// 15.2p2: the handler the subobjects already built need, pushed where the step
// begins.  The list of them only grows, so a step needing exactly what the step
// before it needed names that block again rather than writing a second copy of
// the same destructions.
LowirFunctionLowering::UnwindRegion LowirFunctionLowering::open_unwind_region(
	const UnwindMark& mark)
{
	UnwindRegion region;
	region.live = unwind_live_.size();
	region.behind = unwind_dispatch_;
	region.chained = region.live > kUnwindSuffixLimit &&
		!region.behind.empty() && unwind_dispatch_live_ + 1 == region.live;
	region.ended = ended_lifetimes_;
	ended_in_region_.clear();
	if (!unwind_live_.empty())
	{
		// 12.6.2p10's chain needs the one object the step before it added, and
		// nothing else keeps a list until an end of a lifetime asks for one.
		region.owed.assign(unwind_live_.end() - 1, unwind_live_.end());
	}
	const bool held = region.live < dispatch_cache_.size() &&
		!dispatch_cache_[region.live].empty();
	region.fresh = !held;
	region.dispatch = held ? dispatch_cache_[region.live]
	                       : reserve_block("call_unwind_dispatch");
	Instruction open;
	open.kind = Instruction::IK_EH_TRY;
	open.first = named_operand(Operand::OP_LABEL, region.dispatch);
	std::vector<Instruction>& written = out_.blocks[mark.block].instructions;
	// 8.5.1p1's clause reached the subobject down a path the step did not walk,
	// so the region begins where the initialization does; everywhere else the
	// naming is the step's own and stands inside it.
	const std::size_t opens = mark.at_call ? written.size() : mark.at;
	written.insert(written.begin() + static_cast<std::ptrdiff_t>(opens), open);
	return region;
}

// The end of that region on the path the step took without throwing, and the
// block the other path runs.
void LowirFunctionLowering::close_unwind_region(const UnwindRegion& held)
{
	emit_handler_end();
	if (!held.fresh)
	{
		return;
	}
	// The list this handler destroys from is held while it is written, so
	// nothing written into it may open a region and take that list away.
	// The block the step goes on in is named here rather than where the region
	// opened, because a step that opened blocks of its own - an arm, a loop -
	// numbered those first.
	// Nothing written into the handler's own block is code the region machinery
	// acts on again: the instructions it holds are the ones an exception runs,
	// and a handler around them would be a handler around the handler.
	const bool nested = closing_region_;
	closing_region_ = true;
	UnwindRegion region = held;
	region.end = reserve_block("call_unwind_end");
	jump(region.end);
	open_block(region.dispatch);
	if (region.chained)
	{
		// 12.6.2p10 again, written as the chain 12.4p8's suffix is written as:
		// what this handler owes is the subobject the step before it built plus
		// everything that step's own handler owed, so past the count a reader
		// wants to see written out it destroys the one and enters the other.
		// n subobjects then cost n destructions and not n(n+1)/2.
		replay_unwind(region.owed.back());
		jump(region.behind);
	}
	else
	{
		// 12.2p3's end of a temporary's lifetime is written inside the region
		// that covered the temporary, so what this handler owes is what stood
		// when it opened - the objects still standing, with the ones the
		// program's own ends took out from under them put back where they
		// stood.  The list is rebuilt only here, where one destruction per
		// object is about to be written anyway, so it costs no more than the
		// handler itself does.
		std::vector<LowUnwind> reopened;
		if (region.ended != ended_lifetimes_)
		{
			reopened = unwind_live_;
			for (std::size_t at = ended_in_region_.size(); at-- > 0;)
			{
				const std::size_t place =
					ended_in_region_[at].first < reopened.size()
						? ended_in_region_[at].first
						: reopened.size();
				reopened.insert(
					reopened.begin() + static_cast<std::ptrdiff_t>(place),
					ended_in_region_[at].second);
			}
		}
		const std::vector<LowUnwind>& owed =
			region.ended == ended_lifetimes_ ? unwind_live_ : reopened;
		for (std::size_t index = region.live; index-- > 0;)
		{
			// 12.6.2p10: a subobject is destroyed in the reverse of the order it
			// was created in, which an exception does not change.
			if (index < owed.size())
			{
				replay_unwind(owed[index]);
			}
		}
		emit_resume();
	}
	open_block(region.end);
	closing_region_ = nested;
	if (region.ended != ended_lifetimes_)
	{
		// 3.8p1: an end of a lifetime fell inside this region, so what the
		// handler destroys is what stood *then* and no longer what stands now.
		// A later step needing a handler for the objects standing here would
		// find this one destroying an object whose lifetime the program has
		// already ended and leaving the one that took its place standing, so it
		// is no block for a later step to name.
		return;
	}
	unwind_dispatch_ = region.dispatch;
	unwind_dispatch_live_ = region.live;
	if (dispatch_cache_.size() <= region.live)
	{
		dispatch_cache_.resize(region.live + 1);
	}
	dispatch_cache_[region.live] = region.dispatch;
}

// One of those destructions.  The instructions that named the subobject where
// it was built are written again here with temporaries of this block, because a
// block reached only by an exception names nothing another block produced.
void LowirFunctionLowering::replay_unwind(const LowUnwind& live)
{
	std::unordered_map<std::string, std::string> renamed;
	for (std::size_t index = 0; index < live.address.size(); ++index)
	{
		Instruction copy = live.address[index];
		Operand* const operands[3] = { &copy.first, &copy.second, &copy.third };
		for (std::size_t at = 0; at < 3; ++at)
		{
			if (operands[at]->kind != Operand::OP_TEMP)
			{
				continue;
			}
			const std::unordered_map<std::string, std::string>::const_iterator
				found = renamed.find(operands[at]->text);
			if (found != renamed.end())
			{
				operands[at]->text = found->second;
			}
		}
		if (copy.dest.empty())
		{
			emit_void(copy);
			continue;
		}
		const std::string produced = copy.dest;
		renamed[produced] = emit(copy).text;
	}
	Operand at = live.at;
	if (at.kind == Operand::OP_TEMP)
	{
		const std::unordered_map<std::string, std::string>::const_iterator found =
			renamed.find(at.text);
		if (found != renamed.end())
		{
			at.text = found->second;
		}
	}
	if (live.elements != 0)
	{
		// 12.6p1: the subobject is an array written as a loop, so what an
		// exception owes it is that loop run backwards over every element of
		// it.  `at` is already the pointer to the first element, because that
		// is what the step it replays produced.
		destroy_array_loop(
			at, named_operand(Operand::OP_INTEGER, decimal(live.elements)),
			live.stride, *live.destructor, live.base_subobject, false, nullptr);
		return;
	}
	// The name is asked for here rather than where the subobject joined the
	// list, because a step whose subobject no later step leaves standing writes
	// no handler at all - and a declaration nothing calls is not one this unit
	// owes.  This call names one of the ABI's two entry points, so it owes that
	// one and not the other.
	unit_.declare_call_target(*live.destructor, live.base_subobject);
	Instruction out;
	out.kind = Instruction::IK_CALL;
	out.type.text = "void";
	out.first = named_operand(
		Operand::OP_GLOBAL,
		unit_.function_symbol(*live.destructor, live.base_subobject));
	out.args.push_back(at);
	emit_void(out);
}

// 15.2p2: the subobject this step built joins the ones an exception out of a
// later step has to destroy.  A class whose destructor does nothing leaves
// nothing to destroy, so it joins nothing.
void LowirFunctionLowering::push_unwind(const SemaEntity& constructor,
                                        const std::vector<Instruction>& address,
                                        const Operand& at,
                                        unsigned long long elements,
                                        unsigned long long stride)
{
	const SemaEntity* const destructor = subobject_destructor(constructor);
	if (destructor == nullptr)
	{
		return;
	}
	LowUnwind live;
	live.destructor = destructor;
	live.elements = elements;
	live.stride = stride;
	// 12.4 and the ABI: the references name the complete-object entry here even
	// where the subobject is a base class subobject, which 12.4p8's suffix does
	// not.  This milestone has no virtual base, so the two entries destroy the
	// same storage, and the text of the object file is what says which name a
	// unit owes - so this is written the way the references write it.
	live.base_subobject = false;
	live.address = address;
	live.at = at;
	unwind_live_.push_back(live);
}

// 15.2p2: the handler-stack instructions those regions are written with.  A
// cleanup runs and goes on unwinding; `eh_end` takes the handler off again on
// the path that did not throw.
void LowirFunctionLowering::emit_handler(bool cleanup, const std::string& label)
{
	Instruction out;
	out.kind = cleanup ? Instruction::IK_EH_CLEANUP : Instruction::IK_EH_TRY;
	out.first = named_operand(Operand::OP_LABEL, label);
	emit_void(out);
}

void LowirFunctionLowering::emit_handler_end()
{
	Instruction out;
	out.kind = Instruction::IK_EH_END;
	emit_void(out);
}

void LowirFunctionLowering::emit_resume()
{
	Instruction out;
	out.kind = Instruction::IK_RESUME;
	terminate(out);
}
