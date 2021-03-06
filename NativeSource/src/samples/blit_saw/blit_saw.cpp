/*
 * blit_saw.cpp
 *
 * Copyright (c) 2015, fukuroda (https://github.com/fukuroder)
 * Released under the MIT license
 */

#include"dspapi.h"
#include"../library/Midi.h"
#include"../library/Constants.h"
#include<cmath>
#include<array>
#include<algorithm>

DSP_EXPORT uint audioOutputsCount = 0;
DSP_EXPORT double sampleRate = 0;

DSP_EXPORT string name = "blit saw";
DSP_EXPORT string description = "BLIT-Based sawtooth wave synthesis";

// note
class blit_saw_oscillator_note
{
public:
	// current time
	double t;

	// current value
	double value;

	// nyquist limit
	int n;

	// delta t
	double dt;

	// note number [0,127]
	int note_no;

	// velocity [0,1]
	double velocity;

	blit_saw_oscillator_note()
	{
		t = 0.0;
		value = 0.0;
		n = 0;
		dt = 0.0;
		note_no = -1;
		velocity = 0.0;
	}
};

// oscillator
class blit_saw_oscillator
{
	std::array<blit_saw_oscillator_note, 8> notes;

	std::array<double, (1 << 10) + 1> sin_table;

	double pitchbend;
public:
	blit_saw_oscillator()
		:pitchbend(0.0)
	{
		// sine wave table
		for (size_t ii = 0; ii < sin_table.size(); ii++)
		{
			sin_table[ii] = std::sin(2.0*PI * ii / (sin_table.size() - 1));
		}
	}

	void trigger(const MidiEvent& evt)
	{
		auto note = std::find_if(
			notes.begin(),
			notes.end(),
			[](blit_saw_oscillator_note& n) { return n.note_no < 0; });

		if (note != notes.end())
		{
			note->note_no = MidiEventUtils::getNote(evt);
			note->velocity = MidiEventUtils::getNoteVelocity(evt) / 127.0;
			note->value = 0.0;
			note->t = 0.5;

			//
			double freq = 440.0*(std::pow(2.0, (note->note_no + pitchbend - 69.0) / 12.0));
			note->n = static_cast<int>(sampleRate / 2.0 / freq);
			note->dt = freq / sampleRate;
		}
	}

	void update_pitchbend(const MidiEvent& evt)
	{
		pitchbend = MidiEventUtils::getPitchWheelValue(evt) / 4096.0;

		for (blit_saw_oscillator_note& note : notes)
		{
			if (note.note_no >= 0)
			{
				double freq = 440.0*(std::pow(2.0, (note.note_no + pitchbend - 69.0) / 12.0));
				note.n = static_cast<int>(sampleRate / 2.0 / freq);
				note.dt = freq / sampleRate;
			}
		}
	}

	void release(const MidiEvent& evt)
	{
		int note_no = MidiEventUtils::getNote(evt);

		auto note = std::find_if(
			notes.begin(),
			notes.end(),
			[note_no](blit_saw_oscillator_note& n) { return n.note_no == note_no; });

		if (note != notes.end())
		{
			// note off
			note->note_no = -1;
		}
	}

	double linear_interpolated_sin(double x)
	{
		//
		double pos = (sin_table.size() - 1) * x;

		//
		int idx_A = static_cast<int>(pos);

		//
		double s = pos - idx_A;

		//
		return (1.0 - s) * sin_table[idx_A] + s*sin_table[idx_A + 1];
	}

	//
	double bandlimited_impulse(double t, int n)
	{
		if (t < 1.0e-8 || 1.0 - 1.0e-8 < t)
		{
			return 2.0*(2 * n + 1);
		}
		else
		{
			double den = linear_interpolated_sin(0.5*t);
			double num = linear_interpolated_sin(std::fmod((n + 0.5)*t, 1.0));
			return 2.0*num / den;
		}
	}

	//
	double process_sample()
	{
		double value = 0.0;
		for (blit_saw_oscillator_note& note : notes)
		{
			if (note.note_no >= 0)
			{
				// render
				value += note.value * note.velocity;

				// update
				note.t += note.dt;
				if (1.0 <= note.t)note.t -= 1.0;
				note.value = note.value*0.999 + (bandlimited_impulse(note.t, note.n) - 2.0)*note.dt;
			}
		}
		return value;
	}
};

blit_saw_oscillator blit_saw;

DSP_EXPORT void processBlock(BlockData& data)
{
	uint event_idx = 0;
	const MidiQueue& inputMidiEvents = data.inputMidiEvents;
	for (uint i = 0; i < data.samplesToProcess; ++i)
	{
		while (event_idx < inputMidiEvents.length
			&& inputMidiEvents[event_idx].timeStamp <= static_cast<double>(i))
		{
			const MidiEvent& event = inputMidiEvents[event_idx];
			MidiEventType evt_type = MidiEventUtils::getType(event);
			if (evt_type == kMidiNoteOn)
			{
				blit_saw.trigger(event);
			}
			else if (evt_type == kMidiNoteOff)
			{
				blit_saw.release(event);
			}
			else if (evt_type == kMidiPitchWheel)
			{
				blit_saw.update_pitchbend(event);
			}
			++event_idx;
		}

		double value = blit_saw.process_sample();
		for (uint ch = 0; ch < audioOutputsCount; ++ch)
		{
			data.samples[ch][i] = value;
		}
	}
}

DSP_EXPORT int getTailSize()
{
	return -1;
}
