#include "GridPuzzleRules.h"

#include "Algo/AllOf.h"

// 从资源重建规则状态，先在临时对象中完成布局解析和机关校验。
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

	// 字符地图转换成坐标索引，空格跳过，因此没有地块的坐标自然形成虚空。
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

	// 每名角色必须各有一个起点和终点，上锁出口还需要至少一个开关。
	if (Starts[0] != 1 || Starts[1] != 1 || Goals[0] != 1 || Goals[1] != 1 ||
		(bHasLockedGoal && Switches == 0))
	{
		return false;
	}

	// 校验桥组引用，保证后续 FindChecked 访问的桥与条件源都有有效配置。
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

	// 寄印源、接收端与触发格需要兼容，搬运时也要避免源目标互相覆盖。
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
	if (!Initial.InitializeFusion(Level)) { return false; }
	// 到这里整关配置已有效，可以一次性提交新局面。
	*this = MoveTemp(Initial);
	return true;
}

// 为转桥补建备用朝向槽位，并检查回放与换位所需的印板。
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
		// 两套桥面必须围绕同一轴心整体旋转 90 度，且备用落点不可覆盖现有地块。
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

// 压板检查角色实时站位，印板检查留存的脚印颜色。
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

// 普通移动入口，只有 A 的成功移动会追加到正在录制的三步方向中。
FGridMove FGridPuzzleRules::TryMove(EPlayerRole Role, FIntPoint Direction)
{
	FGridMove Move = MoveWithTerrain(Role, Direction, false);
	if (Move.Result == EGridMoveResult::Moved && bRecording && Role == EPlayerRole::PlayerA)
	{
		Recording.Add(Direction);
		bRecording = Recording.Num() < RecordedLength;
	}
	return Move;
}

// 手动移动与回放共用滑行结算，每经过一格都重新检查脚印、箱子与时钟。
FGridMove FGridPuzzleRules::MoveWithTerrain(EPlayerRole Role, FIntPoint Direction, bool bReplaying)
{
	FGridMove Move = MoveCharacter(Role, Direction, bReplaying);
	if (Move.Result == EGridMoveResult::Moved)
	{
		Move.Path.Add(Move.To);
		Move.BorrowedPrintCount = Move.bFootprintTransferred ? 1 : 0;
		// 冰面逐格借印，下一格不合法时停在最后一个合法落点。
		while (Tiles.FindChecked(GetPosition(Role)).bIce && !IsFinished(Role))
		{
			const FGridMove Step = MoveCharacter(Role, Direction, bReplaying);
			if (Step.Result != EGridMoveResult::Moved) { break; }
			Move.To = Step.To;
			Move.Path.Add(Step.To);
			Move.BorrowedPrintCount += Step.bFootprintTransferred ? 1 : 0;
			Move.bFootprintTransferred |= Step.bFootprintTransferred;
			for (FIntPoint Position : Step.ChangedTiles) { Move.ChangedTiles.AddUnique(Position); }
			Move.bSwitchActivated |= Step.bSwitchActivated;
			Move.bReachedGoal |= Step.bReachedGoal;
			Move.bSentPrint |= Step.bSentPrint;
		}
	}
	return Move;
}

// 再次从 REC 开始录制时覆盖旧方向序列。
bool FGridPuzzleRules::StartRecording(EPlayerRole Role)
{
	if (Prototype != EGridPrototype::RecordRoute || Role != EPlayerRole::PlayerA ||
		IsFinished(Role) || GetPosition(Role) != RecordStart) { return false; }
	Recording.Reset();
	bRecording = true;
	return true;
}

// 回放由 B 在 PLAY 发起，必须已经录满三步。
bool FGridPuzzleRules::CanReplayFrom(EPlayerRole Role) const
{
	return Prototype == EGridPrototype::RecordRoute && Role == EPlayerRole::PlayerB && !IsFinished(Role) &&
		GetPosition(Role) == ReplayStart && Recording.Num() == RecordedLength;
}

// 在 Trial 上逐步试算，任一步失败就丢弃快照，原局面保持完整。
// Steps 保留到失败那一步，便于界面标出阻挡位置。
bool FGridPuzzleRules::PlanReplay(EPlayerRole Role, TArray<FGridMove>& Steps, TArray<FGridPuzzleRules>& States) const
{
	Steps.Reset();
	States.Reset();
	if (!CanReplayFrom(Role)) { return false; }
	FGridPuzzleRules Trial = *this;
	for (const FIntPoint Direction : Recording)
	{
		FGridMove& Step = Steps.Add_GetRef(Trial.MoveWithTerrain(Role, Direction, true));
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

// 桥面上无人时，将每块石板的脚印搬到对应的新槽位，同时隐藏旧槽位。
// 转桥本身不增加 MoveCount，也不推进成功行走所驱动的倒计时。
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
		// 桥上的箱子和表面机关随实体石板搬运，另一朝向只是该石板的备用槽位。
		Next.bCrate = Previous.bCrate;
		Next.bIce = Previous.bIce;
		Next.bRhythm = Previous.bRhythm;
		Next.bFoldedAway = false;
		Previous.FootprintOwner = EFootprintOwner::None;
		Previous.bFreshPrint = false;
		Previous.bCrate = false;
		Previous.bIce = false;
		Previous.bRhythm = false;
		Previous.bFoldedAway = true;
		Changed.Add(From[Index]);
		Changed.Add(To[Index]);
	}
	bAlternateDeck = !bAlternateDeck;
	UpdateFusionTiles(Changed);
	return true;
}

