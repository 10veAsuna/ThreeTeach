#include "GridPuzzleSound.h"

#include "GridPuzzleAudioWave.h"
#include "Kismet/GameplayStatics.h"

// 根据事件生成单声道 PCM 数据，通过 USoundWaveProcedural 播放。
void PlayGridSound(UObject* Context, EGridSound Sound, float Volume)
{
	// 每行依次配置时长、主频、后续叠加频率和音量，顺序对应 EGridSound。
	static constexpr struct
	{
		float Duration;
		float Pitch;
		float Follow;
		float Level;
	} Cues[] = {
		{.11f, 178.f,  0.f, .22f}, // Step
		{.28f, 392.f,  588.f, .21f},
		{.39f, 523.f,  784.f, .24f},
		{.16f, 121.f,  92.f, .18f},
		{.50f, 330.f,  660.f, .22f},
		{.48f, 80.f,   36.f, .30f},
		{.45f, 440.f,  660.f, .21f},
		{.88f, 523.f, 1047.f, .25f},
		{.16f, 310.f,  420.f, .14f},
		{.34f, 263.f,  526.f, .17f},
	};

	const auto& Cue = Cues[static_cast<int32>(Sound)];
	constexpr int32 SampleRate = 24000;
	const int32 SampleCount = FMath::CeilToInt(Cue.Duration * SampleRate);
	TArray<int16> Data;
	Data.SetNumUninitialized(SampleCount);
	// 固定种子让同一类事件的噪声表现一致，步伐与坍塌会混入短促噪声。
	FRandomStream Random(201 + static_cast<int32>(Sound));
	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		const float Time = static_cast<float>(Index) / SampleRate;
		const float Progress = Time / Cue.Duration;
		// 快速起音后逐渐衰减，避免采样从高振幅突然开始或结束。
		const float Envelope = FMath::Min(Time * 125.f, 1.f) * FMath::Square(1.f - Progress);
		const float Phase = 2.f * PI * Cue.Pitch * Time;
		float Signal = .58f * FMath::Sin(Phase) + .14f * FMath::Sin(2.003f * Phase);
		if (Cue.Follow > 0.f && Progress > .20f)
		{
			const float Offset = Time - Cue.Duration * .20f;
			Signal += .25f * FMath::Min(Offset * 95.f, 1.f) * FMath::Sin(2.f * PI * Cue.Follow * Offset);
		}
		if (Sound == EGridSound::Collapse || Sound == EGridSound::Step)
		{
			Signal += .28f * Random.FRandRange(-1.f, 1.f) * FMath::Square(1.f - Progress);
		}
		Data[Index] = static_cast<int16>(Envelope * Signal * Cue.Level * 32767.f);
	}
	UGridPcmSoundWave* Wave = NewObject<UGridPcmSoundWave>();
	Wave->SetPcm(MoveTemp(Data), 1, SampleRate, false);
	UGameplayStatics::PlaySound2D(Context, Wave, FMath::Clamp(Volume, 0.f, 1.f));
}
