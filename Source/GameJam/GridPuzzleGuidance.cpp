#include "GridPuzzleGame.h"

#include "GridPuzzleVisuals.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"

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
			LOCTEXT("UndoHint", "当前局面已走不通。按 Z 撤销 {0} 次，再查看下一步提示。"), FText::AsNumber(UndoCount)) :
			LOCTEXT("DeadEnd", "当前局面没有可行路线，可以撤销或重新开始。");
	}
	else
	{
		StepHintStatus = Status == FGridHintSearch::EStatus::Complete ? LOCTEXT("AlreadyComplete", "两人均已到达出口。") :
			LOCTEXT("SearchLimit", "暂时没找到后续走法，可以撤销后再试。尚未确定这个局面是否走得通。");
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
			Instruction = FText::Format(LOCTEXT("DeliveryHint", "{0}这一步会让等待中的脚印送达。"), Instruction);
		}
	}
	else
	{
		switch (Action.Kind)
		{
		case EGridHintKind::Turn: Instruction = FText::Format(LOCTEXT("TurnHint", "{0} 按两次 E 转桥：先预览，再确认。"), RoleName); break;
		case EGridHintKind::Swap: Instruction = FText::Format(LOCTEXT("SwapHint", "{0} 按两次 E 换位：先预览，再确认。"), RoleName); break;
		case EGridHintKind::Record: Instruction = FText::Format(LOCTEXT("RecordHint", "{0} 按 E 开始录制，再查看下一步提示。"), RoleName); break;
		default: Instruction = FText::Format(LOCTEXT("ReplayHint", "{0} 按两次 E 回放：先预览，再确认。"), RoleName); break;
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
	if (Rules.GetPrintEffects().IsEmpty()) { return LOCTEXT("HintOnlyHelp", "F1 下一步提示"); }
	const FText Mode = PrintLinkMode == 0 ? LOCTEXT("RelatedLinks", "相关") :
		PrintLinkMode == 1 ? LOCTEXT("AllLinks", "全部") : LOCTEXT("HiddenLinks", "隐藏");
	return FText::Format(LOCTEXT("LinkLegend", "F1 下一步提示　L 连线：{0}　虚线取色 · 箭头送达 · 同号为同组"), Mode);
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
		if (Effect.Trigger == Target) { Rank = 0; }
		else if (Effect.Trigger == Focus) { Rank = 1; }
		else if (Effect.Links.ContainsByPredicate([&](const FGridPrintLink& Link) { return Link.Source == Focus || Link.Target == Focus || Link.Target == Target; })) { Rank = 2; }
		else if (FMath::Abs(Effect.Trigger.X - Focus.X) + FMath::Abs(Effect.Trigger.Y - Focus.Y) == 1) { Rank = 3; }
		else if (Rules.IsPrintEffectPending(Index)) { Rank = 4; }
		if (Rank == MAX_int32) { continue; }
		if (Rules.IsPrintEffectUsed(Index) && !Rules.IsPrintEffectPending(Index)) { Rank += 20; }
		if (Rank < Best) { Best = Rank; Selected = Index; }
	}
	static const FLinearColor Colors[] = {FLinearColor(.30f,.84f,.84f), FLinearColor(.96f,.72f,.35f),
		FLinearColor(.74f,.60f,.98f), FLinearColor(.95f,.53f,.70f), FLinearColor(.58f,.87f,.45f)};
	TMap<FIntPoint, FString> Badges;
	TMap<FIntPoint, FColor> BadgeColors;
	for (const FPrintConnection& Connection : PrintConnections)
	{
		const int32 Index = Connection.EffectIndex;
		if (!Effects.IsValidIndex(Index)) { continue; }
		const bool bVisible = PrintLinkMode == 1 || (PrintLinkMode == 0 && Index == Selected);
		const bool bSpent = Rules.IsPrintEffectUsed(Index) && !Rules.IsPrintEffectPending(Index);
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

#undef LOCTEXT_NAMESPACE