// 两人的脚下都要是刚写出的自身脚印，换位时才能同时借到对方的脚印。
bool FGridPuzzleRules::CanSwap() const
{
	if (Finished[0] || Finished[1] || !SwapTiles.Contains(Positions[0]) || !SwapTiles.Contains(Positions[1]))
	{
		return false;
	}
	const FTileData& Blue = Tiles.FindChecked(Positions[0]);
	const FTileData& Orange = Tiles.FindChecked(Positions[1]);
	const bool bClosedRhythm = (MoveCount + 1) % RhythmPeriod == RhythmClosedPhase &&
		(Blue.bRhythm || Orange.bRhythm);
	const bool bPatrolCrossing = !PatrolRoute.IsEmpty() &&
		(PatrolRoute[(MoveCount + 1) % PatrolRoute.Num()] == Positions[0] ||
		 PatrolRoute[(MoveCount + 1) % PatrolRoute.Num()] == Positions[1]);
	if (Blue.bCrate || Orange.bCrate || bClosedRhythm || bPatrolCrossing) { return false; }
	return Blue.bFreshPrint && Blue.FootprintOwner == EFootprintOwner::PlayerA &&
		Orange.bFreshPrint && Orange.FootprintOwner == EFootprintOwner::PlayerB;
}

// 换位以一个完整行动结算，两名角色的位置与对应地块的脚印一起交换。
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

// 核心移动判定。前半段只校验条件，通过后才写回位置、脚印与机关状态。
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
	// 主题规则在这里统一执行，禁止踩自己的印，严格模式也禁止踏入空印格。
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
	// 节拍随成功落脚推进，切换角色和非法输入不会推动时钟。
	if (Destination->bRhythm && (MoveCount + 1) % RhythmPeriod == RhythmClosedPhase)
	{
		Move.Result = EGridMoveResult::RhythmClosed;
		return Move;
	}
	if (!PatrolRoute.IsEmpty())
	{
		const FIntPoint Next = PatrolRoute[(MoveCount + 1) % PatrolRoute.Num()];
		const int32 Other = 1 - Index;
		if (Next == Move.To || (!Finished[Other] && Next == Positions[Other]))
		{
			Move.Result = EGridMoveResult::PatrolBlocked;
			return Move;
		}
	}
	FTileData* CrateTarget = nullptr;
	if (Destination->bCrate)
	{
		CrateTarget = Tiles.Find(Move.To + Direction);
		if (!CrateTarget || !CrateTarget->HasFootprints() || CrateTarget->bCrate ||
			CrateTarget->bIsOccupied || CrateTarget->bIsCollapsed || CrateTarget->bFoldedAway)
		{
			Move.Result = EGridMoveResult::CrateBlocked;
			return Move;
		}
	}

	// 所有检查通过，从这里开始提交成功行动，拒绝的输入不会消耗步数。
	Move.Result = EGridMoveResult::Moved;
	++MoveCount;
	FTileData& Source = Tiles.FindChecked(Move.From);
	Source.bIsOccupied = false;
	Destination->bIsOccupied = true;
	Positions[Index] = Move.To;
	Move.ChangedTiles = { Move.From, Move.To };
	if (CrateTarget)
	{
		// 推箱者已经通过目标格脚印检查，再移箱并踏入箱子腾出的那格。
		Destination->bCrate = false;
		CrateTarget->bCrate = true;
		Move.ChangedTiles.AddUnique(CrateTarget->GridPosition);
	}

	if (Source.TileType == ETileType::Plate)
	{
		Source.bIsActivated = false;
	}
	if (Destination->TileType == ETileType::Plate)
	{
		Destination->bIsActivated = Destination->SwitchRole == Role;
	}
	// 借来的脚印在落脚后改成自己的颜色，形成下一次供同伴借用的路。
	if (Destination->HasFootprints())
	{
		Move.bFootprintTransferred = Destination->FootprintOwner != EFootprintOwner::None;
		Destination->FootprintOwner = Owner;
		Destination->bFreshPrint = true;
	}
	// 一次性印板离开即塌，易碎板落脚时恢复完整寿命，后续由 ResolveStep 扣时。
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

// 成功行动的统一收尾，按顺序处理解锁、易碎倒计时、寄印与桥条件。
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
		// 占用中、未实际踩过以及本次刚离开的格子跳过扣时，给同伴保留交接窗口。
		if (Tile.TileType != ETileType::Fragile || Tile.bIsOccupied || Tile.bIsCollapsed ||
			!Tile.bFreshPrint || Entry.Key == Move.From)
		{
			continue;
		}
		Tile.bIsCollapsed = --Tile.RemainingMoves == 0;
		Move.ChangedTiles.Add(Entry.Key);
	}

	UpdatePrintEffects(Move, Owner);
	UpdateFusionTiles(Move.ChangedTiles);

	// Entry used the previous bridge state. Recompute after the whole step so a
	// traveler can leave a bridge that closed beneath them during that step.
	// 入桥使用行动前的开合状态，整步结束后重新计算，使角色能从刚关闭的桥上离开。
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

// 一组寄印需要全部接收格都空闲，避免一半送达造成不完整局面。
bool FGridPuzzleRules::ArePrintTargetsFree(const TArray<FPrintWrite>& Writes) const
{
	return Algo::AllOf(Writes, [this](const FPrintWrite& Write)
	{
		return !Tiles.FindChecked(Write.Target).bIsOccupied;
	});
}

// 寄印写回目标格，并把变化记录交给表现层刷新颜色。
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

// 先推进旧包裹，再处理本步触发的新寄印，最后刷新接收端的 WAIT 标记。
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

		// 读取触发这一刻的源印快照，延迟期间源格再变色也不会更改包裹内容。
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

		// 搬运在发件时清除源印，副本与源印都包含在同一份可撤销状态中。
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
