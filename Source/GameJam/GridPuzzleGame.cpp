#include "GridPuzzleGame.h"

#include "GridPuzzleScreen.h"
#include "GridPuzzleSound.h"
#include "GridPuzzleMusic.h"
#include "GridPuzzleAudioAudit.h"
#include "GridPuzzleVisuals.h"
#include "Algo/AllOf.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "GridPuzzle"

// 阅读主流程可从文件末尾 StartPlay 开始，再看 Initialize、LoadLevel 与 Move。
// Rules 保存局面，TileActors 和 Characters 将规则结果呈现到 UE 场景。
namespace
{
	constexpr float TileSize = 100.f;
	constexpr float StepTime = .18f;
	constexpr float RejectTime = .14f;
	constexpr float DeckTime = .46f;
	constexpr float SwapTime = .35f;
	const TCHAR* ProgressSlot = TEXT("OneStep_Progress_V7");
	const EPlayerRole Roles[] = { EPlayerRole::PlayerA, EPlayerRole::PlayerB };

	const TCHAR* RoleLetter(EPlayerRole Role)
	{
		return Role == EPlayerRole::PlayerA ? TEXT("A") : TEXT("B");
	}

	FString DirectionGlyph(FIntPoint Direction)
	{
		if (Direction.X != 0) { return Direction.X > 0 ? TEXT("→") : TEXT("←"); }
		return Direction.Y > 0 ? TEXT("↓") : TEXT("↑");
	}
}

// C++ 默认类作为兜底，BP_Board 可以把它们替换成带美术配置的派生蓝图。
AGridManager::AGridManager()
{
	Music = CreateDefaultSubobject<UGridMusicComponent>(TEXT("Music"));
	TileClass = AGridTile::StaticClass();
	CharacterClass = AGridPuzzleCharacter::StaticClass();
}

const UGridLevelDataAsset* AGridManager::CurrentSection() const
{
	const UGridLevelDataAsset* Chapter = Levels.IsValidIndex(LevelIndex) ? Levels[LevelIndex].Get() : nullptr;
	if (!Chapter || Chapter->Sections.IsEmpty()) { return Chapter; }
	return Chapter->Sections.IsValidIndex(SectionIndex) ? Chapter->Sections[SectionIndex].Get() : nullptr;
}

int32 AGridManager::GetSectionCount() const
{
	return Levels.IsValidIndex(LevelIndex) && Levels[LevelIndex] ? FMath::Max(1, Levels[LevelIndex]->Sections.Num()) : 1;
}

FText AGridManager::GetSectionTitle() const
{
	return CurrentSection() ? CurrentSection()->Title : FText::GetEmpty();
}

int32 AGridManager::GetSourceLevelIndex() const
{
	return CurrentSection() ? CurrentSection()->LegacyChapterIndex : INDEX_NONE;
}

void AGridManager::SaveUndo(FGridPuzzleRules Previous, EPlayerRole Mover, TArray<FIntPoint> Changed)
{
	ClearStepHint();
	UndoHistory.Add({MoveTemp(Previous), Mover, MoveTemp(Changed), SectionIndex, ChapterMoveCount});
}

// 初始化调用链为 GameMode::StartPlay -> Initialize -> ShowMenu -> LoadLevel。
void AGridManager::Initialize(APlayerController& Controller)
{
	Player = &Controller;
	// 老版 BP_Board 储存的是十四张旧图，V6 储存的是五组分区；两者都改用整图。
	if (Levels.IsEmpty() || Levels.Num() == 14 ||
		(Levels.Num() == 5 && Levels[0] && (!Levels[0]->Sections.IsEmpty() ||
		 Levels[0]->GetPathName().StartsWith(TEXT("/Game/OneStep/Levels/V7/")) ||
		 Levels[0]->GetPathName().StartsWith(TEXT("/Game/OneStep/Levels/V8/")))))
	{
		Levels.Reset();
		Levels.Append(CreateCampaignLevels());
	}
	// 已导入的关卡只更新文案，保留编辑器里调整过的棋盘和机关配置。
	if (Levels.Num() == 5 && Levels[0] && Levels[0]->GetPathName().StartsWith(TEXT("/Game/")))
	{
		const auto CopyDefaults = CreateCampaignLevels();
		for (int32 Index = 0; Index < Levels.Num(); ++Index)
		{
			if (Levels[Index] && CopyDefaults.IsValidIndex(Index))
			{
				Levels[Index]->Title = CopyDefaults[Index]->Title;
				Levels[Index]->Hint = CopyDefaults[Index]->Hint;
			}
		}
	}
	// 第一个挑战关之前均为主线，追加的玩法试玩使用独立的索引区间。
	const int32 FirstChallenge = Levels.IndexOfByPredicate([](const UGridLevelDataAsset* Level)
	{
		return Level && Level->bChallenge;
	});
	MainLevelCount = FirstChallenge == INDEX_NONE ? Levels.Num() : FirstChallenge;
	CampaignLevelCount = Levels.Num();
	Levels.Append(CreatePrototypeLevels());
	// 存档只恢复进度与收集项，关内位置和撤销历史从关卡初始状态重建。
	const bool bLegacyProgress = !UGameplayStatics::DoesSaveGameExist(ProgressSlot, 0);
	const TCHAR* LoadSlot = bLegacyProgress ?
		(UGameplayStatics::DoesSaveGameExist(TEXT("OneStep_Progress_V6"), 0) ?
			TEXT("OneStep_Progress_V6") : TEXT("OneStep_Progress_V4")) :
		ProgressSlot;
	if (UGameplayStatics::DoesSaveGameExist(LoadSlot, 0))
	{
		if (const UGridPuzzleProgress* Saved = Cast<UGridPuzzleProgress>(UGameplayStatics::LoadGameFromSlot(LoadSlot, 0)))
		{
			bMusicMuted = Saved->bMusicMuted;
			bEffectsMuted = Saved->bEffectsMuted;
			CompletedMainLevels = FMath::Clamp(Saved->CompletedMainLevels, 0, MainLevelCount);
			if (bLegacyProgress && MainLevelCount == 5)
			{
				CompletedMainLevels = 0;
				for (int32 Threshold : {3, 9, 11, 12, 14})
				{
					if (Saved->CompletedMainLevels < Threshold) { break; }
					++CompletedMainLevels;
				}
			}
			for (const int32 Level : Saved->CompletedLevels)
			{
				if (Level >= 0 && Level < CampaignLevelCount) { CompletedLevels.Add(Level); }
			}
			if (CompletedLevels.IsEmpty())
			{
				for (int32 Level = 0; Level < FMath::Min(CompletedMainLevels, CampaignLevelCount); ++Level)
				{
					CompletedLevels.Add(Level);
				}
			}
			for (const int32 Echo : Saved->EarnedEchoes)
			{
				if (!bLegacyProgress && Echo >= 0 && Echo < MainLevelCount) { EarnedEchoes.Add(Echo); }
			}
			if (!bLegacyProgress)
			{
				for (const int32 Intro : Saved->SeenMechanicIntros)
				{
					if (Intro >= 0 && Intro < MainLevelCount) { SeenMechanicIntros.Add(Intro); }
				}
			}
		}
	}
	if (!PaletteMaterial)
	{
		PaletteMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	// 灯光与正交摄像机也在运行时创建，停止试玩后这些对象随运行世界一起清理。
	ADirectionalLight* Light = GetWorld()->SpawnActor<ADirectionalLight>(FVector(0,-700,900), FRotator(-52, -32, 0));
	Light->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Light->GetLightComponent()->SetIntensity(3.2f);
	Light->GetLightComponent()->SetLightColor(FLinearColor(.76f,.87f,1.f));
	Light->GetLightComponent()->SetShadowBias(.40f);

	Camera = GetWorld()->SpawnActor<ACameraActor>();
	UCameraComponent* View = Camera->GetCameraComponent();
	View->SetProjectionMode(ECameraProjectionMode::Orthographic);
	View->SetAutoCalculateOrthoPlanes(false);
	View->SetOrthoNearClipPlane(0.f);
	View->SetOrthoFarClipPlane(4600.f);
	View->PostProcessSettings.bOverride_AutoExposureMethod = true;
	View->PostProcessSettings.AutoExposureMethod = AEM_Manual;
	View->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	View->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	View->PostProcessSettings.bOverride_BloomIntensity = true;
	View->PostProcessSettings.BloomIntensity = .28f;

	Player->SetViewTarget(Camera);
	BuildEnvironment();
	ShowMenu();
	BeginGridAudioAudit(this, Music, IsSoundEnabled(), IsMusicEnabled());
}

// 用基础几何体搭建背景底面与悬浮碎石，和可通行网格分开管理。
void AGridManager::BuildEnvironment()
{
	using namespace GridVisuals;
	Environment = GetWorld()->SpawnActor<AActor>();
	USceneComponent* Root = NewObject<USceneComponent>(Environment);
	Environment->SetRootComponent(Root);
	Root->RegisterComponent();
	Shape(*Environment, Root, PaletteMaterial, EGridShape::Cube, FVector(0,0,-625),
		FVector(49.f,49.f,.12f), FLinearColor(.010f,.025f,.043f), .24f);

	FRandomStream Random(354L);
	for (int32 Index = 0; Index < 55; ++Index)
	{
		const float Angle = Random.FRandRange(0.f, 2.f*PI);
		const float Radius = Random.FRandRange(485.f, 1630.f);
		const FVector Position(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle),
			Random.FRandRange(-400.f,-140.f));
		const float Scale = Random.FRandRange(.065f,.46f);
		const FLinearColor Tint = FMath::Lerp(FLinearColor(.027f,.071f,.105f),
			FLinearColor(.058f,.16f,.20f), Random.FRand());
		Shape(*Environment, Root, PaletteMaterial, EGridShape::Cube, Position,
			FVector(Scale,Scale,Scale*.66f), Tint, .1f,
			FRotator(Random.FRandRange(-22,22),Random.FRandRange(0,180),Random.FRandRange(-15,15)));
	}
	for (int32 Index = 0; Index < 54; ++Index)
	{
		const FVector Position(Random.FRandRange(-1400,1400), Random.FRandRange(-1650,1650),
			Random.FRandRange(-510,-120));
		Shape(*Environment, Root, PaletteMaterial, EGridShape::Sphere, Position,
			FVector(Random.FRandRange(.013f,.032f)), FLinearColor(.14f,.37f,.45f), .61f);
	}
}

// 将棋盘居中后映射到世界平面，网格向右对应世界 +Y，网格向下对应世界 -X。
FVector AGridManager::ToWorld(FIntPoint Coordinate) const
{
	return FVector((.5f*(Rules.GetSize().Y-1) - Coordinate.Y) * TileSize,
		(Coordinate.X - .5f*(Rules.GetSize().X-1)) * TileSize, 0);
}

