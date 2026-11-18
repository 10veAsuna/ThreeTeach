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

namespace
{
	constexpr float TileSize = 100.f;
	constexpr float StepTime = .18f;
	constexpr float RejectTime = .14f;
	constexpr float DeckTime = .46f;
	constexpr float SwapTime = .35f;
	const TCHAR* ProgressSlot = TEXT("OneStep_Progress_V4");
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

AGridManager::AGridManager()
{
	TileClass = AGridTile::StaticClass();
	CharacterClass = AGridPuzzleCharacter::StaticClass();
}

void AGridManager::Initialize(APlayerController& Controller)
{
	Player = &Controller;
	if (Levels.IsEmpty())
	{
		Levels.Append(CreateDefaultLevels());
	}
	const int32 FirstChallenge = Levels.IndexOfByPredicate([](const UGridLevelDataAsset* Level)
	{
		return Level && Level->bChallenge;
	});
	MainLevelCount = FirstChallenge == INDEX_NONE ? Levels.Num() : FirstChallenge;
	CampaignLevelCount = Levels.Num();
	Levels.Append(CreatePrototypeLevels());
	if (UGameplayStatics::DoesSaveGameExist(ProgressSlot, 0))
	{
		if (const UGridPuzzleProgress* Saved = Cast<UGridPuzzleProgress>(UGameplayStatics::LoadGameFromSlot(ProgressSlot, 0)))
		{
			CompletedMainLevels = FMath::Clamp(Saved->CompletedMainLevels, 0, MainLevelCount);
		}
	}
	if (!PaletteMaterial)
	{
		PaletteMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
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

FVector AGridManager::ToWorld(FIntPoint Coordinate) const
{
	return FVector((.5f*(Rules.GetSize().Y-1) - Coordinate.Y) * TileSize,
		(Coordinate.X - .5f*(Rules.GetSize().X-1)) * TileSize, 0);
}

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

bool AGridManager::LoadLevel(int32 Index)
{
	if (!Levels.IsValidIndex(Index) || !Levels[Index] || !Rules.Initialize(*Levels[Index]))
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid puzzle level at index %d. Check the Blueprint's Levels data assets."), Index);
		return false;
	}
	TGuardValue<bool> ApplyingMove(bApplyingMove, true);
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
	UndoHistory.Reset();
	PreviewMarkers.Reset();
	ReplaySteps.Reset();
	ReplayStates.Reset();
	bActionPreview = false;
	LevelIndex = Index;
	ActiveRole = EPlayerRole::PlayerA;
	Feedback = FText::GetEmpty();
	UnlockInputAt = 0.f;
	bEndingReady = false;

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

void AGridManager::SetScreen(EGridScreen NewScreen)
{
	Screen = NewScreen;
	const bool bPaused = Screen == EGridScreen::Paused ||
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
	ViewportUI = MakeGridPuzzleScreen(*this);
	Viewport->AddViewportWidgetContent(ViewportUI.ToSharedRef(), 10);
	FSlateApplication::Get().SetAllUserFocusToGameViewport();
}

void AGridManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport(); Viewport && ViewportUI)
	{
		Viewport->RemoveViewportWidgetContent(ViewportUI.ToSharedRef());
	}
	ViewportUI.Reset();
	Super::EndPlay(EndPlayReason);
}

void AGridManager::Start()
{
	if (LoadLevel(GetResumeIndex())) { SetScreen(EGridScreen::Playing); }
}

void AGridManager::ShowControls()
{
	bControlsFromPause = Screen == EGridScreen::Paused;
	SetScreen(EGridScreen::Controls);
}

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

void AGridManager::SaveLevelProgress()
{
	if (IsChallenge() || IsPrototype() || GetLevelNumber() <= CompletedMainLevels) { return; }
	CompletedMainLevels = GetLevelNumber();
	UGridPuzzleProgress* Save = CastChecked<UGridPuzzleProgress>(
		UGameplayStatics::CreateSaveGameObject(UGridPuzzleProgress::StaticClass()));
	Save->CompletedMainLevels = CompletedMainLevels;
	if (!UGameplayStatics::SaveGameToSlot(Save, ProgressSlot, 0))
	{
		UE_LOG(LogTemp, Warning, TEXT("Could not save One Step progress to slot %s."), ProgressSlot);
	}
}

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

void AGridManager::ResetLevel()
{
	if (bApplyingMove || !ReplaySteps.IsEmpty() || (Screen != EGridScreen::Playing && Screen != EGridScreen::Paused)) { return; }
	if (LoadLevel(LevelIndex))
	{
		SetScreen(EGridScreen::Playing);
		SetFeedback(LOCTEXT("ResetDone", "已重置"));
	}
}

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
	FUndoStep Step = UndoHistory.Pop(EAllowShrinking::No);
	const bool bTurned = Step.PreviousRules.GetNextDeck() != Rules.GetNextDeck();
	Rules = MoveTemp(Step.PreviousRules);
	ActiveRole = Step.Mover;
	const bool bSwapped = Algo::AllOf(Characters, [this](const AGridPuzzleCharacter* Character)
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

void AGridManager::UpdateControl()
{
	for (int32 Index = 0; Index < Characters.Num(); ++Index)
	{
		Characters[Index]->SetControlState(ActiveRole == Roles[Index], Rules.IsFinished(Roles[Index]));
	}
}

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
	return Levels.IsValidIndex(LevelIndex) && Levels[LevelIndex] ? Levels[LevelIndex]->Hint :
		LOCTEXT("MissingLevel", "关卡数据未配置");
}

FText AGridManager::GetRecordingStatus() const
{
	if (Levels[LevelIndex]->Prototype != EGridPrototype::RecordRoute) { return FText::GetEmpty(); }
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

void AGridManager::AnimateTurnedDeck(const TArray<FIntPoint>& ChangedTiles, bool bReverse)
{
	const FVector Pivot = ToWorld(Levels[LevelIndex]->TurnPivot);
	for (int32 Index = 0; Index + 1 < ChangedTiles.Num(); Index += 2)
	{
		const FIntPoint From = ChangedTiles[Index + (bReverse ? 1 : 0)];
		const FIntPoint To = ChangedTiles[Index + (bReverse ? 0 : 1)];
		TileActors.FindChecked(To)->AnimateDeckFrom(Pivot, ToWorld(From), DeckTime);
	}
}

void AGridManager::UseAction()
{
	if (Screen != EGridScreen::Playing || !HasLevelAction() || bApplyingMove || !ReplaySteps.IsEmpty() ||
		GetWorld()->GetTimeSeconds() < UnlockInputAt) { return; }
	TGuardValue<bool> ApplyingMove(bApplyingMove, true);
	FGridPuzzleRules Trial = Rules;
	EGridPrototype Interaction = Levels[LevelIndex]->Prototype;
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
		UndoHistory.Add({MoveTemp(Rules), ActiveRole, Changed});
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
			UndoHistory.Add({MoveTemp(Rules), ActiveRole, {}});
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
		UndoHistory.Add({Rules, ActiveRole, MoveTemp(Changed)});
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
		UndoHistory.Add({MoveTemp(Rules), ActiveRole, Move.ChangedTiles});
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
		if (bPlaySound) { PlayGridSound(this, EGridSound::Transfer); }
		return;
	}
	}
}

