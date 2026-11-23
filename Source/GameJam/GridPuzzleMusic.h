#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GridPuzzleRules.h"
#include <vector>
#include <cstdint>
#include "GridPuzzleMusic.generated.h"

class UAudioComponent;
class UGridPcmSoundWave;

// 配乐使用独立音量。底乐在暂停页继续轻声播放，行动乐句只由已提交的规则结果触发。
UCLASS()
class GAMEJAM_API UGridMusicComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGridMusicComponent();
	void StartChapter(int32 Chapter, FIntPoint BoardSize, int32 BeatPeriod);
	void SetMood(bool bQuiet, bool bCompleted);
	void SetVolume(float Volume);
	void NotifyMove(EPlayerRole Mover, const FGridMove& Move, int32 MoveCount);
	void CancelAccents();
	void StopMusic();

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTick) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void PlayPhrase(const std::vector<std::int16_t>& Data, float Gain = 1.f);
	void UpdateBedVolume();
	UPROPERTY() TObjectPtr<UAudioComponent> BedAudio;
	UPROPERTY() TObjectPtr<UGridPcmSoundWave> BedWave;
	TArray<TWeakObjectPtr<UAudioComponent>> Accents;
	int32 ChapterIndex = INDEX_NONE;
	FIntPoint GridSize = FIntPoint(2, 2);
	float MusicVolume = .65f;
	float MoodVolume = .55f;
	int32 RhythmPeriod = 0;
	double LastBorrowTime = -10;
};