// 根据关卡网格尺寸调整正交视野，菜单和结局使用额外的取景偏移。
void AGridManager::PositionCamera(bool bMenu, bool bOutro)
{
	const FVector Offset(-1040.f, -255.f, 1230.f);
	const float Width = FMath::Max3(1020.f, Rules.GetSize().X * 126.f, Rules.GetSize().Y * 165.f);
	const FVector Focus(bOutro ? 110.f : 0.f, bMenu ? -225.f : 0.f, IsPrototype() && !bMenu ? -37.f : 15.f);
	Camera->SetActorLocationAndRotation(Focus + Offset, (-Offset).Rotation());
	Camera->GetCameraComponent()->SetOrthoWidth(Width + (bMenu ? 230.f : bOutro ? 420.f : 0.f));
	for (const auto& Pair : TileActors)
	{
		Pair.Value->FaceMarker(Offset.Rotation());
	}
}

// 先验证新关卡数据，再清理旧场景并生成新地块与双角色。
bool AGridManager::LoadLevel(int32 Index, int32 Section, bool bContinueChapter)
{
	const UGridLevelDataAsset* Chapter = Levels.IsValidIndex(Index) ? Levels[Index].Get() : nullptr;
	const UGridLevelDataAsset* Data = Chapter && !Chapter->Sections.IsEmpty() ?
		(Chapter->Sections.IsValidIndex(Section) ? Chapter->Sections[Section].Get() : nullptr) : Chapter;
	if (!Data || !Rules.Initialize(*Data))
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid puzzle level at index %d. Check the Blueprint's Levels data assets."), Index);
		return false;
	}
	TGuardValue<bool> ApplyingMove(bApplyingMove, true);
	ClearStepHint();
	ClearPrintTrails();
	PrintConnections.Reset();
	RecentPrintEffect = INDEX_NONE;
	RelatedPrintEffect = INDEX_NONE;
	OpeningHighlight.Reset();
	HintGuide = FGridHintGuide::Build(Rules, CampaignLevelCount == 5 ? Index : -1);
	// 清理旧棋盘的定时器和音效。结算页会等通关乐句播完再推进关卡。
	StopCompletionCue();
	StopEffects();
	GetWorldTimerManager().ClearAllTimersForObject(this);
	for (const auto& Pair : TileActors) { Pair.Value->Destroy(); }
	for (AGridPuzzleCharacter* Character : Characters) { Character->Destroy(); }
	if (Outro)
	{
		Outro->Destroy();
		Outro = nullptr;
	}
	TileActors.Reset();
	MovePreviewMarkers.Reset();
	Characters.Reset();
	if (!bContinueChapter) { UndoHistory.Reset(); ChapterMoveCount = 0; }
	PreviewMarkers.Reset();
	ReplaySteps.Reset();
	ReplayStates.Reset();
	bActionPreview = false;
	LevelIndex = Index;
	SectionIndex = Section;
	ActiveRole = EPlayerRole::PlayerA;
	Feedback = FText::GetEmpty();
	UnlockInputAt = 0.f;
	bEndingReady = false;
	bMechanicIntroPending = Index > 0 && Index < CampaignLevelCount && LevelHasMechanicIntro(Index) &&
		!SeenMechanicIntros.Contains(Index);

	// 规则给出每个格子的坐标与类型，地块 Setup 负责创建模型和标记。
	for (const auto& Pair : Rules.GetTiles())
	{
		AGridTile* Tile = GetWorld()->SpawnActor<AGridTile>(TileClass, ToWorld(Pair.Key), FRotator::ZeroRotator);
		Tile->Setup(Pair.Value, PaletteMaterial);
		TileActors.Add(Pair.Key, Tile);
		if (Pair.Value.TileType == ETileType::Bridge)
		{
			for (const FGridBridgeCondition& Condition : Rules.GetBridgeConditions(Pair.Value.Marker))
			{
				Tile->AddBridgeLink(ToWorld(Condition.Source) - ToWorld(Pair.Key), PaletteMaterial);
			}
		}
	}
	// 每组机关保留独立的线条与编号，显示时可聚焦一组而不是全部堆叠。
	for (int32 Index = 0; Index < Rules.GetPrintEffects().Num(); ++Index)
	{
		const FGridPrintEffect& Effect = Rules.GetPrintEffects()[Index];
		FPrintConnection& Connection = PrintConnections.AddDefaulted_GetRef();
		Connection.EffectIndex = Index;
		TSet<FIntPoint> Sources;
		TMap<FIntPoint, TSet<FIntPoint>> Targets;
		for (const FGridPrintLink& Link : Effect.Links)
		{
			if (Link.Source != Effect.Trigger && !Sources.Contains(Link.Source))
			{
				Sources.Add(Link.Source);
				for (UStaticMeshComponent* Piece : TileActors.FindChecked(Effect.Trigger)->AddPrintLink(
					ToWorld(Link.Source) - ToWorld(Effect.Trigger), PaletteMaterial, true, Index)) { Connection.Pieces.Emplace(Piece); }
			}
			if (!Targets.FindOrAdd(Link.Source).Contains(Link.Target))
			{
				Targets.FindOrAdd(Link.Source).Add(Link.Target);
				for (UStaticMeshComponent* Piece : TileActors.FindChecked(Link.Source)->AddPrintLink(
					ToWorld(Link.Target) - ToWorld(Link.Source), PaletteMaterial, false, Index)) { Connection.Pieces.Emplace(Piece); }
			}
		}
	}
	FIntPoint EchoPosition;
	if (GetEchoLocationForLevel(LevelIndex, EchoPosition) && !EarnedEchoes.Contains(LevelIndex))
	{
		if (AGridTile* EchoTile = TileActors.FindRef(EchoPosition))
		{
			EchoTile->SetEchoAvailable(true, PaletteMaterial);
		}
	}
	// 按 A、B 顺序生成角色，Characters 的下标与 Rules::RoleIndex 保持一致。
	for (EPlayerRole Traveler : Roles)
	{
		const FIntPoint Position = Rules.GetPosition(Traveler);
		AGridPuzzleCharacter* Character = GetWorld()->SpawnActor<AGridPuzzleCharacter>(CharacterClass,
			ToWorld(Position), FRotator::ZeroRotator);
		Character->Setup(Traveler, Position, PaletteMaterial);
		Characters.Add(Character);
	}
	UpdateControl();
	UpdateBridgeDisplays();
	PositionCamera(false);
	if (bPlayMusic)
	{
		Music->SetVolume(IsMusicEnabled() ? MusicVolume : 0.f);
		Music->StartChapter(IsPrototype() ? 0 : LevelIndex, Rules.GetSize(), Data->RhythmTiles.IsEmpty() ? 0 : Rules.GetRhythmPeriod());
	}
	else { Music->StopMusic(); }
	UE_LOG(LogTemp, Display, TEXT("Puzzle loaded: %d / %d, %d tiles"), GetLevelNumber(), GetLevelCount(), TileActors.Num());
	return true;
}

// 界面切换同时更新暂停状态、鼠标输入模式与 Slate 根控件。
void AGridManager::SetScreen(EGridScreen NewScreen)
{
	if (NewScreen != EGridScreen::Playing && NewScreen != EGridScreen::Paused) { ClearStepHint(); }
	ClearMovePreview();
	Screen = NewScreen == EGridScreen::Playing && bMechanicIntroPending ?
		EGridScreen::MechanicIntro : NewScreen;
	const bool bPaused = Screen == EGridScreen::Paused ||
		Screen == EGridScreen::Confirm ||
		Screen == EGridScreen::MechanicIntro ||
		(Screen == EGridScreen::Controls && bControlsFromPause);
	Player->SetPause(bPaused);
	UpdateOpeningGuide();
	Music->SetMood(Screen != EGridScreen::Playing, Screen == EGridScreen::Complete || Screen == EGridScreen::Ending);
	const bool bMenuInput = Screen != EGridScreen::Playing && Screen != EGridScreen::Transition;
	Player->bShowMouseCursor = bMenuInput;
	if (bMenuInput)
	{
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Player->SetInputMode(Mode);
	}
	else
	{
		Player->SetInputMode(FInputModeGameOnly());
	}

	UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	if (!Viewport)
	{
		return;
	}
	if (ViewportUI)
	{
		Viewport->RemoveViewportWidgetContent(ViewportUI.ToSharedRef());
	}
	// 状态改变时重建界面结构，步数与角色状态等动态文本通过绑定读取最新值。
	ViewportUI = MakeGridPuzzleScreen(*this);
	Viewport->AddViewportWidgetContent(ViewportUI.ToSharedRef(), 10);
	FSlateApplication::Get().SetAllUserFocusToGameViewport();
}

// Slate 控件由视口持有，结束试玩时主动移除并释放管理器的共享引用。
void AGridManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport(); Viewport && ViewportUI)
	{
		Viewport->RemoveViewportWidgetContent(ViewportUI.ToSharedRef());
	}
	ViewportUI.Reset();
	ClearPrintTrails();
	HintSearch.Reset();
	HintGuide.Reset();
	StopCompletionCue();
	StopEffects();
	Music->StopMusic();
	Super::EndPlay(EndPlayReason);
}

// 从已解锁的续玩关卡开始，首次进入新机关关时由 SetScreen 展示引导。
void AGridManager::Start()
{
	if (IsAllClearAchievementUnlocked()) { ShowSelection(); return; }
	if (LoadLevel(GetResumeIndex())) { SetScreen(EGridScreen::Playing); }
}

// 记录说明页的来源，关闭时才能回到原先的暂停页或主菜单。
void AGridManager::ShowControls()
{
	bControlsFromPause = Screen == EGridScreen::Paused;
	SetScreen(EGridScreen::Controls);
}

// 主菜单背后也显示一套棋盘，因此进入菜单时会加载第一关作为背景。
void AGridManager::ShowMenu()
{
	bControlsFromPause = false;
	if (LoadLevel(0)) { PositionCamera(true); }
	SetScreen(EGridScreen::Menu);
}

void AGridManager::ShowSelection()
{
	SetScreen(EGridScreen::Selection);
}

void AGridManager::ShowPractice()
{
	SetScreen(EGridScreen::Practice);
}

void AGridManager::RequestMenu() { RequestBoardChange(EBoardChange::Menu); }
void AGridManager::RequestSelection() { RequestBoardChange(EBoardChange::Selection); }
void AGridManager::RequestReset() { RequestBoardChange(EBoardChange::Reset); }

void AGridManager::RequestBoardChange(EBoardChange Change)
{
	if (bApplyingMove || !ReplaySteps.IsEmpty() ||
		(Screen != EGridScreen::Playing && Screen != EGridScreen::Paused)) { return; }
	PendingBoardChange = Change;
	ScreenBeforeConfirm = Screen;
	if (HasUndoMove()) { SetScreen(EGridScreen::Confirm); }
	else { ConfirmBoardChange(); }
}

void AGridManager::ConfirmBoardChange()
{
	if (Screen == EGridScreen::Confirm) { SetScreen(ScreenBeforeConfirm); }
	switch (PendingBoardChange)
	{
	case EBoardChange::Reset: ResetLevel(); break;
	case EBoardChange::Selection: ShowSelection(); break;
	case EBoardChange::Menu: ShowMenu(); break;
	}
}

void AGridManager::CancelBoardChange()
{
	if (Screen == EGridScreen::Confirm) { SetScreen(ScreenBeforeConfirm); }
}

