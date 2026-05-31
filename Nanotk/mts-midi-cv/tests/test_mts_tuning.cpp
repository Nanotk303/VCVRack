#include "../src/MtsTuning.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using nanotk::MtsTuning;

static void require(bool condition, const std::string& message) {
	if (!condition) {
		std::cerr << "FAIL: " << message << "\n";
		std::exit(1);
	}
}

static void near(float actual, float expected, const std::string& message) {
	if (std::fabs(actual - expected) > 1e-5f) {
		std::cerr << "FAIL: " << message << " expected " << expected << " got " << actual << "\n";
		std::exit(1);
	}
}

int main() {
	{
		MtsTuning tuning;
		require(!tuning.processSysex({0x90, 60, 100}), "ordinary MIDI should not be consumed as MTS");
		require(!tuning.seen, "ordinary MIDI should not mark MTS as seen");
		near(tuning.getPitchVoltage(60, 0.f, 2.f), 0.f, "middle C fallback voltage");
		near(tuning.getPitchVoltage(69, 0.f, 2.f), 0.75f, "A4 fallback voltage");
	}
	{
		MtsTuning tuning;
		std::vector<uint8_t> single = {0xf0, 0x7f, 0x7f, 0x08, 0x02, 0x00, 0x01, 60, 60, 0x20, 0x00, 0xf7};
		require(tuning.processSysex(single), "single note tuning change should be consumed");
		require(tuning.seen, "single note tuning change should mark MTS as seen");
		near(tuning.semitones[60], 60.25f, "single note tuning semitone decode");
		near(tuning.getPitchVoltage(60, 0.f, 2.f), 0.25f / 12.f, "single note tuning voltage");
	}
	{
		MtsTuning tuning;
		std::vector<uint8_t> bulk = {0xf0, 0x7e, 0x7f, 0x08, 0x01, 0x00};
		for (int i = 0; i < 16; ++i)
			bulk.push_back(0);
		for (int note = 0; note < 128; ++note) {
			bulk.push_back(note == 69 ? 70 : note);
			bulk.push_back(0);
			bulk.push_back(0);
		}
		bulk.push_back(0xf7);
		require(tuning.processSysex(bulk), "bulk tuning dump should be consumed");
		near(tuning.semitones[69], 70.f, "bulk tuning should retune A4 to A#4");
		near(tuning.getPitchVoltage(69, 0.f, 2.f), (70.f - 60.f) / 12.f, "bulk tuning voltage");
	}
	{
		MtsTuning tuning;
		std::vector<uint8_t> scale1 = {0xf0, 0x7e, 0x7f, 0x08, 0x04, 0x00};
		for (int pc = 0; pc < 12; ++pc)
			scale1.push_back(pc == 0 ? 114 : 64);
		scale1.push_back(0xf7);
		require(tuning.processSysex(scale1), "scale/octave 1-byte tuning should be consumed");
		near(tuning.semitones[60], 60.5f, "scale/octave 1-byte should retune C by 50 cents");
		near(tuning.semitones[61], 61.f, "scale/octave 1-byte should leave C# unchanged");
	}
	{
		MtsTuning tuning;
		std::vector<uint8_t> scale2 = {0xf0, 0x7f, 0x7f, 0x08, 0x05, 0x00};
		for (int pc = 0; pc < 12; ++pc) {
			int raw = 8192 + (pc == 9 ? 4096 : 0);
			scale2.push_back((uint8_t) ((raw >> 7) & 0x7f));
			scale2.push_back((uint8_t) (raw & 0x7f));
		}
		scale2.push_back(0xf7);
		require(tuning.processSysex(scale2), "scale/octave 2-byte tuning should be consumed");
		near(tuning.semitones[69], 69.5f, "scale/octave 2-byte should retune A by 50 cents");
	}
	{
		MtsTuning tuning;
		std::vector<uint8_t> banked = {0xf0, 0x7f, 0x7f, 0x08, 0x07, 0x02, 0x03, 0x01, 72, 72, 0x40, 0x00, 0xf7};
		require(tuning.processSysex(banked), "banked single note tuning should be consumed");
		near(tuning.semitones[72], 72.5f, "banked single note tuning semitone decode");
		near(tuning.getPitchVoltage(72, 0.25f, 2.f), (73.f - 60.f) / 12.f, "pitch wheel should add after MTS");
	}
	{
		MtsTuning tuning;
		std::vector<uint8_t> malformed = {0xf0, 0x7f, 0x7f, 0x08, 0x02, 0x00, 0x01, 60, 127};
		require(tuning.processSysex(malformed), "malformed MTS should be consumed");
		require(!tuning.seen, "malformed MTS should not mark tuning as seen");
	}

	std::cout << "MTS tuning tests passed\n";
	return 0;
}
