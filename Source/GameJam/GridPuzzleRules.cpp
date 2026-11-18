#include "GridPuzzleRules.h"

#include "Algo/AllOf.h"

bool FGridPuzzleRules::Initialize(const UGridLevelDataAsset& Level)
{
	FGridPuzzleRules Initial;
	Initial.FragileLifetime = FMath::Max(1, Level.FragileLifetime);
	Initial.bRequireBorrowedPrints = Level.bRequireBorrowedPrints;
	Initial.Size.Y = Level.Rows.Num();
	int32 Starts[2] = {};
	int32 Goals[2] = {};
	int32 Switches = 0;
	bool bHasLockedGoal = false;
	TSet<FString> BridgeMarkers;

	for (int32 Y = 0; Y < Level.Rows.Num(); ++Y)
	{
		const FString& Row = Level.Rows[Y];
		Initial.Size.X = FMath::Max(Initial.Size.X, Row.Len());
		for (int32 X = 0; X < Row.Len(); ++X)
		{
			const TCHAR Symbol = Row[X];
			if (Symbol == ' ')
			{
				continue;
			}
			const FIntPoint Position(X, Y);
			FTileData& Tile = Initial.Tiles.Add(Position);
			Tile.GridPosition = Position;
			switch (Symbol)
			{
			case '.': break;
			case '#': Tile.TileType = ETileType::Obstacle; break;
			case 'm': Tile.TileType = ETileType::Memory; break;
			case 'o': case 'u':
				Tile.TileType = ETileType::Memory;
				Tile.FootprintOwner = Symbol == 'o' ? EFootprintOwner::PlayerB : EFootprintOwner::PlayerA;
				break;
			case 'c': case 'd':
				Tile.TileType = ETileType::SingleUse;
				Tile.FootprintOwner = Symbol == 'c' ? EFootprintOwner::PlayerB : EFootprintOwner::PlayerA;
				break;
			case 'I': case 'J':
				Tile.TileType = ETileType::Imprint;
				Tile.Marker.AppendChar(Symbol);
				break;
			case 'P': case 'Q':
				Tile.TileType = ETileType::Plate;
				Tile.Marker.AppendChar(Symbol);
				break;
			case 'G': case 'H': case 'K':
				Tile.TileType = ETileType::Bridge;
				Tile.Marker.AppendChar(Symbol);
				BridgeMarkers.Add(Tile.Marker);
				break;
			case 'f':
				Tile.TileType = ETileType::Fragile;
				Tile.RemainingMoves = Initial.FragileLifetime;
				if (Initial.bRequireBorrowedPrints)
				{
					Tile.FootprintOwner = EFootprintOwner::PlayerB;
				}
				break;
			case 'A': case 'B':
			{
				const int32 Index = Symbol == 'A' ? 0 : 1;
				++Starts[Index];
				Initial.Positions[Index] = Position;
				Tile.bIsOccupied = true;
				break;
			}
			case 's': case 't':
				Tile.TileType = ETileType::Switch;
				Tile.SwitchRole = Symbol == 's' ? EPlayerRole::PlayerA : EPlayerRole::PlayerB;
				++Switches;
				break;
			case 'a': case 'b': case 'x': case 'y':
				Tile.TileType = ETileType::Goal;
				Tile.GoalOwner = Symbol == 'a' || Symbol == 'x' ? EPlayerRole::PlayerA : EPlayerRole::PlayerB;
				Tile.bIsLocked = Symbol == 'x' || Symbol == 'y';
				bHasLockedGoal |= Tile.bIsLocked;
				++Goals[RoleIndex(Tile.GoalOwner)];
				break;
			default:
				return false;
			}
		}
	}

	if (Starts[0] != 1 || Starts[1] != 1 || Goals[0] != 1 || Goals[1] != 1 ||
		(bHasLockedGoal && Switches == 0))
	{
		return false;
	}

	TMap<FIntPoint, EPlayerRole> PlateRoles;
	for (const FGridBridgeDefinition& Bridge : Level.Bridges)
	{
		if (BridgeMarkers.Remove(Bridge.Marker) != 1 || Bridge.Conditions.IsEmpty())
		{
			return false;
		}
		TSet<FIntPoint> Sources;
		for (const FGridBridgeCondition& Condition : Bridge.Conditions)
		{
			FTileData* Source = Initial.Tiles.Find(Condition.Source);
			if (!Source || (Source->TileType != ETileType::Plate && Source->TileType != ETileType::Imprint) ||
				Sources.Contains(Condition.Source) ||
				(Condition.RequiredRole != EPlayerRole::PlayerA && Condition.RequiredRole != EPlayerRole::PlayerB))
			{
				return false;
			}
			Sources.Add(Condition.Source);
			if (Source->TileType == ETileType::Plate)
			{
				const EPlayerRole* AssignedRole = PlateRoles.Find(Condition.Source);
				if (AssignedRole && *AssignedRole != Condition.RequiredRole)
				{
					return false;
				}
				PlateRoles.Add(Condition.Source, Condition.RequiredRole);
				Source->SwitchRole = Condition.RequiredRole;
			}
		}
		Initial.Bridges.Add(Bridge.Marker, Bridge.Conditions);
	}
	if (!BridgeMarkers.IsEmpty())
	{
		return false;
	}

	for (const FGridPrintEffect& Effect : Level.PrintEffects)
	{
		FTileData* Trigger = Initial.Tiles.Find(Effect.Trigger);
		if (!Trigger || !Trigger->HasFootprints() || Effect.Links.IsEmpty() || Effect.DelayMoves < 0)
		{
			return false;
		}
		Trigger->bPrintTrigger = true;
		TSet<FIntPoint> Sources;
		TSet<FIntPoint> Targets;
		for (const FGridPrintLink& Link : Effect.Links)
		{
			FTileData* Source = Initial.Tiles.Find(Link.Source);
			FTileData* Target = Initial.Tiles.Find(Link.Target);
			if (!Source || !Source->HasFootprints() || !Target ||
				(Target->TileType != ETileType::Memory && Target->TileType != ETileType::Imprint) ||
				Link.Source == Link.Target || Targets.Contains(Link.Target) ||
				(Effect.bMovePrint && (Source->TileType != ETileType::Memory || Sources.Contains(Link.Source))))
			{
				return false;
			}
			Sources.Add(Link.Source);
			Targets.Add(Link.Target);
			Target->bPrintReceiver = true;
		}
		if (Effect.bMovePrint && Sources.Intersect(Targets).Num() > 0)
		{
			return false;
		}
		for (const FGridBridgeCondition& Requirement : Effect.Requirements)
		{
			const FTileData* Source = Initial.Tiles.Find(Requirement.Source);
			if (!Source || !Source->HasFootprints())
			{
				return false;
			}
		}
	}
	Initial.PrintEffects = Level.PrintEffects;
	if (!Initial.InitializePrototype(Level)) { return false; }
	*this = MoveTemp(Initial);
	return true;
}