FText AGridManager::GetBoardChangePrompt() const
{
	switch (PendingBoardChange)
	{
	case EBoardChange::Reset: return LOCTEXT("ConfirmReset", "从本关起点重新开始？");
	case EBoardChange::Selection: return LOCTEXT("ConfirmSelection", "离开本关并返回选关？");
	case EBoardChange::Menu: return LOCTEXT("ConfirmMenu", "离开本关并返回主菜单？");
	}
	return FText::GetEmpty();
}

void AGridManager::ShowAchievements()
{
	SetScreen(EGridScreen::Achievements);
}

// 引导已读状态立即持久化，之后重进该关可以直接开始游玩。
void AGridManager::DismissMechanicIntro()
{
	if (Screen != EGridScreen::MechanicIntro) { return; }
	SeenMechanicIntros.Add(LevelIndex);
	bMechanicIntroPending = false;
	PersistProgress();
	SetScreen(EGridScreen::Playing);
}

int32 AGridManager::GetResumeIndex() const
{
	int32 Unlocked = 0;
	while (Unlocked < MainLevelCount && CompletedLevels.Contains(Unlocked)) { ++Unlocked; }
	return FMath::Clamp(Unlocked, 0, FMath::Max(0, MainLevelCount - 1));
}

// 主线逐关解锁；主菜单里的机关教学不参与进度锁。
bool AGridManager::CanSelectLevel(int32 Index) const
{
	if (!Levels.IsValidIndex(Index)) { return false; }
	if (Index >= CampaignLevelCount) { return true; }
	if (Index < MainLevelCount) { return Index <= GetResumeIndex(); }
	if (!AreChallengesUnlocked()) { return false; }
	for (int32 Previous = MainLevelCount; Previous < Index; ++Previous)
	{
		if (!CompletedLevels.Contains(Previous)) { return false; }
	}
	return true;
}

void AGridManager::SelectLevel(int32 Index)
{
	if ((Screen == EGridScreen::Selection || Screen == EGridScreen::Practice) && CanSelectLevel(Index) && LoadLevel(Index))
	{
		SetScreen(EGridScreen::Playing);
	}
}

// 主线结算前往下一关，终章进入演出，挑战与试玩返回选关。
void AGridManager::Advance()
{
	if (Screen != EGridScreen::Complete) { return; }
	if (CompletionAudio)
	{
		bAdvanceAfterCue = true;
		return;
	}
	if (IsFinalLevel())
	{
		UndoHistory.Reset();
		BeginOutro();
		return;
	}
	if (IsPrototype()) { ShowPractice(); return; }
	if (IsChallenge())
	{
		ShowSelection();
		return;
	}
	if (LoadLevel(LevelIndex+1)) { SetScreen(EGridScreen::Playing); }
}

// 完成任意正式关卡都会记录；主线解锁只跟随连续完成的进度。
void AGridManager::SaveLevelProgress()
{
	bool bChanged = false;
	if (!IsPrototype() && LevelIndex < CampaignLevelCount)
	{
		bChanged |= !CompletedLevels.Contains(LevelIndex);
		CompletedLevels.Add(LevelIndex);
	}
	if (!IsChallenge() && !IsPrototype() && GetLevelNumber() > CompletedMainLevels)
	{
		CompletedMainLevels = GetLevelNumber();
		bChanged = true;
	}
	if (bChanged) { PersistProgress(); }
}

// 将进度、回响收集和已读引导写入固定的 SaveGame 槽位。
void AGridManager::PersistProgress()
{
	UGridPuzzleProgress* Save = CastChecked<UGridPuzzleProgress>(
		UGameplayStatics::CreateSaveGameObject(UGridPuzzleProgress::StaticClass()));
	Save->CompletedMainLevels = CompletedMainLevels;
	Save->CompletedLevels = CompletedLevels.Array();
	Save->EarnedEchoes = EarnedEchoes.Array();
	Save->SeenMechanicIntros = SeenMechanicIntros.Array();
	Save->bMusicMuted = bMusicMuted;
	Save->bEffectsMuted = bEffectsMuted;
	if (!UGameplayStatics::SaveGameToSlot(Save, ProgressSlot, 0))
	{
		UE_LOG(LogTemp, Warning, TEXT("Could not save One Step progress to slot %s."), ProgressSlot);
	}
}

// 回响以关卡索引去重，拾取时更新显示并立即保存。
bool AGridManager::TryCollectEchoAt(FIntPoint Position)
{
	FIntPoint EchoPosition;
	if (!GetEchoLocationForLevel(LevelIndex, EchoPosition) || EchoPosition != Position ||
		GetLevelEchoName(LevelIndex).IsEmpty() || EarnedEchoes.Contains(LevelIndex))
	{
		return false;
	}
	EarnedEchoes.Add(LevelIndex);
	if (AGridTile* EchoTile = TileActors.FindRef(Position))
	{
		EchoTile->SetEchoAvailable(false, PaletteMaterial);
	}
	SetFeedback(FText::Format(LOCTEXT("EchoCollected", "找到回响：{0}"), GetLevelEchoName(LevelIndex)));
	PersistProgress();
	PlayEffect(EGridSound::Select);
	return true;
}

// 回放播放中屏蔽暂停切换，机关预览中按返回则先取消预览。
void AGridManager::TogglePause()
{
	if (!ReplaySteps.IsEmpty()) { return; }
	if (Screen == EGridScreen::Playing && (bActionPreview || !PreviewMarkers.IsEmpty()))
	{
		ClearActionPreview();
		SetFeedback(LOCTEXT("PreviewCancelled", "预览已取消"));
		return;
	}
	switch (Screen)
	{
	case EGridScreen::MechanicIntro: DismissMechanicIntro(); break;
	case EGridScreen::Playing: SetScreen(EGridScreen::Paused); break;
	case EGridScreen::Paused: SetScreen(EGridScreen::Playing); break;
	case EGridScreen::Confirm: CancelBoardChange(); break;
	case EGridScreen::Controls: SetScreen(bControlsFromPause ? EGridScreen::Paused : EGridScreen::Menu); break;
	case EGridScreen::Selection: ShowMenu(); break;
	case EGridScreen::Practice: ShowSelection(); break;
	case EGridScreen::Achievements: ShowMenu(); break;
	default: break;
	}
}

void AGridManager::ToggleMusic()
{
	if (!bPlayMusic) { return; }
	bMusicMuted = !bMusicMuted;
	Music->SetVolume(IsMusicEnabled() ? MusicVolume : 0.f);
	PersistProgress();
	SetFeedback(IsMusicEnabled() ? LOCTEXT("MusicOn", "音乐已打开") : LOCTEXT("MusicOff", "音乐已关闭"));
	if (Screen == EGridScreen::Paused) { SetScreen(EGridScreen::Paused); }
}

void AGridManager::ToggleEffects()
{
	if (!bPlaySound) { return; }
	bEffectsMuted = !bEffectsMuted;
	if (bEffectsMuted)
	{
		StopEffects();
		Music->CancelAccents();
		FinishCompletionCue();
	}
	else { PlayEffect(EGridSound::Select); }
	PersistProgress();
	if (Screen == EGridScreen::Paused) { SetScreen(EGridScreen::Paused); }
}

void AGridManager::Exit()
{
	UKismetSystemLibrary::QuitGame(this, Player, EQuitPreference::Quit, false);
}

// 重置复用切关流程，重新从同一个关卡资源构造初始局面。
void AGridManager::ResetLevel()
{
	if (bApplyingMove || !ReplaySteps.IsEmpty() || (Screen != EGridScreen::Playing && Screen != EGridScreen::Paused)) { return; }
	if (LoadLevel(LevelIndex))
	{
		SetScreen(EGridScreen::Playing);
		SetFeedback(LOCTEXT("ResetDone", "回到起点了。"));
	}
}

// 撤销恢复完整规则快照，再让地块和两个人物追随恢复后的逻辑状态。
// 已收集回响属于持久进度，和关内行动快照分开保存。
void AGridManager::UndoMove()
{
	if (bApplyingMove || !ReplaySteps.IsEmpty() || (Screen != EGridScreen::Playing && Screen != EGridScreen::Paused &&
		Screen != EGridScreen::Transition && Screen != EGridScreen::Complete)) { return; }
	ClearActionPreview();
	if (!HasUndoMove())
	{
		if (Screen == EGridScreen::Playing) { SetFeedback(LOCTEXT("NothingToUndo", "没有可以撤销的操作。")); }
		return;
	}
	TGuardValue<bool> ApplyingMove(bApplyingMove, true);
	ClearStepHint();
	Music->CancelAccents();
	ClearPrintTrails();
	RecentPrintEffect = INDEX_NONE;
	StopCompletionCue();
	StopEffects();
	GetWorldTimerManager().ClearTimer(ScreenTimer);
	// 转桥方向与人物位置的差异决定此次恢复使用转桥、换位还是普通行走动画。
	FUndoStep Step = UndoHistory.Pop(EAllowShrinking::No);
	const bool bCrossedSection = Step.Section != SectionIndex;
	if (bCrossedSection)
	{
		if (!LoadLevel(LevelIndex, Step.Section, true)) { return; }
		Step.ChangedTiles.Reset();
		for (const auto& Pair : Step.PreviousRules.GetTiles()) { Step.ChangedTiles.Add(Pair.Key); }
	}
	ChapterMoveCount = Step.ChapterMoves;
	const bool bTurned = !bCrossedSection && Step.PreviousRules.GetNextDeck() != Rules.GetNextDeck();
	Rules = MoveTemp(Step.PreviousRules);
	ActiveRole = Step.Mover;
	const bool bSwapped = !bCrossedSection && Algo::AllOf(Characters, [this](const AGridPuzzleCharacter* Character)
		{
			return Character->GridPosition != Rules.GetPosition(Character->TravelerRole);
		});
	UnlockInputAt = GetWorld()->GetTimeSeconds() + (bTurned ? DeckTime : bSwapped ? SwapTime : StepTime);
	bool bCollapsed = false;
	bool bBridgeChanged = false;
	ApplyTileUpdates(Step.ChangedTiles, false, bCollapsed, bBridgeChanged);
	if (bTurned) { AnimateTurnedDeck(Step.ChangedTiles, true); }
	for (AGridPuzzleCharacter* Character : Characters)
	{
		const EPlayerRole Traveler = Character->TravelerRole;
		const FIntPoint Position = Rules.GetPosition(Traveler);
		if (Position == Character->GridPosition) { continue; }
		Character->MoveTo(Position, ToWorld(Position), bSwapped ? SwapTime : StepTime, bSwapped);
		OnCharacterMoved.Broadcast(Traveler, Position);
	}
	UpdateBridgeDisplays();
	UpdateControl();
	if (Screen != EGridScreen::Playing) { SetScreen(EGridScreen::Playing); }
	SetFeedback(LOCTEXT("UndoDone", "退回上一步。"));
	PlayEffect(EGridSound::Select);
}

// 统一刷新人物的当前控制标记与抵达状态。
void AGridManager::UpdateControl()
{
	for (int32 Index = 0; Index < Characters.Num(); ++Index)
	{
		Characters[Index]->SetControlState(ActiveRole == Roles[Index], Rules.IsFinished(Roles[Index]));
	}
	UpdatePrintConnections();
	UpdateOpeningGuide();
}

