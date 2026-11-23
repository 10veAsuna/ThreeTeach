#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// 原创乐谱与合成器不依赖引擎，游戏播放和导出的试听使用完全相同的采样。
namespace GridMusic
{
	constexpr int SampleRate = 24000;
	constexpr double Pi = 3.14159265358979323846;
	constexpr double Beat = 60.0 / 72.0;
	constexpr double BarDuration = Beat * 4.0;
	enum class Voice { Bell, Wood, Pad, Glass };

	struct Note
	{
		double Start;
		double Duration;
		int Midi;
		float Gain;
		float Pan;
		float EndPan;
		Voice Instrument;
	};

	using Score = std::vector<Note>;
	using Pcm = std::vector<std::int16_t>;

	inline void Add(Score& Notes, double Start, double Duration, int Midi, float Gain,
		float Pan, Voice Instrument)
	{
		Notes.push_back({Start, Duration, Midi, Gain, Pan, Pan, Instrument});
	}

	inline Score ChapterBar(int Chapter, int Bar)
	{
		Score Notes;
		const int Roots[] = {50, 46, 53, 48}; // Dm, Bb, F, C，八小节循环。
		const int Root = Roots[(Bar / 2) % 4];
		const int Third = (Bar / 2) % 4 == 0 ? 3 : 4;
		for (int Interval : {0, Third, 7})
		{
			Add(Notes, 0, BarDuration, Root + Interval, .048f, -.20f, Voice::Pad);
		}
		const int Blue[] = {74, 77, 81, 77, 72, 76, 79, 76};
		const int Orange[] = {69, 72, 74, 72, 67, 69, 72, 69};
		const int Phrase = Bar % 8;
		// A 留出后半句，B 用另一种音色回应；有些小节保持留白。
		if (Bar % 2 == 0)
		{
			Add(Notes, Beat * .25, Beat * 1.3, Blue[Phrase], .13f, -.40f, Voice::Bell);
			Add(Notes, Beat * 1.25, Beat, Blue[(Phrase + 1) % 8], .075f, -.25f, Voice::Bell);
		}
		Add(Notes, Beat * 2.25, Beat * 1.1, Orange[Phrase], .11f, .40f, Voice::Wood);
		if (Chapter == 1)
		{
			Add(Notes, Beat * 3.1, Beat * .8, Blue[Phrase] + 12, .055f, .15f, Voice::Glass);
		}
		if (Chapter == 2)
		{
			Add(Notes, Beat * 1.9, Beat, Root + 24, .075f, -.65f, Voice::Wood);
			Add(Notes, Beat * 3.2, Beat * .7, Root + 31, .065f, .65f, Voice::Bell);
		}
		if (Chapter == 3)
		{
			for (int Index = 0; Index < 3; ++Index)
			{
				Add(Notes, Beat * (.2 + Index * .55), Beat * .6,
					Blue[(Phrase + Index) % 8], .06f, -.45f, Voice::Bell);
				Add(Notes, Beat * (2.1 + Index * .55), Beat * .6,
					Blue[(Phrase + Index) % 8] - 12, .065f, .45f, Voice::Wood);
			}
		}
		if (Chapter == 4)
		{
			Add(Notes, Beat * .15, BarDuration - Beat * .15, Root - 12, .05f, 0, Voice::Pad);
			Add(Notes, Beat * 3.1, Beat * .8, Blue[(Phrase + 2) % 8], .09f, -.15f, Voice::Glass);
		}
		return Notes;
	}

	inline Score Borrow(bool BlueReceives)
	{
		Score Notes;
		Add(Notes, 0, .65, BlueReceives ? 69 : 74, .16f,
			BlueReceives ? .45f : -.45f, BlueReceives ? Voice::Wood : Voice::Bell);
		Add(Notes, .24, .8, BlueReceives ? 77 : 72, .17f,
			BlueReceives ? -.25f : .25f, BlueReceives ? Voice::Bell : Voice::Wood);
		return Notes;
	}

	inline Score Transfer(bool BluePrint, float From, float To, bool Arrival)
	{
		Score Notes;
		const int Pitch = BluePrint ? 77 : 69;
		Notes.push_back({0, Arrival ? .85 : .65, Pitch, .13f, Arrival ? To : From, To,
			BluePrint ? Voice::Bell : Voice::Wood});
		if (Arrival) { Add(Notes, .20, .7, Pitch + 7, .10f, To, Voice::Glass); }
		return Notes;
	}