bool FGridPuzzleRules::InitializePrototype(const UGridLevelDataAsset& Level)
{
	Prototype = Level.Prototype;
	const auto IsMemory = [this](FIntPoint Position)
	{
		const FTileData* Tile = Tiles.Find(Position);
		return Tile && Tile->TileType == ETileType::Memory && !Tile->bPrintTrigger;
	};
	switch (Prototype)
	{
	case EGridPrototype::None:
		return true;
	case EGridPrototype::TurnBridgeAndSwap:
	case EGridPrototype::TurnBridge:
	{
		if (Level.TurnControls.IsEmpty() || Level.InitialDeck.IsEmpty() ||
			Level.InitialDeck.Num() != Level.AlternateDeck.Num()) { return false; }
		for (FIntPoint Control : Level.TurnControls)
		{
			FTileData* Tile = Tiles.Find(Control);
			if (!Tile || (Tile->TileType != ETileType::Normal && Tile->TileType != ETileType::Switch &&
				Tile->TileType != ETileType::Memory)) { return false; }
			Tile->InteractionLabel = TEXT("TURN");
		}
		bool bClockwise = true;
		bool bCounterclockwise = true;
		for (int32 Index = 0; Index < Level.InitialDeck.Num(); ++Index)
		{
			const FIntPoint Start = Level.InitialDeck[Index];
			const FIntPoint End = Level.AlternateDeck[Index];
			const FIntPoint Offset = Start - Level.TurnPivot;
			bClockwise &= End - Level.TurnPivot == FIntPoint(-Offset.Y, Offset.X);
			bCounterclockwise &= End - Level.TurnPivot == FIntPoint(Offset.Y, -Offset.X);
			if (!IsMemory(Start) || Tiles.FindChecked(Start).bPrintReceiver || InitialDeck.Contains(Start) ||
				Tiles.Contains(End) || (!bClockwise && !bCounterclockwise) ||
				End.X < 0 || End.Y < 0 || End.X >= Size.X || End.Y >= Size.Y) { return false; }
			Tiles.FindChecked(Start).InteractionLabel = TEXT("TURN");
			FTileData& Slot = Tiles.Add(End);
			Slot.GridPosition = End;
			Slot.TileType = ETileType::Memory;
			Slot.InteractionLabel = TEXT("TURN");
			Slot.bFoldedAway = true;
			InitialDeck.Add(Start);
			AlternateDeck.Add(End);
		}
		TurnControls = Level.TurnControls;
		break;
	}
	case EGridPrototype::RecordRoute:
		if (!Algo::AllOf(Level.ReplayTiles, IsMemory) || Level.ReplayTiles.Num() < RecordedLength ||
			!Tiles.Contains(Level.RecordStart) || !Tiles.Contains(Level.ReplayStart)) { return false; }
		ReplayTiles = Level.ReplayTiles;
		RecordStart = Level.RecordStart;
		ReplayStart = Level.ReplayStart;
		Tiles.FindChecked(RecordStart).InteractionLabel = RecordStart == ReplayStart ? TEXT("REC / PLAY") : TEXT("REC");
		if (RecordStart != ReplayStart) { Tiles.FindChecked(ReplayStart).InteractionLabel = TEXT("PLAY"); }
		for (FIntPoint Position : ReplayTiles) { Tiles.FindChecked(Position).InteractionLabel = TEXT("PLAY"); }
		return true;
	case EGridPrototype::SwapPlaces:
		break;
	default:
		return false;
	}
	if (Prototype == EGridPrototype::SwapPlaces || Prototype == EGridPrototype::TurnBridgeAndSwap)
	{
		if (Level.SwapTiles.Num() != 2 || !Algo::AllOf(Level.SwapTiles, IsMemory)) { return false; }
		const FIntPoint Delta = Level.SwapTiles[0] - Level.SwapTiles[1];
		if (FMath::Abs(Delta.X) + FMath::Abs(Delta.Y) != 1) { return false; }
		SwapTiles = Level.SwapTiles;
		for (FIntPoint Position : SwapTiles)
		{
			Tiles.FindChecked(Position).InteractionLabel = TurnControls.Contains(Position) ? TEXT("SWAP / TURN") : TEXT("SWAP");
		}
	}
	return true;
}