void AGridManager::UpdateOpeningGuide()
{
	TOptional<FIntPoint> Target;
	if (Screen == EGridScreen::Playing && LevelIndex == 0 && CampaignLevelCount == 5 && !SeenMechanicIntros.Contains(0))
	{
		if (Rules.GetMoveCount() == 0 && ActiveRole == EPlayerRole::PlayerA)
		{
			const FIntPoint First = Rules.GetPosition(EPlayerRole::PlayerA) + FIntPoint(1,0);
			if (const FTileData* Tile = Rules.GetTiles().Find(First); Tile && Tile->FootprintOwner == EFootprintOwner::PlayerB)
			{
				Target = First;
			}
		}
		else if (Rules.GetPosition(EPlayerRole::PlayerA) == FIntPoint(4,0) && ActiveRole == EPlayerRole::PlayerB)
		{
			const FIntPoint Next = Rules.GetPosition(EPlayerRole::PlayerB) + FIntPoint(-1,0);
			if (const FTileData* Tile = Rules.GetTiles().Find(Next); Tile && Tile->bFreshPrint && Tile->FootprintOwner == EFootprintOwner::PlayerA)
			{
				Target = Next;
			}
		}
	}
	if (Target == OpeningHighlight) { return; }
	if (OpeningHighlight.IsSet())
	{
		if (AGridTile* Tile = TileActors.FindRef(OpeningHighlight.GetValue())) { Tile->SetOpeningHighlight(false); }
	}
	OpeningHighlight = Target;
	if (Target.IsSet())
	{
		if (AGridTile* Tile = TileActors.FindRef(Target.GetValue())) { Tile->SetOpeningHighlight(true); }
	}
}

FText AGridManager::GetOpeningGuideText() const
{
	if (LevelIndex != 0 || CampaignLevelCount != 5 || SeenMechanicIntros.Contains(0)) { return FText::GetEmpty(); }
	if (Rules.GetMoveCount() == 0 && ActiveRole == EPlayerRole::PlayerA)
	{
		return LOCTEXT("OpeningFirstStep", "先让 A 踩上亮起的橙色脚印。用方向键或 WASD 移动。");
	}
	if (Rules.GetMoveCount() == 1 && Rules.GetPosition(EPlayerRole::PlayerA) == FIntPoint(2,1))
	{
		return LOCTEXT("OpeningRewrite", "看，脚印变蓝了。等 A 让开，B 就能踩着它跟上。");
	}
	if (OpeningHighlight.IsSet()) { return LOCTEXT("OpeningBorrow", "轮到 B 了。踩上 A 留下的蓝色脚印。"); }
	if (Rules.GetPosition(EPlayerRole::PlayerA) == FIntPoint(4,0) && ActiveRole == EPlayerRole::PlayerA && !Rules.IsFinished(EPlayerRole::PlayerB))
	{
		return LOCTEXT("OpeningSwitch", "A 已经让开了。按 Tab 换 B，沿蓝色脚印走。");
	}
	return FText::GetEmpty();
}

// 桥面读数逐项展示条件满足情况，逻辑判定仍由 Rules 负责。
void AGridManager::UpdateBridgeDisplays()
{
	for (const auto& Pair : TileActors)
	{
		AGridTile* Tile = Pair.Value;
		if (Tile->TileData.TileType != ETileType::Bridge) { continue; }
		TArray<FString> Labels;
		TArray<bool> Matches;
		for (const FGridBridgeCondition& Condition : Rules.GetBridgeConditions(Tile->TileData.Marker))
		{
			const bool bMet = Rules.IsBridgeConditionMet(Condition);
			const FTileData& Source = Rules.GetTiles().FindChecked(Condition.Source);
			Labels.Add(FString::Printf(TEXT("%s:%s%s"), *Source.Marker, RoleLetter(Condition.RequiredRole),
				bMet ? TEXT("+") : TEXT("-")));
			Matches.Add(bMet);
		}
		Tile->UpdateBridgeStatus(Labels, Matches);
	}
}

// 将未满足的桥条件转换成提示，明确缺少谁的站位或脚印。
FText AGridManager::GetClosedBridgeFeedback(FIntPoint Position) const
{
	const FTileData& Bridge = Rules.GetTiles().FindChecked(Position);
	TArray<FText> Missing;
	for (const FGridBridgeCondition& Condition : Rules.GetBridgeConditions(Bridge.Marker))
	{
		if (Rules.IsBridgeConditionMet(Condition)) { continue; }
		const FTileData& Source = Rules.GetTiles().FindChecked(Condition.Source);
		const FText Label = FText::FromString(Source.Marker);
		const FText RequiredTraveler = FText::FromString(RoleLetter(Condition.RequiredRole));
		Missing.Add(FText::Format(Source.TileType == ETileType::Plate ?
			LOCTEXT("PlateNeeded", "让 {1} 站在 {0} 上") : LOCTEXT("ImprintNeeded", "{0} 上需要 {1} 的脚印"),
			Label, RequiredTraveler));
	}
	return FText::Format(LOCTEXT("BridgeClosed", "{0} 还没开：{1}"),
		FText::FromString(Bridge.Marker), FText::Join(LOCTEXT("BridgeConditionSeparator", "　·　"), Missing));
}

// 切换控制角色不结算步数，已录下的移动方向也继续保留。
void AGridManager::SwitchCharacter()
{
	if (Screen != EGridScreen::Playing || bApplyingMove || !ReplaySteps.IsEmpty()) { return; }
	if (bActionPreview)
	{
		ClearActionPreview();
		Feedback = FText::GetEmpty();
	}
	ClearMovePreview();
	ActiveRole = ActiveRole == EPlayerRole::PlayerA ? EPlayerRole::PlayerB : EPlayerRole::PlayerA;
	UpdateControl();
	PlayEffect(EGridSound::Select);
}

// 临时反馈使用同一个定时器，新消息会替换旧消息的清除时间。
void AGridManager::SetFeedback(FText Message)
{
	Feedback = MoveTemp(Message);
	GetWorldTimerManager().SetTimer(FeedbackTimer, FTimerDelegate::CreateWeakLambda(this,
		[this]() { Feedback = FText::GetEmpty(); }), 1.8f, false);
}

FText AGridManager::GetLevelTitle() const
{
	return GetLevelTitle(LevelIndex);
}

FText AGridManager::GetLevelTitle(int32 Index) const
{
	return Levels.IsValidIndex(Index) && Levels[Index] ? Levels[Index]->Title : FText::GetEmpty();
}

FText AGridManager::GetLevelHint() const
{
	if (StepHint.IsSet()) { return FormatStepHint(); }
	if (!StepHintStatus.IsEmpty()) { return StepHintStatus; }
	if (!CurrentSection()) { return LOCTEXT("MissingLevel", "这关没有加载成功。"); }
	const FText Opening = GetOpeningGuideText();
	if (!Opening.IsEmpty()) { return Opening; }
	const FText PrintCondition = GetPrintConditionHint();
	if (!PrintCondition.IsEmpty()) { return PrintCondition; }
	if (IsPrototype() || CampaignLevelCount != 5) { return CurrentSection()->Hint; }
	const auto FootprintAt = [this](FIntPoint Position)
	{
		const FTileData* Tile = Rules.GetTiles().Find(Position);
		return Tile ? Tile->FootprintOwner : EFootprintOwner::None;
	};
	switch (LevelIndex)
	{
	case 0:
	{
		const FTileData* Goal = Rules.GetTiles().Find({6, 1});
		if (Goal && Goal->bIsLocked)
		{
			return LOCTEXT("SidePocketHint", "窄路上怎么错身？看看上面那一格。");
		}
		return LOCTEXT("ReturnFromPocketHint", "门开了。先给还在上面的人留个落脚处。");
	}
	case 1:
		if (FootprintAt({2, 0}) == EFootprintOwner::None)
		{
			return LOCTEXT("FragileHandoffHint", "站在薄冰上时不会倒数。离开前，想想谁还得从这儿过。");
		}
		if (FootprintAt({2, 5}) == EFootprintOwner::None)
		{
			return LOCTEXT("IceReturnHint", "冰道入口已经有橙色脚印了。A 滑过去，也能给 B 留路。");
		}
		return LOCTEXT("BothExitsHint", "出口前都有脚印了。两个人可以各自往出口走。");
	case 2:
		if (FootprintAt({8, 7}) == EFootprintOwner::None) { return CurrentSection()->Hint; }
		if (FootprintAt({9, 7}) == EFootprintOwner::None)
		{
			return LOCTEXT("RevisitRelayHint", "南边收到脚印了。让开北边那块石板，等回信。");
		}
		return LOCTEXT("RelayFinishedHint", "回信到了。看看各自的出口前，现在能踩哪一格。");
	case 3:
		if (Rules.GetRecording().IsEmpty())
		{
			return LOCTEXT("PrepareRecordingHint", "想想 A 录下这三步后，B 在对面能不能踩上脚印。");
		}
		if (FootprintAt({9, 5}) == EFootprintOwner::PlayerB &&
			FootprintAt({10, 5}) == EFootprintOwner::None)
		{
			return LOCTEXT("ReturnBridgeHint", "B 离开桥了。把他留下的橙色脚印转回来。");
		}
		if (FootprintAt({10, 5}) != EFootprintOwner::None)
		{
			return LOCTEXT("FinalBridgeReplyHint", "B 还得把橙色脚印送回去，A 才有路走。");
		}
		return LOCTEXT("CarryBridgeHint", "先在桥上留脚印。等两人都下桥，再把脚印转到对面。");
	case 4:
	{
		if (FootprintAt({5, 2}) == EFootprintOwner::None ||
			FootprintAt({5, 6}) == EFootprintOwner::None) { return CurrentSection()->Hint; }
		const FTileData* Bridge = Rules.GetTiles().Find({6, 2});
		if (Bridge && Bridge->bFoldedAway)
		{
			return LOCTEXT("PatrolMeetingHint", "走之前看一眼 NEXT，也看看同伴正站在哪。");
		}
		const FTileData* Receiver = Rules.GetTiles().Find({8, 7});
		if (Rules.IsFinished(EPlayerRole::PlayerA))
		{
			return LOCTEXT("FinalTravelerHint", "A 到了。还差 B，出口在上面。");
		}
		if (Receiver && Receiver->bPrintPending)
		{
			if (Receiver->PendingPrintSteps == 0)
			{
				return LOCTEXT("SavedPrintWaitForRoom", "出口前那格有人。等那格空出来，橙色脚印就能送到。");
			}
			return FText::Format(LOCTEXT("SavedPrintInTransitHint", "橙色脚印还差 {0} 步送到。A 或 B 走都算，Tab 换人不算。"),
				FText::AsNumber(Receiver->PendingPrintSteps));
		}
		if (Receiver && Receiver->FootprintOwner == EFootprintOwner::PlayerB)
		{
			return LOCTEXT("SavedPrintArrivedHint", "橙色脚印到出口前了。让 A 踩上去。");
		}
		if (Rules.GetPosition(EPlayerRole::PlayerA) == FIntPoint(8, 7))
		{
			return LOCTEXT("FinalExitAheadHint", "A 再往前就是出口。");
		}
		return LOCTEXT("SavedPrintHint", "南边的 OUT 连着右侧的 SAVE。A 踩上 OUT，就会把存着的橙色脚印寄往出口前。");
	}
	default:
		return CurrentSection()->Hint;
	}
}

