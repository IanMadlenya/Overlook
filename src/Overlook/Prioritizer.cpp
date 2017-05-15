#include "Overlook.h"

namespace Overlook {





void Combination::Zero() {
	for(int i = 0; i < value.GetCount(); i++) {
		value[i] = 0;
		dep_mask[i] = 1;
		exp_mask[i] = 1;
	}
}

bool Combination::GetValue(int bit) const {
	typedef const byte ConstByte;
	int byt = bit / 8;
	bit = bit % 8;
	ConstByte* b = value.Begin() + byt;
	byte mask = 1 << bit;
	return *b & mask;
}

void Combination::SetSize(int bytes) {
	value		.SetCount(bytes, 0);
	dep_mask	.SetCount(bytes, 0);
	exp_mask	.SetCount(bytes, 0);
}

void Combination::SetValue(int bit, bool value) {
	int byt = bit / 8;
	bit = bit % 8;
	byte* b = this->value.Begin() + byt;
	byte mask = 1 << bit;
	if (value)
		*b |= mask;
	else
		*b &= ~mask;
}








Prioritizer::Prioritizer() {
	running = false;
	stopped = true;
	thread_count = 0;
	combination_errors = 0;
	
	
	
}

Prioritizer::~Prioritizer() {
	Stop();
}

void Prioritizer::Init() {
	CreateCombination();
	RefreshQueue();
	
	if (0) {
		if (combination_errors) {
			Panic("Found " + IntStr(combination_errors) + " errors. Can't run system.");
		}
		Start();
	}
}

void Prioritizer::Deinit() {
	Stop();
}

uint32 Prioritizer::GetPipeDefaultCombination(int fac_id) {
	uint32 comb = 1;
	CombinationPart& part = combparts[fac_id];
	
	for(int i = 0; i < part.input_src[fac_id].GetCount(); i++) {
		
		
		
	}
	
	return comb;
}

Pipe& Prioritizer::GetPipe(int fac_id, uint32 input_comb, int sym_id, int tf_id, VectorMap<String, Value>* args) {
	input_comb |= 1; // always enabled
	VectorMap<uint32, VectorMap<int, VectorMap<int, Pipe*> > >& combs = GetPipeData()[fac_id];
	VectorMap<int, VectorMap<int, Pipe*> >& symtf = combs.GetAdd(input_comb);
	VectorMap<int, Pipe*>& tf = symtf.GetAdd(sym_id);
	Pipe*& pipe = tf.GetAdd(tf_id, NULL);
	if (pipe) return *pipe;
	Lock().Enter();
	if (pipe) {Lock().Leave(); return *pipe;} // in case of same object being created while waiting lock
	
	pipe = Factory::GetPipeFactories()[fac_id].b();
	
	pipe->SetSymbol(sym_id);
	pipe->SetTimeframe(tf_id);
	
	if (args)
		pipe->SetArguments(*args);
	
	pipe->Init();
	
	Lock().Leave();
	return *pipe;
}

Pipe& Prioritizer::GetPipe(int fac_id, int sym_id, int tf_id) {
	
	
	uint32 comb = GetPipeDefaultCombination(fac_id);
	Pipe* pipe = &GetPipe(fac_id, comb, sym_id, tf_id);
	
	
	
	return *pipe;
}

void Prioritizer::CreateCombination() {
	ASSERT(combparts.IsEmpty());
	
	combination_bits = 0;
	
	ASSERT_(Factory::GetFactoryInputs().GetCount() > 0, "Recompile Overlook.icpp to fix this stupid and weird problem");
	for(int i = 0; i < Factory::GetFactoryInputs().GetCount(); i++) {
		const Vector<ValueType>& factory_inputs = Factory::GetFactoryInputs()[i];
		
		// One part per slot-factory
		CombinationPart& part = combparts.Add();
		part.input_src.SetCount(factory_inputs.GetCount());
		part.begin = combination_bits;
		
		combination_bits++; // enabled bit
		
		for(int i2 = 0; i2 < factory_inputs.GetCount(); i2++) {
			const ValueType& input = factory_inputs[i2];
			part.inputs.Add(input);
			
			// Find output to input.
			// Limit range to input position, because it simplifies dependency mask and this
			// way some wrong combinations can be easily avoided. Be sure to register slots in
			// correct order.
			for(int j = 0; j < i; j++) {
				const Vector<ValueType>& factory_outputs = Factory::GetFactoryOutputs()[j];
				
				for(int k = 0; k < factory_outputs.GetCount(); k++) {
					const ValueType& output = factory_outputs[k];
					
					// If one of outputs of the registered slot factory is equal to input
					if (output.type == input.type && output.phase == input.phase) {
						
						part.input_src[i2].Add(IntPair(j, k));
						combination_bits++;
					}
				}
			}
		}
		
		part.end = combination_bits;
		part.size = part.end - part.begin;
		
		// Check if slot can branch with different sources
		part.single_sources = true;
		for(int j = 0; j < part.input_src.GetCount(); j++) {
			if (part.input_src[j].GetCount() > 1) {
				part.single_sources = false;
				break;
			}
		}
	}
	
	// Connect input source slots to their enabled bits
	inputs_to_enabled.SetCount(combination_bits, -1);
	enabled_to_factory.SetCount(combination_bits, -1);
	for(int i = 0; i < combparts.GetCount(); i++) {
		const CombinationPart& part = combparts[i];
		for(int j = 0; j < part.input_src.GetCount(); j++) {
			const Vector<IntPair>& src = part.input_src[j];
			for(int k = 0; k < src.GetCount(); k++) {
				const IntPair& ip = src[k];
				int input_bit = GetBitCore(i, j, k);
				int enabled_bit = GetBitEnabled(ip.a);
				inputs_to_enabled[input_bit] = enabled_bit;
				enabled_to_factory[enabled_bit] = ip.a;
			}
		}
	}
	
	
	combination_bits += 200 + 10; // max symbols and timeframes
	combination_bytes = combination_bits / 8 + (combination_bits % 8 != 0 ? 1 : 0);
	
	// Print stuff
	LOG("Combination size: " << combination_bits << " bits, " << combination_bytes << " bytes");
	combination_errors = 0;
	for(int i = 0; i < combparts.GetCount(); i++) {
		const CombinationPart& part = combparts[i];
		LOG("    part " << i << ": \"" << Factory::GetPipeFactories()[i].a << "\" begin=" << part.begin << " size=" << part.size << " single-source=" << (int)part.single_sources);
		
		for(int j = 0; j < part.input_src.GetCount(); j++) {
			const Vector<IntPair>& inputs = part.input_src[j];
			if (inputs.IsEmpty()) {
				LOG("        ERROR: input not found: " << part.inputs[j].ToString());
				combination_errors++;
			} else {
				String str;
				for(int k = 0; k < inputs.GetCount(); k++) {
					const IntPair& ip = inputs[k];
					str << " (" << ip.a << "," << ip.b << ")";
				}
				LOG("        " << str);
			}
		}
	}
	if (combination_errors) {
		LOG("Found " << combination_errors << " errors. Can't run system.");
	}
}


void Prioritizer::RefreshQueue() {
	lock.Enter();
	
	// Create queue from last slot to first
	
	// Skip all bad exports based on QueryTable reading results vector.
	// Add random combinations based on Randomf() < exploration_epsilon.
	// Add all meaningful combinations if the result-vector is too small for QueryTable.
	pl_queue.Clear();
	Combination comb;
	comb.SetSize(combination_bytes);
	VisitSymTf(comb);
	
	
	// Create unique-slot-queue from the whole pipeline-queue.
	RefreshCoreQueue();
	
	
	// Create job-queue from unique-slot-queue
	RefreshJobQueue();
	
	
	// Move slot-objects from previous slot-queue to the new one and remove those which aren't
	// in the new. Store them to hard drive without deleting...
	
	
	
	// Create new slot-objects to the slot-queue and load previous data from hard drive.
	
	
	// Sort queues based on priorities
	
	
	lock.Leave();
}

void Prioritizer::VisitSymTf(Combination& comb) {
	BaseSystem& bs = GetBaseSystem();
	
	int sym_count = bs.GetSymbolCount();
	int tf_count = bs.GetPeriodCount();
	ASSERT(sym_count > 0 && tf_count > 0);
	
	if (IsBegin()) {
		
		// Find EURUSD and add smallest timeframe
		int tf_id = 0;
		int sym_id = 0;
		for(int i = 0; i < sym_count; i++) {
			if (bs.GetSymbol(i) == "EURUSD") {
				sym_id = i;
				break;
			}
		}
		
		// Set only that enabled in the combination vector
		comb.Zero();
		comb.SetValue(GetBitTf(tf_id), true);
		comb.SetValue(GetBitSym(sym_id), true);
		
	} else {
		typedef Tuple3<int, int, int> Priority;
		Vector<Priority> priorities;
		
		for(int i = 0; i < tf_count; i++) {
			for(int j = 0; j < sym_count; j++) {
				
				// Get priority from QueryTable
				int priority = 0;
				
				
				priorities.Add(Priority(i, j, priority));
			}
		}
		
		struct PrioritySorter {
			bool operator()(const Priority& a, const Priority& b) const {
				return a.c < b.c;
			}
		};
		Sort(priorities, PrioritySorter());
		
	}
	
	// Enable the last factory and visit it
	int last_factory = Factory::GetPipeFactoryCount() - 1;
	int last_factory_enabled = GetBitEnabled(last_factory);
	comb.SetValue(last_factory_enabled, true);
	VisitCombination(last_factory, comb);
}

void Prioritizer::VisitCombination(int factory, Combination& comb) {
	CombinationPart& part = combparts[factory];
	
	// Check, that slots are false
	#ifdef flagDEBUG
	ASSERT(comb.GetValue(GetBitEnabled(factory)) == true);
	for(int i = 0; i < part.input_src.GetCount(); i++) {
		Vector<IntPair>& src = part.input_src[i];
		for(int j = 0; j < src.GetCount(); j++) {
			ASSERT(comb.GetValue(GetBitCore(factory, i, j)) == false);
		}
	}
	#endif
	
	// Visit combination until source slot
	if (factory > 0) {
		
		// If inputs has multiple sources
		if (!part.single_sources) {
			
			// If query-table method can't be used yet, due the lack of results.
			if (IsBegin()) {
				
				
				// Copy existing.
				Combination comb2 = comb;
				
				
				// Add all minimal cases
				int case_count = 0;
				for(int i = 0; i < part.input_src.GetCount(); i++) {
					Vector<IntPair>& src = part.input_src[i];
					
					if (src.GetCount() <= 1) {
						int bit = GetBitCore(factory, i, 0);
						comb2.SetValue(bit, true);
						bit = InputToEnabled(bit);
						comb2.SetValue(bit, true);
					}
				}
				
				
				// Find the ranges for sub-combination
				typedef Tuple3<int, int, int> InpMaxSrc; // input-id, source-count, current source
				Vector<InpMaxSrc> sub_comb;
				for(int i = 0; i < part.input_src.GetCount(); i++) {
					Vector<IntPair>& src = part.input_src[i];
					
					if (src.GetCount() > 1) {
						sub_comb.Add(InpMaxSrc(i, src.GetCount(), 0));
					}
				}
				int sub_comb_count = sub_comb.GetCount();
				
				
				// Iterate all sub-combinations
				bool looping = true;
				while (looping) {
					
					// Copy existing with fixed flags
					Combination comb3 = comb2;
					
					
					// Set current combination values
					int highest_factory = -1;
					for(int i = 0; i < sub_comb_count; i++) {
						const InpMaxSrc& s = sub_comb[i];
						int bit = GetBitCore(factory, s.a, s.c);
						comb3.SetValue(bit, true);
						bit = InputToEnabled(bit);
						comb3.SetValue(bit, true);
						int src_factory = EnabledToFactory(bit);
						if (src_factory > highest_factory) highest_factory = src_factory;
					}
					ASSERT(highest_factory < factory);
					ASSERT(highest_factory != -1);
					
					
					// Visit new combination
					VisitCombination(highest_factory, comb3);
					case_count++;
					
					
					// Increase combination by one
					for(int i = 0; i < sub_comb_count; i++) {
						InpMaxSrc& s = sub_comb[i];
						s.c++; // increase current by one
						// if current more than maximum
						if (s.c == s.b) {
							s.c = 0; // reset current
							// if this was the last number, don't start over, but break the loop
							if (i == sub_comb_count-1) {
								looping = false;
								break;
							}
						}
						else break;
					}
				}
				ASSERT(case_count > 0);
			}
			else {
				
				// Limit results to current end masked combination
				Panic("TODO");
				
				// Use QueryTable to prune useless combinations
				
				VisitCombination(factory-1, comb);
				
			}
		}
		
		// With single input add that
		else {
			// Copy existing.
			Combination comb2 = comb;
			
			int highest_factory = -1;
			for(int i = 0; i < part.input_src.GetCount(); i++) {
				ASSERT(part.input_src[i].GetCount() == 1);
				
				// Set slot value true
				int bit = GetBitCore(factory, i, 0);
				comb2.SetValue(bit, true);
				bit = InputToEnabled(bit);
				comb2.SetValue(bit, true);
				int src_factory = EnabledToFactory(bit);
				if (src_factory > highest_factory) highest_factory = src_factory;
			}
			
			ASSERT(highest_factory < factory);
			ASSERT(highest_factory >= 0);
			VisitCombination(highest_factory, comb2);
			
		}
		
		
	}
	// Add combination to the queue
	else {
		int priority = pl_queue.GetCount();
		PipelineItem& pi = pl_queue.Add();
		pi.value <<= comb.value;
		pi.priority = priority;
	}
}

void Prioritizer::RefreshCoreQueue() {
	BaseSystem& bs = GetBaseSystem();
	
	slot_queue.Clear();
	
	Vector<byte> dep_mask;
	Vector<byte> unique_slot_comb;
	Vector<int> depmask_begins, depmask_ends;
	int fac_count = Factory::GetPipeFactoryCount();
	int sym_count = bs.GetSymbolCount();
	int tf_count = bs.GetPeriodCount();
	
	for(int i = 0; i < pl_queue.GetCount(); i++) {
		PipelineItem& pi = pl_queue[i];
		ASSERT(!pi.value.IsEmpty());
		
		// Combination consist of all factories + sym + tf
		for(int j = 0; j < fac_count; j++) {
			const CombinationPart& part = combparts[j];
			
			// Skip disabled slots
			int enabled_bit = GetBitEnabled(j);
			bool enabled = ReadBit(pi.value, enabled_bit);
			if (!enabled) continue;
			
			
			// Create dependency mask based on combination
			dep_mask.SetCount(0);
			dep_mask.SetCount(combination_bytes, 0);
			
			// Get true-value ranges in the mask
			depmask_begins.SetCount(0);
			depmask_ends.SetCount(0);
			depmask_begins.Add(part.begin);
			depmask_ends.Add(part.end);
			for(int k = j-1; k >= 0; k--) {
				const CombinationPart& part = combparts[k];
				depmask_begins.Add(part.begin);
				depmask_ends.Add(part.end);
			}
			ASSERT(!depmask_begins.IsEmpty());
			
			// Write ranges to the mask
			for(int k = 0; k < depmask_begins.GetCount(); k++) {
				int begin = depmask_begins[k];
				int end = depmask_ends[k];
				for (int l = begin; l < end; l++) {
					int byt = l / 8;
					int bit = l % 8;
					byte* b = dep_mask.Begin() + byt;
					byte mask = 1 << bit;
					*b |= mask;
				}
			}
			
			
			// Create unique slot combination
			unique_slot_comb.SetCount(0);
			unique_slot_comb.SetCount(combination_bytes, 0);
			for(int k = 0; k < combination_bytes; k++)
				unique_slot_comb[k] = dep_mask[k] & pi.value[k];
			
			
			// Check if current slot is currently at the queue already
			bool found = false;
			for(int k = 0; k < slot_queue.GetCount(); k++) {
				CoreItem& si = slot_queue[k];
				
				bool equal = true;
				for(int i = 0; i < combination_bytes; i++) {
					if (si.value[i] != unique_slot_comb[i]) {
						equal = false;
						break;
					}
				}
				
				if (equal) {
					found = true;
					
					// Add pipeline-item to iniator list
					si.pipeline_src.Add(i);
					
					// Add priorities of symbols and timeframes
					for (int s = 0; s < sym_count; s++) {
						if (ReadBit(pi.value, GetBitSym(s))) {
							int& prio = si.symlist.GetAdd(s, INT_MAX);
							prio = Upp::min(prio, pi.priority);
						}
					}
					for (int t = 0; t < tf_count; t++) {
						if (ReadBit(pi.value, GetBitTf(t))) {
							int& prio = si.tflist.GetAdd(t, INT_MAX);
							prio = Upp::min(prio, pi.priority);
						}
					}
					break;
				}
			}
			
			
			// If combination was original, add it to the processing list
			if (!found) {
				CoreItem& si = slot_queue.Add();
				
				si.value <<= unique_slot_comb;
				si.factory = j;
				si.priority = pi.priority * Factory::GetPipeFactories().GetCount() + j;
				si.pipeline_src.Add(i);
				
				// Add priorities of symbols and timeframes
				for (int s = 0; s < sym_count; s++)
					if (ReadBit(pi.value, GetBitSym(s)))
						si.symlist.Add(s, pi.priority);
				for (int t = 0; t < tf_count; t++)
					if (ReadBit(pi.value, GetBitTf(t)))
						si.tflist.Add(t, pi.priority);
			}
		}
	}
	
	
	struct PrioritySorter {
		bool operator()(const CoreItem& a, const CoreItem& b) const {
			return a.priority < b.priority;
		}
	};
	Sort(slot_queue, PrioritySorter());
	
	LOG("Found " << slot_queue.GetCount() << " unique slots");
}

void Prioritizer::RefreshJobQueue() {
	BaseSystem& base = GetBaseSystem();
	
	int sym_count = base.GetSymbolCount();
	int tf_count = base.GetPeriodCount();
	int total = sym_count * tf_count;
	
	job_queue.Clear();
	
	for(int i = 0; i < slot_queue.GetCount(); i++) {
		CoreItem& ci = slot_queue[i];
		
		int64 priority_base = (int64)ci.priority * (int64)total;
		
		for(int j = 0; j < ci.symlist.GetCount(); j++) {
			int sym = ci.symlist.GetKey(j);
			int sym_prio = ci.symlist[j];
			for(int k = 0; k < ci.tflist.GetCount(); k++) {
				int tf = ci.tflist.GetKey(k);
				int tf_prio = ci.tflist[k];
				
				JobItem& ji = job_queue.Add();
				ji.sym = sym;
				ji.tf = tf;
				ji.priority = priority_base + (int64)sym * tf_count + (int64)tf;
				ji.factory = ci.factory;
			}
		}
	}
}

void Prioritizer::Run() {
	ASSERT(!thread_count);
	thread_count = CPU_Cores();
	for(int i = 0; i < thread_count; i++)
		Thread::Start(THISBACK1(ProcessThread, i));
	
	while (running) {
		
		
		
		Sleep(100);
	}
	
	while (thread_count > 0) Sleep(100);
	stopped = true;
}

void Prioritizer::ProcessThread(int thread_id) {
	while (running) {
		int i = cursor++;
		
		if (i >= slot_queue.GetCount()) {
			LOG("ERROR: queue finished");
			Sleep(1000);
			continue;
		}
		
		CoreItem& qi = slot_queue[i];
		
		LOG("Process " << i << ", thread-id " << thread_id);
		
		Process(qi);
		
		
		Sleep(100);
	}
	
	thread_count--;
}

void Prioritizer::Process(CoreItem& qi) {
	
}











bool Prioritizer::IsBegin() const {
	return results.GetCount() < 100; // TODO: fix
}

int Prioritizer::GetBitTf(int tf_id) const {
	ASSERT(tf_id >= 0 && tf_id < 10);
	int begin = combparts.Top().end;
	return begin + 200 + tf_id;
}

int Prioritizer::GetBitSym(int sym_id) const {
	ASSERT(sym_id >= 0 && sym_id < 200);
	int begin = combparts.Top().end;
	return begin + sym_id;
}

int Prioritizer::GetBitCore(int fac_id, int input_id, int src_id) const {
	const CombinationPart& part = combparts[fac_id];
	int begin = part.begin;
	
	begin++; // enabled bit
	
	for(int i = 0; i < input_id; i++) {
		begin += part.input_src[i].GetCount();
	}
	#ifdef flagDEBUG
	const Vector<IntPair>& src = part.input_src[input_id];
	ASSERT(src_id >= 0 && src_id < src.GetCount());
	#endif
	return begin + src_id;
}

int Prioritizer::GetBitEnabled(int fac_id) const {
	return combparts[fac_id].begin;
}

int Prioritizer::InputToEnabled(int bit) const {
	return inputs_to_enabled[bit];
}

int Prioritizer::EnabledToFactory(int bit) const {
	return enabled_to_factory[bit];
}

}