#include "GridPuzzleGame.h"

#include "GridPuzzleVisuals.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "GridPuzzleGuidance"

void AGridManager::ClearStepHint()
{
	HintSearch.Reset();
	StepHint.Reset();
	StepHintStatus = FText::GetEmpty();
	for (FIntPoint Position : HintMarkers)
	{
		if (AGridTile* Tile = TileActors.FindRef(Position)) { Tile->ShowActionMarker(FText::GetEmpty(), FColor::White); }
	}
	HintMarkers.Reset();
}

void AGridManager::RequestNextStep()
{
	if (Screen == EGridScreen::Paused) { SetScreen(EGridScreen::Playing); }
	if (Screen != EGridScreen::Playing || bApplyingMove || !ReplaySteps.IsEmpty() ||
		GetWorld()->GetTimeSeconds() < UnlockInputAt) { return; }
	ClearStepHint();
	ClearActionPreview();
	if (!HintGuide) { HintGuide = MakeShared<FGridHintGuide>(); }
	HintSearch = MakeShared<FGridHintSearch>(Rules, HintGuide);
	StepHintStatus = LOCTEXT("Searching", "正在寻找下一步……");
	if (!AdvanceStepHint()) { return; }
	const TSharedPtr<FGridHintSearch> Job = HintSearch;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[Weak = TWeakObjectPtr<AGridManager>(this), Job](float)
		{
			return Weak.IsValid() && Weak->HintSearch == Job && Weak->AdvanceStepHint();
		}));
}

bool AGridManager::AdvanceStepHint()
{
	const FGridHintSearch::EStatus Status = HintSearch->Advance();
	if (Status == FGridHintSearch::EStatus::Searching) { return true; }
	if (Status == FGridHintSearch::EStatus::Found)
	{
		StepHint = HintSearch->GetAction();
		StepHintStatus = FText::GetEmpty();
		const FGridHintAction& Action = StepHint.GetValue();
		const TCHAR* RoleName = Action.Role == EPlayerRole::PlayerA ? TEXT("A") : TEXT("B");
		const FString Label = Action.Kind == EGridHintKind::Move ?
			FString(RoleName) + TEXT(" ") + (Action.Direction.X > 0 ? TEXT("D") : Action.Direction.X < 0 ? TEXT("A") :
				Action.Direction.Y < 0 ? TEXT("W") : TEXT("S")) : FString(RoleName) + TEXT(" E");
		const FIntPoint Target = Action.Kind == EGridHintKind::Move && !Action.Path.IsEmpty() ? Action.Path[0] : Action.Target;
		if (AGridTile* Tile = TileActors.FindRef(Target))
		{
			Tile->ShowActionMarker(FText::FromString(Label), FColor(255,235,150));
			HintMarkers.Add(Target);
		}
	}
	else if (Status == FGridHintSearch::EStatus::NoSolution)
	{
		int32 UndoCount = 0;
		for (int32 Index = UndoHistory.Num() - 1; Index >= 0; --Index)
		{
			if (HintGuide->Find(UndoHistory[Index].PreviousRules)) { UndoCount = UndoHistory.Num() - Index; break; }
		}
		StepHintStatus = UndoCount > 0 ? FText::Format(
			LOCTEXT("UndoHint", "这条路走不通了。按 Z 退回 {0} 步，再按 H 看下一步。"), FText::AsNumber(UndoCount)) :
			LOCTEXT("DeadEnd", "这里已经走不通了。按 Z 退一步，或按 R 重来。");
	}
	else
	{
		StepHintStatus = Status == FGridHintSearch::EStatus::Complete ? LOCTEXT("AlreadyComplete", "两人都到出口了。") :
			LOCTEXT("SearchLimit", "暂时还没找到下一步。可以按 Z 退一步再试。");
	}
	HintSearch.Reset();
	UpdatePrintConnections();
	return false;
}

