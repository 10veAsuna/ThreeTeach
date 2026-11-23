#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include "GridPuzzleAudioWave.generated.h"

// 一次性声音明确报告结束，循环声音直接在音频线程续播，避免空队列一直占用通道。
UCLASS()
class GAMEJAM_API UGridPcmSoundWave : public USoundWaveProcedural
{
	GENERATED_BODY()
public:
	void SetPcm(TArray<int16>&& Samples, int32 Channels, int32 Rate, bool bRepeat);
	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& Params) override;

private:
	TSharedPtr<const TArray<int16>, ESPMode::ThreadSafe> Pcm;
};