bool FGridPuzzleRules::IsBridgeConditionMet(const FGridBridgeCondition& Condition) const
{
	const FTileData& Source = Tiles.FindChecked(Condition.Source);
	if (Source.bFoldedAway) { return false; }
	if (Source.TileType == ETileType::Plate)
	{
		return Positions[RoleIndex(Condition.RequiredRole)] == Condition.Source;
	}
	const EFootprintOwner RequiredOwner = Condition.RequiredRole == EPlayerRole::PlayerA ?
		EFootprintOwner::PlayerA : EFootprintOwner::PlayerB;
	return Source.FootprintOwner == RequiredOwner;
}

FGridMove FGridPuzzleRules::TryMove(EPlayerRole Role, FIntPoint Direction)
{
	FGridMove Move = MoveCharacter(Role, Direction, false);
	if (Move.Result == EGridMoveResult::Moved && bRecording && Role == EPlayerRole::PlayerA)
	{
		Recording.Add(Direction);
		bRecording = Recording.Num() < RecordedLength;
	}
	return Move;
}

bool FGridPuzzleRules::StartRecording(EPlayerRole Role)
{
	if (Prototype != EGridPrototype::RecordRoute || Role != EPlayerRole::PlayerA ||
		IsFinished(Role) || GetPosition(Role) != RecordStart) { return false; }
	Recording.Reset();
	bRecording = true;
	return true;
}

bool FGridPuzzleRules::CanReplayFrom(EPlayerRole Role) const
{
	return Prototype == EGridPrototype::RecordRoute && Role == EPlayerRole::PlayerB && !IsFinished(Role) &&
		GetPosition(Role) == ReplayStart && Recording.Num() == RecordedLength;
}

