#include "GridPuzzleSound.h"

#include "GridMusicScore.h"
#include "GridPuzzleAudioWave.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"

// 根据事件生成单声道 PCM 数据，通过 USoundWaveProcedural 播放。
UAudioComponent* PlayGridSound(UObject* Context, EGridSound Sound, float Volume)
{
	// 每行依次配置时长、主频、后续叠加频率和音量，顺序对应 EGridSound。
	static constexpr struct
	{
		float Duration;
		float Pitch;
		float Follow;
		float Level;
	} Cues[] = {
		{.09f, 178.f,    0.f, .0275f}, // Step
		{.17f, 392.f,  588.f, .0260f}, // Print
		{.19f, 523.f,  784.f, .0300f}, // Transfer
		{.15f, 121.f,   92.f, .0620f}, // Reject
		{.35f, 330.f,  660.f, .0700f}, // Switch
		{.34f,  80.f,   36.f, .0750f}, // Collapse
		{.38f, 440.f,  660.f, .0800f}, // Goal
		{},                          // Complete
		{.13f, 310.f,  420.f, .0360f}, // Select
		{.28f, 263.f,  526.f, .0520f}, // Bridge
		{},                          // Ending
	};

	constexpr int32 SampleRate = GridMusic::SampleRate;
	TArray<int16> Data;
	int32 Channels = 1;
	const bool bCompletionCue = Sound == EGridSound::Complete || Sound == EGridSound::Ending;
	if (bCompletionCue)
	{
		GridMusic::Score Notes;
		if (Sound == EGridSound::Ending) { Notes = GridMusic::Goal(true, true); }
		else
		{
			// 两个角色轮流回应，最后一起落在主音和弦上。
			GridMusic::Add(Notes, 0., .70, 74, .17f, -.22f, GridMusic::Voice::Bell);
			GridMusic::Add(Notes, .42, .72, 77, .17f, .22f, GridMusic::Voice::Wood);
			GridMusic::Add(Notes, .84, 1.18, 74, .16f, -.12f, GridMusic::Voice::Bell);
			GridMusic::Add(Notes, .84, 1.18, 69, .12f, .12f, GridMusic::Voice::Wood);
			GridMusic::Add(Notes, .84, 1.50, 50, .07f, 0.f, GridMusic::Voice::Pad);
		}
		const auto Samples = GridMusic::Render(Notes, Sound == EGridSound::Ending ? GridEndingDuration : GridCompletionDuration);
		Data.Reset();
		Data.Append(Samples.data(), int32(Samples.size()));
		Channels = 2;
	}
	else
	{
		const auto& Cue = Cues[static_cast<int32>(Sound)];
		const int32 SampleCount = FMath::CeilToInt(Cue.Duration * SampleRate);
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
	}
	auto* Wave = NewObject<UGridPcmSoundWave>(Context);
	Wave->SetPcm(MoveTemp(Data), Channels, SampleRate, false);
	Wave->Priority = bCompletionCue ? 10.f : 1.f;
	Wave->bBypassVolumeScaleForPriority = bCompletionCue;

	UAudioComponent* Audio = UGameplayStatics::CreateSound2D(Context, Wave, FMath::Clamp(Volume, 0.f, 1.f));
	if (!Audio) { return nullptr; }
	Audio->SetUISound(true);
	Audio->bStopWhenOwnerDestroyed = true;
	Audio->Play();
	return Audio;
}
