#include "GridPuzzleHints.h"

bool FGridPuzzleRules::IsPrintEffectPending(int32 Index) const
{
	return PendingPrints.ContainsByPredicate([Index](const FPendingPrint& Pending) { return Pending.EffectIndex == Index; });
}

FString FGridPuzzleRules::GetHintStateKey() const
{
	FString Key;
	Key.Reserve(384);
	Key += FString::Printf(TEXT("%d,%d,%d,%d:%d%d%d%d|"), Positions[0].X, Positions[0].Y,
		Positions[1].X, Positions[1].Y, Finished[0], Finished[1], bRecording, bAlternateDeck);
	TArray<FIntPoint> Coordinates;
	Tiles.GetKeys(Coordinates);
	Coordinates.Sort([](FIntPoint A, FIntPoint B) { return A.Y == B.Y ? A.X < B.X : A.Y < B.Y; });
	bool bHasRhythm = false;
	for (FIntPoint Position : Coordinates)
	{
		const FTileData& Tile = Tiles.FindChecked(Position);
		const int32 Flags = (Tile.bFreshPrint ? 1 : 0) | (Tile.bIsCollapsed ? 2 : 0) |
			(Tile.bIsLocked ? 4 : 0) | (Tile.bIsActivated ? 8 : 0) | (Tile.bFoldedAway ? 16 : 0) | (Tile.bCrate ? 32 : 0);
		Key.AppendChar(TEXT('A') + Flags);
		Key.AppendChar(TEXT('0') + int32(Tile.FootprintOwner));
		Key.AppendChar(TEXT('A') + Tile.RemainingMoves);
		bHasRhythm |= Tile.bRhythm;
	}
	Key += FString::Printf(TEXT("|%d,%d|"), bHasRhythm ? MoveCount % RhythmPeriod : 0,
		PatrolRoute.IsEmpty() ? 0 : MoveCount % PatrolRoute.Num());
	for (int32 Index = 0; Index < PrintEffects.Num(); ++Index) { Key.AppendChar(UsedPrintEffects.Contains(Index) ? TEXT('1') : TEXT('0')); }
	for (const FPendingPrint& Pending : PendingPrints)
	{
		Key += FString::Printf(TEXT(";%d,%d:"), Pending.EffectIndex, Pending.Steps);
		for (const FPrintWrite& Write : Pending.Writes)
		{
			Key += FString::Printf(TEXT("%d,%d,%d,%d/"), Write.Target.X, Write.Target.Y, int32(Write.Owner), Write.bFresh);
		}
	}
	Key += TEXT("|");
	for (FIntPoint Direction : Recording) { Key += FString::Printf(TEXT("%d,%d;"), Direction.X, Direction.Y); }
	return Key;
}

bool FGridHintSearch::Apply(FGridPuzzleRules& Rules, FGridHintAction& Action)
{
	Action.Path.Reset();
	Action.bDeliversWaitingPrint = false;
	Action.Target = Rules.GetPosition(Action.Role);
	switch (Action.Kind)
	{
	case EGridHintKind::Move:
	{
		const FGridMove Move = Rules.TryMove(Action.Role, Action.Direction);
		if (Move.Result != EGridMoveResult::Moved) { return false; }
		Action.Target = Move.To;
		Action.Path = Move.Path;
		Action.bDeliversWaitingPrint = Move.PrintArrivals.ContainsByPredicate([&Move](const FGridPrintTransfer& Arrived)
		{
			return !Move.PrintDispatches.ContainsByPredicate([&Arrived](const FGridPrintTransfer& Sent)
			{
				return Sent.From == Arrived.From && Sent.To == Arrived.To && Sent.Owner == Arrived.Owner;
			});
		});
		return true;
	}
	case EGridHintKind::Turn:
	{
		if (Rules.CanSwap()) { return false; }
		TArray<FIntPoint> Changed;
		return Rules.TryTurnBridge(Action.Role, Changed);
	}
	case EGridHintKind::Swap:
	{
		FGridMove Move;
		return Rules.TrySwap(Action.Role, Move);
	}
	case EGridHintKind::Record:
		return !Rules.IsAtTurnControl(Action.Role) && Rules.StartRecording(Action.Role);
	case EGridHintKind::Replay:
	{
		if (Rules.IsAtTurnControl(Action.Role)) { return false; }
		TArray<FGridMove> Moves;
		TArray<FGridPuzzleRules> States;
		if (!Rules.PlanReplay(Action.Role, Moves, States) || States.IsEmpty()) { return false; }
		for (const FGridMove& Move : Moves) { Action.Path.Append(Move.Path); }
		Rules = MoveTemp(States.Last());
		return true;
	}
	}
	return false;
}