bool FGridPuzzleRules::PlanReplay(EPlayerRole Role, TArray<FGridMove>& Steps, TArray<FGridPuzzleRules>& States) const
{
	Steps.Reset();
	States.Reset();
	if (!CanReplayFrom(Role)) { return false; }
	FGridPuzzleRules Trial = *this;
	for (const FIntPoint Direction : Recording)
	{
		FGridMove& Step = Steps.Add_GetRef(Trial.MoveCharacter(Role, Direction, true));
		if (Step.Result != EGridMoveResult::Moved)
		{
			States.Reset();
			return false;
		}
		States.Add(Trial);
	}
	return true;
}

bool FGridPuzzleRules::IsAtTurnControl(EPlayerRole Role) const
{
	return !IsFinished(Role) && TurnControls.Contains(GetPosition(Role));
}

bool FGridPuzzleRules::TryTurnBridge(EPlayerRole Role, TArray<FIntPoint>& Changed)
{
	Changed.Reset();
	if (!IsAtTurnControl(Role))
	{
		return false;
	}
	const TArray<FIntPoint>& From = bAlternateDeck ? AlternateDeck : InitialDeck;
	const TArray<FIntPoint>& To = GetNextDeck();
	if (!Algo::AllOf(From, [this](FIntPoint Position) { return !Tiles.FindChecked(Position).bIsOccupied; }))
	{
		return false;
	}
	for (int32 Index = 0; Index < From.Num(); ++Index)
	{
		FTileData& Previous = Tiles.FindChecked(From[Index]);
		FTileData& Next = Tiles.FindChecked(To[Index]);
		Next.FootprintOwner = Previous.FootprintOwner;
		Next.bFreshPrint = Previous.bFreshPrint;
		Next.bFoldedAway = false;
		Previous.FootprintOwner = EFootprintOwner::None;
		Previous.bFreshPrint = false;
		Previous.bFoldedAway = true;
		Changed.Add(From[Index]);
		Changed.Add(To[Index]);
	}
	bAlternateDeck = !bAlternateDeck;
	return true;
}

bool FGridPuzzleRules::CanSwap() const
{
	if (Finished[0] || Finished[1] || !SwapTiles.Contains(Positions[0]) || !SwapTiles.Contains(Positions[1]))
	{
		return false;
	}
	const FTileData& Blue = Tiles.FindChecked(Positions[0]);
	const FTileData& Orange = Tiles.FindChecked(Positions[1]);
	return Blue.bFreshPrint && Blue.FootprintOwner == EFootprintOwner::PlayerA &&
		Orange.bFreshPrint && Orange.FootprintOwner == EFootprintOwner::PlayerB;
}

bool FGridPuzzleRules::TrySwap(EPlayerRole Role, FGridMove& Move)
{
	if (!CanSwap()) { return false; }
	FTileData& Blue = Tiles.FindChecked(Positions[0]);
	FTileData& Orange = Tiles.FindChecked(Positions[1]);
	Move = FGridMove();
	const int32 RoleSlot = RoleIndex(Role);
	Move.From = Positions[RoleSlot];
	Swap(Positions[0], Positions[1]);
	Move.To = Positions[RoleSlot];
	Move.Result = EGridMoveResult::Moved;
	Move.bFootprintTransferred = true;
	Move.ChangedTiles = { Move.From, Move.To };
	Swap(Blue.FootprintOwner, Orange.FootprintOwner);
	++MoveCount;
	const EFootprintOwner Owner = Role == EPlayerRole::PlayerA ? EFootprintOwner::PlayerA : EFootprintOwner::PlayerB;
	ResolveStep(Move, Owner);
	return true;
}

