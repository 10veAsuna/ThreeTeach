#include "GridPuzzleGame.h"

#include "GridPuzzleScreen.h"
#include "GridPuzzleSound.h"
#include "GridPuzzleVisuals.h"
#include "Algo/AllOf.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
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
	const TCHAR* ProgressSlot = TEXT("OneStep_Progress_V6");
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
	UndoHistory.Add({MoveTemp(Previous), Mover, MoveTemp(Changed), SectionIndex, ChapterMoveCount});
}

// 初始化调用链为 GameMode::StartPlay -> Initialize -> ShowMenu -> LoadLevel。
void AGridManager::Initialize(APlayerController& Controller)
{
	Player = &Controller;
	// 老版 BP_Board 储存的是十四张旧图，V6 储存的是五组分区；两者都改用整图。
	if (Levels.IsEmpty() || Levels.Num() == 14 ||
		(Levels.Num() == 5 && Levels[0] && !Levels[0]->Sections.IsEmpty()))
	{
		Levels.Reset();
		Levels.Append(CreateCampaignLevels());
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
	const TCHAR* LoadSlot = bLegacyProgress ? TEXT("OneStep_Progress_V4") : ProgressSlot;
	if (UGameplayStatics::DoesSaveGameExist(LoadSlot, 0))
	{
		if (const UGridPuzzleProgress* Saved = Cast<UGridPuzzleProgress>(UGameplayStatics::LoadGameFromSlot(LoadSlot, 0)))
		{
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
			for (const int32 Echo : Saved->EarnedEchoes)
			{
				if (Echo >= 0 && Echo < 14) { EarnedEchoes.Add(Echo); }
			}
			for (const int32 Intro : Saved->SeenMechanicIntros)
			{
				if (Intro >= 0 && Intro < 14) { SeenMechanicIntros.Add(Intro); }
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
	// 切关会结束旧关所有延时回调，避免上一关的动画或提示写入新局面。
	GetWorldTimerManager().ClearAllTimersForObject(this);
	for (const auto& Pair : TileActors) { Pair.Value->Destroy(); }
	for (AGridPuzzleCharacter* Character : Characters) { Character->Destroy(); }
	if (Outro)
	{
		Outro->Destroy();
		Outro = nullptr;
	}
	TileActors.Reset();
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
	bMechanicIntroPending = Index < CampaignLevelCount && LevelHasMechanicIntro(GetSourceLevelIndex()) &&
		!SeenMechanicIntros.Contains(GetSourceLevelIndex());

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
	// 规则中的寄印关系在场景里画成连接线，同一源目标组合只绘制一次。
	TMap<FIntPoint, TSet<FIntPoint>> LinkedTargets;
	for (const FGridPrintEffect& Effect : Rules.GetPrintEffects())
	{
		TSet<FIntPoint>& Targets = LinkedTargets.FindOrAdd(Effect.Trigger);
		for (const FGridPrintLink& Link : Effect.Links)
		{
			if (Link.Target != Effect.Trigger && !Targets.Contains(Link.Target))
			{
				Targets.Add(Link.Target);
				TileActors.FindChecked(Effect.Trigger)->AddPrintLink(
					ToWorld(Link.Target) - ToWorld(Effect.Trigger), PaletteMaterial);
			}
		}
	}
	FIntPoint EchoPosition;
	if (GetEchoLocationForLevel(GetSourceLevelIndex(), EchoPosition) && !EarnedEchoes.Contains(GetSourceLevelIndex()))
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
	UE_LOG(LogTemp, Display, TEXT("Puzzle loaded: %d / %d, %d tiles"), GetLevelNumber(), GetLevelCount(), TileActors.Num());
	return true;
}

// 界面切换同时更新暂停状态、鼠标输入模式与 Slate 根控件。
void AGridManager::SetScreen(EGridScreen NewScreen)
{
	Screen = NewScreen == EGridScreen::Playing && bMechanicIntroPending ?
		EGridScreen::MechanicIntro : NewScreen;
	const bool bPaused = Screen == EGridScreen::Paused ||
		Screen == EGridScreen::MechanicIntro ||
		(Screen == EGridScreen::Controls && bControlsFromPause);
	Player->SetPause(bPaused);
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
	Super::EndPlay(EndPlayReason);
}

// 从已解锁的续玩关卡开始，首次进入新机关关时由 SetScreen 展示引导。
void AGridManager::Start()
{
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

// 引导已读状态立即持久化，之后重进该关可以直接开始游玩。
void AGridManager::DismissMechanicIntro()
{
	if (Screen != EGridScreen::MechanicIntro) { return; }
	SeenMechanicIntros.Add(GetSourceLevelIndex());
	bMechanicIntroPending = false;
	PersistProgress();
	SetScreen(EGridScreen::Playing);
}

// 主线逐关解锁，挑战要求完成主线，玩法试玩可直接进入。
bool AGridManager::CanSelectLevel(int32 Index) const
{
	return Levels.IsValidIndex(Index) && (Index >= CampaignLevelCount ||
		(Index < MainLevelCount ? Index <= CompletedMainLevels : AreChallengesUnlocked()));
}

void AGridManager::SelectLevel(int32 Index)
{
	if (Screen == EGridScreen::Selection && CanSelectLevel(Index) && LoadLevel(Index))
	{
		SetScreen(EGridScreen::Playing);
	}
}

// 主线结算前往下一关，终章进入演出，挑战与试玩返回选关。
void AGridManager::Advance()
{
	if (Screen != EGridScreen::Complete) { return; }
	if (IsFinalLevel())
	{
		UndoHistory.Reset();
		BeginOutro();
		return;
	}
	if (IsChallenge() || IsPrototype())
	{
		ShowSelection();
		return;
	}
	if (LoadLevel(LevelIndex+1)) { SetScreen(EGridScreen::Playing); }
}

// 主线通关只提升完成进度，重复游玩旧关不会回退解锁状态。
void AGridManager::SaveLevelProgress()
{
	bool bChanged = false;
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
	Save->EarnedEchoes = EarnedEchoes.Array();
	Save->SeenMechanicIntros = SeenMechanicIntros.Array();
	if (!UGameplayStatics::SaveGameToSlot(Save, ProgressSlot, 0))
	{
		UE_LOG(LogTemp, Warning, TEXT("Could not save One Step progress to slot %s."), ProgressSlot);
	}
}

// 回响以关卡索引去重，拾取时更新显示并立即保存。
bool AGridManager::TryCollectEchoAt(FIntPoint Position)
{
	FIntPoint EchoPosition;
	if (!GetEchoLocationForLevel(GetSourceLevelIndex(), EchoPosition) || EchoPosition != Position ||
		GetLevelEchoName(GetSourceLevelIndex()).IsEmpty() || EarnedEchoes.Contains(GetSourceLevelIndex()))
	{
		return false;
	}
	EarnedEchoes.Add(GetSourceLevelIndex());
	if (AGridTile* EchoTile = TileActors.FindRef(Position))
	{
		EchoTile->SetEchoAvailable(false, PaletteMaterial);
	}
	SetFeedback(FText::Format(LOCTEXT("EchoCollected", "发现回响 · {0}"), GetLevelEchoName(GetSourceLevelIndex())));
	PersistProgress();
	if (bPlaySound) { PlayGridSound(this, EGridSound::Select); }
	return true;
}

// 回放播放中屏蔽暂停切换，机关预览中按返回则先取消预览。
void AGridManager::TogglePause()
{
	if (!ReplaySteps.IsEmpty()) { return; }
	if (Screen == EGridScreen::Playing && bActionPreview)
	{
		ClearActionPreview();
		SetFeedback(LOCTEXT("PreviewCancelled", "已取消预览"));
		return;
	}
	switch (Screen)
	{
	case EGridScreen::MechanicIntro: DismissMechanicIntro(); break;
	case EGridScreen::Playing: SetScreen(EGridScreen::Paused); break;
	case EGridScreen::Paused: SetScreen(EGridScreen::Playing); break;
	case EGridScreen::Controls: SetScreen(bControlsFromPause ? EGridScreen::Paused : EGridScreen::Menu); break;
	case EGridScreen::Selection: ShowMenu(); break;
	default: break;
	}
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
		SetFeedback(LOCTEXT("ResetDone", "已重置"));
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
		if (Screen == EGridScreen::Playing) { SetFeedback(LOCTEXT("NothingToUndo", "还没有可以撤销的移动")); }
		return;
	}
	TGuardValue<bool> ApplyingMove(bApplyingMove, true);
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
	SetFeedback(LOCTEXT("UndoDone", "已撤销上一步"));
	if (bPlaySound) { PlayGridSound(this, EGridSound::Select); }
}

// 统一刷新人物的当前控制标记与抵达状态。
void AGridManager::UpdateControl()
{
	for (int32 Index = 0; Index < Characters.Num(); ++Index)
	{
		Characters[Index]->SetControlState(ActiveRole == Roles[Index], Rules.IsFinished(Roles[Index]));
	}
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
			LOCTEXT("PlateNeeded", "{1} 站上踏板 {0}") : LOCTEXT("ImprintNeeded", "{0} 留下 {1} 的脚印"),
			Label, RequiredTraveler));
	}
	return FText::Format(LOCTEXT("BridgeClosed", "{0} 桥需要：{1}"),
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
	ActiveRole = ActiveRole == EPlayerRole::PlayerA ? EPlayerRole::PlayerB : EPlayerRole::PlayerA;
	UpdateControl();
	if (bPlaySound) { PlayGridSound(this, EGridSound::Select); }
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
	return CurrentSection() ? CurrentSection()->Hint :
		LOCTEXT("MissingLevel", "关卡数据未配置");
}

int32 AGridManager::GetEarnedEchoCount() const
{
	int32 Count = 0;
	FIntPoint Position;
	for (int32 Index = 0; Index < CampaignLevelCount; ++Index)
	{
		const int32 Key = Levels[Index]->LegacyChapterIndex;
		Count += GetEchoLocationForLevel(Key, Position) && EarnedEchoes.Contains(Key);
	}
	return Count;
}

int32 AGridManager::GetEchoTargetCount() const
{
	int32 Count = 0;
	FIntPoint Position;
	for (int32 Index = 0; Index < CampaignLevelCount; ++Index)
	{
		Count += GetEchoLocationForLevel(Levels[Index]->LegacyChapterIndex, Position);
	}
	return Count;
}

// 回响位置按从 0 开始的关卡索引配置，自动化测试会搜索其可达性。
bool AGridManager::GetEchoLocationForLevel(int32 LevelIndex, FIntPoint& OutPosition)
{
	switch (LevelIndex)
	{
	case 0: OutPosition = {2, 2}; return true;
	case 4: OutPosition = {2, 2}; return true;
	case 6: OutPosition = {4, 4}; return true;
	case 9: OutPosition = {4, 3}; return true;
	case 12: OutPosition = {5, 1}; return true;
	case 13: OutPosition = {7, 4}; return true;
	default: return false;
	}
}

// 只在首次引入机关的指定关卡显示说明，复用已有规则的关卡直接开始。
bool AGridManager::LevelHasMechanicIntro(int32 LevelIndex)
{
	switch (LevelIndex)
	{
	case 0:
	case 2:
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 10:
	case 11:
	case 12:
	case 13:
		return true;
	default:
		return false;
	}
}

FText AGridManager::GetLevelEchoName(int32 Index) const
{
	switch (Index)
	{
	case 0: return LOCTEXT("Echo01", "初见回响");
	case 4: return LOCTEXT("Echo05", "脆石回声");
	case 6: return LOCTEXT("Echo07", "两印之间");
	case 9: return LOCTEXT("Echo10", "岔路回声");
	case 12: return LOCTEXT("Echo13", "旧路藏声");
	case 13: return LOCTEXT("Echo14", "岸边迟信");
	default: return FText::GetEmpty();
	}
}

FText AGridManager::GetMechanicIntroTitle() const
{
	switch (GetSourceLevelIndex())
	{
	case 0: return LOCTEXT("IntroTitle01", "借同伴的脚印");
	case 2: return LOCTEXT("IntroTitle03", "开关与锁定终点");
	case 4: return LOCTEXT("IntroTitle05", "脆弱石板");
	case 5: return LOCTEXT("IntroTitle06", "TURN · 转动桥面");
	case 6: return LOCTEXT("IntroTitle07", "OUT 与 IN · 寄送脚印");
	case 7: return LOCTEXT("IntroTitle08", "REC 与 PLAY · 录印回放");
	case 8: return LOCTEXT("IntroTitle09", "一次性印板");
	case 10: return LOCTEXT("IntroTitle11", "保存拓本");
	case 11: return LOCTEXT("IntroTitle12", "SWAP 与 TURN · 交班");
	case 12: return LOCTEXT("IntroTitle13", "旧印复原");
	case 13: return LOCTEXT("IntroTitle14", "延迟寄印");
	default: return FText::GetEmpty();
	}
}

// 引导文案与运行中的规则对应，显示由 SetScreen 和已读存档共同控制。
FText AGridManager::GetMechanicIntroBody() const
{
	switch (GetSourceLevelIndex())
	{
	case 0:
		return LOCTEXT("IntroBody01",
			"你不能踩自己刚留下的脚印，但可以踩同伴的脚印。踩上去时，这格会变成你的颜色，并留给同伴继续借。\n按 Tab 切换当前角色；角色切换本身不计步。");
	case 2:
		return LOCTEXT("IntroBody03",
			"带锁终点只允许对应角色进入，并需要同色的开关先被踩亮。开关启动后，锁会在本局一直保持打开。\n终点上的小旗和字母标出它属于 A 还是 B。");
	case 4:
		return LOCTEXT("IntroBody05",
			"脆弱石板上的橙色竖条表示剩余借用次数。有人站住时倒计时暂停；离开后，每次成功移动都会让它减少一格。\n只剩最后一格时会闪烁，此时仍能接手。成功换上自己的脚印会恢复本关寿命。");
	case 5:
		return LOCTEXT("IntroBody06",
			"站上 TURN 碑按 E，可以预览桥面转动后的落点，再按 E 确认。桥上的脚印会跟着石板一起转过去。\n确认前两人都必须离开当前桥面；桥上有人时不能转桥。");
	case 6:
		return LOCTEXT("IntroBody07",
			"OUT 是发件印板：角色踩上 OUT 的瞬间会读取连线来源的颜色，并寄给对应的 IN 印板。\nIN 是收件印板：它保留寄出瞬间的颜色；角色必须踩上这格才能借用，踩上后会改成自己的颜色。目标暂时被占用时显示 WAIT，等目标腾空后再送达。");
	case 7:
		return LOCTEXT("IntroBody08",
			"A 站上 REC 按 E 开始录制，之后三次成功移动会成为方向和节奏。B 站上 PLAY 按 E 可以先预览整段路线，再确认回放。\n被拒绝的移动不会录入；回放要求每一步都能借到 A 的蓝色脚印。切换角色不会打断已经录下的内容。");
	case 8:
		return LOCTEXT("IntroBody09",
			"标着 1 的印板只能消费一次。站上去时它还完整，离开后会立即坍塌，不能再回来。\n决定由谁先用、何时离开，是这一关的核心。");
	case 10:
		return LOCTEXT("IntroBody11",
			"第一块满足条件的印板会保存一份拓本；之后即使原印再次改色，保存下来的拓本仍保留当时的颜色。\n拓本是一次性记录，不能重复覆盖。看清哪一块是可继续改写的原印，哪一块是已经锁定的历史印。");
	case 11:
		return LOCTEXT("IntroBody12",
			"让两人分别站上相邻的 SWAP 印板并按 E，可以一次交换位置。站在 TURN 碑上时，同一个 E 键用于转桥或换位。\n先预览落点，再确认；桥面和脚印会一起转向。");
	case 12:
		return LOCTEXT("IntroBody13",
			"旧印机关会记录某一刻的颜色，并在条件满足后把这份历史颜色复原到印板上。\n复原改变的是印板，不会移动角色。先安排两人的站位，再决定封存哪一枚印。");
	case 13:
		return LOCTEXT("IntroBody14",
			"延迟送印不会立刻改变 IN 印板。触发后要等三次成功移动，脚印才会抵达；期间 IN 会以 WAIT 闪烁。\n换角色不会重置倒计时，目标被占用时也会等待。走路和交接可以同时推进送印。");
	default:
		return FText::GetEmpty();
	}
}

FText AGridManager::GetMechanicIntroExample() const
{
	switch (GetSourceLevelIndex())
	{
	case 0: return LOCTEXT("IntroExample01", "A 走过一格留下蓝印 → Tab 切到 B → B 借这枚蓝印前进");
	case 2: return LOCTEXT("IntroExample03", "让标记对应的角色踩亮开关 → 再去自己的锁门终点");
	case 4: return LOCTEXT("IntroExample05", "先靠近石板 → 让原主人离开 → 在下一次或最后一次计数内接手");
	case 5: return LOCTEXT("IntroExample06", "站上 TURN 按 E 预览 → 确认两人都在桥外 → 再按 E 转向");
	case 6: return LOCTEXT("IntroExample07", "角色踩上 OUT → 颜色沿连线抵达 IN → 另一角色走到 IN 借用");
	case 7: return LOCTEXT("IntroExample08", "A 在 REC 开始录印 → 走三步并让开 → B 在 PLAY 预览并回放");
	case 8: return LOCTEXT("IntroExample09", "需要回程时不要先走一次性印板 → 让真正要离开的人最后消费它");
	case 10: return LOCTEXT("IntroExample11", "先用要求的颜色保存拓本 → 之后再安全改掉原印");
	case 11: return LOCTEXT("IntroExample12", "两人站上相邻 SWAP 印板 → 按 E 预览换位 → 再按 E 同时迈步");
	case 12: return LOCTEXT("IntroExample13", "先封存同伴需要的颜色 → 等两人站位完成 → 再触发复原");
	case 13: return LOCTEXT("IntroExample14", "踩上寄印触发点 → 三次成功移动内走向 IN → 接住随后出现的颜色");
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
		return FText::Format(LOCTEXT("RhythmStatus", "下一次落脚 · 第 {0}/{1} 拍  |  BEAT WAIT 时请同伴让拍"),
			FText::AsNumber((Rules.GetMoveCount() + 1) % Rules.GetRhythmPeriod() + 1), FText::AsNumber(Rules.GetRhythmPeriod()));
	}
	if (CurrentSection()->Prototype != EGridPrototype::RecordRoute) { return FText::GetEmpty(); }
	const TArray<FIntPoint>& Directions = Rules.GetRecording();
	if (!Rules.IsRecording() && Directions.IsEmpty()) { return FText::GetEmpty(); }
	FString Sequence;
	for (const FIntPoint Direction : Directions)
	{
		if (!Sequence.IsEmpty()) { Sequence += TEXT("   "); }
		Sequence += DirectionGlyph(Direction);
	}
	return FText::Format(LOCTEXT("RecordingStatus", "{0}  {1}/3    {2}"),
		Rules.IsRecording() ? LOCTEXT("Recording", "录印中") : LOCTEXT("Recorded", "已录印"),
		FText::AsNumber(Directions.Num()), FText::FromString(Sequence));
}

// 清除只用于预览的编号和颜色标记，实际地块状态从 Rules 恢复。
void AGridManager::ClearActionPreview()
{
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
			SetFeedback(LOCTEXT("CannotTurn", "站上 TURN 碑，并让两人都离开桥面"));
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
			SetFeedback(LOCTEXT("TurnPreview", "标记是转桥后的落点。再按 E 转桥，Esc 取消"));
			return;
		}
		ClearActionPreview();
		SaveUndo(MoveTemp(Rules), ActiveRole, Changed);
		Rules = MoveTemp(Trial);
		bool bCollapsed = false;
		bool bBridgeChanged = false;
		ApplyTileUpdates(Changed, false, bCollapsed, bBridgeChanged);
		AnimateTurnedDeck(Changed, false);
		UnlockInputAt = GetWorld()->GetTimeSeconds() + DeckTime;
		SetFeedback(LOCTEXT("TurnDone", "桥面和脚印已一起转向"));
		if (bPlaySound) { PlayGridSound(this, EGridSound::Bridge); }
		return;
	}
	case EGridPrototype::RecordRoute:
	{
		if (Trial.StartRecording(ActiveRole))
		{
			SaveUndo(MoveTemp(Rules), ActiveRole, {});
			Rules = MoveTemp(Trial);
			SetFeedback(LOCTEXT("RecordStarted", "已开始录印。记录 A 接下来的三次成功移动"));
			return;
		}
		if (!Rules.CanReplayFrom(ActiveRole))
		{
			SetFeedback(LOCTEXT("NoRecording", "A 在 REC 录满三步，B 在 PLAY 回放"));
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
				SetFeedback(LOCTEXT("ReplayPreview", "回放沿 1 → 2 → 3 前进。再按 E 执行，Esc 取消"));
			}
			else
			{
				const EGridMoveResult Failure = Steps.Last().Result;
				const FText Reason = Failure == EGridMoveResult::Occupied ? LOCTEXT("ReplayOccupied", "同伴还站在落点上") :
					(Failure == EGridMoveResult::Void || Failure == EGridMoveResult::ReplayOnly) ?
					LOCTEXT("ReplayOutOfPath", "落点不在回放路段内") : LOCTEXT("ReplayNoPrint", "落点没有可借的蓝印");
				SetFeedback(FText::Format(LOCTEXT("ReplayBlocked", "第 {0} 步无法回放。{1}"), FText::AsNumber(Steps.Num()), Reason));
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
			SetFeedback(LOCTEXT("CannotSwap", "让两人分别站上相邻的 SWAP 印板，各自留下自己的印"));
			return;
		}
		if (!bActionPreview)
		{
			MarkActionTile(Trial.GetPosition(EPlayerRole::PlayerA), FText::FromString(TEXT("A")), FColor(124,220,255));
			MarkActionTile(Trial.GetPosition(EPlayerRole::PlayerB), FText::FromString(TEXT("B")), FColor(255,186,123));
			bActionPreview = true;
			SetFeedback(LOCTEXT("SwapPreview", "标记是两人的换位落点。再按 E 同时迈步，Esc 取消"));
			return;
		}
		ClearActionPreview();
		SaveUndo(MoveTemp(Rules), ActiveRole, Move.ChangedTiles);
		Rules = MoveTemp(Trial);
		bool bCollapsed = false;
		bool bBridgeChanged = false;
		ApplyTileUpdates(Move.ChangedTiles, true, bCollapsed, bBridgeChanged);
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
		SetFeedback(LOCTEXT("SwapDone", "两人同时借到了同伴刚让出的脚印"));
		TryCollectEchoAt(Rules.GetPosition(EPlayerRole::PlayerA));
		TryCollectEchoAt(Rules.GetPosition(EPlayerRole::PlayerB));
		if (bPlaySound) { PlayGridSound(this, EGridSound::Transfer); }
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
		SetFeedback(LOCTEXT("ReplayDone", "已接过同伴的三步"));
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
		case EGridMoveResult::CrateBlocked: Reason = LOCTEXT("CrateBlocked", "箱子后面需要空闲印板，先给同伴让出推箱位置"); break;
		case EGridMoveResult::RhythmClosed: Reason = LOCTEXT("RhythmClosed", "下一拍关门，让同伴先踩一步来让拍"); break;
		case EGridMoveResult::PatrolBlocked: Reason = LOCTEXT("PatrolBlocked", "巡逻将经过这里，让另一人先沿脚印脱离危险"); break;
		case EGridMoveResult::OwnFootprint: Reason = LOCTEXT("OwnPrint", "这是你留下的脚印，交给同伴吧"); break;
		case EGridMoveResult::EmptyPrint: Reason = LOCTEXT("EmptyPrint", "这格还没有脚印，等同伴留下印记。"); break;
		case EGridMoveResult::Occupied: Reason = LOCTEXT("Occupied", "同伴正站在这一格"); break;
		case EGridMoveResult::Finished: Reason = LOCTEXT("Finished", "这个角色已抵达，按 Tab 切换"); break;
		case EGridMoveResult::Collapsed: Reason = LOCTEXT("Collapsed", "石板已坍塌，可按 R 重来"); break;
		case EGridMoveResult::LockedGoal: Reason = LOCTEXT("Locked", "先由标记对应的角色启动开关"); break;
		case EGridMoveResult::ClosedBridge: Reason = GetClosedBridgeFeedback(MoveResult.To); break;
		case EGridMoveResult::OtherGoal: Reason = LOCTEXT("OtherGoal", "这是同伴的终点"); break;
		case EGridMoveResult::ReplayOnly: Reason = LOCTEXT("ReplayOnly", "在 PLAY 碑按 E 预览并回放这段路"); break;
		default: Reason = LOCTEXT("Blocked", "无法往这个方向移动"); break;
		}
		SetFeedback(Reason);
		if (!Rules.IsFinished(Mover))
		{
			Character->RejectMove(ToWorld(MoveResult.To) - ToWorld(MoveResult.From));
		}
		UnlockInputAt = GetWorld()->GetTimeSeconds() + RejectTime;
		if (bPlaySound) { PlayGridSound(this, EGridSound::Reject); }
		return;
	}
	PresentMove(Mover, MoveResult);
	SaveUndo(MoveTemp(PreviousRules), Mover, MoveTemp(MoveResult.ChangedTiles));
	CheckCompletion();
}

