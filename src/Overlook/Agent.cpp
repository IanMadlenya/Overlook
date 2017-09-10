#include "Overlook.h"

namespace Overlook {



AgentFilter::AgentFilter() {
	
}

void AgentFilter::Create() {
	dqn.Reset();
	iter = 0;
}

void AgentFilter::ResetEpoch() {
	if (cursor > 1) {
		result_equity.Add(reward_sum);
		double posneg_sum = pos_reward_sum + neg_reward_sum;
		double dd = posneg_sum > 0.0 ? (neg_reward_sum / posneg_sum * 100.0) : 100.0;
		result_drawdown.Add(dd);
	}
	
	signal = 0;
	lower_output_signal = 0;
	
	timestep_actual = 0;
	timestep_total = -1;
	fwd_cursor = 1;
	cursor = 1;
	skip_learn = true;
	
	reward_sum = 0;
	pos_reward_sum = 0;
	neg_reward_sum = 0;
	
	change_av.Clear();
}

void AgentFilter::Main(Vector<Snapshot>& snaps) {
	ASSERT(level >= 0 && level < FILTER_COUNT);
	Snapshot& cur_snap = snaps[cursor - 0];			// Get the snapshot in current data reading position
	timestep_actual--;								// Expect main to move 1 time-step forward every time
	lower_output_signal = level == 0 ? 1 : cur_snap.GetFilterOutput(agent->group_id, level-1, agent->sym_id);
	// Do new actions when timestep_total has been moved, or when source input has zeroed.
	if (timestep_actual <= 0 || lower_output_signal == 0) {
		int sym_id = agent->sym_id;
		Snapshot& fwd_snap = snaps[fwd_cursor];		// Snapshot of previous Forward call
		Snapshot& prev_snap = snaps[cursor - 1];	// Previous snapshot is used as input also. Next is unknown.
		double reward;
		if (signal == 0) {
			reward = 0.0;
		} else {
			double change = fabs(cur_snap.GetOpen(sym_id) / fwd_snap.GetOpen(sym_id) - 1.0);
			change /= timestep_total;					// Get average change per timestep
			change_av.Add(change);						// Get online average of change of value
			double reward_change = (change - change_av.mean) * 1000.0;
			reward = reward_change;
			if (reward_change >= 0)
				pos_reward_sum += reward_change;
			else
				neg_reward_sum -= reward_change;
			reward_sum += reward;
		}
		Backward(reward);							// Learn the reward
		Forward(cur_snap, prev_snap);				// Do new actions
		fwd_cursor = cursor;
	}
	Write(cur_snap);								// Always write sensors to the common table
	
	equity[cursor] = reward_sum;		// Get some equity data for visualization
	
	cursor++;										// Move 1 time-step forward also with data reading cursor
}

void AgentFilter::Forward(Snapshot& cur_snap, Snapshot& prev_snap) {
	ASSERT(level >= 0 && level < FILTER_COUNT);
	int group_id = agent->group_id, sym_id = agent->sym_id;
	
	// Input values
	// - time_values
	// - input sensors
	// - previous signals
	int cursor = 0;
	double input_array[FILTER_STATES];
	
	
	// time_values
	input_array[cursor++] = cur_snap.GetYearSensor();
	input_array[cursor++] = cur_snap.GetWeekSensor();
	input_array[cursor++] = cur_snap.GetDaySensor();
	
	
	// sensors of value of current
	for(int i = 0; i < SENSOR_SIZE; i++)
		input_array[cursor++] = cur_snap.GetSensorUnsafe(i);
	
	// all previous outputs from the same phase
	for(int i = 0; i < FILTER_GROUP_SIZE; i++)
		input_array[cursor++] = prev_snap.GetFilterSensorUnsafe(group_id, level, i);
	
	ASSERT(cursor == FILTER_STATES);
	
	if (lower_output_signal == 0) {
		skip_learn = true;
		timestep_total = 1;
		signal = 0;
	}
	else {
		skip_learn = false;

		int action = dqn.Act(input_array);
		ASSERT(action >= 0 && action < FILTER_ACTIONCOUNT);
		ASSERT(FILTER_ACTIONCOUNT == (FILTER_POS_FWDSTEPS + FILTER_ZERO_FWDSTEPS));
		
		int timefwd_step;
		if (action < FILTER_POS_FWDSTEPS) {
			timefwd_step = action;
			sig_mul = +1;
		}
		else {
			timefwd_step = action - FILTER_POS_FWDSTEPS;
			sig_mul = 0;
		}
		
		timestep_total = 1 << (FILTER_FWDSTEP_BEGIN + timefwd_step - level);
		if (agent->is_training)
			timestep_total += Random(RANDOM_TIMESTEPS);
		signal = sig_mul;
	}
	
	// Export value.
	ASSERT(level >= 0 && level < FILTER_COUNT);
	cur_snap.SetFilterOutput(group_id, level, sym_id, signal);
	timestep_actual = timestep_total;
	
}

void AgentFilter::Write(Snapshot& cur_snap) {
	int group_id = agent->group_id, sym_id = agent->sym_id;
	cur_snap.SetFilterOutput(group_id, level, sym_id, signal);
	
	if (timestep_actual < 0) timestep_actual = 0;
	double timestep_sensor = 0.75 - 0.75 * timestep_actual / timestep_total;
	double prev_signals[FILTER_SENSORS];
	
	if (sig_mul == 0) {
		prev_signals[0] = timestep_sensor;
		prev_signals[1] = 1.0;
	}
	else {
		prev_signals[0] = 1.0;
		prev_signals[1] = timestep_sensor;
	}
	
	for(int i = 0; i < FILTER_SENSORS; i++)
		cur_snap.SetFilterSensor(group_id, level, sym_id, i, prev_signals[i]);
}

void AgentFilter::Backward(double reward) {
	
	// pass to brain for learning
	if (agent->is_training && !skip_learn) {
		dqn.Learn(reward);
		iter++;
	}
}

















AgentSignal::AgentSignal() {
	
}

void AgentSignal::Create() {
	dqn.Reset();
	iter = 0;
}

void AgentSignal::ResetEpoch() {
	if (broker.order_count > 0) {
		result_equity.Add(broker.equity);
		result_drawdown.Add(broker.GetDrawdown());
	}
	broker.Reset();
	signal = 0;
	prev_equity = broker.AccountEquity();
	
	timestep_actual = 0;
	timestep_total = -1;
	cursor = 1;
	skip_learn = true;
	
	cursor_sigbegin = 0;
	for(int i = 0; i < SIGSENS_COUNT; i++)
		epoch_av[i].Clear();
}

void AgentSignal::Main(Vector<Snapshot>& snaps) {
	Snapshot& cur_snap  = snaps[cursor - 0];
	Snapshot& prev_snap = snaps[cursor - 1];
	timestep_actual--;
	lower_output_signal = cur_snap.GetFilterOutput(agent->group_id, FILTER_COUNT-1, agent->sym_id);
	if (timestep_actual <= 0 || lower_output_signal == 0) {
		broker.RefreshOrders(cur_snap);
		double equity = broker.AccountEquity();
		double reward = equity - prev_equity;
		reward /= timestep_total;
		Backward(reward);
		if (broker.equity < 0.25 * broker.begin_equity)
			broker.Reset();
		prev_equity = broker.equity;
		Forward(cur_snap, prev_snap);
	}
	Write(cur_snap, snaps);
	equity[cursor] = broker.AccountEquity();
	cursor++;
}

void AgentSignal::Forward(Snapshot& cur_snap, Snapshot& prev_snap) {
	int group_id = agent->group_id, sym_id = agent->sym_id;
	
	// Input values
	// - time_values
	// - input sensors
	// - previous signals
	int cursor = 0;
	double input_array[SIGNAL_STATES];
	
	
	// time_values
	input_array[cursor++] = cur_snap.GetYearSensor();
	input_array[cursor++] = cur_snap.GetWeekSensor();
	input_array[cursor++] = cur_snap.GetDaySensor();
	
	
	// sensors of value of current
	for(int i = 0; i < SENSOR_SIZE; i++)
		input_array[cursor++] = cur_snap.GetSensorUnsafe(i);
	
	
	// all previous outputs from the same phase
	for(int i = 0; i < SIGNAL_GROUP_SIZE; i++)
		input_array[cursor++] = prev_snap.GetSignalSensorUnsafe(group_id, i);
	
	
	ASSERT(cursor == SIGNAL_STATES);
	
	
	int prev_signal = signal;
	
	
	if (lower_output_signal == 0) {
		skip_learn = true;
		timestep_total = 1;
		signal = 0;
	} else {
		skip_learn = false;
		
		int action = dqn.Act(input_array);
		ASSERT(action >= 0 && action < SIGNAL_ACTIONCOUNT);
		
		if (action < SIGNAL_POS_FWDSTEPS) {
			signal = +1;
			timestep_total = 1 << (SIGNAL_FWDSTEP_BEGIN + action);
		}
		else {
			signal = -1;
			action -= SIGNAL_POS_FWDSTEPS;
			timestep_total = 1 << (SIGNAL_FWDSTEP_BEGIN + action);
		}

		if (agent->is_training)
			timestep_total += Random(RANDOM_TIMESTEPS);
	}
	
	
	// Export value
	cur_snap.SetSignalOutput(group_id, sym_id, signal);
	timestep_actual = timestep_total;
	if (prev_signal != signal)
		cursor_sigbegin = cursor;
	
	
	// Set signal to broker
	broker.Cycle(signal, cur_snap);
	
	
}

void AgentSignal::Write(Snapshot& cur_snap, const Vector<Snapshot>& snaps) {
	int group_id = agent->group_id, sym_id = agent->sym_id;
	cur_snap.SetSignalOutput(group_id, sym_id, signal);
	
	if (timestep_actual < 0) timestep_actual = 0;
	double timestep_sensor = 0.75 - 0.75 * timestep_actual / timestep_total;
	double prev_signals[SIGNAL_SENSORS];
	
	if (signal == 0) {
		prev_signals[0] = 1.0;
		prev_signals[1] = 1.0;
	}
	else if (signal > 0) {
		prev_signals[0] = timestep_sensor;
		prev_signals[1] = 1.0;
	}
	else {
		prev_signals[0] = 1.0;
		prev_signals[1] = timestep_sensor;
	}
	
	if (signal == 0 || cursor == cursor_sigbegin) {
		for(int i = 0; i < SIGSENS_COUNT*2; i++)
			prev_signals[2 + i] = 1.0;
	} else {
		const int periods[SIGSENS_COUNT] = SIGSENS_PERIODS;
		for(int i = 0; i < SIGSENS_COUNT; i++) {
			const Snapshot& snap  = snaps[Upp::max(cursor_sigbegin, cursor - periods[i])];
			double change  = (cur_snap.GetOpen(sym_id) / snap.GetOpen(sym_id)  - 1.0) * (1.0 / periods[i]);
			change *= signal;
			epoch_av[i].Add(fabs(change));
			double sensor = change / (epoch_av[i].mean * 3.0);
			if (sensor >= 0.0) {
				prev_signals[2 + i * 2 + 0] = 1.0 - Upp::max(0.0, Upp::min(+1.0, +sensor));
				prev_signals[2 + i * 2 + 1] = 1.0;
			} else {
				prev_signals[2 + i * 2 + 0] = 1.0;
				prev_signals[2 + i * 2 + 1] = 1.0 - Upp::max(0.0, Upp::min(+1.0, -sensor));
			}
		}
	}
	
	
	for(int i = 0; i < SIGNAL_SENSORS; i++)
		cur_snap.SetSignalSensor(group_id, sym_id, i, prev_signals[i]);
}

void AgentSignal::Backward(double reward) {
	
	// pass to brain for learning
	if (agent->is_training && !skip_learn) {
		dqn.Learn(reward);
		iter++;
	}
}

















AgentAmp::AgentAmp() {
	
}

void AgentAmp::Create() {
	dqn.Reset();
	iter = 0;
}

void AgentAmp::ResetEpoch() {
	if (broker.order_count > 0) {
		result_equity.Add(broker.equity);
		result_drawdown.Add(broker.GetDrawdown());
	}
	broker.Reset();
	signal = 0;
	lower_output_signal = 0;
	prev_lower_output_signal = 0;
	prev_equity = broker.PartialEquity();
	
	timestep_actual = 0;
	timestep_total = -1;
	cursor = 1;
	skip_learn = true;
	
	cursor_sigbegin = 0;
	for(int i = 0; i < SIGSENS_COUNT; i++)
		epoch_av[i].Clear();
}

void AgentAmp::Main(Vector<Snapshot>& snaps) {
	Snapshot& cur_snap = snaps[cursor - 0];
	timestep_actual--;
	lower_output_signal = cur_snap.GetSignalOutput(agent->group_id, agent->sym_id);
	if (timestep_actual <= 0 || lower_output_signal != prev_lower_output_signal) {
		Snapshot& prev_snap = snaps[cursor - 1];
		broker.RefreshOrders(cur_snap);
		double equity = broker.PartialEquity();
		double reward = equity - prev_equity;
		reward /= timestep_total;
		reward /= broker.equity / broker.begin_equity;
		Backward(reward);
		if (broker.equity < 0.25 * broker.begin_equity)
			broker.Reset();
		prev_equity = broker.PartialEquity();
		Forward(cur_snap, prev_snap);
	}
	Write(cur_snap, snaps);
	prev_lower_output_signal = lower_output_signal;
	equity[cursor] = broker.PartialEquity();
	cursor++;
}

void AgentAmp::Forward(Snapshot& cur_snap, Snapshot& prev_snap) {
	int group_id = agent->group_id, sym_id = agent->sym_id;
	
	// Input values
	// - time_values
	// - input sensors
	// - previous signals
	int cursor = 0;
	double input_array[AMP_STATES];
	
	
	// time_values
	input_array[cursor++] = cur_snap.GetYearSensor();
	input_array[cursor++] = cur_snap.GetWeekSensor();
	input_array[cursor++] = cur_snap.GetDaySensor();
	
	
	// sensors of value of current
	for(int i = 0; i < SENSOR_SIZE; i++)
		input_array[cursor++] = cur_snap.GetSensorUnsafe(i);
	
	
	// all current signals from same snapshot
	for(int i = 0; i < SIGNAL_GROUP_SIZE; i++)
		input_array[cursor++] = cur_snap.GetSignalSensorUnsafe(group_id, i);
	
	
	// all previous outputs from the same phase
	for(int i = 0; i < AMP_GROUP_SIZE; i++)
		input_array[cursor++] = prev_snap.GetAmpSensorUnsafe(group_id, i);
	
	ASSERT(cursor == AMP_STATES);
	
	int prev_signal = signal;
	
	if (lower_output_signal == 0) {
		skip_learn = true;
		prev_signals[0]		= 1.0;
		prev_signals[1]		= 1.0;
		timestep_total = 1;
		signal = 0;
	}
	else {
		skip_learn = false;

		int action = dqn.Act(input_array);
		ASSERT(action >= 0 && action < AMP_ACTIONCOUNT);
		
		const int maxscale_steps = AMP_MAXSCALES;
		const int timefwd_steps = AMP_FWDSTEPS;
		ASSERT(AMP_ACTIONCOUNT == (maxscale_steps * timefwd_steps));
		
		int maxscale_step	= action % maxscale_steps;			action /= maxscale_steps;
		int timefwd_step	= action % timefwd_steps;			action /= timefwd_steps;
		
		prev_signals[0]		= 1.0 * maxscale_step / maxscale_steps;
		prev_signals[1]		= 1.0 * timefwd_step  / timefwd_steps;
		
		int maxscale		= 1 + maxscale_step * 2;
		timestep_total		= 1 << (timefwd_step + AMP_FWDSTEP_BEGIN);
		
		if (agent->is_training)
			timestep_total += Random(RANDOM_TIMESTEPS);
		signal = lower_output_signal * maxscale;
	}
	
	// Export value.
	cur_snap.SetAmpOutput(group_id, sym_id, signal);
	timestep_actual = timestep_total;
	if (prev_signal != signal)
		cursor_sigbegin = cursor;
	
	
	// Following accepts previous iteration values.
	// Rest of the function does NOT affect the exported value.
	// Copying signals back from snapshot does NOT affect realtime quality.
	// Invalid accumulation in the broker does NOT affect following forward calls in the forward-only mode.
	// This is not skipped in the forward-only mode, because the equity graph is being drawn for visualization.
	// Broker signals may NOT be read in realtime, because it can be reseted in a unsuccessful try.
	
	for(int i = 0; i < SYM_COUNT; i++)
		broker.SetSignal(i, cur_snap.GetAmpOutput(group_id, i));
	
	bool succ = broker.Cycle(cur_snap);
	
	if (!succ)
		broker.Reset();
}

void AgentAmp::Write(Snapshot& cur_snap, const Vector<Snapshot>& snaps) {
	int group_id = agent->group_id, sym_id = agent->sym_id;
	cur_snap.SetAmpOutput(group_id, sym_id, signal);
	
	if (timestep_actual < 0) timestep_actual = 0;
	prev_signals[2] = 0.75 - 0.75 * timestep_actual / timestep_total;
	
	if (signal == 0 || cursor == cursor_sigbegin) {
		for(int i = 0; i < SIGSENS_COUNT*2; i++)
			prev_signals[3 + i] = 1.0;
	} else {
		const int periods[SIGSENS_COUNT] = SIGSENS_PERIODS;
		for(int i = 0; i < SIGSENS_COUNT; i++) {
			const Snapshot& snap  = snaps[Upp::max(cursor_sigbegin, cursor - periods[i])];
			double change  = (cur_snap.GetOpen(sym_id) / snap.GetOpen(sym_id)  - 1.0) * (1.0 / periods[i]);
			change *= signal;
			epoch_av[i].Add(fabs(change));
			double sensor = change / (epoch_av[i].mean * 3.0);
			if (sensor >= 0.0) {
				prev_signals[3 + i * 2 + 0] = 1.0 - Upp::max(0.0, Upp::min(+1.0, +sensor));
				prev_signals[3 + i * 2 + 1] = 1.0;
			} else {
				prev_signals[3 + i * 2 + 0] = 1.0;
				prev_signals[3 + i * 2 + 1] = 1.0 - Upp::max(0.0, Upp::min(+1.0, -sensor));
			}
		}
	}
	
	
	for(int i = 0; i < AMP_SENSORS; i++)
		cur_snap.SetAmpSensor(group_id, sym_id, i, prev_signals[i]);
}

void AgentAmp::Backward(double reward) {
	
	// pass to brain for learning
	if (agent->is_training && !skip_learn) {
		dqn.Learn(reward);
		iter++;
	}
}
















Agent::Agent()
{
	for(int i = 0; i < FILTER_COUNT; i++) {
		filter[i].level = i;
		filter[i].agent = this;
	}
	sig.agent	= this;
	amp.agent	= this;
}

void Agent::CreateAll() {
	for(int i = 0; i < FILTER_COUNT; i++) {
		filter[i].Create();
	}
	sig.Create();
	amp.Create();
}


void Agent::Init() {
	ASSERT(sym_id >= 0);
	
	group->sys->SetSingleFixedBroker(sym_id,	sig.broker);
	group->sys->SetFixedBroker(sym_id,			amp.broker);
	
	RefreshSnapEquities();
	
	ResetEpochAll();
}

void Agent::RefreshSnapEquities() {
	int snap_count = group->sys->snaps.GetCount();
	for(int i = 0; i < FILTER_COUNT; i++) {
		filter[i].equity.SetCount(snap_count, 0);
	}
	sig.equity.SetCount(snap_count, 0);
	amp.equity.SetCount(snap_count, 0);
}

void Agent::ResetEpochAll() {
	for(int i = 0; i < FILTER_COUNT; i++) {
		filter[i].ResetEpoch();
	}
	sig.ResetEpoch();
	amp.ResetEpoch();
}

void Agent::ResetEpoch(int phase) {
	ASSERT(phase >= 0 && phase < PHASE_REAL);
	switch (phase) {
		case PHASE_SIGNAL_TRAINING: sig.ResetEpoch(); break;
		case PHASE_AMP_TRAINING: amp.ResetEpoch(); break;
		default: filter[phase].ResetEpoch(); break;
	}
}

void Agent::Main(int phase, Vector<Snapshot>& snaps) {
	ASSERT(phase >= 0 && phase < PHASE_REAL);
	switch (phase) {
		case PHASE_SIGNAL_TRAINING: sig.Main(snaps); break;
		case PHASE_AMP_TRAINING: amp.Main(snaps); break;
		default: filter[phase].Main(snaps); break;
	}
}

int Agent::GetCursor(int phase) const {
	ASSERT(phase >= 0 && phase < PHASE_REAL);
	switch (phase) {
		case PHASE_SIGNAL_TRAINING: return sig.cursor;
		case PHASE_AMP_TRAINING: return amp.cursor;
		default: return filter[phase].cursor;
	}
	return 0;
}

double Agent::GetLastDrawdown(int phase) const {
	ASSERT(phase >= 0 && phase < PHASE_REAL);
	switch (phase) {
		case PHASE_SIGNAL_TRAINING: return sig.GetLastDrawdown();
		case PHASE_AMP_TRAINING: return amp.GetLastDrawdown();
		default: return filter[phase].GetLastDrawdown();
	}
	return 0;
}

double Agent::GetLastResult(int phase) const {
	ASSERT(phase >= 0 && phase < PHASE_REAL);
	switch (phase) {
		case PHASE_SIGNAL_TRAINING: return sig.GetLastResult();
		case PHASE_AMP_TRAINING: return amp.GetLastResult();
		default: return filter[phase].GetLastResult();
	}
	return 0;
}

void Agent::Serialize(Stream& s) {
	for(int i = 0; i < FILTER_COUNT; i++)
		s % filter[i];
	s % sig % amp % sym_id % sym % group_id;
}

}
