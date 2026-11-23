#include "GridPuzzleMusic.h"

#include "GridMusicScore.h"
#include "GridPuzzleAudioWave.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	constexpr float BackgroundGain = 1.4f;
	UGridPcmSoundWave* MakeMusicWave(UObject* Outer, const std::vector<std::int16_t>& Samples, bool bRepeat)
	{
		auto* Wave = NewObject<UGridPcmSoundWave>(Outer);
		TArray<int16> Pcm;
		Pcm.Append(Samples.data(), int32(Samples.size()));
		Wave->SetPcm(MoveTemp(Pcm), 2, GridMusic::SampleRate, bRepeat);
		Wave->SoundGroup = SOUNDGROUP_Music;
		return Wave;
	}
}

UGridMusicComponent::UGridMusicComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
}

void UGridMusicComponent::StartChapter(int32 Chapter, FIntPoint BoardSize, int32 BeatPeriod)
{
	GridSize = FIntPoint(FMath::Max(2, BoardSize.X), FMath::Max(2, BoardSize.Y));
	RhythmPeriod = FMath::Max(0, BeatPeriod);
	CancelAccents();
	if (ChapterIndex == FMath::Clamp(Chapter, 0, 4) && BedAudio)
	{
		UpdateBedVolume();
		return;
	}
	StopMusic();
	ChapterIndex = FMath::Clamp(Chapter, 0, 4);
	// 预先合成完整八小节，由音频线程直接循环，不依赖游戏帧补队列。
	std::vector<std::int16_t> Loop;
	for (int32 Bar = 0; Bar < 8; ++Bar)
	{
		const auto Samples = GridMusic::RenderBar(ChapterIndex, Bar);
		Loop.insert(Loop.end(), Samples.begin(), Samples.end());
	}
	BedWave = MakeMusicWave(this, Loop, true);
	BedWave->Priority = 2.f;
	BedAudio = UGameplayStatics::CreateSound2D(this, BedWave, MusicVolume * MoodVolume * BackgroundGain, 1.f, 0.f, nullptr, false, false);
	if (BedAudio)
	{
		BedAudio->SetUISound(true);
		// 淡入系数与实际音量分开，避免音量被重复相乘。
		BedAudio->FadeIn(1.f, 1.f);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Music could not start: no audio component was created for chapter %d."), ChapterIndex + 1);
	}
}

void UGridMusicComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTick)
{
	Super::TickComponent(DeltaTime, TickType, ThisTick);
	// 若设备切换或通道回收使底乐停止，在恢复播放条件后重新启动。
	if (BedAudio && MusicVolume > 0.f && !BedAudio->IsPlaying()) { UpdateBedVolume(); }
}

void UGridMusicComponent::UpdateBedVolume()
{
	if (!BedAudio) { return; }
	// AdjustVolume(..., 0) 会结束播放；用音量乘数静音，保留循环和播放位置。
	BedAudio->SetVolumeMultiplier(MusicVolume * MoodVolume * BackgroundGain);
	if (MusicVolume > 0.f && !BedAudio->IsPlaying()) { BedAudio->Play(); }
}

void UGridMusicComponent::SetMood(bool bQuiet, bool bCompleted)
{
	MoodVolume = bCompleted ? .28f : bQuiet ? .48f : 1.f;
	UpdateBedVolume();
}

void UGridMusicComponent::SetVolume(float Volume)
{
	MusicVolume = FMath::Clamp(Volume, 0.f, 1.f);
	UpdateBedVolume();
	// 静音也立即停止尚未结束的回应乐句。
	if (MusicVolume == 0.f) { CancelAccents(); }
}

void UGridMusicComponent::PlayPhrase(const std::vector<std::int16_t>& Data, float Gain)
{
	if (MusicVolume == 0.f || !BedAudio || Data.empty()) { return; }
	Accents.RemoveAll([](const TWeakObjectPtr<UAudioComponent>& Accent)
	{
		return !Accent.IsValid() || !Accent->IsPlaying();
	});
	// 多端寄印合并为短乐句，快速移动时也限制同时发声的装饰层数。
	if (Accents.Num() >= 4)
	{
		if (Accents[0].IsValid()) { Accents[0]->Stop(); }
		Accents.RemoveAt(0);
	}
	auto* Wave = MakeMusicWave(this, Data, false);
	if (UAudioComponent* Accent = UGameplayStatics::SpawnSound2D(this, Wave, MusicVolume * Gain))
	{
		Accent->SetUISound(false);
		Accents.Add(Accent);
	}
}

