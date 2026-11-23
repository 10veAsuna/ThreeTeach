#include "GridPuzzleAudioWave.h"

#include "Sound/SoundGenerator.h"

namespace
{
	class FGridPcmGenerator final : public ISoundGenerator
	{
	public:
		FGridPcmGenerator(TSharedPtr<const TArray<int16>, ESPMode::ThreadSafe> InPcm,
			int32 InChannels, bool bInRepeat)
			: Pcm(MoveTemp(InPcm)), Channels(InChannels), bRepeat(bInRepeat) {}

		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
		{
			int32 Written = 0;
			while (Pcm && !Pcm->IsEmpty() && Written < NumSamples)
			{
				if (Cursor == Pcm->Num())
				{
					if (!bRepeat) { break; }
					Cursor = 0;
				}
				const int32 Count = FMath::Min(NumSamples - Written, Pcm->Num() - Cursor);
				for (int32 Index = 0; Index < Count; ++Index)
				{
					OutAudio[Written + Index] = (*Pcm)[Cursor + Index] / 32768.f;
				}
				Cursor += Count;
				Written += Count;
			}
			FMemory::Memzero(OutAudio + Written, (NumSamples - Written) * sizeof(float));
			return NumSamples;
		}

		virtual bool IsFinished() const override
		{
			return !Pcm || Pcm->IsEmpty() || (!bRepeat && Cursor == Pcm->Num());
		}
		virtual int32 GetNumChannels() const override { return Channels; }

	private:
		TSharedPtr<const TArray<int16>, ESPMode::ThreadSafe> Pcm;
		int32 Channels;
		int32 Cursor = 0;
		bool bRepeat;
	};
}

void UGridPcmSoundWave::SetPcm(TArray<int16>&& Samples, int32 Channels, int32 Rate, bool bRepeat)
{
	check(Channels > 0 && Rate > 0 && Samples.Num() % Channels == 0);
	SetSampleRate(Rate);
	NumChannels = Channels;
	bLooping = bRepeat;
	Duration = bRepeat ? INDEFINITELY_LOOPING_DURATION : float(Samples.Num()) / (Channels * Rate);
	// 仅底乐保留静音中的播放位置；短音效结束后正常释放通道。
	VirtualizationMode = bRepeat ? EVirtualizationMode::PlayWhenSilent : EVirtualizationMode::Disabled;
	Pcm = MakeShared<TArray<int16>, ESPMode::ThreadSafe>(MoveTemp(Samples));
}

ISoundGeneratorPtr UGridPcmSoundWave::CreateSoundGenerator(const FSoundGeneratorInitParams& Params)
{
	return MakeShared<FGridPcmGenerator, ESPMode::ThreadSafe>(Pcm, NumChannels, bool(bLooping));
}