// 将已经结算的规则结果转换成走路动画、地块刷新、事件通知与音效。
void AGridManager::PresentMove(EPlayerRole Mover, const FGridMove& MoveResult)
{
	const float Duration = StepTime * FMath::Max(1, MoveResult.Path.Num());
	UnlockInputAt = GetWorld()->GetTimeSeconds() + Duration;
	Feedback = FText::GetEmpty();
	Characters[FGridPuzzleRules::RoleIndex(Mover)]->MoveTo(MoveResult.To, ToWorld(MoveResult.To), Duration);
	bool bCollapsed = false;
	bool bBridgeChanged = false;
	const FTileData Destination = Rules.GetTiles().FindChecked(MoveResult.To);
	ApplyTileUpdates(MoveResult.ChangedTiles, true, bCollapsed, bBridgeChanged);
	UpdateBridgeDisplays();
	UpdateControl();
	OnCharacterMoved.Broadcast(Mover, MoveResult.To);
	if (MoveResult.bSwitchActivated)
	{
		OnSwitchActivated.Broadcast(Destination);
		SetFeedback(LOCTEXT("Unlocked", "开关已启动，终点已解锁"));
	}
	if (MoveResult.bReachedGoal && !Rules.IsComplete())
	{
		SetFeedback(LOCTEXT("OneArrived", "已抵达终点，按 Tab 继续帮助同伴"));
	}
	if (Destination.TileType == ETileType::Bridge && !Destination.bIsActivated)
	{
		SetFeedback(LOCTEXT("BridgeExit", "桥已合拢，仍可向相邻的石板离开。"));
	}
	TryCollectEchoAt(MoveResult.To);
	if (bPlaySound)
	{
		EGridSound Sound = EGridSound::Step;
		if (MoveResult.bReachedGoal) { Sound = EGridSound::Goal; }
		else if (MoveResult.bSwitchActivated) { Sound = EGridSound::Switch; }
		else if (MoveResult.bFootprintTransferred) { Sound = EGridSound::Transfer; }
		else if (Destination.HasFootprints()) { Sound = EGridSound::Print; }
		PlayGridSound(this, Sound);
		if (bCollapsed) { PlayGridSound(this, EGridSound::Collapse); }
		if (bBridgeChanged) { PlayGridSound(this, EGridSound::Bridge); }
		if (MoveResult.bSentPrint) { PlayGridSound(this, EGridSound::Bridge); }
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
		if (SectionIndex + 1 < GetSectionCount())
		{
			SetScreen(EGridScreen::Transition);
			GetWorldTimerManager().SetTimer(ScreenTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				ChapterMoveCount += Rules.GetMoveCount();
				if (LoadLevel(LevelIndex, SectionIndex + 1, true)) { SetScreen(EGridScreen::Playing); }
			}), FMath::Max(StepTime * 2.f, AnimationRemaining + .1f), false);
			return;
		}
		SaveLevelProgress();
		SetScreen(EGridScreen::Transition);
		OnLevelCompleted.Broadcast();
		if (bPlaySound) { PlayGridSound(this, EGridSound::Complete); }
		GetWorldTimerManager().SetTimer(ScreenTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			SetScreen(EGridScreen::Complete);
		}), FMath::Max(.85f, AnimationRemaining + .1f), false);
	}
}