FGridMove FGridPuzzleRules::MoveCharacter(EPlayerRole Role, FIntPoint Direction, bool bReplaying)
{
	const int32 Index = RoleIndex(Role);
	const EFootprintOwner Owner = Index == 0 ? EFootprintOwner::PlayerA : EFootprintOwner::PlayerB;
	FGridMove Move;
	Move.From = Positions[Index];
	if (!((Direction.X == 0 && (Direction.Y == -1 || Direction.Y == 1)) ||
		  (Direction.Y == 0 && (Direction.X == -1 || Direction.X == 1))))
	{
		return Move;
	}

	Move.To = Move.From + Direction;
	FTileData* Destination = Tiles.Find(Move.To);
	if (Finished[Index]) { Move.Result = EGridMoveResult::Finished; return Move; }
	if (!Destination || Destination->bFoldedAway) { Move.Result = EGridMoveResult::Void; return Move; }
	if (Role == EPlayerRole::PlayerB && ReplayTiles.Contains(Move.To) != bReplaying)
	{
		Move.Result = EGridMoveResult::ReplayOnly;
		return Move;
	}
	if (Destination->TileType == ETileType::Obstacle) { Move.Result = EGridMoveResult::Blocked; return Move; }
	if (Destination->bIsOccupied) { Move.Result = EGridMoveResult::Occupied; return Move; }
	if (Destination->bIsCollapsed) { Move.Result = EGridMoveResult::Collapsed; return Move; }
	if (Destination->FootprintOwner == Owner) { Move.Result = EGridMoveResult::OwnFootprint; return Move; }
	if (bRequireBorrowedPrints && Destination->HasFootprints() && Destination->FootprintOwner == EFootprintOwner::None)
	{
		Move.Result = EGridMoveResult::EmptyPrint;
		return Move;
	}
	if (bReplaying && Destination->FootprintOwner != EFootprintOwner::PlayerA)
	{
		Move.Result = EGridMoveResult::EmptyPrint;
		return Move;
	}
	if (Destination->TileType == ETileType::Bridge && !Destination->bIsActivated)
	{
		Move.Result = EGridMoveResult::ClosedBridge;
		return Move;
	}
	if (Destination->TileType == ETileType::Goal)
	{
		if (Destination->GoalOwner != Role) { Move.Result = EGridMoveResult::OtherGoal; return Move; }
		if (Destination->bIsLocked) { Move.Result = EGridMoveResult::LockedGoal; return Move; }
	}

	Move.Result = EGridMoveResult::Moved;
	++MoveCount;
	FTileData& Source = Tiles.FindChecked(Move.From);
	Source.bIsOccupied = false;
	Destination->bIsOccupied = true;
	Positions[Index] = Move.To;
	Move.ChangedTiles = { Move.From, Move.To };

	if (Source.TileType == ETileType::Plate)
	{
		Source.bIsActivated = false;
	}
	if (Destination->TileType == ETileType::Plate)
	{
		Destination->bIsActivated = Destination->SwitchRole == Role;
	}
	if (Destination->HasFootprints())
	{
		Move.bFootprintTransferred = Destination->FootprintOwner != EFootprintOwner::None;
		Destination->FootprintOwner = Owner;
		Destination->bFreshPrint = true;
	}
	if (Source.TileType == ETileType::SingleUse)
	{
		Source.bIsCollapsed = true;
	}
	if (Destination->TileType == ETileType::Fragile)
	{
		Destination->RemainingMoves = FragileLifetime;
	}
	if (Destination->TileType == ETileType::Switch && Destination->SwitchRole == Role && !Destination->bIsActivated)
	{
		Destination->bIsActivated = true;
		Move.bSwitchActivated = true;
	}
	if (Destination->TileType == ETileType::Goal)
	{
		Finished[Index] = true;
		Move.bReachedGoal = true;
	}
	ResolveStep(Move, Owner);
	return Move;
}

void FGridPuzzleRules::ResolveStep(FGridMove& Move, EFootprintOwner Owner)
{
	for (TPair<FIntPoint, FTileData>& Entry : Tiles)
	{
		FTileData& Tile = Entry.Value;
		if (Move.bSwitchActivated && Tile.bIsLocked)
		{
			Tile.bIsLocked = false;
			Move.ChangedTiles.Add(Entry.Key);
		}

		// Fresh prints start their countdown after departure. Preprinted stone waits for its first traveler.
		if (Tile.TileType != ETileType::Fragile || Tile.bIsOccupied || Tile.bIsCollapsed ||
			!Tile.bFreshPrint || Entry.Key == Move.From)
		{
			continue;
		}
		Tile.bIsCollapsed = --Tile.RemainingMoves == 0;
		Move.ChangedTiles.Add(Entry.Key);
	}

	UpdatePrintEffects(Move, Owner);

	// Entry used the previous bridge state. Recompute after the whole step so a
	// traveler can leave a bridge that closed beneath them during that step.
	for (TPair<FIntPoint, FTileData>& Entry : Tiles)
	{
		FTileData& Tile = Entry.Value;
		if (Tile.TileType == ETileType::Bridge)
		{
			const bool bOpen = Algo::AllOf(GetBridgeConditions(Tile.Marker),
				[this](const FGridBridgeCondition& Condition) { return IsBridgeConditionMet(Condition); });
			if (Tile.bIsActivated != bOpen)
			{
				Tile.bIsActivated = bOpen;
				Move.ChangedTiles.AddUnique(Entry.Key);
			}
		}
	}
}