int32 AGridManager::GetEarnedEchoCount() const
{
	int32 Count = 0;
	for (int32 Index = 0; Index < MainLevelCount; ++Index)
	{
		Count += EarnedEchoes.Contains(Index);
	}
	return Count;
}

int32 AGridManager::GetEchoTargetCount() const
{
	return MainLevelCount;
}

int32 AGridManager::GetUnlockedAchievementCount() const
{
	return GetEarnedEchoCount() + (IsAllClearAchievementUnlocked() ? 1 : 0);
}

bool AGridManager::IsAchievementUnlocked(int32 AchievementIndex) const
{
	if (AchievementIndex >= 0 && AchievementIndex < MainLevelCount) { return EarnedEchoes.Contains(AchievementIndex); }
	return AchievementIndex == MainLevelCount && IsAllClearAchievementUnlocked();
}

bool AGridManager::IsAllClearAchievementUnlocked() const
{
	if (MainLevelCount <= 0) { return false; }
	for (int32 Index = 0; Index < MainLevelCount; ++Index)
	{
		if (!CompletedLevels.Contains(Index)) { return false; }
	}
	return true;
}

FText AGridManager::GetAchievementName(int32 AchievementIndex) const
{
	switch (AchievementIndex)
	{
	case 0: return LOCTEXT("Achievement01", "初见 · 回响");
	case 1: return LOCTEXT("Achievement02", "薄冰 · 回响");
	case 2: return LOCTEXT("Achievement03", "来信 · 回响");
	case 3: return LOCTEXT("Achievement04", "回声 · 回响");
	case 4: return LOCTEXT("Achievement05", "同行 · 回响");
	case 5: return LOCTEXT("AchievementAll", "并肩至终");
	default: return FText::GetEmpty();
	}
}

FText AGridManager::GetAchievementDescription(int32 AchievementIndex) const
{
	if (AchievementIndex >= 0 && AchievementIndex < MainLevelCount)
	{
		return FText::Format(LOCTEXT("AchievementLevelDescription",
			"在「{0}」中找到隐藏回响。"), GetLevelTitle(AchievementIndex));
	}
	return AchievementIndex == MainLevelCount ?
		LOCTEXT("AchievementAllDescription", "完成全部主线关卡。") : FText::GetEmpty();
}

// 回响位置按从 0 开始的关卡索引配置，自动化测试会搜索其可达性。
bool AGridManager::GetEchoLocationForLevel(int32 LevelIndex, FIntPoint& OutPosition)
{
	switch (LevelIndex)
	{
	case 0: OutPosition = {4, 1}; return true;
	case 1: OutPosition = {2, 2}; return true;
	case 2: OutPosition = {8, 7}; return true;
	case 3: OutPosition = {8, 3}; return true;
	case 4: OutPosition = {4, 6}; return true;
	default: return false;
	}
}

// 每张主线棋盘都会引入新的组合机关，首次进入时先暂停讲解。
bool AGridManager::LevelHasMechanicIntro(int32 LevelIndex)
{
	return LevelIndex >= 0 && LevelIndex < 5;
}

FText AGridManager::GetLevelEchoName(int32 Index) const
{
	switch (Index)
	{
	case 0: return LOCTEXT("Echo01", "初见");
	case 1: return LOCTEXT("Echo02", "薄冰");
	case 2: return LOCTEXT("Echo03", "来信");
	case 3: return LOCTEXT("Echo04", "回声");
	case 4: return LOCTEXT("Echo05", "同行余音");
	default: return FText::GetEmpty();
	}
}

FText AGridManager::GetMechanicIntroTitle() const
{
	switch (LevelIndex)
	{
	case 0: return LOCTEXT("IntroTitle01", "在窄路上让一让");
	case 1: return LOCTEXT("IntroTitle02", "冰上也能给对方留路");
	case 2: return LOCTEXT("IntroTitle03", "给对面送一枚脚印");
	case 3: return LOCTEXT("IntroTitle04", "把三步留给对面的人");
	case 4: return LOCTEXT("IntroTitle05", "留意巡逻，等脚印送到");
	default: return FText::GetEmpty();
	}
}

FText AGridManager::GetMechanicIntroBody() const
{
	switch (LevelIndex)
	{
	case 0:
		return LOCTEXT("IntroBody01",
			"A 踩橙色脚印，B 踩蓝色脚印。走过的那格会留下自己的脚印。窄路上碰面时，可以到旁边那格让一下。\n"
			"门上标着谁，就让谁去踩同色开关。按 Tab 换人，这不算一步。");
	case 1:
		return LOCTEXT("IntroBody02",
			"踩上 ICE 会往前滑，离开冰面或走不通时就停。踩上薄冰会补满短条，人离开后才开始倒数。标着 1 的石板离开就会塌。\n"
			"OUT 会把脚印送到箭头指的 IN。IN 还是空的，就先等脚印送到。");
	case 2:
		return LOCTEXT("IntroBody03",
			"踩着箱子底下对方的脚印，就能往前推箱子。先看箱子后面有没有地方放。\n"
			"BEAT 显示 WAIT 时过不去，可以先让同伴走一步。有些 OUT 得等对面留下对的颜色，才会把脚印寄出去。");
	case 3:
		return LOCTEXT("IntroBody04",
			"A 在 REC 按 E，然后走三步。撞到障碍的那步不算。B 到 PLAY，按 E 看这三步，再按一次照着走。每一步都得踩到 A 的脚印。\n"
			"转桥会把脚印一起带过去。两人都下桥后，到 TURN 按 E 看桥往哪转，再按一次确认。");
	case 4:
		return LOCTEXT("IntroBody05",
			"你每走一步，巡逻也走一格。NEXT 是它要去的地方。走之前也看看同伴站在哪。\n"
			"南边的 OUT 会寄出 SAVE 存着的橙色脚印。虚线指向脚印来自哪格，实线箭头指向送到哪格。看到 WAIT 1，让 A 或 B 再走一步就能送到。Tab 换人不算。\n"
			"两人各站上一块 SWAP，再按两次 E 换位。用 TURN 转桥前，先让两人下桥。");
	default:
		return FText::GetEmpty();
	}
}

FText AGridManager::GetMechanicIntroExample() const
{
	switch (LevelIndex)
	{
	case 0: return LOCTEXT("IntroExample01", "A 先走过去。等他让开，换 B 踩蓝色脚印。");
	case 1: return LOCTEXT("IntroExample02", "上冰前先看看会滑到哪儿。那段脚印一会儿可能还要留给同伴。");
	case 2: return LOCTEXT("IntroExample03", "收到脚印的人，也能把路送回去。看看两边的 OUT 各在等什么颜色。");
	case 3: return LOCTEXT("IntroExample04", "录之前先想好，B 从 PLAY 出发照着走，会踩到哪三格？");
	case 4: return LOCTEXT("IntroExample05", "A 在等脚印时，可以让 B 先走。B 走的那一步也能把脚印送到。");
	default: return FText::GetEmpty();
	}
}

// 将录制方向转换成界面的箭头序列，显示已经录入的步数。
FText AGridManager::GetRecordingStatus() const
{
	if (!CurrentSection()) { return FText::GetEmpty(); }
	if (!CurrentSection()->RhythmTiles.IsEmpty() &&
		(CurrentSection()->Prototype != EGridPrototype::RecordRoute || (!Rules.IsRecording() && Rules.GetRecording().IsEmpty())))
	{
		return FText::Format(LOCTEXT("RhythmStatus", "下一步：第 {0}/{1} 拍。BEAT 显示 WAIT 时过不去。"),
			FText::AsNumber((Rules.GetMoveCount() + 1) % Rules.GetRhythmPeriod() + 1), FText::AsNumber(Rules.GetRhythmPeriod()));
	}
	if (CurrentSection()->Prototype != EGridPrototype::RecordRoute) { return FText::GetEmpty(); }
	const TArray<FIntPoint>& Directions = Rules.GetRecording();
	if (!Rules.IsRecording() && Directions.IsEmpty()) { return FText::GetEmpty(); }
	FString Sequence;
	for (int32 Index = 0; Index < Directions.Num(); ++Index)
	{
		if (!Sequence.IsEmpty()) { Sequence += TEXT("   "); }
		Sequence += FString::Printf(TEXT("%d %s"), Index + 1, *DirectionGlyph(Directions[Index]));
	}
	return FText::Format(LOCTEXT("RecordingStatus", "{0}  {1}/3    {2}"),
		Rules.IsRecording() ? LOCTEXT("Recording", "正在录制") : LOCTEXT("Recorded", "已录下"),
		FText::AsNumber(Directions.Num()), FText::FromString(Sequence));
}

// F1 在副本中执行四个方向的完整行动，冰面上的每一步也使用真实的脚印和时钟规则。
void AGridManager::ToggleMovePreview()
{
	if (Screen != EGridScreen::Playing || bApplyingMove || !ReplaySteps.IsEmpty() ||
		GetWorld()->GetTimeSeconds() < UnlockInputAt) { return; }
	ClearStepHint();
	UpdatePrintConnections();
	if (!MovePreviewMarkers.IsEmpty())
	{
		ClearMovePreview();
		return;
	}
	ClearActionPreview();
	const FIntPoint Directions[] = {{0, -1}, {-1, 0}, {0, 1}, {1, 0}};
	const TCHAR* Keys[] = {TEXT("W"), TEXT("A"), TEXT("S"), TEXT("D")};
	TMap<FIntPoint, FString> Labels;
	TSet<FIntPoint> Legal;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Directions); ++Index)
	{
		FGridPuzzleRules Trial = Rules;
		const FGridMove Step = Trial.TryMove(ActiveRole, Directions[Index]);
		if (Step.Result != EGridMoveResult::Moved)
		{
			if (!Legal.Contains(Step.To))
			{
				Labels.FindOrAdd(Step.To) = FString(Keys[Index]) + TEXT(" X");
			}
			continue;
		}
		for (int32 Landing = 0; Landing < Step.Path.Num(); ++Landing)
		{
			const FIntPoint Position = Step.Path[Landing];
			FString& Label = Labels.FindOrAdd(Position);
			if (!Legal.Contains(Position)) { Label.Reset(); }
			if (!Label.IsEmpty()) { Label += TEXT(" / "); }
			Label += FString::Printf(TEXT("%s%d"), Keys[Index], Landing + 1);
			Legal.Add(Position);
		}
	}
	for (const auto& Pair : Labels)
	{
		if (AGridTile* Tile = TileActors.FindRef(Pair.Key))
		{
			Tile->ShowActionMarker(FText::FromString(Pair.Value),
				Legal.Contains(Pair.Key) ? FColor(112, 231, 181) : FColor(245, 116, 104));
			MovePreviewMarkers.Add(Pair.Key);
		}
	}
	SetFeedback(Legal.IsEmpty() ?
		LOCTEXT("NoLegalStep", "这边暂时走不动。看看脚下的机关，或按 Tab 换人、Z 撤销。") :
		LOCTEXT("MovePreviewHelp", "数字是落脚顺序，红色 X 表示走不了。"));
}

