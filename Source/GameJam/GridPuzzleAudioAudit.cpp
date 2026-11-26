#include "GridPuzzleAudioAudit.h"

#include "GridPuzzleMusic.h"
#include "GridPuzzleSound.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Components/AudioComponent.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"
#include "UObject/UObjectIterator.h"

namespace
{
	struct FAudioAudit : TSharedFromThis<FAudioAudit>
	{
		TWeakObjectPtr<UObject> Context;
		TWeakObjectPtr<UGridMusicComponent> Music;
		FString Folder, Report;
		int32 Phase = -1, BurstCount = 0;
		float Elapsed = 0, PhaseElapsed = 0, Duration = 0, ExportWait = 0;
		bool bExporting = false;

		void StartPhase()
		{
			const TCHAR* Names[] = {TEXT("silence"), TEXT("step"), TEXT("print"), TEXT("transfer"),
				TEXT("reject"), TEXT("switch"), TEXT("collapse"), TEXT("goal"), TEXT("complete"),
				TEXT("select"), TEXT("bridge"), TEXT("bgm1"), TEXT("muted"), TEXT("resumed"),
				TEXT("bgm2"), TEXT("bgm3"), TEXT("bgm4"), TEXT("bgm5"), TEXT("paused_bgm"),
				TEXT("complete_music_muted"), TEXT("rapid_footsteps"), TEXT("after_cleanup"), TEXT("complete_after_stress")};
			Duration = Phase >= 11 && Phase <= 17 && Phase != 12 ? 4.f : 1.f;
			if (Phase == 8 || Phase == 19 || Phase == 22) { Duration = 5.1f; }
			if (Phase == 18) { Duration = 2.f; }
			if (Phase == 20) { Duration = 6.3f; }
			bool bComponentCreated = true;
			if (Phase >= 1 && Phase <= 10)
			{
				bComponentCreated = PlayGridSound(Context.Get(), EGridSound(Phase - 1)) != nullptr;
			}
			else if (Phase == 11 || (Phase >= 14 && Phase <= 17))
			{
				Music->SetVolume(.65f);
				Music->SetMood(false, false);
				Music->StartChapter(Phase == 11 ? 0 : Phase - 13, {10, 9}, 0);
			}
			else if (Phase == 12) { Music->SetVolume(0.f); }
			else if (Phase == 13) { Music->SetVolume(.65f); }
			else if (Phase == 18) { UGameplayStatics::SetGamePaused(Context.Get(), true); }
			else if (Phase == 19)
			{
				UGameplayStatics::SetGamePaused(Context.Get(), false);
				Music->StopMusic();
				bComponentCreated = PlayGridSound(Context.Get(), EGridSound::Complete) != nullptr;
			}
			else if (Phase == 22)
			{
				bComponentCreated = PlayGridSound(Context.Get(), EGridSound::Complete) != nullptr;
			}
			Report += FString::Printf(TEXT("%s,%.3f,%.3f,%d\n"), Names[Phase], Elapsed, Elapsed + Duration, bComponentCreated);
			FFileHelper::SaveStringToFile(Report, *(Folder / TEXT("Phases.csv")));
		}

		bool Tick(float Delta)
		{
			if (!Context.IsValid() || !Music.IsValid()) { return false; }
			if (bExporting)
			{
				ExportWait += Delta;
				if (IFileManager::Get().FileSize(*(Folder / TEXT("EngineOutput.wav"))) > 44 || ExportWait > 8.f)
				{
					FFileHelper::SaveStringToFile(ExportWait > 8.f ? TEXT("recording export failed") : TEXT("recording exported"),
						*(Folder / TEXT("Result.txt")));
					FPlatformMisc::RequestExit(false);
					return false;
				}
				return true;
			}
			if (Phase < 0)
			{
				Music->StopMusic();
				UAudioMixerBlueprintLibrary::StartRecordingOutput(Context.Get(), 70.f);
				Phase = 0;
				StartPhase();
			}
			Elapsed += Delta;
			PhaseElapsed += Delta;
			if (Phase == 20)
			{
				while (BurstCount < FMath::Min(100, int32(PhaseElapsed / .06f)))
				{
					PlayGridSound(Context.Get(), BurstCount++ % 2 == 0 ? EGridSound::Step : EGridSound::Print, .45f);
				}
			}
			if (PhaseElapsed < Duration) { return true; }
			if (Phase == 21)
			{
				int32 Active = 0;
				for (TObjectIterator<UAudioComponent> It; It; ++It)
				{
					if (It->Sound && It->Sound->GetOuter() == Context.Get() && It->IsPlaying()) { ++Active; }
				}
				FFileHelper::SaveStringToFile(FString::Printf(TEXT("active_short_sounds=%d"), Active), *(Folder / TEXT("Cleanup.txt")));
			}
			PhaseElapsed = 0;
			if (++Phase < 23) { StartPhase(); return true; }
			UAudioMixerBlueprintLibrary::StopRecordingOutput(Context.Get(), EAudioRecordingExportType::WavFile, TEXT("EngineOutput"), Folder);
			bExporting = true;
			return true;
		}
	};
}

void BeginGridAudioAudit(UObject* Context, UGridMusicComponent* Music, bool bSoundEnabled, bool bMusicEnabled)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("OneStepAudioAudit"))) { return; }
	auto Audit = MakeShared<FAudioAudit>();
	Audit->Context = Context;
	Audit->Music = Music;
	Audit->Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Verification/AudioAudit"));
	FParse::Value(FCommandLine::Get(), TEXT("AudioAuditDir="), Audit->Folder);
	IFileManager::Get().MakeDirectory(*Audit->Folder, true);
	FFileHelper::SaveStringToFile(FString::Printf(TEXT("sound_enabled=%d\nmusic_enabled=%d\n"), bSoundEnabled, bMusicEnabled),
		*(Audit->Folder / TEXT("Settings.txt")));
	Audit->Report = TEXT("phase,start,end,component_created\n");
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Audit](float Delta) { return Audit->Tick(Delta); }));
}