bool FGridPuzzleRules::ArePrintTargetsFree(const TArray<FPrintWrite>& Writes) const
{
	return Algo::AllOf(Writes, [this](const FPrintWrite& Write)
	{
		return !Tiles.FindChecked(Write.Target).bIsOccupied;
	});
}

void FGridPuzzleRules::ApplyPrintWrites(const TArray<FPrintWrite>& Writes, FGridMove& Move)
{
	for (const FPrintWrite& Write : Writes)
	{
		FTileData& Target = Tiles.FindChecked(Write.Target);
		Target.FootprintOwner = Write.Owner;
		Target.bFreshPrint = Write.bFresh;
		Move.ChangedTiles.AddUnique(Write.Target);
	}
	Move.bSentPrint = true;
}

void FGridPuzzleRules::UpdatePrintEffects(FGridMove& Move, EFootprintOwner Owner)
{
	for (int32 Index = 0; Index < PendingPrints.Num();)
	{
		FPendingPrint& Pending = PendingPrints[Index];
		Pending.Steps = FMath::Max(0, Pending.Steps - 1);
		if (Pending.Steps == 0 && ArePrintTargetsFree(Pending.Writes))
		{
			ApplyPrintWrites(Pending.Writes, Move);
			PendingPrints.RemoveAt(Index);
		}
		else
		{
			++Index;
		}
	}

	for (int32 Index = 0; Index < PrintEffects.Num(); ++Index)
	{
		const FGridPrintEffect& Effect = PrintEffects[Index];
		if (Effect.Trigger != Move.To || UsedPrintEffects.Contains(Index) ||
			(Effect.TriggerOwner != EFootprintOwner::None && Effect.TriggerOwner != Owner) ||
			PendingPrints.ContainsByPredicate([Index](const FPendingPrint& Pending) { return Pending.EffectIndex == Index; }) ||
			!Algo::AllOf(Effect.Requirements,
				[this](const FGridBridgeCondition& Condition) { return IsBridgeConditionMet(Condition); }))
		{
			continue;
		}

		TArray<FPrintWrite> Writes;
		for (const FGridPrintLink& Link : Effect.Links)
		{
			const FTileData& Source = Tiles.FindChecked(Link.Source);
			if (Source.FootprintOwner == EFootprintOwner::None || Source.bIsCollapsed ||
				(Effect.bMovePrint && Source.bIsOccupied))
			{
				Writes.Reset();
				break;
			}
			Writes.Add({Link.Target, Source.FootprintOwner, Source.bFreshPrint});
		}
		if (Writes.IsEmpty()) { continue; }

		if (Effect.bMovePrint)
		{
			for (const FGridPrintLink& Link : Effect.Links)
			{
				FTileData& Source = Tiles.FindChecked(Link.Source);
				Source.FootprintOwner = EFootprintOwner::None;
				Source.bFreshPrint = false;
				Move.ChangedTiles.AddUnique(Link.Source);
			}
		}
		if (Effect.bOnce) { UsedPrintEffects.Add(Index); }
		if (Effect.DelayMoves == 0 && ArePrintTargetsFree(Writes))
		{
			ApplyPrintWrites(Writes, Move);
		}
		else
		{
			PendingPrints.Add({Index, Effect.DelayMoves, MoveTemp(Writes)});
			Move.bSentPrint = true;
		}
	}

	for (TPair<FIntPoint, FTileData>& Pair : Tiles)
	{
		FTileData& Tile = Pair.Value;
		if (!Tile.bPrintReceiver) { continue; }
		const bool bPending = PendingPrints.ContainsByPredicate([&Pair](const FPendingPrint& Pending)
		{
			return Pending.Writes.ContainsByPredicate([&Pair](const FPrintWrite& Write) { return Write.Target == Pair.Key; });
		});
		if (bPending != Tile.bPrintPending)
		{
			Tile.bPrintPending = bPending;
			Move.ChangedTiles.AddUnique(Pair.Key);
		}
	}
}