void AGridManager::ClearMovePreview()
{
	for (FIntPoint Position : MovePreviewMarkers)
	{
		if (AGridTile* Tile = TileActors.FindRef(Position))
		{
			Tile->ShowActionMarker(FText::GetEmpty(), FColor::White);
		}
	}
	MovePreviewMarkers.Reset();
}

// 清除只用于预览的编号和颜色标记，实际地块状态从 Rules 恢复。
void AGridManager::ClearActionPreview()
{
	ClearMovePreview();
	for (FIntPoint Position : PreviewMarkers)
	{
		TileActors.FindChecked(Position)->ShowActionMarker(FText::GetEmpty(), FColor::White);
	}
	PreviewMarkers.Reset();
	bActionPreview = false;
}

void AGridManager::MarkActionTile(FIntPoint Position, FText Label, FColor Color)
{
	if (AGridTile* Tile = TileActors.FindRef(Position))
	{
		Tile->ShowActionMarker(Label, Color);
		PreviewMarkers.AddUnique(Position);
	}
}

// 规则已完成换槽，这里让新槽位的模型从旧位置沿轴心转入。
void AGridManager::AnimateTurnedDeck(const TArray<FIntPoint>& ChangedTiles, bool bReverse)
{
	const FVector Pivot = ToWorld(CurrentSection()->TurnPivot);
	for (int32 Index = 0; Index + 1 < ChangedTiles.Num(); Index += 2)
	{
		const FIntPoint From = ChangedTiles[Index + (bReverse ? 1 : 0)];
		const FIntPoint To = ChangedTiles[Index + (bReverse ? 0 : 1)];
		TileActors.FindChecked(To)->AnimateDeckFrom(Pivot, ToWorld(From), DeckTime);
	}
}

// E 键机关入口，先在 Trial 副本上试算，首次展示预览，再次确认才应用结果。
// 开始录制立即生效，转桥、换位和回放使用预览确认流程。
void AGridManager::UseAction()
{
	if (Screen != EGridScreen::Playing || !HasLevelAction() || bApplyingMove || !ReplaySteps.IsEmpty() ||
		GetWorld()->GetTimeSeconds() < UnlockInputAt) { return; }
	TGuardValue<bool> ApplyingMove(bApplyingMove, true);
	ClearStepHint();
	UpdatePrintConnections();
	ClearMovePreview();
	FGridPuzzleRules Trial = Rules;
	EGridPrototype Interaction = CurrentSection()->Prototype;
	// 组合机关优先执行已满足的换位，尚不能换位时允许在 TURN 控制格转桥。
	if (Interaction == EGridPrototype::RecordRoute && Rules.IsAtTurnControl(ActiveRole))
	{
		Interaction = EGridPrototype::TurnBridge;
	}
	if (Interaction == EGridPrototype::TurnBridgeAndSwap)
	{
		Interaction = Rules.IsAtTurnControl(ActiveRole) && !Rules.CanSwap() ?
			EGridPrototype::TurnBridge : EGridPrototype::SwapPlaces;
	}
	switch (Interaction)
	{
	default:
		return;
	case EGridPrototype::TurnBridge:
	{
		TArray<FIntPoint> Changed;
		if (!Trial.TryTurnBridge(ActiveRole, Changed))
		{
			SetFeedback(LOCTEXT("CannotTurn", "站到 TURN 处，两个人都离开桥面后才能转。"));
			return;
		}
		if (!bActionPreview)
		{
			for (FIntPoint Position : Rules.GetNextDeck())
			{
				const EFootprintOwner PrintOwner = Trial.GetTiles().FindChecked(Position).FootprintOwner;
				MarkActionTile(Position, FText::FromString(PrintOwner == EFootprintOwner::PlayerA ? TEXT("A") :
					PrintOwner == EFootprintOwner::PlayerB ? TEXT("B") : TEXT("·")), PrintOwner == EFootprintOwner::PlayerA ?
					FColor(124,220,255) : PrintOwner == EFootprintOwner::PlayerB ? FColor(255,186,123) : FColor::White);
			}
			bActionPreview = true;
			SetFeedback(LOCTEXT("TurnPreview", "再按 E 转桥，Esc 取消。"));
			return;
		}
		ClearActionPreview();
		SaveUndo(MoveTemp(Rules), ActiveRole, Changed);
		Rules = MoveTemp(Trial);
		bool bCollapsed = false;
		bool bBridgeChanged = false;
		ApplyTileUpdates(Changed, false, bCollapsed, bBridgeChanged);
		AnimateTurnedDeck(Changed, false);
		UpdatePrintConnections();
		UnlockInputAt = GetWorld()->GetTimeSeconds() + DeckTime;
		SetFeedback(LOCTEXT("TurnDone", "桥转好了。"));
		PlayEffect(EGridSound::Bridge);
		return;
	}
	case EGridPrototype::RecordRoute:
	{
		if (Trial.StartRecording(ActiveRole))
		{
			SaveUndo(MoveTemp(Rules), ActiveRole, {});
			Rules = MoveTemp(Trial);
			SetFeedback(LOCTEXT("RecordStarted", "开始录制。记下 A 接下来走的三步。"));
			return;
		}
		if (!Rules.CanReplayFrom(ActiveRole))
		{
			SetFeedback(LOCTEXT("NoRecording", "先让 A 在 REC 录满三步，再让 B 到 PLAY 回放。"));
			return;
		}
		TArray<FGridMove> Steps;
		TArray<FGridPuzzleRules> States;
		const bool bValid = Rules.PlanReplay(ActiveRole, Steps, States);
		if (!bValid || !bActionPreview)
		{
			ClearActionPreview();
			for (int32 Index = 0; Index < Steps.Num(); ++Index)
			{
				const FGridMove& Step = Steps[Index];
				MarkActionTile(Step.To, FText::AsNumber(Index + 1), Step.Result == EGridMoveResult::Moved ?
					FColor(124,220,255) : FColor(255,115,115));
			}
			bActionPreview = true;
			if (bValid)
			{
				SetFeedback(LOCTEXT("ReplayPreview", "按编号走三步。再按 E 回放，Esc 取消。"));
			}
			else
			{
				const EGridMoveResult Failure = Steps.Last().Result;
				ClearActionPreview();
				MarkActionTile(Steps.Last().To, FText::FromString(FString::Printf(TEXT("%d X"), Steps.Num())), FColor(245,116,104));
				const FText Reason = Failure == EGridMoveResult::Occupied ? LOCTEXT("ReplayOccupied", "同伴还站在那里") :
					(Failure == EGridMoveResult::Void || Failure == EGridMoveResult::ReplayOnly) ?
					LOCTEXT("ReplayOutOfPath", "这一步出了回放范围") : LOCTEXT("ReplayNoPrint", "这里没有可踩的蓝色脚印");
				SetFeedback(FText::Format(LOCTEXT("ReplayBlocked", "第 {0} 步走不了：{1}。"), FText::AsNumber(Steps.Num()), Reason));
			}
			return;
		}
		ClearActionPreview();
		TArray<FIntPoint> Changed;
		for (const FGridMove& Step : Steps)
		{
			for (FIntPoint Position : Step.ChangedTiles) { Changed.AddUnique(Position); }
		}
		// 整段三步回放共用一个撤销快照，播放期间锁住其他移动输入。
		SaveUndo(Rules, ActiveRole, MoveTemp(Changed));
		ReplayIndex = 0;
		ReplaySteps = MoveTemp(Steps);
		ReplayStates = MoveTemp(States);
		PlayReplayStep();
		return;
	}
	case EGridPrototype::SwapPlaces:
	{
		FGridMove Move;
		if (!Trial.TrySwap(ActiveRole, Move))
		{
			SetFeedback(LOCTEXT("CannotSwap", "先分别走上两块 SWAP 石板，再按 E。"));
			return;
		}
		if (!bActionPreview)
		{
			MarkActionTile(Trial.GetPosition(EPlayerRole::PlayerA), FText::FromString(TEXT("A")), FColor(124,220,255));
			MarkActionTile(Trial.GetPosition(EPlayerRole::PlayerB), FText::FromString(TEXT("B")), FColor(255,186,123));
			bActionPreview = true;
			SetFeedback(LOCTEXT("SwapPreview", "再按 E 换位置，Esc 取消。"));
			return;
		}
		ClearActionPreview();
		SaveUndo(MoveTemp(Rules), ActiveRole, Move.ChangedTiles);
		Rules = MoveTemp(Trial);
		bool bCollapsed = false;
		bool bBridgeChanged = false;
		ApplyTileUpdates(Move.ChangedTiles, true, bCollapsed, bBridgeChanged);
		RecentPrintEffect = INDEX_NONE;
		PresentPrintTransfers(Move);
		for (AGridPuzzleCharacter* Character : Characters)
		{
			const EPlayerRole Traveler = Character->TravelerRole;
			const FIntPoint Position = Rules.GetPosition(Traveler);
			Character->MoveTo(Position, ToWorld(Position), SwapTime, true);
			OnCharacterMoved.Broadcast(Traveler, Position);
		}
		UpdateBridgeDisplays();
		UpdateControl();
		UnlockInputAt = GetWorld()->GetTimeSeconds() + SwapTime;
		if (IsSoundEnabled()) { Music->NotifyMove(ActiveRole, Move, Rules.GetMoveCount()); }
		SetFeedback(LOCTEXT("SwapDone", "换好位置了。"));
		TryCollectEchoAt(Rules.GetPosition(EPlayerRole::PlayerA));
		TryCollectEchoAt(Rules.GetPosition(EPlayerRole::PlayerB));
		PlayEffect(EGridSound::Transfer);
		return;
	}
	}
}

// 逐个应用预演得到的快照，用定时器把规则结果呈现成可观察的三步动作。
void AGridManager::PlayReplayStep()
{
	TGuardValue<bool> ApplyingMove(bApplyingMove, true);
	const float Duration = StepTime * FMath::Max(1, ReplaySteps[ReplayIndex].Path.Num());
	Rules = MoveTemp(ReplayStates[ReplayIndex]);
	PresentMove(EPlayerRole::PlayerB, ReplaySteps[ReplayIndex]);
	if (++ReplayIndex == ReplaySteps.Num())
	{
		ReplaySteps.Reset();
		ReplayStates.Reset();
		CheckCompletion();
		SetFeedback(LOCTEXT("ReplayDone", "三步走完了。"));
		return;
	}
	GetWorldTimerManager().SetTimer(ReplayTimer, this, &AGridManager::PlayReplayStep, Duration, false);
}