void AGridManager::PlayReplayStep()
{
	TGuardValue<bool> ApplyingMove(bApplyingMove, true);
	Rules = MoveTemp(ReplayStates[ReplayIndex]);
	PresentMove(EPlayerRole::PlayerB, ReplaySteps[ReplayIndex]);
	if (++ReplayIndex == ReplaySteps.Num())
	{
		ReplaySteps.Reset();
		ReplayStates.Reset();
		SetFeedback(LOCTEXT("ReplayDone", "已接过同伴的三步"));
		return;
	}
	GetWorldTimerManager().SetTimer(ReplayTimer, this, &AGridManager::PlayReplayStep, StepTime, false);
}

void AGridManager::Move(FIntPoint Direction)
{
	if (Screen != EGridScreen::Playing || bApplyingMove || !ReplaySteps.IsEmpty() ||
		GetWorld()->GetTimeSeconds() < UnlockInputAt) { return; }
	TGuardValue<bool> ApplyingMove(bApplyingMove, true);
	ClearActionPreview();

	const EPlayerRole Mover = ActiveRole;
	AGridPuzzleCharacter* Character = Characters[FGridPuzzleRules::RoleIndex(Mover)];
	FGridPuzzleRules PreviousRules = Rules;
	FGridMove MoveResult = Rules.TryMove(Mover, Direction);
	if (MoveResult.Result != EGridMoveResult::Moved)
	{
		FText Reason;
		switch (MoveResult.Result)
		{
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
	UndoHistory.Add({MoveTemp(PreviousRules), Mover, MoveTemp(MoveResult.ChangedTiles)});
	CheckCompletion();
}

void AGridManager::PresentMove(EPlayerRole Mover, const FGridMove& MoveResult)
{
	UnlockInputAt = GetWorld()->GetTimeSeconds() + StepTime;
	Feedback = FText::GetEmpty();
	Characters[FGridPuzzleRules::RoleIndex(Mover)]->MoveTo(MoveResult.To, ToWorld(MoveResult.To), StepTime);
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

void AGridManager::CheckCompletion()
{
	if (Rules.IsComplete())
	{
		SaveLevelProgress();
		SetScreen(EGridScreen::Transition);
		OnLevelCompleted.Broadcast();
		if (bPlaySound) { PlayGridSound(this, EGridSound::Complete); }
		GetWorldTimerManager().SetTimer(ScreenTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			SetScreen(EGridScreen::Complete);
		}), .85f, false);
	}
}

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

void AGridManager::PulseOutro()
{
	const float Time = GetWorld()->GetTimeSeconds() - OutroStartedAt;
	for (const auto& Pair : TileActors)
	{
		const float Position = Pair.Key.Y * .72f + Pair.Key.X * .38f;
		Pair.Value->AnimateOutro(.57f + .43f*FMath::Sin(Time*4.8f - Position));
	}
}

void AGridManager::FinishOutro()
{
	bEndingReady = true;
	SetScreen(EGridScreen::Ending);
}

AGridPuzzleGameMode::AGridPuzzleGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AGridPuzzlePlayerController::StaticClass();
	ManagerClass = AGridManager::StaticClass();
}

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

