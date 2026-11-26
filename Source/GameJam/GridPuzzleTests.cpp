#include "GridPuzzleGame.h"

// 开发构建中的规则测试，控制台使用 Automation RunTests OneStep 执行已注册用例。
// 测试直接推进规则状态，人物渲染、动画和输入衔接由实际试玩检查。
#if WITH_DEV_AUTOMATION_TESTS

#include "Containers/Queue.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Components/StaticMeshComponent.h"
#include "GridPuzzleAudioWave.h"
#include "Sound/SoundGenerator.h"

namespace
{
	constexpr EPlayerRole Blue = EPlayerRole::PlayerA;
	constexpr EPlayerRole Orange = EPlayerRole::PlayerB;
	const FIntPoint Up(0, -1), Down(0, 1), Left(-1, 0), Right(1, 0);
}

// 复制局面并搜索合法后继，用于验证机关必要性、借用新印的必要性与收集物可达性。
struct FGridTestSearch
{
	// 把角色、脚印、地块、寄印和录制状态编码为去重键，避免重复展开相同局面。
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
		// 新机关的时钟相位与箱位参与去重，避免把不同后继的局面误判成同一状态。
		int32 RhythmPhase = 0;
		for (const auto& Pair : Rules.Tiles)
		{
			if (Pair.Value.bRhythm) { RhythmPhase = Rules.MoveCount % Rules.RhythmPeriod; break; }
		}
		Key += FString::Printf(TEXT("|%d,%d|"), RhythmPhase,
			Rules.PatrolRoute.IsEmpty() ? 0 : Rules.MoveCount % Rules.PatrolRoute.Num());
		for (const auto& Pair : Rules.Tiles) { Key.AppendChar(Pair.Value.bCrate ? TEXT('C') : TEXT('.')); }
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

	// BannedAction 禁用指定机关，BannedRoles 使用 A=1、B=2 的位掩码限制借用新印。
	// 如果这些限制下仍能通关，说明当前关卡存在绕过设计核心的路线。
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
					const FGridMove Move = Next.TryMove(Role, Direction);
					bool bAllowed = true;
					for (FIntPoint Position : Move.Path)
					{
						bAllowed &= bAllowFreshPrints || !Current.GetTiles().FindChecked(Position).bFreshPrint;
					}
					if (Move.Result == EGridMoveResult::Moved && bAllowed) { Enqueue(MoveTemp(Next)); }
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

	// 允许普通移动与机关操作，搜索任意角色能否实际到达指定收集坐标。
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

	// 为融合小关生成一条可复现路线，测试日志会记录该路线供手动试玩对照。
	static TArray<FString> SolveFusion(const UGridLevelDataAsset& Level)
	{
		struct FNode { FGridPuzzleRules Rules; TArray<FString> Path; };
		FNode Start;
		if (!Start.Rules.Initialize(Level)) { return {}; }
		TArray<FNode> Queue;
		TSet<FString> Seen;
		Seen.Add(StateKey(Start.Rules));
		Queue.Add(MoveTemp(Start));
		const FIntPoint Directions[] = {Right, Up, Left, Down};
		const TCHAR* Letters[] = {TEXT("R"), TEXT("U"), TEXT("L"), TEXT("D")};
		for (int32 Index = 0; Index < Queue.Num() && Queue.Num() < 100000; ++Index)
		{
			const FNode Current = MoveTemp(Queue[Index]);
			if (Current.Rules.IsComplete()) { return Current.Path; }
			for (EPlayerRole Role : {Blue, Orange})
			{
				for (int32 Direction = 0; Direction < 4; ++Direction)
				{
					FNode Next = Current;
					if (Next.Rules.TryMove(Role, Directions[Direction]).Result != EGridMoveResult::Moved) { continue; }
					const FString Key = StateKey(Next.Rules);
					if (Seen.Contains(Key)) { continue; }
					Seen.Add(Key);
					Next.Path.Add(FString(Role == Blue ? TEXT("A") : TEXT("B")) + Letters[Direction]);
					Queue.Add(MoveTemp(Next));
				}
			}
		}
		return {};
	}
};