// 正常输入入口，先调用规则判定，成功后呈现并保存撤销快照。
void AGridManager::Move(FIntPoint Direction)
{
	if (Screen != EGridScreen::Playing || bApplyingMove || !ReplaySteps.IsEmpty() ||
		GetWorld()->GetTimeSeconds() < UnlockInputAt) { return; }
	TGuardValue<bool> ApplyingMove(bApplyingMove, true);
	ClearActionPreview();

	const EPlayerRole Mover = ActiveRole;
	AGridPuzzleCharacter* Character = Characters[FGridPuzzleRules::RoleIndex(Mover)];
	// 复制完整行动前状态，包含脚印、倒计时、寄印队列和录制方向。
	FGridPuzzleRules PreviousRules = Rules;
	FGridMove MoveResult = Rules.TryMove(Mover, Direction);
	if (MoveResult.Result != EGridMoveResult::Moved)
	{
		FText Reason;
		switch (MoveResult.Result)
		{
		case EGridMoveResult::CrateBlocked: Reason = LOCTEXT("CrateBlocked", "箱子后面没有空位。"); break;
		case EGridMoveResult::RhythmClosed: Reason = LOCTEXT("RhythmClosed", "这一拍过不去，先换个人走。"); break;
		case EGridMoveResult::PatrolBlocked:
		{
			const EPlayerRole Other = Mover == EPlayerRole::PlayerA ? EPlayerRole::PlayerB : EPlayerRole::PlayerA;
			const FTileData& Waiting = Rules.GetTiles().FindChecked(Rules.GetPosition(Other));
			Reason = Waiting.bPatrolNext && !Rules.IsFinished(Other) ? FText::Format(
				LOCTEXT("PatrolNeedsEvacuation", "巡逻下一步会撞到 {0}。先让 {0} 离开 NEXT 那格。"),
				FText::FromString(RoleLetter(Other))) : LOCTEXT("PatrolBlocked", "巡逻下一步就走到这格。先走别处。");
			break;
		}
		case EGridMoveResult::OwnFootprint: Reason = LOCTEXT("OwnPrint", "这格是自己的脚印，留给对方踩。"); break;
		case EGridMoveResult::EmptyPrint: Reason = LOCTEXT("EmptyPrint", "这格还没有脚印。等对方留下脚印再走。"); break;
		case EGridMoveResult::Occupied: Reason = LOCTEXT("Occupied", "同伴站在这格，先让他挪开。"); break;
		case EGridMoveResult::Finished: Reason = LOCTEXT("Finished", "已经到出口了，按 Tab 换人。"); break;
		case EGridMoveResult::Collapsed: Reason = LOCTEXT("Collapsed", "这块板已经塌了，按 Z 撤销。"); break;
		case EGridMoveResult::LockedGoal: Reason = LOCTEXT("Locked", "门还没开，先找开关。"); break;
		case EGridMoveResult::ClosedBridge: Reason = GetClosedBridgeFeedback(MoveResult.To); break;
		case EGridMoveResult::OtherGoal: Reason = LOCTEXT("OtherGoal", "这是另一个人的出口。"); break;
		case EGridMoveResult::ReplayOnly: Reason = LOCTEXT("ReplayOnly", "这段路要站到 PLAY 上回放。"); break;
		default: Reason = LOCTEXT("Blocked", "这边走不了。"); break;
		}
		SetFeedback(Reason);
		if (!Rules.IsFinished(Mover))
		{
			Character->RejectMove(ToWorld(MoveResult.To) - ToWorld(MoveResult.From));
		}
		UnlockInputAt = GetWorld()->GetTimeSeconds() + RejectTime;
		PlayEffect(EGridSound::Reject);
		return;
	}
	ClearStepHint();
	PresentMove(Mover, MoveResult);
	SaveUndo(MoveTemp(PreviousRules), Mover, MoveTemp(MoveResult.ChangedTiles));
	CheckCompletion();
}

// 将已经结算的规则结果转换成走路动画、地块刷新、事件通知与音效。
void AGridManager::PresentMove(EPlayerRole Mover, const FGridMove& MoveResult)
{
	RecentPrintEffect = INDEX_NONE;
	if (IsSoundEnabled()) { Music->NotifyMove(Mover, MoveResult, Rules.GetMoveCount()); }
	const float Duration = StepTime * FMath::Max(1, MoveResult.Path.Num());
	UnlockInputAt = GetWorld()->GetTimeSeconds() + Duration;
	Feedback = FText::GetEmpty();
	Characters[FGridPuzzleRules::RoleIndex(Mover)]->MoveTo(MoveResult.To, ToWorld(MoveResult.To), Duration);
	bool bCollapsed = false;
	bool bBridgeChanged = false;
	const FTileData Destination = Rules.GetTiles().FindChecked(MoveResult.To);
	ApplyTileUpdates(MoveResult.ChangedTiles, true, bCollapsed, bBridgeChanged);
	PresentPrintTransfers(MoveResult);
	UpdateBridgeDisplays();
	const bool bFirstBorrow = LevelIndex == 0 && CampaignLevelCount == 5 && Mover == EPlayerRole::PlayerB &&
		MoveResult.FreshBorrowedPrintCount > 0 && !SeenMechanicIntros.Contains(0);
	if (bFirstBorrow) { SeenMechanicIntros.Add(0); PersistProgress(); }
	UpdateControl();
	OnCharacterMoved.Broadcast(Mover, MoveResult.To);
	if (MoveResult.bSwitchActivated)
	{
		OnSwitchActivated.Broadcast(Destination);
		for (FIntPoint Position : MoveResult.ChangedTiles)
		{
			AGridTile* Tile = TileActors.FindRef(Position);
			if (Tile && Tile->TileData.TileType == ETileType::Goal && !Tile->TileData.bIsLocked) { Tile->PulseTile(); }
		}
		SetFeedback(LOCTEXT("Unlocked", "对应的出口打开了。"));
	}
	if (MoveResult.Path.Num() > 1)
	{
		for (int32 Index = 0; Index < MoveResult.Path.Num(); ++Index)
		{
			if (AGridTile* Tile = TileActors.FindRef(MoveResult.Path[Index]))
			{
				FTimerHandle PulseTimer;
				GetWorldTimerManager().SetTimer(PulseTimer, Tile, &AGridTile::PulseTile, StepTime * (Index + 1), false);
			}
		}
		SetFeedback(FText::Format(LOCTEXT("IceSlide", "沿冰面滑过 {0} 格，每格都踩着同伴的脚印。"), FText::AsNumber(MoveResult.Path.Num())));
	}
	if (bFirstBorrow) { SetFeedback(LOCTEXT("OpeningBorrowed", "B 跟上了，也给 A 留了一枚橙色脚印。")); }
	if (MoveResult.bReachedGoal && !Rules.IsComplete())
	{
		const EPlayerRole Other = Mover == EPlayerRole::PlayerA ? EPlayerRole::PlayerB : EPlayerRole::PlayerA;
		SetFeedback(FText::Format(LOCTEXT("OneArrived", "{0} 到了。按 Tab 换 {1} 继续。"),
			FText::FromString(RoleLetter(Mover)), FText::FromString(RoleLetter(Other))));
	}
	if (Destination.TileType == ETileType::Bridge && !Destination.bIsActivated)
	{
		SetFeedback(LOCTEXT("BridgeExit", "桥关了，站在上面的人还能走出去。"));
	}
	const bool bCollectedEcho = TryCollectEchoAt(MoveResult.To);
	if (!bCollectedEcho && !MoveResult.bReachedGoal)
	{
		bool bWaiting = false;
		for (const FGridPrintTransfer& Sent : MoveResult.PrintDispatches)
		{
			const FTileData& Receiver = Rules.GetTiles().FindChecked(Sent.To);
			if (!Receiver.bPrintPending) { continue; }
			const FText Color = Sent.Owner == EFootprintOwner::PlayerA ?
				LOCTEXT("BluePrintColor", "蓝色") : LOCTEXT("OrangePrintColor", "橙色");
			SetFeedback(Receiver.PendingPrintSteps > 0 ?
				FText::Format(LOCTEXT("PrintSentWithDelay", "{0} 寄出了{1}脚印。还差 {2} 步，谁走都算。"),
					FText::FromString(RoleLetter(Mover)), Color, FText::AsNumber(Receiver.PendingPrintSteps)) :
				LOCTEXT("PrintWaitingForSpace", "脚印还没到。先让接收那格空出来。"));
			bWaiting = true;
			break;
		}
		if (!bWaiting && !MoveResult.PrintArrivals.IsEmpty())
		{
			const FText Color = MoveResult.PrintArrivals[0].Owner == EFootprintOwner::PlayerA ?
				LOCTEXT("BluePrintColor", "蓝色") : LOCTEXT("OrangePrintColor", "橙色");
			SetFeedback(FText::Format(LOCTEXT("PrintDelivered", "{0}脚印到了。看箭头指向的那格。"), Color));
		}
	}
	// 抵达与通关属于音效，不随音乐静音，也不依赖底乐是否成功播放。
	if (!Rules.IsComplete())
	{
		EGridSound Sound = EGridSound::Step;
		if (MoveResult.bReachedGoal) { Sound = EGridSound::Goal; }
		else if (MoveResult.bSwitchActivated) { Sound = EGridSound::Switch; }
		else if (bCollapsed) { Sound = EGridSound::Collapse; }
		else if (bBridgeChanged || (MoveResult.bSentPrint && !IsMusicEnabled())) { Sound = EGridSound::Bridge; }
		else if (MoveResult.bFootprintTransferred) { Sound = EGridSound::Transfer; }
		else if (Destination.HasFootprints()) { Sound = EGridSound::Print; }
		PlayEffect(Sound);
	}
}

// 只刷新行动涉及的格子，并收集坍塌与桥变化，供调用者播放反馈。
void AGridManager::ApplyTileUpdates(const TArray<FIntPoint>& Positions, bool bPulse, bool& bCollapsed, bool& bBridgeChanged)
{
	for (const FIntPoint Position : Positions)
	{
		AGridTile* Tile = TileActors.FindChecked(Position);
		const FTileData& Data = Rules.GetTiles().FindChecked(Position);
		const bool bChangedPrint = Data.HasFootprints() && Data.FootprintOwner != Tile->TileData.FootprintOwner;
		const bool bNewCollapse = Data.bIsCollapsed && !Tile->TileData.bIsCollapsed;
		const bool bChangedBridge = Data.TileType == ETileType::Bridge && Data.bIsActivated != Tile->TileData.bIsActivated;
		Tile->Update(Data, bPulse && bChangedPrint && Data.FootprintOwner != EFootprintOwner::None);
		if (bChangedPrint) { OnFootprintChanged.Broadcast(Data); }
		if (bNewCollapse)
		{
			bCollapsed = true;
			OnTileCollapsed.Broadcast(Data);
		}
		if (bChangedBridge)
		{
			bBridgeChanged = true;
			OnBridgeChanged.Broadcast(Data);
		}
	}
}

// 两人到达后保存解锁进度，再安排结算界面的切换。
void AGridManager::CheckCompletion()
{
	if (Rules.IsComplete())
	{
		const float AnimationRemaining = FMath::Max(0.f, UnlockInputAt - GetWorld()->GetTimeSeconds());
		SaveLevelProgress();
		SetScreen(EGridScreen::Transition);
		OnLevelCompleted.Broadcast();
		PlayCompletionCue(false);
		GetWorldTimerManager().SetTimer(ScreenTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			SetScreen(EGridScreen::Complete);
		}), FMath::Max(.85f, AnimationRemaining + .1f), false);
	}
}

void AGridManager::PlayEffect(EGridSound Sound)
{
	if (!IsSoundEnabled() || CompletionAudio) { return; }
	ActiveEffects.RemoveAllSwap([](const TWeakObjectPtr<UAudioComponent>& Effect)
	{
		return !Effect.IsValid() || !Effect->IsPlaying();
	});
	if (UAudioComponent* Effect = PlayGridSound(this, Sound)) { ActiveEffects.Add(Effect); }
}