	inline Score Goal(bool BlueArrives, bool Both)
	{
		Score Notes;
		const int Blue[] = {74, 77, 81};
		const int Orange[] = {69, 72, 74};
		for (int Index = 0; Index < 3; ++Index)
		{
			if (BlueArrives || Both)
			{
				Add(Notes, Index * .35, 1.2, Blue[Index], .17f, -.35f, Voice::Bell);
			}
			if (!BlueArrives || Both)
			{
				Add(Notes, (Both ? 1.15 : 0) + Index * .35, 1.2,
					Orange[Index], .18f, .35f, Voice::Wood);
			}
		}
		if (Both)
		{
			for (int Pitch : {50, 57, 62, 65, 69}) { Add(Notes, 2.1, 2.2, Pitch, .09f, 0, Voice::Pad); }
		}
		return Notes;
	}

	inline float Tone(Voice Instrument, double Phase, double Time)
	{
		switch (Instrument)
		{
		case Voice::Bell:
			return float((.70 * std::sin(Phase) + .18 * std::sin(Phase * 2.756) * std::exp(-Time * 4)) * std::exp(-Time * 2.7));
		case Voice::Wood:
			return float((.70 * std::sin(Phase) + .22 * std::sin(Phase * 2) * std::exp(-Time * 7)) * std::exp(-Time * 4.5));
		case Voice::Glass:
			return float((.55 * std::sin(Phase) + .22 * std::sin(Phase * 3.997)) * std::exp(-Time * 3.5));
		default:
			return float(.72 * std::sin(Phase) + .16 * std::sin(Phase * 2.001));
		}
	}

	inline Pcm Render(const Score& Notes, double Duration)
	{
		const int Frames = int(std::ceil(Duration * SampleRate));
		std::vector<float> Mix(Frames * 2, 0.f);
		for (const Note& N : Notes)
		{
			const double Frequency = 440.0 * std::pow(2.0, (N.Midi - 69) / 12.0);
			const int EchoCount = N.Instrument == Voice::Pad ? 1 : 3;
			for (int Echo = 0; Echo < EchoCount; ++Echo)
			{
				const int Start = int((N.Start + Echo * .155) * SampleRate);
				const int Count = std::min(int(N.Duration * SampleRate), Frames - Start);
				for (int Frame = 0; Frame < Count; ++Frame)
				{
					const double Time = double(Frame) / SampleRate;
					const double Edge = N.Instrument == Voice::Pad ? .28 : .009;
					const double Envelope = std::min(1.0, Time / Edge) * std::min(1.0, (N.Duration - Time) / .12);
					const float Pan = std::clamp(N.Pan + (N.EndPan - N.Pan) * float(Time / N.Duration), -1.f, 1.f);
					const double Angle = (Pan + 1) * Pi * .25;
					const float Sample = Tone(N.Instrument, 2 * Pi * Frequency * Time, Time) *
						float(Envelope) * N.Gain * std::pow(.22f, float(Echo));
					Mix[(Start + Frame) * 2] += Sample * float(std::cos(Angle));
					Mix[(Start + Frame) * 2 + 1] += Sample * float(std::sin(Angle));
				}
			}
		}
		Pcm Result(Mix.size());
		for (std::size_t Index = 0; Index < Mix.size(); ++Index)
		{
			Result[Index] = std::int16_t(std::tanh(Mix[Index] * 2.f) * 30000.0);
		}
		return Result;
	}

	inline Pcm RenderBar(int Chapter, int Bar)
	{
		// 把上一小节的尾音一起渲染，再截取当前小节，避免循环边界截断回声。
		Score Notes = ChapterBar(Chapter, (Bar + 7) % 8);
		for (Note N : ChapterBar(Chapter, Bar))
		{
			N.Start += BarDuration;
			Notes.push_back(N);
		}
		const Pcm TwoBars = Render(Notes, BarDuration * 2);
		const std::size_t FirstSample = std::size_t(std::ceil(BarDuration * SampleRate)) * 2;
		return Pcm(TwoBars.begin() + FirstSample, TwoBars.end());
	}
}