FText AGridManager::FormatStepHint() const
{
	const FGridHintAction& Action = StepHint.GetValue();
	const FText RoleName = FText::FromString(Action.Role == EPlayerRole::PlayerA ? TEXT("A") : TEXT("B"));
	FText Instruction;
	if (Action.Kind == EGridHintKind::Move)
	{
		const FText Direction = Action.Direction.X > 0 ? LOCTEXT("Right", "右") : Action.Direction.X < 0 ? LOCTEXT("Left", "左") :
			Action.Direction.Y < 0 ? LOCTEXT("Up", "上") : LOCTEXT("Down", "下");
		const FText Key = FText::FromString(Action.Direction.X > 0 ? TEXT("D / →") : Action.Direction.X < 0 ? TEXT("A / ←") :
			Action.Direction.Y < 0 ? TEXT("W / ↑") : TEXT("S / ↓"));
		Instruction = FText::Format(LOCTEXT("MoveHint", "{0} 向{1}走一步（{2}）。"), RoleName, Direction, Key);
		if (Action.Path.Num() > 1)
		{
			Instruction = FText::Format(LOCTEXT("IceHint", "{0} 向{1}移动（{2}），这次会连续滑过 {3} 格。"),
				RoleName, Direction, Key, FText::AsNumber(Action.Path.Num()));
		}
		if (Action.bDeliversWaitingPrint)
		{
		Instruction = FText::Format(LOCTEXT("DeliveryHint", "{0}走完这一步，脚印就会送到。"), Instruction);
		}
	}
	else
	{
		switch (Action.Kind)
		{
		case EGridHintKind::Turn: Instruction = FText::Format(LOCTEXT("TurnHint", "让 {0} 按 E 看桥往哪转，再按一次确认。"), RoleName); break;
		case EGridHintKind::Swap: Instruction = FText::Format(LOCTEXT("SwapHint", "让 {0} 按 E 看换位后的落脚处，再按一次确认。"), RoleName); break;
		case EGridHintKind::Record: Instruction = FText::Format(LOCTEXT("RecordHint", "{0} 按 E 开始录制，再查看下一步提示。"), RoleName); break;
		default: Instruction = FText::Format(LOCTEXT("ReplayHint", "让 {0} 按 E 看回放路线，再按一次跟着走。"), RoleName); break;
		}
	}
	return Action.Role == ActiveRole ? Instruction : FText::Format(
		LOCTEXT("SwitchThenHint", "先按 Tab 切换到 {0}。{1}"), RoleName, Instruction);
}

void AGridManager::TogglePrintLinks()
{
	if (Screen != EGridScreen::Playing && Screen != EGridScreen::Paused) { return; }
	if (PrintConnections.IsEmpty()) { SetFeedback(LOCTEXT("NoPrintLinks", "本关没有脚印传送连线。")); return; }
	PrintLinkMode = (PrintLinkMode + 1) % 3;
	UpdatePrintConnections();
	SetFeedback(GetLinkHelp());
}

FText AGridManager::GetLinkHelp() const
{
	if (Rules.GetPrintEffects().IsEmpty()) { return FText::GetEmpty(); }
	const FText Mode = PrintLinkMode == 0 ? LOCTEXT("RelatedLinks", "相关") :
		PrintLinkMode == 1 ? LOCTEXT("AllLinks", "全部") : LOCTEXT("HiddenLinks", "隐藏");
	return FText::Format(LOCTEXT("LinkLegend", "L 连线 · {0}"), Mode);
}