void AGridPuzzlePlayerController::MoveUp() { if (Manager) { Manager->Move({0,-1}); } }
void AGridPuzzlePlayerController::MoveDown() { if (Manager) { Manager->Move({0,1}); } }
void AGridPuzzlePlayerController::MoveLeft() { if (Manager) { Manager->Move({-1,0}); } }
void AGridPuzzlePlayerController::MoveRight() { if (Manager) { Manager->Move({1,0}); } }
void AGridPuzzlePlayerController::Switch() { if (Manager) { Manager->SwitchCharacter(); } }
void AGridPuzzlePlayerController::Interact() { if (Manager) { Manager->UseAction(); } }
void AGridPuzzlePlayerController::Undo() { if (Manager) { Manager->UndoMove(); } }
void AGridPuzzlePlayerController::Reset() { if (Manager) { Manager->ResetLevel(); } }
void AGridPuzzlePlayerController::PauseOrBack() { if (Manager) { Manager->TogglePause(); } }

void AGridPuzzlePlayerController::Confirm()
{
	if (!Manager) { return; }
	switch (Manager->GetScreen())
	{
	case EGridScreen::Menu: Manager->Start(); break;
	case EGridScreen::Paused:
	case EGridScreen::Controls: Manager->TogglePause(); break;
	case EGridScreen::Complete: Manager->Advance(); break;
	case EGridScreen::Ending: if (Manager->GetEndingReady()) { Manager->ShowMenu(); } break;
	default: break;
	}
}

#undef LOCTEXT_NAMESPACE