TSharedPtr<FGridHintGuide> FGridHintGuide::Build(const FGridPuzzleRules& Initial, int32 CampaignIndex)
{
	auto Guide = MakeShared<FGridHintGuide>();
	static const TCHAR* Witnesses[] = {
		TEXT("AR AR BL BR AR AU BL BL AD AR AR BL BL BL"),
		TEXT("AR AR BL AU BL BL BL AD AR BR BU AL BU AL BD BD BD AR AU AU AU AR AR AR BD BD"),
		TEXT("AR AR AR AD AD AR BR BR BR BR BU BU BU AL AD AD AR AR AU BU BR BR BD BD BL AU AU AU AR AR AU BR BD BD BR BR BD AR AL AR AR BR BR"),
		TEXT("AR AR BR BR AR AE AR AR AU AR AD AR AR AD AU AE BR BR BE BR BR BR AE AD AU BR BR AL AL AL AL AL AL AL AL AU"),
		TEXT("AR AR AR BR BR BR BR AR BR BU BU BU AR AE AD AD AD BE BR AR AR AR BR AD AD BR BU BU"),
		TEXT("AR AR AR AE BR BD BD BE BD AL AU"),
		TEXT("AE AR AR AU AU BU BE BR"),
		TEXT("AR BL AE AR AR BL BL"),
		TEXT("AR AR AU AR BL BL AD AR AR BD"),
		TEXT("BL BR AR AU BL BL AD AR AR BL"),
		TEXT("AR BL AR BR AR AU AR BL AD BL AR BL BL BL"),
		TEXT("AR BL AR BR AR AU BL BL AD BL BL AR AR BL")
	};
	if (CampaignIndex < 0 || CampaignIndex >= UE_ARRAY_COUNT(Witnesses)) { return Guide; }
	FGridPuzzleRules Trial = Initial;
	TArray<FString> Tokens, Keys;
	TArray<FGridHintAction> Actions;
	FString(Witnesses[CampaignIndex]).ParseIntoArrayWS(Tokens);
	for (const FString& Token : Tokens)
	{
		Keys.Add(Trial.GetHintStateKey());
		FGridHintAction Action;
		Action.Role = Token[0] == 'A' ? EPlayerRole::PlayerA : EPlayerRole::PlayerB;
		if (Token[1] != 'E')
		{
			Action.Direction = Token[1] == 'R' ? FIntPoint(1,0) : Token[1] == 'L' ? FIntPoint(-1,0) :
				Token[1] == 'U' ? FIntPoint(0,-1) : FIntPoint(0,1);
			if (!FGridHintSearch::Apply(Trial, Action)) { return Guide; }
		}
		else
		{
			bool bApplied = false;
			for (EGridHintKind Kind : {EGridHintKind::Swap, EGridHintKind::Turn, EGridHintKind::Record, EGridHintKind::Replay})
			{
				FGridPuzzleRules Next = Trial;
				Action.Kind = Kind;
				if (!FGridHintSearch::Apply(Next, Action)) { continue; }
				Trial = MoveTemp(Next);
				bApplied = true;
				break;
			}
			if (!bApplied) { return Guide; }
		}
		Actions.Add(MoveTemp(Action));
	}
	// 编辑过的地图若不能走完整条参考路线，就不使用这份缓存。
	if (!Trial.IsComplete()) { return Guide; }
	TSet<FString> Cached;
	for (int32 Index = Actions.Num() - 1; Index >= 0; --Index)
	{
		if (Cached.Contains(Keys[Index])) { continue; }
		Cached.Add(Keys[Index]);
		Guide->Add(Keys[Index], Actions[Index]);
	}
	return Guide;
}