// 宏把测试类注册为控制台可查找的名字，RunTest 是对应的执行函数。
// 本用例检查独立试玩的转桥带印、回放失败回滚与双人换位状态。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridPrototypeTest, "OneStep.Rules.Prototypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridPrototypeTest::RunTest(const FString& Parameters)
{
	TArray<UGridLevelDataAsset*> Levels = AGridManager::CreatePrototypeLevels();
	Levels.SetNum(3);
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
	const FGridMove RecordedStep = Record.TryMove(Blue, Right);
	TestTrue(TEXT("A recorded landing carries its musical direction"), RecordedStep.bRecordedStep);
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
	TestTrue(TEXT("Only committed replay steps carry their response marker"),
		Steps.Num() == 3 && Steps[0].bReplayStep && Steps[1].bReplayStep && Steps[2].bReplayStep);
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

// 逐关执行参考解，并检查机关与双方新脚印在对应关卡中的必要性。
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
	// 每个词先写角色 A/B，再写方向 U/D/L/R；E 表示该角色触发关卡机关。
	// 数组顺序与关卡数组一致，机关试算执行完整规则，避免跳过其中的脚印判定。
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
		// 机关关额外搜索禁用机关后的局面，确认玩家必须使用该关的新玩法。
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
		// 统计参考解中实际借到脚印的行动比例，约束路线对主题规则的依赖。
		TestTrue(Chapter + TEXT(" borrows a print on at least 60 percent of its reference steps"),
			BorrowedPrintMoves * 5 >= Rules.GetMoveCount() * 3);
	}
	return true;
}

// 覆盖易碎板占用、离开、非法输入和归零坍塌的边界时机。
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

// 检查桥对站位与印色条件的响应，以及关闭时留在桥上的角色能否离开。
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

// 检查普通关卡必须借用玩家现场留下的新印，并需要达到预设的角色交接次数。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridFreshPrintTest, "OneStep.Rules.RequiresFreshFootprints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridFreshPrintTest::RunTest(const FString& Parameters)
{
	// 记录到达局面的最少角色切换次数，过滤成本更高的重复分支。
	// 发现通关或超过搜索上限时返回失败，避免把未搜索完误认为不可达。
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

// 检查延迟寄印的送达时机、目标占用等待，以及一次性印板的消耗时机。
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
	const FGridMove Dispatch = Rules.TryMove(A, {1,0});
	TestEqual(TEXT("Dispatch reports one outgoing print"), Dispatch.PrintDispatches.Num(), 1);
	TestTrue(TEXT("A delayed print has no arrival yet"), Dispatch.PrintArrivals.IsEmpty());
	TestTrue(TEXT("Blue print is in transit"), Rules.GetTiles().FindChecked(Target).bPrintPending);
	Rules.TryMove(A, {1,0});
	TestTrue(TEXT("Blank print pad rejects a traveler"), Rules.TryMove(A, {1,0}).Result == EGridMoveResult::EmptyPrint);
	Rules.TryMove(B, {1,0});
	TestTrue(TEXT("Two successful steps leave print undelivered"), Owner() == EFootprintOwner::None);
	const FGridMove Delivery = Rules.TryMove(B, {1,0});
	if (TestEqual(TEXT("The eventual delivery reports one arrival"), Delivery.PrintArrivals.Num(), 1))
	{
		TestTrue(TEXT("Delivery preserves the sending color and location"),
			Delivery.PrintArrivals[0].Owner == EFootprintOwner::PlayerA &&
			Delivery.PrintArrivals[0].From == FIntPoint(2, 2) && Delivery.PrintArrivals[0].To == Target);
	}
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
	FGridPuzzleRules Borrowing;
	TestTrue(TEXT("Fresh-print sound fixture initializes"), Borrowing.Initialize(*Levels[0]));
	TestEqual(TEXT("Preprinted terrain does not trigger a new-print response"),
		Borrowing.TryMove(A, {1, 0}).FreshBorrowedPrintCount, 0);
	Borrowing.TryMove(A, {1, 0});
	Borrowing.TryMove(A, {0, -1});
	Borrowing.TryMove(B, {-1, 0});
	TestEqual(TEXT("Borrowing the partner's live print triggers one response"),
		Borrowing.TryMove(B, {-1, 0}).FreshBorrowedPrintCount, 1);
	const FGridMove Blocked = Borrowing.TryMove(B, {0, -1});
	TestTrue(TEXT("Rejected actions emit no music events"),
		Blocked.Result == EGridMoveResult::Occupied && Blocked.FreshBorrowedPrintCount == 0 &&
		Blocked.PrintDispatches.IsEmpty() && Blocked.PrintArrivals.IsEmpty());
	return true;
}