void UGridMusicComponent::NotifyMove(EPlayerRole Mover, const FGridMove& Move, int32 MoveCount)
{
	if (!BedAudio || MusicVolume == 0.f || Move.bReachedGoal) { return; }
	const bool bBlue = Mover == EPlayerRole::PlayerA;
	if (Move.bRecordedStep || Move.bReplayStep)
	{
		const FIntPoint Direction = Move.Path.IsEmpty() ? Move.To - Move.From : Move.Path[0] - Move.From;
		const int32 Pitch = Direction.X > 0 ? 77 : Direction.X < 0 ? 72 : Direction.Y < 0 ? 74 : 81;
		GridMusic::Score Notes;
		GridMusic::Add(Notes, 0, .65, Pitch - (bBlue ? 0 : 12), .19f,
			bBlue ? -.35f : .35f, bBlue ? GridMusic::Voice::Bell : GridMusic::Voice::Wood);
		PlayPhrase(GridMusic::Render(Notes, 1.1));
	}
	else if (Move.FreshBorrowedPrintCount > 0 && FPlatformTime::Seconds() - LastBorrowTime > .28)
	{
		LastBorrowTime = FPlatformTime::Seconds();
		PlayPhrase(GridMusic::Render(GridMusic::Borrow(bBlue), 1.35));
	}
	const auto Pan = [this](FIntPoint Position)
	{
		// 对应等距摄像机的屏幕横向；南北寄印也会在立体声中移动。
		const float Span = .971f * (GridSize.X - 1) + .238f * (GridSize.Y - 1);
		const float ScreenX = .971f * Position.X + .238f * Position.Y;
		return FMath::Clamp(ScreenX / Span * 1.4f - .7f, -.7f, .7f);
	};
	if (!Move.PrintDispatches.IsEmpty())
	{
		const auto& Print = Move.PrintDispatches[0];
		PlayPhrase(GridMusic::Render(GridMusic::Transfer(Print.Owner == EFootprintOwner::PlayerA,
			Pan(Print.From), Pan(Print.To), false), 1.05));
	}
	for (const auto& Print : Move.PrintArrivals)
	{
		const bool bImmediate = Move.PrintDispatches.ContainsByPredicate([&Print](const FGridPrintTransfer& Sent)
		{
			return Sent.From == Print.From && Sent.To == Print.To && Sent.Owner == Print.Owner;
		});
		if (!bImmediate)
		{
			PlayPhrase(GridMusic::Render(GridMusic::Transfer(Print.Owner == EFootprintOwner::PlayerA,
				Pan(Print.From), Pan(Print.To), true), 1.3));
			break;
		}
	}
	if (RhythmPeriod > 0)
	{
		GridMusic::Score Beat;
		const int32 Landings = FMath::Max(1, Move.Path.Num());
		for (int32 Index = 0; Index < Landings; ++Index)
		{
			const int32 Phase = (MoveCount - Landings + Index + 1) % RhythmPeriod;
			GridMusic::Add(Beat, Index * .07, .16, 57 + (Phase % 5) * 2, .055f, 0, GridMusic::Voice::Wood);
		}
		PlayPhrase(GridMusic::Render(Beat, .5 + (Landings - 1) * .07));
	}
}

void UGridMusicComponent::CancelAccents()
{
	for (const auto& Accent : Accents)
	{
		if (Accent.IsValid()) { Accent->Stop(); }
	}
	Accents.Reset();
	LastBorrowTime = -10;
}

void UGridMusicComponent::StopMusic()
{
	CancelAccents();
	if (BedAudio) { BedAudio->Stop(); BedAudio->DestroyComponent(); }
	BedAudio = nullptr;
	BedWave = nullptr;
	ChapterIndex = INDEX_NONE;
}

void UGridMusicComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	StopMusic();
	Super::EndPlay(Reason);
}