void AGridManager::UpdatePrintConnections()
{
	if (PrintConnections.IsEmpty()) { return; }
	const auto& Effects = Rules.GetPrintEffects();
	const EPlayerRole FocusRole = StepHint.IsSet() ? StepHint.GetValue().Role : ActiveRole;
	const FIntPoint Focus = Rules.GetPosition(FocusRole);
	const FIntPoint Target = StepHint.IsSet() ? StepHint.GetValue().Target : Focus;
	int32 Selected = INDEX_NONE, Best = MAX_int32;
	for (int32 Index = 0; Index < Effects.Num(); ++Index)
	{
		const FGridPrintEffect& Effect = Effects[Index];
		int32 Rank = MAX_int32;
		if (Rules.IsPrintEffectPending(Index)) { Rank = -2; }
		else if (Index == RecentPrintEffect) { Rank = -1; }
		else if (Effect.Trigger == Target) { Rank = 0; }
		else if (Effect.Trigger == Focus) { Rank = 1; }
		else if (Effect.Links.ContainsByPredicate([&](const FGridPrintLink& Link) { return Link.Source == Focus || Link.Target == Focus || Link.Target == Target; })) { Rank = 2; }
		else if (FMath::Abs(Effect.Trigger.X - Focus.X) + FMath::Abs(Effect.Trigger.Y - Focus.Y) == 1) { Rank = 3; }
		if (Rank == MAX_int32) { continue; }
		if (Rank >= 0 && Rules.IsPrintEffectUsed(Index)) { Rank += 20; }
		if (Rank < Best) { Best = Rank; Selected = Index; }
	}
	RelatedPrintEffect = Selected;
	static const FLinearColor Colors[] = {FLinearColor(.30f,.84f,.84f), FLinearColor(.96f,.72f,.35f),
		FLinearColor(.74f,.60f,.98f), FLinearColor(.95f,.53f,.70f), FLinearColor(.58f,.87f,.45f)};
	TMap<FIntPoint, FString> Badges;
	TMap<FIntPoint, FColor> BadgeColors;
	for (const FPrintConnection& Connection : PrintConnections)
	{
		const int32 Index = Connection.EffectIndex;
		if (!Effects.IsValidIndex(Index)) { continue; }
		const bool bVisible = PrintLinkMode == 1 || (PrintLinkMode == 0 && Index == Selected);
		const bool bSpent = Rules.IsPrintEffectUsed(Index) && !Rules.IsPrintEffectPending(Index) && Index != RecentPrintEffect;
		const FLinearColor Color = Colors[Index % UE_ARRAY_COUNT(Colors)] * (bSpent ? .42f : 1.f);
		for (const TWeakObjectPtr<UStaticMeshComponent>& Piece : Connection.Pieces)
		{
			if (!Piece.IsValid()) { continue; }
			Piece->SetVisibility(bVisible);
			if (bVisible) { GridVisuals::Paint(Piece.Get(), Color, bSpent ? .18f : 1.f); }
		}
		if (!bVisible) { continue; }
		TSet<FIntPoint> Endpoints;
		Endpoints.Add(Effects[Index].Trigger);
		for (const FGridPrintLink& Link : Effects[Index].Links) { Endpoints.Add(Link.Source); Endpoints.Add(Link.Target); }
		for (FIntPoint Position : Endpoints)
		{
			FString& Label = Badges.FindOrAdd(Position);
			if (!Label.IsEmpty()) { Label += TEXT("/"); BadgeColors.Add(Position, FColor(230,238,240)); }
			else { BadgeColors.Add(Position, Color.ToFColor(true)); }
			Label += FString::Printf(TEXT("P%d"), Index + 1);
		}
	}
	for (const auto& Pair : TileActors)
	{
		const FString* Label = Badges.Find(Pair.Key);
		Pair.Value->ShowLinkBadge(Label ? *Label : FString(), Label ? BadgeColors.FindChecked(Pair.Key) : FColor::White);
	}
}

FText AGridManager::GetPrintConditionHint() const
{
	const auto& Effects = Rules.GetPrintEffects();
	if (LevelIndex != 2 || !Effects.IsValidIndex(RelatedPrintEffect) || Rules.IsPrintEffectUsed(RelatedPrintEffect)) { return FText::GetEmpty(); }
	const FGridPrintEffect& Effect = Effects[RelatedPrintEffect];
	for (const FGridBridgeCondition& Requirement : Effect.Requirements)
	{
		if (Rules.IsBridgeConditionMet(Requirement)) { continue; }
		const bool bBlue = Requirement.RequiredRole == EPlayerRole::PlayerA;
		return FText::Format(LOCTEXT("WaitingForPrint", "P{0} 还在等 {1} 留下{2}脚印。"), FText::AsNumber(RelatedPrintEffect + 1),
			FText::FromString(bBlue ? TEXT("A") : TEXT("B")), bBlue ? LOCTEXT("BluePrint", "蓝色") : LOCTEXT("OrangePrint", "橙色"));
	}
	return FText::GetEmpty();
}

void AGridManager::ClearPrintTrails()
{
	GetWorldTimerManager().ClearTimer(PrintTrailTimer);
	for (const FPrintTrail& Trail : PrintTrails)
	{
		if (Trail.Light.IsValid()) { Trail.Light->DestroyComponent(); }
	}
	PrintTrails.Reset();
}