// 搜索五张主线的回响可达性，并确认每张主线都有机制导览。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridEchoPickupTest, "OneStep.Rules.EchoPickups",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridEchoPickupTest::RunTest(const FString& Parameters)
{
	const TArray<UGridLevelDataAsset*> Levels = AGridManager::CreateCampaignLevels();
	int32 PickupCount = 0;
	for (int32 Index = 0; Index < Levels.Num(); ++Index)
	{
		FIntPoint Pickup;
		if (!AGridManager::GetEchoLocationForLevel(Index, Pickup)) { continue; }
		++PickupCount;
		TestTrue(FString::Printf(TEXT("Echo pickup on board %d is reachable"), Index + 1),
			FGridTestSearch::CanVisit(*Levels[Index], Pickup));
		TestTrue(FString::Printf(TEXT("Board %d has a mechanic introduction"), Index + 1),
			AGridManager::LevelHasMechanicIntro(Index));
	}
	TestEqual(TEXT("Every campaign board contains one echo pickup"), PickupCount, 5);
	TestFalse(TEXT("Mechanic previews do not show a campaign introduction"),
		AGridManager::LevelHasMechanicIntro(5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridFusionTest, "OneStep.Rules.Fusion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridFusionTest::RunTest(const FString& Parameters)
{
	const TArray<UGridLevelDataAsset*> Levels = AGridManager::CreateFusionLevels();
	if (!TestEqual(TEXT("Four fusion experiments"), Levels.Num(), 4)) { return false; }
	for (int32 Index = 0; Index < Levels.Num(); ++Index)
	{
		const TArray<FString> Path = FGridTestSearch::SolveFusion(*Levels[Index]);
		TestFalse(FString::Printf(TEXT("Fusion %d has a reference solution"), Index), Path.IsEmpty());
		UE_LOG(LogTemp, Display, TEXT("FUSION_ROUTE %d %s"), Index, *FString::Join(Path, TEXT(" ")));
		for (int32 RoleMask : {1, 2})
		{
			TestFalse(FString::Printf(TEXT("Fusion %d needs fresh prints for role %d"), Index, RoleMask),
				FGridTestSearch::CanClear(*Levels[Index], EGridPrototype::None, RoleMask));
		}
		FGridPuzzleRules Rules;
		TestTrue(TEXT("Fusion initializes"), Rules.Initialize(*Levels[Index]));
		const FString Before = FGridTestSearch::StateKey(Rules);
		TestTrue(TEXT("Invalid direction rejected"), Rules.TryMove(Blue, {1,1}).Result == EGridMoveResult::InvalidDirection);
		TestEqual(TEXT("Rejected action is atomic"), FGridTestSearch::StateKey(Rules), Before);
	}
	FGridPuzzleRules Ice;
	Ice.Initialize(*Levels[1]);
	const FGridPuzzleRules BeforeIce = Ice;
	const FGridMove Slide = Ice.TryMove(Blue, Right);
	TestEqual(TEXT("Slide borrows both consecutive prints"), Slide.Path.Num(), 2);
	TestEqual(TEXT("Slide stops before own print"), Ice.GetPosition(Blue), FIntPoint(3,1));
	Ice = BeforeIce;
	TestEqual(TEXT("Restoring snapshot restores every slid footprint"), Ice.GetMoveCount(), 0);
	TestTrue(TEXT("Restored slide can be repeated"), Ice.TryMove(Blue, Right).Path.Num() == 2);

	// 推箱也要先借到箱子脚下的印，不能靠推动箱子绕过自身脚印限制。
	UGridLevelDataAsset* OwnCrate = AGridManager::CreateFusionLevels()[0];
	OwnCrate->Rows[1][3] = TCHAR('u');
	FGridPuzzleRules CrateRules;
	TestTrue(TEXT("Own-print crate fixture initializes"), CrateRules.Initialize(*OwnCrate));
	CrateRules.TryMove(Blue, Right);
	const FString BeforeCrate = FGridTestSearch::StateKey(CrateRules);
	TestTrue(TEXT("Crate cannot bypass own print"), CrateRules.TryMove(Blue, Right).Result == EGridMoveResult::OwnFootprint);
	TestEqual(TEXT("Blocked push preserves crate and clock"), FGridTestSearch::StateKey(CrateRules), BeforeCrate);

	TArray<UGridLevelDataAsset*> Prototypes = AGridManager::CreatePrototypeLevels();
	UGridLevelDataAsset* Turning = Prototypes[0];
	Turning->TurnControls.Add({2, 2});
	Turning->Crates = {{3, 2}};
	Turning->IceTiles = {{3, 2}};
	Turning->RhythmTiles = {{3, 2}};
	FGridPuzzleRules Deck;
	TArray<FIntPoint> Changed;
	TestTrue(TEXT("Loaded fusion bridge initializes"), Deck.Initialize(*Turning));
	TestTrue(TEXT("Loaded bridge can turn"), Deck.TryTurnBridge(Blue, Changed));
	const FTileData& MovedDeck = Deck.GetTiles().FindChecked({2, 3});
	TestTrue(TEXT("Bridge carries crate and surface mechanics"), MovedDeck.bCrate && MovedDeck.bIce && MovedDeck.bRhythm);
	TestFalse(TEXT("Old slot releases crate"), Deck.GetTiles().FindChecked({3, 2}).bCrate);
	TestTrue(TEXT("Loaded bridge can turn back"), Deck.TryTurnBridge(Blue, Changed));
	TestTrue(TEXT("Bridge returns its crate and ice"), Deck.GetTiles().FindChecked({3, 2}).bCrate && Deck.GetTiles().FindChecked({3, 2}).bIce);

	UGridLevelDataAsset* Exchange = Prototypes[2];
	Exchange->RhythmTiles = Exchange->SwapTiles;
	Exchange->RhythmClosedPhase = 0;
	FGridPuzzleRules GateSwap;
	TestTrue(TEXT("Rhythmic swap initializes"), GateSwap.Initialize(*Exchange));
	GateSwap.TryMove(Blue, Right);
	GateSwap.TryMove(Orange, Left);
	const FString BeforeSwap = FGridTestSearch::StateKey(GateSwap);
	FGridMove SwapMove;
	TestFalse(TEXT("Swap obeys closed beat"), GateSwap.TrySwap(Blue, SwapMove));
	TestEqual(TEXT("Blocked swap is atomic"), FGridTestSearch::StateKey(GateSwap), BeforeSwap);

	UGridLevelDataAsset* Recording = Prototypes[1];
	Recording->IceTiles = {{2, 3}};
	FGridPuzzleRules ReplayIce;
	TestTrue(TEXT("Icy replay initializes"), ReplayIce.Initialize(*Recording));
	ReplayIce.StartRecording(Blue);
	ReplayIce.TryMove(Blue, Right);
	ReplayIce.TryMove(Blue, Up);
	ReplayIce.TryMove(Blue, Up);
	ReplayIce.TryMove(Orange, Up);
	TArray<FGridMove> Replay;
	TArray<FGridPuzzleRules> States;
	const FString BeforeReplay = FGridTestSearch::StateKey(ReplayIce);
	TestFalse(TEXT("Occupied final replay landing rejects plan"), ReplayIce.PlanReplay(Orange, Replay, States));
	if (TestEqual(TEXT("Icy replay evaluates all three commands"), Replay.Num(), 3))
	{
		TestEqual(TEXT("Replay borrows both icy cells"), Replay[0].Path.Num(), 2);
	}
	TestTrue(TEXT("Failed icy replay discards snapshots"), States.IsEmpty());
	TestEqual(TEXT("Icy replay preview preserves live state"), FGridTestSearch::StateKey(ReplayIce), BeforeReplay);

	const auto Chapters = AGridManager::CreateCampaignLevels();
	const TCHAR* Routes[] = {
		TEXT("AR AR BL BR AR AU BL BL AD AR AR BL BL BL"),
		TEXT("AR AR BL AU BL BL BL AD AR BR BU AL BU AL BD BD BD AR AU AU AU AR AR AR BD BD"),
		TEXT("AR AR AR AD AD AR BR BR BR BR BU BU BU AL AD AD AR AR AU BU BR BR BD BD BL AU AU AU AR AR AU BR BD BD BR BR BD AR AL AR AR BR BR"),
		TEXT("AR AR BR BR AR AE AR AR AU AR AD AR AR AD AU AE BR BR BE BR BR BR AE AD AU BR BR AL AL AL AL AL AL AL AL AU"),
		TEXT("AR AR AR BR BR BR BR AR BR BU BU BU AR AE AD AD AD BE BR AR AR AR BR AD AD BR BU BU"),
	};
	if (!TestEqual(TEXT("Five playable campaign boards"), Chapters.Num(), 5)) { return false; }
	for (int32 Index = 0; Index < Chapters.Num(); ++Index)
	{
		const auto* Board = Chapters[Index];
		FGridPuzzleRules Rules;
		if (!TestTrue(Board->Title.ToString(), Board->Sections.IsEmpty() && Rules.Initialize(*Board))) { return false; }
		TArray<FString> Inputs;
		FString(Routes[Index]).ParseIntoArrayWS(Inputs);
		int32 TurnCount = 0;
		bool bWaitedForReturnPrint = false;
		bool bQueuedFinalExit = false, bDeliveredFinalExit = false;
		for (const FString& Input : Inputs)
		{
			const EPlayerRole Role = Input[0] == 'A' ? Blue : Orange;
			if (Input[1] == 'E')
			{
				FGridMove ExchangeMove;
				TArray<FIntPoint> DeckChanges;
				if (Rules.TrySwap(Role, ExchangeMove)) { continue; }
				if (Rules.TryTurnBridge(Role, DeckChanges)) { ++TurnCount; continue; }
				if (Rules.StartRecording(Role)) { continue; }
				TArray<FGridMove> Moves;
				TArray<FGridPuzzleRules> CampaignStates;
				if (!TestTrue(TEXT("Legal replay on campaign board"), Rules.PlanReplay(Role, Moves, CampaignStates))) { return false; }
				Rules = MoveTemp(CampaignStates.Last());
			}
			else
			{
				const FIntPoint Dir = Input[1] == 'R' ? Right : Input[1] == 'L' ? Left : Input[1] == 'U' ? Up : Down;
				const FGridMove Move = Rules.TryMove(Role, Dir);
				if (!TestTrue(Board->Title.ToString() + TEXT(" ") + Input, Move.Result == EGridMoveResult::Moved)) { return false; }
				if (Index == 4 && Role == Blue && Move.To == FIntPoint(8, 6))
				{
					bQueuedFinalExit = true;
					const FTileData& Receiver = Rules.GetTiles().FindChecked({8, 7});
					TestTrue(TEXT("The stored orange print is visibly identified"), Rules.GetTiles().FindChecked({9, 6}).bStoredPrintSource);
					TestEqual(TEXT("The send tile displays its one-step delay"), Rules.GetTiles().FindChecked({8, 6}).PrintDelayMoves, 1);
					TestTrue(TEXT("A's move queues the final print"), Receiver.bPrintPending);
					TestEqual(TEXT("WAIT shows exactly one remaining move"), Receiver.PendingPrintSteps, 1);
					TestTrue(TEXT("WAIT identifies the queued color as orange"), Receiver.PendingPrintOwner == EFootprintOwner::PlayerB);
				}
				if (Index == 4 && Role == Orange && Move.To == FIntPoint(7, 2))
				{
					bDeliveredFinalExit = true;
					const FTileData& Receiver = Rules.GetTiles().FindChecked({8, 7});
					TestFalse(TEXT("B's next move clears the pending marker"), Receiver.bPrintPending);
					TestEqual(TEXT("The delivered receiver clears its countdown"), Receiver.PendingPrintSteps, 0);
					TestTrue(TEXT("B advances A's earlier orange dispatch"), Move.PrintArrivals.ContainsByPredicate([](const FGridPrintTransfer& Arrival)
					{
						return Arrival.From == FIntPoint(8, 6) && Arrival.To == FIntPoint(8, 7) && Arrival.Owner == EFootprintOwner::PlayerB;
					}));
				}
				if (Index == 2 && Rules.GetPosition(Blue) == FIntPoint(8, 1) &&
					Rules.GetPosition(Orange) != FIntPoint(8, 7))
				{
					bWaitedForReturnPrint = true;
					TestTrue(TEXT("First dispatch leaves the final southern gate empty"),
						Rules.GetTiles().FindChecked({9, 7}).FootprintOwner == EFootprintOwner::None);
				}
			}
		}
		TestTrue(TEXT("Both players finish on their original board"), Rules.IsComplete());
		if (Index == 4)
		{
			TestTrue(TEXT("The final route exercises both dispatch and delivery"), bQueuedFinalExit && bDeliveredFinalExit);
		}
		if (Index == 2)
		{
			TestTrue(TEXT("The first visit cannot replace the later return"), bWaitedForReturnPrint);
			TestTrue(TEXT("Both handoff slabs have been rewritten"),
				Rules.GetTiles().FindChecked({8, 1}).bFreshPrint &&
				Rules.GetTiles().FindChecked({8, 7}).bFreshPrint);
		}
		if (Index == 3) { TestEqual(TEXT("The same bridge makes an outward and return trip"), TurnCount, 2); }
		// 直接搜索禁用转桥／回放／换位后的合法局面，检查同图里的机关依赖。
		if (Index == 3 || Index == 4)
		{
			TestFalse(TEXT("The turning bridge cannot be bypassed"),
				FGridTestSearch::CanClear(*Board, EGridPrototype::TurnBridge));
			TestFalse(TEXT("The other interactive mechanism cannot be bypassed"),
				FGridTestSearch::CanClear(*Board, Index == 3 ? EGridPrototype::RecordRoute : EGridPrototype::SwapPlaces));
		}
	}
	return true;
}

// 使用真实管理器和运行时 Actor 检查整图内开锁与撤销，避免只测规则而遗漏场景与撤销历史。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridChapterRuntimeTest, "OneStep.Runtime.ContinuousBoard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridChapterRuntimeTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Runtime test world"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	AGridPuzzlePlayerController* Controller = World->SpawnActor<AGridPuzzlePlayerController>();
	AGridManager* Manager = World->SpawnActor<AGridManager>();
	Manager->bPlaySound = false;
	Manager->bPlayMusic = false;
	Manager->Levels.Append(AGridManager::CreateDefaultLevels());
	Manager->Initialize(*Controller);
	Manager->CompletedLevels.Reset();
	TestTrue(TEXT("First campaign level is initially unlocked"), Manager->CanSelectLevel(0));
	TestFalse(TEXT("Second campaign level is locked before the first is completed"),
		Manager->CanSelectLevel(1));
	TestTrue(TEXT("Mechanic previews remain directly accessible"),
		Manager->CanSelectLevel(Manager->GetCampaignLevelCount()));
	Manager->CompletedLevels.Add(0);
	TestTrue(TEXT("Completing a level unlocks exactly the next campaign level"),
		Manager->CanSelectLevel(1));
	TestFalse(TEXT("Completing one level does not unlock later campaign levels"),
		Manager->CanSelectLevel(2));
	// 回响与引导预先标记已完成，测试过程只操作关内状态。
	Manager->EarnedEchoes.Reset();
	Manager->SeenMechanicIntros.Reset();
	for (int32 Index = 0; Index < Manager->GetMainLevelCount(); ++Index)
	{
		Manager->EarnedEchoes.Add(Index);
		Manager->SeenMechanicIntros.Add(Index);
	}
	for (int32 Index = 0; Index < Manager->GetMainLevelCount(); ++Index)
	{
		Manager->CompletedLevels.Add(Index);
	}
	TestTrue(TEXT("Completing every campaign level unlocks the all-clear achievement"),
		Manager->IsAllClearAchievementUnlocked());
	Manager->SeenMechanicIntros.Remove(0);
	Manager->LoadLevel(0);
	Manager->SetScreen(EGridScreen::Playing);
	TestTrue(TEXT("Fresh players learn on the board with the first legal tile highlighted"),
		Manager->GetScreen() == EGridScreen::Playing && Manager->OpeningHighlight == FIntPoint(2, 1));
	Manager->Move(Right);
	TestFalse(TEXT("The opening highlight clears once the tile is used"), Manager->OpeningHighlight.IsSet());
	const FString BeforeConfirm = FGridTestSearch::StateKey(Manager->Rules);
	Manager->RequestReset();
	TestTrue(TEXT("Restarting a played board asks for confirmation"), Manager->GetScreen() == EGridScreen::Confirm);
	Manager->CancelBoardChange();
	TestEqual(TEXT("Cancel preserves the actual board and undo history"), FGridTestSearch::StateKey(Manager->Rules), BeforeConfirm);
	TestTrue(TEXT("Cancel returns to the previous game screen"), Manager->GetScreen() == EGridScreen::Playing && Manager->HasUndoMove());
	Manager->RequestReset();
	Manager->ConfirmBoardChange();
	TestEqual(TEXT("Confirming a restart returns to the start"), Manager->GetMoveCount(), 0);
	TestTrue(TEXT("Restart preserves unlocked chapters and echoes"), Manager->IsAllClearAchievementUnlocked() && Manager->HasCurrentLevelEcho());
	Manager->ShowPractice();
	Manager->SelectLevel(Manager->GetCampaignLevelCount());
	TestTrue(TEXT("Standalone practice opens a playable practice board"), Manager->IsPrototype() && Manager->GetScreen() == EGridScreen::Playing);
	Manager->SeenMechanicIntros.Add(0);
	Manager->LoadLevel(0);
	Manager->SetScreen(EGridScreen::Playing);
	World->GetTimerManager().Tick(0.f);
	AGridTile* OriginalGoal = Manager->TileActors.FindRef({6, 1});
	TestEqual(TEXT("Campaign map has one board"), Manager->GetSectionCount(), 1);
	const FText BeforeUnlockHint = Manager->GetLevelHint();
	const FString BeforePreview = FGridTestSearch::StateKey(Manager->Rules);
	Manager->RequestNextStep();
	TestTrue(TEXT("The hint shortcut immediately offers a verified opening action"), Manager->StepHint.IsSet());
	TestEqual(TEXT("Requesting a hint preserves the live board"), FGridTestSearch::StateKey(Manager->Rules), BeforePreview);
	Manager->ToggleMovePreview();
	TestFalse(TEXT("Movement preview replaces the old next-step hint"), Manager->StepHint.IsSet());
	TestTrue(TEXT("Movement preview marks the current board"), !Manager->MovePreviewMarkers.IsEmpty());
	TestEqual(TEXT("Movement preview preserves every rule state"),
		FGridTestSearch::StateKey(Manager->Rules), BeforePreview);
	Manager->SwitchCharacter();
	TestTrue(TEXT("Changing players clears the movement preview"), Manager->MovePreviewMarkers.IsEmpty());
	const TCHAR* Path[] = {TEXT("AR"), TEXT("AR"), TEXT("BL"), TEXT("BR"), TEXT("AR"), TEXT("AU")};
	for (const TCHAR* Step : Path)
	{
		Manager->ActiveRole = Step[0] == 'A' ? Blue : Orange;
		Manager->UnlockInputAt = 0.f;
		Manager->Move(Step[1] == 'R' ? Right : Step[1] == 'L' ? Left : Step[1] == 'U' ? Up : Down);
	}
	TestFalse(TEXT("Switch unlocks the far goal"), Manager->Rules.GetTiles().FindChecked({6, 1}).bIsLocked);
	TestFalse(TEXT("Operating the switch does not finish the board"), Manager->Rules.IsComplete());
	TestFalse(TEXT("Switch updates the situational hint"), Manager->GetLevelHint().EqualTo(BeforeUnlockHint));
	Manager->UndoMove();
	TestTrue(TEXT("Undo restores the lock"), Manager->Rules.GetTiles().FindChecked({6, 1}).bIsLocked);
	TestTrue(TEXT("Undo restores the previous hint"), Manager->GetLevelHint().EqualTo(BeforeUnlockHint));
	TestTrue(TEXT("The same goal actor remains on screen"), OriginalGoal && Manager->TileActors.FindRef({6, 1}) == OriginalGoal);
	TestEqual(TEXT("Undo restores the board step count"), Manager->GetMoveCount(), 5);
	Manager->ResetLevel();
	TestEqual(TEXT("Reset restarts whole chapter"), Manager->GetMoveCount(), 0);
	TestFalse(TEXT("Reset clears undo history"), Manager->HasUndoMove());
	Manager->RequestNextStep();
	Manager->UnlockInputAt = 0.f;
	Manager->Move(Right);
	TestFalse(TEXT("A successful move clears a stale hint"), Manager->StepHint.IsSet() || Manager->HintSearch.IsValid());
	Manager->LoadLevel(4);
	Manager->SetScreen(EGridScreen::Playing);
	TestTrue(TEXT("Final chapter exposes its relay groups"), !Manager->PrintConnections.IsEmpty());
	Manager->TogglePrintLinks();
	TestTrue(TEXT("All mode reveals a relay line"), Manager->PrintConnections[0].Pieces[0]->IsVisible());
	Manager->TogglePrintLinks();
	TestFalse(TEXT("Hidden mode hides relay lines"), Manager->PrintConnections[0].Pieces[0]->IsVisible());
	Manager->TogglePrintLinks();
	for (int32 Step = 0; Step < 2; ++Step) { Manager->UnlockInputAt = 0.f; Manager->Move(Right); }
	int32 VisibleGroups = 0;
	for (const auto& Connection : Manager->PrintConnections)
	{
		VisibleGroups += Connection.Pieces.ContainsByPredicate([](const TWeakObjectPtr<UStaticMeshComponent>& Piece)
		{
			return Piece.IsValid() && Piece->IsVisible();
		}) ? 1 : 0;
	}
	TestEqual(TEXT("Related mode shows a single nearby relay group"), VisibleGroups, 1);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

// 直接读取音频线程使用的生成器，验证尾部释放、循环衔接及重复播放时的独立位置。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridAudioPlaybackTest, "OneStep.Audio.PcmPlayback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridAudioPlaybackTest::RunTest(const FString& Parameters)
{
	FSoundGeneratorInitParams Params{};
	UGridPcmSoundWave* Cue = NewObject<UGridPcmSoundWave>();
	Cue->SetPcm(TArray<int16>{16384, -8192, 4096}, 1, 24000, false);
	const auto First = Cue->CreateSoundGenerator(Params);
	const auto Second = Cue->CreateSoundGenerator(Params);
	float Buffer[8];
	TestFalse(TEXT("A short cue starts unfinished"), First->IsFinished());
	First->GetNextBuffer(Buffer, 2);
	TestEqual(TEXT("PCM is converted to the mixer range"), Buffer[0], .5f);
	TestFalse(TEXT("The cue preserves its remaining sample"), First->IsFinished());
	First->GetNextBuffer(Buffer, 8);
	TestEqual(TEXT("The final sample is played"), Buffer[0], .125f);
	for (int32 Index = 1; Index < 8; ++Index)
	{
		TestEqual(TEXT("The end of a short buffer is padded with silence"), Buffer[Index], 0.f);
	}
	TestTrue(TEXT("An exhausted cue releases its voice instead of streaming silence"), First->IsFinished());
	Second->GetNextBuffer(Buffer, 2);
	TestEqual(TEXT("A new playback has an independent cursor"), Buffer[0], .5f);

	UGridPcmSoundWave* Bed = NewObject<UGridPcmSoundWave>();
	Bed->SetPcm(TArray<int16>{16384, -16384, 8192, -8192}, 2, 24000, true);
	const auto Loop = Bed->CreateSoundGenerator(Params);
	for (int32 Callback = 0; Callback < 200; ++Callback)
	{
		Loop->GetNextBuffer(Buffer, 8);
		TestFalse(TEXT("The background remains active across many loop boundaries"), Loop->IsFinished());
		TestEqual(TEXT("The loop wraps without inserting a silent block"), Buffer[4], .5f);
		TestEqual(TEXT("Stereo ordering is preserved across the boundary"), Buffer[5], -.5f);
	}
	TestTrue(TEXT("Muted background music retains its playback position"),
		Bed->VirtualizationMode == EVirtualizationMode::PlayWhenSilent);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridHintPlannerTest, "OneStep.Rules.NextStepHints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridHintPlannerTest::RunTest(const FString& Parameters)
{
	const auto Campaign = AGridManager::CreateCampaignLevels();
	TArray<UGridLevelDataAsset*> Boards = Campaign;
	Boards.Append(AGridManager::CreatePrototypeLevels());
	for (int32 Index = 0; Index < Boards.Num(); ++Index)
	{
		FGridPuzzleRules Live;
		if (!TestTrue(TEXT("Hint board initializes"), Live.Initialize(*Boards[Index]))) { return false; }
		const auto Guide = FGridHintGuide::Build(Live, Index);
		for (int32 Step = 0; Step < 150 && !Live.IsComplete(); ++Step)
		{
			const FString Before = Live.GetHintStateKey();
			FGridHintSearch Search(Live, Guide);
			while (Search.GetStatus() == FGridHintSearch::EStatus::Searching) { Search.Advance(.01); }
			if (!TestTrue(Boards[Index]->Title.ToString() + TEXT(" has a winning next action"),
				Search.GetStatus() == FGridHintSearch::EStatus::Found)) { return false; }
			TestEqual(TEXT("Looking for a hint does not mutate the live board"), Live.GetHintStateKey(), Before);
			FGridHintAction Action = Search.GetAction();
			if (!TestTrue(TEXT("The proposed move or mechanism is legal"), FGridHintSearch::Apply(Live, Action))) { return false; }
		}
		TestTrue(TEXT("Following the hints finishes both roles"), Live.IsComplete());
	}
	FGridPuzzleRules Detour;
	Detour.Initialize(*Campaign[0]);
	const auto Guide = FGridHintGuide::Build(Detour, 0);
	Detour.TryMove(Blue, Right);
	Detour.TryMove(Orange, Left);
	Detour.TryMove(Orange, Right);
	TestTrue(TEXT("The reordered detour is outside the reference guide"), Guide->Find(Detour) == nullptr);
	FGridHintSearch Recovery(Detour, Guide);
	while (Recovery.GetStatus() == FGridHintSearch::EStatus::Searching) { Recovery.Advance(.01); }
	TestTrue(TEXT("An off-guide live state gets a searched continuation"), Recovery.GetStatus() == FGridHintSearch::EStatus::Found);
	if (Recovery.GetStatus() == FGridHintSearch::EStatus::Found)
	{
		FGridHintAction Next = Recovery.GetAction();
		TestTrue(TEXT("The recovered action is legal in the detour state"), FGridHintSearch::Apply(Detour, Next));
	}
	UGridLevelDataAsset* Split = NewObject<UGridLevelDataAsset>();
	Split->Rows = {TEXT("bAo#uBa")};
	FGridPuzzleRules DeadEnd;
	TestTrue(TEXT("Separated exit test board initializes"), DeadEnd.Initialize(*Split));
	FGridHintSearch Impossible(DeadEnd, MakeShared<FGridHintGuide>());
	while (Impossible.GetStatus() == FGridHintSearch::EStatus::Searching) { Impossible.Advance(.01); }
	TestTrue(TEXT("An exhausted search reports a proven dead end"), Impossible.GetStatus() == FGridHintSearch::EStatus::NoSolution);
	FGridHintSearch Limited(DeadEnd, MakeShared<FGridHintGuide>(), 1);
	Limited.Advance(.01);
	TestTrue(TEXT("A resource limit is not reported as an impossible puzzle"), Limited.GetStatus() == FGridHintSearch::EStatus::LimitReached);
	return true;
}

#endif