// 终章通过后运行一次结局演出，复用已生成的人物、棋盘与摄像机。
void AGridManager::BeginOutro()
{
	using namespace GridVisuals;
	SetScreen(EGridScreen::Ending);
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
	GetWorldTimerManager().SetTimer(OutroTimer, this, &AGridManager::FinishOutro, 3.05f, false);
}

// 定时推进结局中沿棋盘传播的脚印发光。
void AGridManager::PulseOutro()
{
	const float Time = GetWorld()->GetTimeSeconds() - OutroStartedAt;
	for (const auto& Pair : TileActors)
	{
		const float Position = Pair.Key.Y * .72f + Pair.Key.X * .38f;
		Pair.Value->AnimateOutro(.57f + .43f*FMath::Sin(Time*4.8f - Position));
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
	InputComponent->BindAction("GridUp", IE_Pressed, this, &AGridPuzzlePlayerController::MoveUp);
	InputComponent->BindAction("GridDown", IE_Pressed, this, &AGridPuzzlePlayerController::MoveDown);
	InputComponent->BindAction("GridLeft", IE_Pressed, this, &AGridPuzzlePlayerController::MoveLeft);
	InputComponent->BindAction("GridRight", IE_Pressed, this, &AGridPuzzlePlayerController::MoveRight);
	InputComponent->BindAction("GridSwitch", IE_Pressed, this, &AGridPuzzlePlayerController::Switch);
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
void AGridPuzzlePlayerController::Interact() { if (Manager) { Manager->UseAction(); } }
void AGridPuzzlePlayerController::Undo() { if (Manager) { Manager->UndoMove(); } }
void AGridPuzzlePlayerController::Reset() { if (Manager) { Manager->ResetLevel(); } }
void AGridPuzzlePlayerController::PauseOrBack() { if (Manager) { Manager->TogglePause(); } }

// 同一个确认动作随当前界面分派为开始、关闭引导、继续或下一关。
void AGridPuzzlePlayerController::Confirm()
{
	if (!Manager) { return; }
	switch (Manager->GetScreen())
	{
	case EGridScreen::Menu: Manager->Start(); break;
	case EGridScreen::MechanicIntro: Manager->DismissMechanicIntro(); break;
	case EGridScreen::Paused:
	case EGridScreen::Controls: Manager->TogglePause(); break;
	case EGridScreen::Complete: Manager->Advance(); break;
	case EGridScreen::Ending: if (Manager->GetEndingReady()) { Manager->ShowMenu(); } break;
	default: break;
	}
}

#undef LOCTEXT_NAMESPACE