void AGridManager::PresentPrintTransfers(const FGridMove& MoveResult)
{
	const auto& Effects = Rules.GetPrintEffects();
	const auto AddTrail = [&](const FGridPrintTransfer& Transfer, bool bArrived)
	{
		// 事件记录触发格，光点从实际取色格出发。
		const int32 EffectIndex = Effects.IndexOfByPredicate([&Transfer](const FGridPrintEffect& Effect)
		{
			return Effect.Trigger == Transfer.From && Effect.Links.ContainsByPredicate([&Transfer](const FGridPrintLink& Link)
			{
				return Link.Target == Transfer.To;
			});
		});
		if (EffectIndex == INDEX_NONE) { return; }
		if (bArrived) { RecentPrintEffect = EffectIndex; }
		const FGridPrintLink* Link = Effects[EffectIndex].Links.FindByPredicate([&Transfer](const FGridPrintLink& Candidate)
		{
			return Candidate.Target == Transfer.To;
		});
		AGridTile* Origin = TileActors.FindRef(Link->Source);
		if (!Origin) { return; }
		const EPlayerRole Owner = Transfer.Owner == EFootprintOwner::PlayerA ? EPlayerRole::PlayerA : EPlayerRole::PlayerB;
		UStaticMeshComponent* Light = GridVisuals::Shape(*Origin, Origin->GetRootComponent(), PaletteMaterial,
			EGridShape::Sphere, FVector(0,0,34), FVector(.12f), GridVisuals::RoleColor(Owner), 2.4f);
		const FVector From = ToWorld(Link->Source);
		TArray<FVector> Points = GridVisuals::PrintLinkPath(ToWorld(Transfer.To) - From, EffectIndex);
		for (FVector& Point : Points) { Point += From + FVector(0,0,5); }
		Light->SetWorldLocation(Points[0]);
		PrintTrails.Add({Light, MoveTemp(Points), Transfer.To, float(GetWorld()->GetTimeSeconds()), bArrived});
	};
	for (const FGridPrintTransfer& Arrived : MoveResult.PrintArrivals) { AddTrail(Arrived, true); }
	for (const FGridPrintTransfer& Sent : MoveResult.PrintDispatches)
	{
		if (!MoveResult.PrintArrivals.ContainsByPredicate([&Sent](const FGridPrintTransfer& Arrived)
			{ return Sent.From == Arrived.From && Sent.To == Arrived.To && Sent.Owner == Arrived.Owner; })) { AddTrail(Sent, false); }
	}
	if (!PrintTrails.IsEmpty() && !GetWorldTimerManager().IsTimerActive(PrintTrailTimer))
	{
		GetWorldTimerManager().SetTimer(PrintTrailTimer, this, &AGridManager::AnimatePrintTrails, .025f, true);
	}
}

void AGridManager::AnimatePrintTrails()
{
	const float Now = GetWorld()->GetTimeSeconds();
	for (int32 Index = PrintTrails.Num() - 1; Index >= 0; --Index)
	{
		const FPrintTrail& Trail = PrintTrails[Index];
		const float Alpha = FMath::Clamp((Now - Trail.StartedAt) / .35f, 0.f, 1.f);
		if (Trail.Light.IsValid())
		{
			const float Travel = Trail.bArrived ? Alpha : .45f * Alpha;
			const float Along = Travel * (Trail.Points.Num() - 1);
			const int32 Segment = FMath::Min(FMath::FloorToInt(Along), Trail.Points.Num() - 2);
			Trail.Light->SetWorldLocation(FMath::Lerp(Trail.Points[Segment], Trail.Points[Segment + 1], Along - Segment));
		}
		if (Alpha >= 1.f && Trail.bArrived)
		{
			if (AGridTile* Tile = TileActors.FindRef(Trail.To)) { Tile->PulseTile(); }
		}
		if (Alpha >= 1.f)
		{
			if (Trail.Light.IsValid()) { Trail.Light->DestroyComponent(); }
			PrintTrails.RemoveAtSwap(Index);
		}
	}
	if (PrintTrails.IsEmpty()) { GetWorldTimerManager().ClearTimer(PrintTrailTimer); }
}

#undef LOCTEXT_NAMESPACE
