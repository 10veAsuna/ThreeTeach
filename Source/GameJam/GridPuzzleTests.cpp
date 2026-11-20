#include "GridPuzzleGame.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Containers/Queue.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"

namespace
{
	constexpr EPlayerRole Blue = EPlayerRole::PlayerA;
	constexpr EPlayerRole Orange = EPlayerRole::PlayerB;
	const FIntPoint Up(0, -1), Down(0, 1), Left(-1, 0), Right(1, 0);
}

struct FGridTestSearch
{
	static FString StateKey(const FGridPuzzleRules& Rules)
	{
		FString Key;
		Key.Reserve(320);
		for (FIntPoint Position : Rules.Positions)
		{
			Key += FString::Printf(TEXT("%d,%d;"), Position.X, Position.Y);
		}
		for (const auto& Pair : Rules.Tiles)
		{
			const FTileData& Tile = Pair.Value;
			const int32 Flags = (Tile.bFreshPrint ? 1 : 0) | (Tile.bIsCollapsed ? 2 : 0) |
				(Tile.bIsLocked ? 4 : 0) | (Tile.bIsActivated ? 8 : 0) | (Tile.bFoldedAway ? 16 : 0);
			Key.AppendChar(TEXT('a') + Flags);
			Key.AppendChar(TEXT('0') + static_cast<int32>(Tile.FootprintOwner));
			if (Tile.HasFootprints()) { Key += FString::Printf(TEXT("%d;"), Tile.RemainingMoves); }
		}
		for (int32 Index = 0; Index < Rules.PrintEffects.Num(); ++Index)
		{
			Key.AppendChar(Rules.UsedPrintEffects.Contains(Index) ? TEXT('1') : TEXT('0'));
		}
		for (const auto& Pending : Rules.PendingPrints)
		{
			Key += FString::Printf(TEXT(";%d,%d"), Pending.EffectIndex, Pending.Steps);
			for (const auto& Write : Pending.Writes)
			{
				Key.AppendChar(TEXT('0') + static_cast<int32>(Write.Owner) + (Write.bFresh ? 4 : 0));
			}
		}
		Key.AppendChar(Rules.IsRecording() ? TEXT('R') : TEXT('|'));
		for (const FIntPoint Direction : Rules.GetRecording()) { Key += Direction.ToString(); }
		return Key;
	}

	static bool CanClear(const UGridLevelDataAsset& Level, EGridPrototype BannedAction = EGridPrototype::None,
		int32 BannedRoles = 0)
	{
		TSet<FString> Seen;
		TArray<FGridPuzzleRules> Queue;
		const auto Enqueue = [&Seen, &Queue](FGridPuzzleRules&& Rules)
		{
			FString Key = StateKey(Rules);
			if (!Seen.Contains(Key))
			{
				Seen.Add(MoveTemp(Key));
				Queue.Add(MoveTemp(Rules));
			}
		};
		FGridPuzzleRules Initial;
		if (!Initial.Initialize(Level)) { return false; }
		Enqueue(MoveTemp(Initial));
		for (int32 Index = 0; Index < Queue.Num(); ++Index)
		{
			const FGridPuzzleRules Current = MoveTemp(Queue[Index]);
			if (Current.IsComplete()) { return true; }
			for (const EPlayerRole Role : { Blue, Orange })
			{
				const bool bAllowFreshPrints = (BannedRoles & (1 << FGridPuzzleRules::RoleIndex(Role))) == 0;
				for (const FIntPoint Direction : { Up, Down, Left, Right })
				{
					const FTileData* Target = Current.GetTiles().Find(Current.GetPosition(Role) + Direction);
					if (!Target || (!bAllowFreshPrints && Target->bFreshPrint)) { continue; }
					FGridPuzzleRules Next = Current;
					if (Next.TryMove(Role, Direction).Result == EGridMoveResult::Moved) { Enqueue(MoveTemp(Next)); }
				}
				FGridPuzzleRules Next = Current;
				TArray<FIntPoint> Changed;
				FGridMove SwapMove;
				if ((BannedAction != EGridPrototype::TurnBridge && Next.TryTurnBridge(Role, Changed)) ||
					(BannedAction != EGridPrototype::RecordRoute && Next.StartRecording(Role)) ||
					(BannedAction != EGridPrototype::SwapPlaces && BannedRoles == 0 && Next.TrySwap(Role, SwapMove)))
				{
					Enqueue(MoveTemp(Next));
				}
				if (BannedAction == EGridPrototype::RecordRoute) { continue; }
				TArray<FGridMove> Steps;
				TArray<FGridPuzzleRules> States;
				if (!Current.PlanReplay(Role, Steps, States)) { continue; }
				bool bCanBorrow = true;
				for (int32 Step = 0; Step < Steps.Num() && !bAllowFreshPrints; ++Step)
				{
					const FGridPuzzleRules& Before = Step == 0 ? Current : States[Step - 1];
					bCanBorrow &= !Before.GetTiles().FindChecked(Steps[Step].To).bFreshPrint;
				}
				if (bCanBorrow) { Enqueue(MoveTemp(States.Last())); }
			}
		}
		return false;
	}