const FGridHintAction* FGridHintGuide::Find(const FGridPuzzleRules& Rules) const
{
	return Steps.Find(Rules.GetHintStateKey());
}

FGridHintSearch::FGridHintSearch(const FGridPuzzleRules& Current, TSharedPtr<FGridHintGuide> InGuide, int32 InNodeLimit)
	: Guide(MoveTemp(InGuide)), NodeLimit(InNodeLimit), StartedAt(FPlatformTime::Seconds())
{
	if (!Guide) { Guide = MakeShared<FGridHintGuide>(); }
	FNode Start;
	Start.Rules = Current;
	Seen.Add(Current.GetHintStateKey());
	Nodes.Add(MoveTemp(Start));
}

void FGridHintSearch::Enqueue(FGridPuzzleRules&& Rules, int32 Parent, const FGridHintAction& Action)
{
	FString Key = Rules.GetHintStateKey();
	if (Seen.Contains(Key)) { return; }
	if (Nodes.Num() >= NodeLimit) { Status = EStatus::LimitReached; return; }
	Seen.Add(MoveTemp(Key));
	FNode Next;
	Next.Rules = MoveTemp(Rules);
	Next.Parent = Parent;
	Next.Action = Action;
	Nodes.Add(MoveTemp(Next));
}

void FGridHintSearch::Finish(int32 Index, const FGridHintAction* Suffix)
{
	FGridHintAction NextAction = Suffix ? *Suffix : Nodes[Index].Action;
	if (Suffix) { Guide->Add(Nodes[Index].Rules.GetHintStateKey(), NextAction); }
	while (Nodes[Index].Parent != INDEX_NONE)
	{
		NextAction = Nodes[Index].Action;
		Index = Nodes[Index].Parent;
		Guide->Add(Nodes[Index].Rules.GetHintStateKey(), NextAction);
	}
	Result = NextAction;
	Status = EStatus::Found;
}

FGridHintSearch::EStatus FGridHintSearch::Advance(double Seconds)
{
	if (Status != EStatus::Searching) { return Status; }
	const double Deadline = FPlatformTime::Seconds() + Seconds;
	do
	{
		if (Head >= Nodes.Num()) { Status = EStatus::NoSolution; break; }
		if (FPlatformTime::Seconds() - StartedAt > 5.0) { Status = EStatus::LimitReached; break; }
		const int32 Index = Head++;
		// 扩展时复制当前节点，避免 Enqueue 导致数组搬迁后引用失效。
		const FGridPuzzleRules Current = Nodes[Index].Rules;
		if (Current.IsComplete())
		{
			if (Index == 0) { Status = EStatus::Complete; }
			else { Finish(Index, nullptr); }
			break;
		}
		if (const FGridHintAction* Cached = Guide->Find(Current)) { Finish(Index, Cached); break; }
		for (EPlayerRole Role : {EPlayerRole::PlayerA, EPlayerRole::PlayerB})
		{
			for (FIntPoint Direction : {FIntPoint(1,0), FIntPoint(0,-1), FIntPoint(-1,0), FIntPoint(0,1)})
			{
				FGridPuzzleRules Next = Current;
				FGridHintAction Action;
				Action.Role = Role;
				Action.Direction = Direction;
				if (Apply(Next, Action)) { Enqueue(MoveTemp(Next), Index, Action); }
			}
			for (EGridHintKind Kind : {EGridHintKind::Swap, EGridHintKind::Turn, EGridHintKind::Record, EGridHintKind::Replay})
			{
				FGridPuzzleRules Next = Current;
				FGridHintAction Action;
				Action.Role = Role;
				Action.Kind = Kind;
				if (Apply(Next, Action)) { Enqueue(MoveTemp(Next), Index, Action); }
			}
		}
	} while (Status == EStatus::Searching && FPlatformTime::Seconds() < Deadline);
	return Status;
}