void AGridManager::StopEffects()
{
	for (const auto& Effect : ActiveEffects)
	{
		if (Effect.IsValid()) { Effect->Stop(); }
	}
	ActiveEffects.Reset();
}

void AGridManager::PlayCompletionCue(bool bEnding)
{
	StopCompletionCue();
	StopEffects();
	Music->CancelAccents();
	if (!IsSoundEnabled()) { return; }

	Music->SetCompletionDucked(true);
	CompletionAudio = PlayGridSound(this, bEnding ? EGridSound::Ending : EGridSound::Complete);
	UE_LOG(LogTemp, Display, TEXT("Chapter %d %s audio: %s"), LevelIndex + 1,
		bEnding ? TEXT("ending") : TEXT("completion"), CompletionAudio ? TEXT("started") : TEXT("audio device unavailable"));
	if (!CompletionAudio)
	{
		Music->SetCompletionDucked(false);
		return;
	}

	const float Duration = bEnding ? GridEndingDuration : GridCompletionDuration;
	// Keep the cue isolated through its last mixer buffer, including when Advance is queued.
	GetWorldTimerManager().SetTimer(CompletionCueTimer, this, &AGridManager::FinishCompletionCue, Duration + .1f, false);
}

void AGridManager::StopCompletionCue()
{
	GetWorldTimerManager().ClearTimer(CompletionCueTimer);
	bAdvanceAfterCue = false;
	if (IsValid(CompletionAudio)) { CompletionAudio->Stop(); }
	CompletionAudio = nullptr;
	Music->SetCompletionDucked(false);
}

void AGridManager::FinishCompletionCue()
{
	const bool bAdvance = bAdvanceAfterCue;
	StopCompletionCue();
	if (bAdvance) { Advance(); }
}

// 终章通过后运行一次结局演出，复用已生成的人物、棋盘与摄像机。
void AGridManager::BeginOutro()
{
	using namespace GridVisuals;
	SetScreen(EGridScreen::Ending);
	PlayCompletionCue(true);
	PositionCamera(false, true);
	OutroStartedAt = GetWorld()->GetTimeSeconds();
	Outro = GetWorld()->SpawnActor<AActor>();
	USceneComponent* Root = NewObject<USceneComponent>(Outro);
	Outro->SetRootComponent(Root);
	Root->RegisterComponent();
	const FVector Exit((Rules.GetSize().Y + 1) * 50.f + 102.f, 0.f, 0.f);
	const FLinearColor Pearl(.58f,.81f,.80f);
	for (const EPlayerRole Traveler : Roles)
	{
		const int32 Index = FGridPuzzleRules::RoleIndex(Traveler);
		const FIntPoint LogicalGoal = Rules.GetPosition(Traveler);
		const FVector StartPoint = ToWorld(LogicalGoal);
		const FVector EndPoint = Exit + FVector(-19, Index == 0 ? -19 : 19, 0);
		const int32 Count = FMath::CeilToInt(FVector::Dist(StartPoint, EndPoint) / 77.f);
		for (int32 Step = 1; Step < Count; ++Step)
		{
			const FVector Position = FMath::Lerp(StartPoint, EndPoint, float(Step) / Count);
			// The high, narrow bridge is a visual track for the final shot.
			Shape(*Outro, Root, PaletteMaterial, EGridShape::Cube, Position+FVector(0,0,-10),
				FVector(.47f,.43f,.08f), RoleColor(Traveler) * .55f, .65f,
				(EndPoint-StartPoint).Rotation());
		}
		Characters[Index]->MoveTo(LogicalGoal, EndPoint, 2.45f);
	}
	Shape(*Outro, Root, PaletteMaterial, EGridShape::Cube, Exit+FVector(0,0,-12),
		FVector(.99f,1.29f,.29f), FLinearColor(.15f,.30f,.34f), .5f);
	for (const float Side : {-47.f, 47.f})
	{
		Shape(*Outro, Root, PaletteMaterial, EGridShape::Cube, Exit+FVector(18,Side,47),
			FVector(.10f,.11f,.95f), Pearl, 1.4f);
	}
	Shape(*Outro, Root, PaletteMaterial, EGridShape::Cube, Exit+FVector(18,0,96),
		FVector(.115f,1.02f,.115f), Pearl, 1.8f);
	GetWorldTimerManager().SetTimer(OutroPulseTimer, this, &AGridManager::PulseOutro, .055f, true);
	GetWorldTimerManager().SetTimer(OutroTimer, this, &AGridManager::FinishOutro, CompletionAudio ? GridEndingDuration + .1f : 3.05f, false);
}

// 定时推进结局中沿棋盘传播的脚印发光。
void AGridManager::PulseOutro()
{
	const float Time = GetWorld()->GetTimeSeconds() - OutroStartedAt;
	for (const auto& Pair : TileActors)
	{
		const FTileData& Tile = Pair.Value->TileData;
		if (!Tile.bFreshPrint) { continue; }
		const bool bBlue = Tile.FootprintOwner == EFootprintOwner::PlayerA;
		const float StartTime = (bBlue ? 0.f : 1.1f) + (Pair.Key.X + Pair.Key.Y) * .085f;
		Pair.Value->AnimateOutro(FMath::Clamp((Time - StartTime) / .5f, 0.f, 1.f));
	}
}

// 演出结束后开放结局界面的选关与返回按钮。
void AGridManager::FinishOutro()
{
	bEndingReady = true;
	SetScreen(EGridScreen::Ending);
}

// 双角色由管理器统一控制，默认 Pawn 留空，输入交给自定义 PlayerController。
AGridPuzzleGameMode::AGridPuzzleGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AGridPuzzlePlayerController::StaticClass();
	ManagerClass = AGridManager::StaticClass();
}

// 点击运行后的场景生成入口，生成配置好的管理器蓝图并初始化。
void AGridPuzzleGameMode::StartPlay()
{
	Super::StartPlay();
	AGridPuzzlePlayerController* Controller = Cast<AGridPuzzlePlayerController>(GetWorld()->GetFirstPlayerController());
	if (Controller)
	{
		TSubclassOf<AGridManager> ConfiguredClass = ManagerClass;
		const TCHAR* BoardPackage = TEXT("/Game/OneStep/Blueprints/BP_Board");
		if (ConfiguredClass == AGridManager::StaticClass() && FPackageName::DoesPackageExist(BoardPackage))
		{
			if (UClass* Board = LoadClass<AGridManager>(nullptr,
				TEXT("/Game/OneStep/Blueprints/BP_Board.BP_Board_C")))
			{
				ConfiguredClass = Board;
			}
		}
		Controller->Manager = GetWorld()->SpawnActor<AGridManager>(ConfiguredClass);
		Controller->Manager->Initialize(*Controller);
	}
}

// 动作名来自 DefaultInput.ini，暂停期间仍允许撤销、重置、返回和确认。
void AGridPuzzlePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::H, IE_Pressed, this, &AGridPuzzlePlayerController::NextStep).bExecuteWhenPaused = true;
	InputComponent->BindKey(EKeys::F1, IE_Pressed, this, &AGridPuzzlePlayerController::PreviewMoves);
	InputComponent->BindKey(EKeys::L, IE_Pressed, this, &AGridPuzzlePlayerController::PrintLinks).bExecuteWhenPaused = true;
	InputComponent->BindAction("GridUp", IE_Pressed, this, &AGridPuzzlePlayerController::MoveUp);
	InputComponent->BindAction("GridDown", IE_Pressed, this, &AGridPuzzlePlayerController::MoveDown);
	InputComponent->BindAction("GridLeft", IE_Pressed, this, &AGridPuzzlePlayerController::MoveLeft);
	InputComponent->BindAction("GridRight", IE_Pressed, this, &AGridPuzzlePlayerController::MoveRight);
	InputComponent->BindAction("GridSwitch", IE_Pressed, this, &AGridPuzzlePlayerController::Switch);
	InputComponent->BindAction("GridMusic", IE_Pressed, this, &AGridPuzzlePlayerController::MuteMusic).bExecuteWhenPaused = true;
	InputComponent->BindAction("GridInteract", IE_Pressed, this, &AGridPuzzlePlayerController::Interact);
	InputComponent->BindAction("GridUndo", IE_Pressed, this, &AGridPuzzlePlayerController::Undo).bExecuteWhenPaused = true;
	InputComponent->BindAction("GridReset", IE_Pressed, this, &AGridPuzzlePlayerController::Reset).bExecuteWhenPaused = true;
	InputComponent->BindAction("GridPause", IE_Pressed, this, &AGridPuzzlePlayerController::PauseOrBack).bExecuteWhenPaused = true;
	InputComponent->BindAction("GridConfirm", IE_Pressed, this, &AGridPuzzlePlayerController::Confirm).bExecuteWhenPaused = true;
}

// 输入方向使用网格坐标，向上使 Y 减一，世界位置由管理器统一换算。
void AGridPuzzlePlayerController::MoveUp() { if (Manager) { Manager->Move({0,-1}); } }
void AGridPuzzlePlayerController::MoveDown() { if (Manager) { Manager->Move({0,1}); } }
void AGridPuzzlePlayerController::MoveLeft() { if (Manager) { Manager->Move({-1,0}); } }
void AGridPuzzlePlayerController::MoveRight() { if (Manager) { Manager->Move({1,0}); } }
void AGridPuzzlePlayerController::Switch() { if (Manager) { Manager->SwitchCharacter(); } }
void AGridPuzzlePlayerController::NextStep() { if (Manager) { Manager->RequestNextStep(); } }
void AGridPuzzlePlayerController::PrintLinks() { if (Manager) { Manager->TogglePrintLinks(); } }
void AGridPuzzlePlayerController::MuteMusic() { if (Manager) { Manager->ToggleMusic(); } }
void AGridPuzzlePlayerController::PreviewMoves() { if (Manager) { Manager->ToggleMovePreview(); } }
void AGridPuzzlePlayerController::Interact() { if (Manager) { Manager->UseAction(); } }
void AGridPuzzlePlayerController::Undo() { if (Manager) { Manager->UndoMove(); } }
void AGridPuzzlePlayerController::Reset() { if (Manager) { Manager->RequestReset(); } }
void AGridPuzzlePlayerController::PauseOrBack() { if (Manager) { Manager->TogglePause(); } }

// 同一个确认动作随当前界面分派为开始、关闭引导、继续或下一关。
void AGridPuzzlePlayerController::Confirm()
{
	if (!Manager) { return; }
	switch (Manager->GetScreen())
	{
	case EGridScreen::Menu: Manager->Start(); break;
	case EGridScreen::Achievements: Manager->ShowMenu(); break;
	case EGridScreen::MechanicIntro: Manager->DismissMechanicIntro(); break;
	case EGridScreen::Paused:
	case EGridScreen::Controls: Manager->TogglePause(); break;
	case EGridScreen::Confirm: Manager->ConfirmBoardChange(); break;
	case EGridScreen::Complete: Manager->Advance(); break;
	case EGridScreen::Ending: if (Manager->GetEndingReady()) { Manager->ShowMenu(); } break;
	default: break;
	}
}

#undef LOCTEXT_NAMESPACE