	static bool CanVisit(const UGridLevelDataAsset& Level, FIntPoint Target)
	{
		TSet<FString> Seen;
		TArray<FGridPuzzleRules> Queue;
		const auto Enqueue = [&Seen, &Queue](FGridPuzzleRules&& Rules)
		{
			FString Key = StateKey(Rules);
			if (!Seen.Contains(Key))
			{
				Seen.Add(MoveTemp(Key));
				Queue.Add(MoveTemp(Rules));
			}
		};
		FGridPuzzleRules Initial;
		if (!Initial.Initialize(Level)) { return false; }
		Enqueue(MoveTemp(Initial));
		for (int32 Index = 0; Index < Queue.Num(); ++Index)
		{
			const FGridPuzzleRules Current = MoveTemp(Queue[Index]);
			if (Current.GetPosition(Blue) == Target || Current.GetPosition(Orange) == Target) { return true; }
			for (const EPlayerRole Role : { Blue, Orange })
			{
				for (const FIntPoint Direction : { Up, Down, Left, Right })
				{
					FGridPuzzleRules Next = Current;
					if (Next.TryMove(Role, Direction).Result == EGridMoveResult::Moved) { Enqueue(MoveTemp(Next)); }
				}
				FGridPuzzleRules Next = Current;
				TArray<FIntPoint> Changed;
				FGridMove SwapMove;
				if (Next.TryTurnBridge(Role, Changed) || Next.StartRecording(Role) ||
					Next.TrySwap(Role, SwapMove))
				{
					Enqueue(MoveTemp(Next));
				}
				TArray<FGridMove> Steps;
				TArray<FGridPuzzleRules> States;
				if (Current.PlanReplay(Role, Steps, States)) { Enqueue(MoveTemp(States.Last())); }
			}
		}
		return false;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridPrototypeTest, "OneStep.Rules.Prototypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridPrototypeTest::RunTest(const FString& Parameters)
{
	const TArray<UGridLevelDataAsset*> Levels = AGridManager::CreatePrototypeLevels();
	for (const UGridLevelDataAsset* Level : Levels)
	{
		const FString Name = Level->Title.ToString();
		TestTrue(Name + TEXT(" is solvable"), FGridTestSearch::CanClear(*Level));
		TestFalse(Name + TEXT(" cannot bypass its action"), FGridTestSearch::CanClear(*Level, Level->Prototype));
		TestFalse(Name + TEXT(" needs a fresh partner print"), FGridTestSearch::CanClear(*Level, EGridPrototype::None, 3));
	}

	FGridPuzzleRules Turn;
	TArray<FIntPoint> Changed;
	if (!TestTrue(TEXT("Turn initializes"), Turn.Initialize(*Levels[0]))) { return false; }
	Turn.TryMove(Blue, Right);
	Turn.TryMove(Blue, Right);
	Turn.TryMove(Blue, Right);
	const FGridPuzzleRules BeforeTurn = Turn;
	TestTrue(TEXT("Bridge rotates with both roles ashore"), Turn.TryTurnBridge(Blue, Changed));
	TestTrue(TEXT("The old landing becomes a gap"), Turn.GetTiles().FindChecked({3, 2}).bFoldedAway);
	const FTileData& Carried = Turn.GetTiles().FindChecked({2, 3});
	TestTrue(TEXT("The moved stone carries the fresh blue print"),
		Carried.FootprintOwner == EFootprintOwner::PlayerA && Carried.bFreshPrint && !Carried.bFoldedAway);
	TestTrue(TEXT("Orange genuinely borrows the carried print"), Turn.TryMove(Orange, Right).bFootprintTransferred);
	TestFalse(TEXT("Bridge rejects a turn with someone aboard"), Turn.TryTurnBridge(Blue, Changed));
	Turn = BeforeTurn;
	TestTrue(TEXT("Restoring a bridge state restores its orientation and print"),
		!Turn.GetTiles().FindChecked({3, 2}).bFoldedAway && Turn.GetTiles().FindChecked({3, 2}).bFreshPrint &&
		Turn.GetTiles().FindChecked({2, 3}).bFoldedAway);

	FGridPuzzleRules Record;
	if (!TestTrue(TEXT("Record initializes"), Record.Initialize(*Levels[1]))) { return false; }
	TestTrue(TEXT("Blue starts recording"), Record.StartRecording(Blue));
	Record.TryMove(Blue, Up);
	TestTrue(TEXT("Rejected moves do not get recorded"), Record.GetRecording().IsEmpty());
	Record.TryMove(Blue, Right);
	Record.TryMove(Blue, Right);
	Record.TryMove(Blue, Up);
	Record.TryMove(Orange, Up);
	TestTrue(TEXT("Entering a replay tile manually is blocked"),
		Record.TryMove(Orange, Right).Result == EGridMoveResult::ReplayOnly);
	TArray<FGridMove> Steps;
	TArray<FGridPuzzleRules> States;
	const int32 BeforeBlockedReplay = Record.GetMoveCount();
	TestFalse(TEXT("The occupied final landing blocks the full sequence"), Record.PlanReplay(Orange, Steps, States));
	TestTrue(TEXT("Failure leaves no intermediate state"), States.IsEmpty() && Steps.Num() == 3 &&
		Steps.Last().Result == EGridMoveResult::Occupied && Record.GetMoveCount() == BeforeBlockedReplay &&
		Record.GetPosition(Orange) == FIntPoint(1, 3));
	Record.TryMove(Blue, Up);
	if (!TestTrue(TEXT("All three replay steps plan after Blue clears the landing"), Record.PlanReplay(Orange, Steps, States)))
	{
		return false;
	}
	TestTrue(TEXT("Replay step one and three retain distinct landing states"),
		States[0].GetPosition(Orange) == FIntPoint(2, 3) && States.Last().GetPosition(Orange) == FIntPoint(3, 2));
	TestTrue(TEXT("All three replay steps borrow prints"),
		Steps.Num() == 3 && Steps[0].bFootprintTransferred && Steps[1].bFootprintTransferred && Steps[2].bFootprintTransferred);

	FGridPuzzleRules Exchange;
	if (!TestTrue(TEXT("Swap initializes"), Exchange.Initialize(*Levels[2]))) { return false; }
	FGridMove SwapMove;
	TestFalse(TEXT("Swap requires both roles at the meeting tiles"), Exchange.TrySwap(Blue, SwapMove));
	Exchange.TryMove(Blue, Right);
	Exchange.TryMove(Orange, Left);
	const FGridPuzzleRules BeforeSwap = Exchange;
	TestTrue(TEXT("Normal movement still enforces occupation"),
		Exchange.TryMove(Blue, Right).Result == EGridMoveResult::Occupied);
	TestTrue(TEXT("Swap moves both travelers in one counted move"), Exchange.TrySwap(Blue, SwapMove) &&
		Exchange.GetMoveCount() == 3 && Exchange.GetPosition(Blue) == FIntPoint(3, 0) &&
		Exchange.GetPosition(Orange) == FIntPoint(2, 0));
	TestTrue(TEXT("Both footprints are rewritten"),
		Exchange.GetTiles().FindChecked({3, 0}).FootprintOwner == EFootprintOwner::PlayerA &&
		Exchange.GetTiles().FindChecked({2, 0}).FootprintOwner == EFootprintOwner::PlayerB);
	Exchange = BeforeSwap;
	TestTrue(TEXT("Restoring a swap state returns both roles"), Exchange.GetMoveCount() == 2 &&
		Exchange.GetPosition(Blue) == FIntPoint(2, 0) && Exchange.GetPosition(Orange) == FIntPoint(3, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridChaptersTest, "OneStep.Rules.AllChapters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridChaptersTest::RunTest(const FString& Parameters)
{
	TArray<UGridLevelDataAsset*> Levels = AGridManager::CreateDefaultLevels();
	// Use the same chapter packages as BP_Board when the campaign has been imported.
	for (int32 Index = 0; Index < Levels.Num(); ++Index)
	{
		const TCHAR* Version = Levels[Index]->Prototype == EGridPrototype::None ? TEXT("V4") : TEXT("V5");
		const FString Name = FString::Printf(TEXT("DA_%s_Chapter%02d"), Version, Index + 1);
		const FString Package = FString::Printf(TEXT("/Game/OneStep/Levels/%s/%s"), Version, *Name);
		if (FPackageName::DoesPackageExist(Package))
		{
			Levels[Index] = LoadObject<UGridLevelDataAsset>(nullptr, *(Package + TEXT(".") + Name));
			if (!TestNotNull(Name, Levels[Index])) { return false; }
		}
	}
	const TCHAR* Solutions[] = {
		TEXT("AR AR AU AU BL BL BD BD"),
		TEXT("AR AR BL BR AR AU BL BL AD AR AR BL BL BL"),
		TEXT("BU BD AR AR AR AR AR BU BR BR BR BD AL AL AL AL AL AU"),
		TEXT("AR BU BU BU AR AD AR AR AR BD BD BR BR BD AL AL AL AU AU AR AR AR AR AD AR"),
		TEXT("AR AR BL AU BL BL BL AD AR BR BU AL BU AL BD BD BD AR AU AU AU BD"),
		TEXT("AR AR AR AR AR AR AD AD AR AR AU AE BR BR BR BR BR BR BU BU BE BU BU BR BR BU BU AD AD AD AD AD"),
		TEXT("AR AR AR AR AD AD AR BR BR BR BR BU BU BU AL AD AD AR AR AU BU BR BR BD BD BL AU AU AU AR AR AU AU BR BD BD BR BR BD BD"),
		TEXT("AR AR BR BR AR AE AR AR AU AR AD AR AR BR BR BE BR BR BD BR AL AL AL AL AL AL AL AL AU"),
		TEXT("BU BR AR AD AR BL AU AR AR AR AR AR AR AR AU AU BR BR BR BR BR BD BD BD"),
		TEXT("AR AR AR AR AD BU BU BU BU AL BR AU BD AR AR BU BL AL AD BR BR AU AL AU AU BR BR BR"),
		TEXT("AR AR AR AR AR BU BU BU BR AR AR AD BR BR BR BR BR AU AL BL BD AR AR BD BU BU BL AL AD AD AR BR BR BU"),
		TEXT("AR AR AR BR BR BR AR BR BR BU BU BU AR AE AD AD AD AR AR AR AD AD BE BR BR BR BU BU"),
		TEXT("AR AR BR BR AR AR AR AR BR BR AL AL AL AU BR BR BR BR BR"),
		TEXT("AR AR AR BR BR BR BR BR AR AR AR AD AD AR BR BU BU BU AL BU AU AD AD AD AL BD BD BD BD BR AR AU AU AU AU AR BL BU BU BU BU BL AL AD AD AD AD AR AR AD AD BR BR BR BU BU")
	};
	if (!TestEqual(TEXT("Chapter count"), Levels.Num(), static_cast<int32>(UE_ARRAY_COUNT(Solutions))))
	{
		return false;
	}
	for (int32 Index = 0; Index < Levels.Num(); ++Index)
	{
		FGridPuzzleRules Rules;
		const FString Chapter = FString::Printf(TEXT("Chapter %d"), Index + 1);
		int32 NeutralTiles = 0;
		for (const FString& Row : Levels[Index]->Rows)
		{
			for (TCHAR Symbol : Row) { NeutralTiles += Symbol == TCHAR('.'); }
		}
		TestTrue(Chapter + TEXT(" has at most one generic neutral tile"), NeutralTiles <= 1);
		if (!TestTrue(Chapter + TEXT(" loads"), Rules.Initialize(*Levels[Index])))
		{
			continue;
		}
		const EGridPrototype Action = Levels[Index]->Prototype;
		if (Action != EGridPrototype::None)
		{
			const bool bCombined = Action == EGridPrototype::TurnBridgeAndSwap;
			TestFalse(Chapter + TEXT(" requires its action"),
				FGridTestSearch::CanClear(*Levels[Index], bCombined ? EGridPrototype::TurnBridge : Action));
			if (bCombined)
			{
				TestFalse(Chapter + TEXT(" requires swap as well as turn"),
					FGridTestSearch::CanClear(*Levels[Index], EGridPrototype::SwapPlaces));
			}
			TestFalse(Chapter + TEXT(" requires a freshly written print for A"),
				FGridTestSearch::CanClear(*Levels[Index], EGridPrototype::None, 1));
			TestFalse(Chapter + TEXT(" requires a freshly written print for B"),
				FGridTestSearch::CanClear(*Levels[Index], EGridPrototype::None, 2));
		}
		TArray<FString> Moves;
		FString(Solutions[Index]).ParseIntoArrayWS(Moves);
		int32 BorrowedPrintMoves = 0;
		for (const FString& Move : Moves)
		{
			const EPlayerRole Traveler = Move[0] == 'A' ? EPlayerRole::PlayerA : EPlayerRole::PlayerB;
			if (Move[1] == 'E')
			{
				TArray<FIntPoint> Changed;
				TArray<FGridMove> Replay;
				TArray<FGridPuzzleRules> ReplayStates;
				FGridMove Exchange;
				bool bSucceeded = false;
				if (Rules.TrySwap(Traveler, Exchange))
				{
					BorrowedPrintMoves += 1;
					bSucceeded = true;
				}
				else if (Rules.TryTurnBridge(Traveler, Changed) || Rules.StartRecording(Traveler))
				{
					bSucceeded = true;
				}
				else if (Rules.PlanReplay(Traveler, Replay, ReplayStates))
				{
					Rules = MoveTemp(ReplayStates.Last());
					BorrowedPrintMoves += Replay.Num();
					bSucceeded = true;
				}
				if (!TestTrue(Chapter + TEXT(" permits ") + Move, bSucceeded)) { break; }
				continue;
			}
			FIntPoint Direction(0, 0);
			switch (Move[1])
			{
			case 'U': Direction.Y = -1; break;
			case 'D': Direction.Y = 1; break;
			case 'L': Direction.X = -1; break;
			case 'R': Direction.X = 1; break;
			}
			const FGridMove Step = Rules.TryMove(Traveler, Direction);
			BorrowedPrintMoves += Step.bFootprintTransferred;
			if (!TestTrue(Chapter + TEXT(" permits ") + Move, Step.Result == EGridMoveResult::Moved))
			{
				break;
			}
		}
		TestTrue(Chapter + TEXT(" completes"), Rules.IsComplete());
		TestTrue(Chapter + TEXT(" borrows a print on at least 60 percent of its reference steps"),
			BorrowedPrintMoves * 5 >= Rules.GetMoveCount() * 3);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridFragileTest, "OneStep.Rules.FragileBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridFragileTest::RunTest(const FString& Parameters)
{
	UGridLevelDataAsset* Level = NewObject<UGridLevelDataAsset>();
	Level->Rows = { TEXT("Afa"), TEXT("B.b") };
	Level->FragileLifetime = 2;
	FGridPuzzleRules Rules;
	if (!TestTrue(TEXT("Fixture loads"), Rules.Initialize(*Level))) { return false; }

	const FIntPoint Fragile(1,0);
	const EPlayerRole A = EPlayerRole::PlayerA;
	const EPlayerRole B = EPlayerRole::PlayerB;
	Rules.TryMove(A, {1,0});
	Rules.TryMove(B, {1,0});
	TestEqual(TEXT("Occupied tile retains its clock"), Rules.GetTiles().FindChecked(Fragile).RemainingMoves, 2);
	TestTrue(TEXT("Other player cannot enter an occupied tile"), Rules.TryMove(B, {0,-1}).Result == EGridMoveResult::Occupied);
	Rules.TryMove(A, {1,0});
	TestEqual(TEXT("Departure retains the full clock"), Rules.GetTiles().FindChecked(Fragile).RemainingMoves, 2);
	Rules.TryMove(B, {-1,0});
	const int32 Moves = Rules.GetMoveCount();
	TestTrue(TEXT("Diagonal input rejected"), Rules.TryMove(B, {1,-1}).Result == EGridMoveResult::InvalidDirection);
	TestEqual(TEXT("Rejected input keeps move count"), Rules.GetMoveCount(), Moves);
	TestEqual(TEXT("Rejected input keeps the clock"), Rules.GetTiles().FindChecked(Fragile).RemainingMoves, 1);
	Rules.TryMove(B, {1,0});
	TestTrue(TEXT("Unoccupied tile collapses at zero"), Rules.GetTiles().FindChecked(Fragile).bIsCollapsed);
	TestTrue(TEXT("Collapsed tile rejects entry"), Rules.TryMove(B, {0,-1}).Result == EGridMoveResult::Collapsed);

	Level->Rows = { TEXT("Afa"), TEXT("B..b") };
	Rules.Initialize(*Level);
	Rules.TryMove(A, {1,0});
	Rules.TryMove(A, {1,0});
	Rules.TryMove(B, {1,0});
	TestEqual(TEXT("One move remains before hand-off"), Rules.GetTiles().FindChecked(Fragile).RemainingMoves, 1);
	FGridMove Handoff = Rules.TryMove(B, {0,-1});
	TestTrue(TEXT("Entering on the last move transfers ownership"), Handoff.bFootprintTransferred);
	TestEqual(TEXT("Handoff restores the clock"), Rules.GetTiles().FindChecked(Fragile).RemainingMoves, 2);
	Rules.TryMove(B, {0,1});
	TestTrue(TEXT("New owner cannot enter its own footprint"), Rules.TryMove(B, {0,-1}).Result == EGridMoveResult::OwnFootprint);
	TestTrue(TEXT("Finished player stays at its goal"), Rules.TryMove(A, {0,1}).Result == EGridMoveResult::Finished);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridBridgesTest, "OneStep.Rules.BridgeConditions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridBridgesTest::RunTest(const FString& Parameters)
{
	UGridLevelDataAsset* Level = NewObject<UGridLevelDataAsset>();
	Level->Rows = { TEXT("A.PGa"), TEXT("B.IHb") };
	const EPlayerRole A = EPlayerRole::PlayerA;
	const EPlayerRole B = EPlayerRole::PlayerB;
	FGridBridgeDefinition& Upper = Level->Bridges.AddDefaulted_GetRef();
	Upper.Marker = TEXT("G");
	Upper.Conditions = { { {2,0}, A } };
	FGridBridgeDefinition& Lower = Level->Bridges.AddDefaulted_GetRef();
	Lower.Marker = TEXT("H");
	Lower.Conditions = { { {2,0}, A }, { {2,1}, B } };

	FGridPuzzleRules Rules;
	if (!TestTrue(TEXT("Bridge fixture loads"), Rules.Initialize(*Level))) { return false; }
	const auto IsOpen = [&Rules](FIntPoint Position) { return Rules.GetTiles().FindChecked(Position).bIsActivated; };
	Rules.TryMove(B, {1,0});
	Rules.TryMove(B, {0,-1});
	Rules.TryMove(B, {1,0});
	TestFalse(TEXT("Wrong traveler does not activate plate"), IsOpen({3,0}));
	Rules.TryMove(B, {-1,0});
	Rules.TryMove(B, {0,1});
	Rules.TryMove(B, {1,0});
	Rules.TryMove(B, {-1,0});
	Rules.TryMove(A, {1,0});
	Rules.TryMove(A, {1,0});
	TestTrue(TEXT("Standing on the assigned plate opens G"), IsOpen({3,0}));
	TestTrue(TEXT("Both conditions open H"), IsOpen({3,1}));
	const FGridMove OnBridge = Rules.TryMove(A, {1,0});
	TestTrue(TEXT("A can enter before its own departure closes G"), OnBridge.Result == EGridMoveResult::Moved);
	TestFalse(TEXT("Leaving the plate closes G"), IsOpen({3,0}));
	TestFalse(TEXT("One missing condition closes H"), IsOpen({3,1}));
	TestTrue(TEXT("A can exit its closed bridge"), Rules.TryMove(A, {1,0}).Result == EGridMoveResult::Moved);
	Rules.TryMove(B, {0,-1});
	Rules.TryMove(B, {1,0});
	TestTrue(TEXT("Wrong traveler still cannot enter a closed bridge"),
		Rules.TryMove(B, {1,0}).Result == EGridMoveResult::ClosedBridge);
	TestTrue(TEXT("Reset restores valid level"), Rules.Initialize(*Level));
	TestFalse(TEXT("Reset clears bridge activation"), IsOpen({3,0}));
	TestEqual(TEXT("Reset clears move counter"), Rules.GetMoveCount(), 0);

	Level->Bridges[1].Conditions[1].Source = FIntPoint(4,1);
	TestFalse(TEXT("A bridge condition cannot reference a goal"), Rules.Initialize(*Level));
	Level->Bridges.Pop();
	TestFalse(TEXT("Every bridge group needs a definition"), Rules.Initialize(*Level));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridFreshPrintTest, "OneStep.Rules.RequiresFreshFootprints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridFreshPrintTest::RunTest(const FString& Parameters)
{
	const auto ProveUnreachable = [](const UGridLevelDataAsset& Level, int32 BannedRoles,
		int32 RoleChangeLimit = MAX_int32)
	{
		struct FSearchNode
		{
			FGridPuzzleRules Rules;
			int32 PreviousRole = 0;
			int32 RoleChanges = 0;
		};
		FSearchNode Current;
		if (!Current.Rules.Initialize(Level)) { return false; }
		const auto SearchKey = [RoleChangeLimit](const FSearchNode& Node)
		{
			FString Key = FGridTestSearch::StateKey(Node.Rules);
			if (RoleChangeLimit != MAX_int32)
			{
				Key.AppendChar(TCHAR('0') + Node.PreviousRole);
			}
			return Key;
		};
		TQueue<FSearchNode> Queue;
		TMap<FString, int32> FewestChanges;
		FewestChanges.Add(SearchKey(Current), 0);
		Queue.Enqueue(MoveTemp(Current));
		const FIntPoint Directions[] = { {1,0}, {0,-1}, {-1,0}, {0,1} };
		while (Queue.Dequeue(Current))
		{
			if (Current.Rules.IsComplete() || FewestChanges.Num() > 100000) { return false; }
			if (Current.RoleChanges > FewestChanges.FindChecked(SearchKey(Current))) { continue; }
			for (EPlayerRole Role : { EPlayerRole::PlayerA, EPlayerRole::PlayerB })
			{
				const int32 RoleMask = 1 << FGridPuzzleRules::RoleIndex(Role);
				const int32 Changed = RoleChangeLimit != MAX_int32 && Current.PreviousRole != 0 &&
					Current.PreviousRole != RoleMask;
				const int32 Cost = Current.RoleChanges + Changed;
				if (Cost > RoleChangeLimit) { continue; }
				for (FIntPoint Direction : Directions)
				{
				const FTileData* Target = Current.Rules.GetTiles().Find(Current.Rules.GetPosition(Role) + Direction);
					if (!Target || (Target->bFreshPrint && (RoleMask & BannedRoles))) { continue; }
					FSearchNode Next{ Current.Rules, RoleMask, Cost };
					if (Next.Rules.TryMove(Role, Direction).Result != EGridMoveResult::Moved) { continue; }
					FString Key = SearchKey(Next);
					const int32* KnownCost = FewestChanges.Find(Key);
					if (KnownCost && *KnownCost <= Cost) { continue; }
					FewestChanges.Add(MoveTemp(Key), Cost);
					Queue.Enqueue(MoveTemp(Next));
				}
			}
		}
		return true;
	};

	const TArray<UGridLevelDataAsset*> Levels = AGridManager::CreateDefaultLevels();
	const int32 MinimumRoleChanges[] = { 1, 3, 3, 3, 7, 3, 4, 4, 4, 7, 9, 7, 5, 8 };
	if (!TestEqual(TEXT("Role-change fixture count"), Levels.Num(), static_cast<int32>(UE_ARRAY_COUNT(MinimumRoleChanges))))
	{
		return false;
	}
	for (int32 Index = 0; Index < Levels.Num(); ++Index)
	{
		// Levels with actions are checked by AllChapters using the action-aware search.
		if (Levels[Index]->Prototype != EGridPrototype::None) { continue; }
		const FString Label = FString::Printf(TEXT("Chapter %d"), Index + 1);
		TestTrue(Label + TEXT(" requires borrowing a freshly written print"), ProveUnreachable(*Levels[Index], 3));
		TestTrue(FString::Printf(TEXT("%s requires at least %d changes of active traveler"),
			*Label, MinimumRoleChanges[Index]),
			ProveUnreachable(*Levels[Index], 0, MinimumRoleChanges[Index] - 1));
		if (Index >= 5)
		{
			TestTrue(Label + TEXT(" requires A to borrow a freshly written print"), ProveUnreachable(*Levels[Index], 1));
			TestTrue(Label + TEXT(" requires B to borrow a freshly written print"), ProveUnreachable(*Levels[Index], 2));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridPrintTimingTest, "OneStep.Rules.PrintDelivery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridPrintTimingTest::RunTest(const FString& Parameters)
{
	const TArray<UGridLevelDataAsset*> Levels = AGridManager::CreateDefaultLevels();
	FGridPuzzleRules Rules;
	const EPlayerRole A = EPlayerRole::PlayerA;
	const EPlayerRole B = EPlayerRole::PlayerB;
	if (!TestTrue(TEXT("Delayed chapter loads"), Rules.Initialize(*Levels.Last()))) { return false; }
	const FIntPoint Target(4,6);
	const auto Owner = [&Rules, Target]() { return Rules.GetTiles().FindChecked(Target).FootprintOwner; };
	Rules.TryMove(A, {1,0});
	Rules.TryMove(A, {1,0});
	TestTrue(TEXT("Blue print is in transit"), Rules.GetTiles().FindChecked(Target).bPrintPending);
	Rules.TryMove(A, {1,0});
	TestTrue(TEXT("Blank print pad rejects a traveler"), Rules.TryMove(A, {1,0}).Result == EGridMoveResult::EmptyPrint);
	Rules.TryMove(B, {1,0});
	TestTrue(TEXT("Two successful steps leave print undelivered"), Owner() == EFootprintOwner::None);
	Rules.TryMove(B, {1,0});
	TestTrue(TEXT("Third successful step delivers blue print"), Owner() == EFootprintOwner::PlayerA);
	TestFalse(TEXT("Delivered print is no longer pending"), Rules.GetTiles().FindChecked(Target).bPrintPending);

	UGridLevelDataAsset* Fixture = NewObject<UGridLevelDataAsset>();
	Fixture->Rows = { TEXT("Aca"), TEXT("B.b") };
	Fixture->bRequireBorrowedPrints = true;
	if (!TestTrue(TEXT("Single-use fixture loads"), Rules.Initialize(*Fixture))) { return false; }
	const FIntPoint SingleUse(1,0);
	TestTrue(TEXT("A can step on the one-use orange print"), Rules.TryMove(A, {1,0}).Result == EGridMoveResult::Moved);
	TestFalse(TEXT("Occupied one-use tile remains intact"), Rules.GetTiles().FindChecked(SingleUse).bIsCollapsed);
	Rules.TryMove(A, {1,0});
	TestTrue(TEXT("One-use print collapses on departure"), Rules.GetTiles().FindChecked(SingleUse).bIsCollapsed);
	Rules.TryMove(B, {1,0});
	TestTrue(TEXT("B cannot enter the collapsed blue print"), Rules.TryMove(B, {0,-1}).Result == EGridMoveResult::Collapsed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridEchoPickupTest, "OneStep.Rules.EchoPickups",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridEchoPickupTest::RunTest(const FString& Parameters)
{
	const TArray<UGridLevelDataAsset*> Levels = AGridManager::CreateDefaultLevels();
	int32 PickupCount = 0;
	for (int32 Index = 0; Index < Levels.Num(); ++Index)
	{
		FIntPoint Pickup;
		if (!AGridManager::GetEchoLocationForLevel(Index, Pickup)) { continue; }
		++PickupCount;
		TestTrue(FString::Printf(TEXT("Echo pickup in chapter %d is reachable"), Index + 1),
			FGridTestSearch::CanVisit(*Levels[Index], Pickup));
	}
	TestEqual(TEXT("Only selected chapters contain echo pickups"), PickupCount, 6);
	FIntPoint Missing;
	TestFalse(TEXT("Chapter 2 is not a new-mechanic chapter and has no echo pickup"),
		AGridManager::GetEchoLocationForLevel(1, Missing));
	TestFalse(TEXT("Chapter 2 does not show a new-mechanic popup for its side pocket route"),
		AGridManager::LevelHasMechanicIntro(1));
	TestFalse(TEXT("Chapter 4 does not treat route planning as a new mechanism"),
		AGridManager::LevelHasMechanicIntro(3));
	TestFalse(TEXT("Chapter 10 does not show a popup for a new use of an existing imprint"),
		AGridManager::LevelHasMechanicIntro(9));
	TestTrue(TEXT("Chapter 3 introduces switches and locked goals"),
		AGridManager::LevelHasMechanicIntro(2));
	return true;
}

#endif
