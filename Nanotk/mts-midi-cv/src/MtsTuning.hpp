#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nanotk {

struct MtsTuning {
	std::array<float, 128> semitones;
	std::array<bool, 128> tuned;
	bool seen = false;

	MtsTuning() {
		clear();
	}

	void clear() {
		for (int i = 0; i < 128; ++i) {
			semitones[i] = (float) i;
			tuned[i] = false;
		}
		seen = false;
	}

	static float decodeFrequency(uint8_t coarse, uint8_t fineMsb, uint8_t fineLsb) {
		int fine = ((int) fineMsb << 7) | (int) fineLsb;
		return (float) coarse + (float) fine / 16384.f;
	}

	bool setNote(uint8_t note, uint8_t coarse, uint8_t fineMsb, uint8_t fineLsb) {
		if (note >= 128 || coarse >= 128)
			return false;
		semitones[note] = decodeFrequency(coarse, fineMsb, fineLsb);
		tuned[note] = true;
		seen = true;
		return true;
	}

	void applyScaleOctave1Byte(const std::vector<uint8_t>& bytes, size_t start) {
		if (bytes.size() < start + 12)
			return;
		for (int pc = 0; pc < 12; ++pc) {
			float cents = (float) bytes[start + pc] - 64.f;
			for (int note = pc; note < 128; note += 12) {
				semitones[note] = (float) note + cents / 100.f;
				tuned[note] = true;
			}
		}
		seen = true;
	}

	void applyScaleOctave2Byte(const std::vector<uint8_t>& bytes, size_t start) {
		if (bytes.size() < start + 24)
			return;
		for (int pc = 0; pc < 12; ++pc) {
			int raw = ((int) bytes[start + 2 * pc] << 7) | (int) bytes[start + 2 * pc + 1];
			float cents = ((float) raw - 8192.f) * (100.f / 8192.f);
			for (int note = pc; note < 128; note += 12) {
				semitones[note] = (float) note + cents / 100.f;
				tuned[note] = true;
			}
		}
		seen = true;
	}

	bool processSysex(const std::vector<uint8_t>& bytes) {
		if (bytes.size() < 7 || bytes.front() != 0xf0 || bytes[3] != 0x08)
			return false;
		if (bytes[1] != 0x7e && bytes[1] != 0x7f)
			return false;

		const uint8_t subId2 = bytes[4];
		if (subId2 == 0x01) {
			size_t p = 22;
			for (int note = 0; note < 128 && p + 2 < bytes.size(); ++note, p += 3)
				setNote(note, bytes[p], bytes[p + 1], bytes[p + 2]);
			return true;
		}
		if (subId2 == 0x02) {
			int count = bytes[6];
			size_t p = 7;
			for (int i = 0; i < count && p + 3 < bytes.size(); ++i, p += 4)
				setNote(bytes[p], bytes[p + 1], bytes[p + 2], bytes[p + 3]);
			return true;
		}
		if (subId2 == 0x04) {
			applyScaleOctave1Byte(bytes, 6);
			return true;
		}
		if (subId2 == 0x05) {
			applyScaleOctave2Byte(bytes, 6);
			return true;
		}
		if (subId2 == 0x07 || subId2 == 0x08) {
			if (bytes.size() < 8)
				return true;
			int count = bytes[7];
			size_t p = 8;
			for (int i = 0; i < count && p + 3 < bytes.size(); ++i, p += 4)
				setNote(bytes[p], bytes[p + 1], bytes[p + 2], bytes[p + 3]);
			return true;
		}
		return true;
	}

	float getPitchVoltage(uint8_t note, float pitchWheel, float pitchWheelRange) const {
		float semitone = tuned[note] ? semitones[note] : (float) note;
		semitone += pitchWheel * pitchWheelRange;
		return (semitone - 60.f) / 12.f;
	}
};

} // namespace nanotk
