#include "GridPuzzleGame.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "GridPuzzleFusion"

// 玩法作为地块上的叠加状态组合，始终复用 MoveCharacter 的借印检查。
bool FGridPuzzleRules::InitializeFusion(const UGridLevelDataAsset& Level)
{
	RhythmPeriod = Level.RhythmPeriod;
	RhythmClosedPhase = Level.RhythmClosedPhase;
	if (RhythmPeriod < 2 || RhythmClosedPhase < 0 || RhythmClosedPhase >= RhythmPeriod) { return false; }
	for (FIntPoint Position : Level.IceTiles)
	{
		FTileData* Tile = Tiles.Find(Position);
		if (!Tile || !Tile->HasFootprints() || Tile->bFoldedAway) { return false; }
		Tile->bIce = true;
	}
	for (FIntPoint Position : Level.Crates)
	{
		FTileData* Tile = Tiles.Find(Position);
		if (!Tile || !Tile->HasFootprints() || Tile->bIsOccupied || Tile->bCrate || Tile->bFoldedAway) { return false; }
		Tile->bCrate = true;
	}
	for (FIntPoint Position : Level.RhythmTiles)
	{
		FTileData* Tile = Tiles.Find(Position);
		if (!Tile || !Tile->HasFootprints() || Tile->bFoldedAway) { return false; }
		Tile->bRhythm = true;
	}
	PatrolRoute = Level.PatrolRoute;
	for (int32 Index = 0; Index < PatrolRoute.Num(); ++Index)
	{
		const FIntPoint Position = PatrolRoute[Index];
		const FTileData* Tile = Tiles.Find(Position);
		const FIntPoint Delta = Position - PatrolRoute[(Index + 1) % PatrolRoute.Num()];
		if (!Tile || Tile->bIsOccupied || FMath::Abs(Delta.X) + FMath::Abs(Delta.Y) != 1) { return false; }
	}
	TArray<FIntPoint> Changed;
	UpdateFusionTiles(Changed);
	return true;
}

void FGridPuzzleRules::UpdateFusionTiles(TArray<FIntPoint>& Changed)
{
	for (auto& Pair : Tiles)
	{
		FTileData& Tile = Pair.Value;
		const bool bOpen = (MoveCount + 1) % RhythmPeriod != RhythmClosedPhase;
		const bool bPatrol = !PatrolRoute.IsEmpty() && Pair.Key == PatrolRoute[MoveCount % PatrolRoute.Num()];
		const bool bNext = !PatrolRoute.IsEmpty() && Pair.Key == PatrolRoute[(MoveCount + 1) % PatrolRoute.Num()];
		if ((Tile.bRhythm && Tile.bRhythmOpen != bOpen) || Tile.bPatrol != bPatrol || Tile.bPatrolNext != bNext)
		{
			Changed.AddUnique(Pair.Key);
		}
		Tile.bRhythmOpen = bOpen;
		Tile.bPatrol = bPatrol;
		Tile.bPatrolNext = bNext;
	}
}

TArray<UGridLevelDataAsset*> AGridManager::CreateFusionLevels()
{
	TArray<UGridLevelDataAsset*> Result;
	const auto Add = [&Result](FText Title, FText Hint, TArray<FString> Rows)
	{
		UGridLevelDataAsset* Level = NewObject<UGridLevelDataAsset>();
		Level->Title = MoveTemp(Title);
		Level->Hint = MoveTemp(Hint);
		Level->Rows = MoveTemp(Rows);
		Level->bRequireBorrowedPrints = true;
		Result.Add(Level);
		return Level;
	};
	UGridLevelDataAsset* Crate = Add(LOCTEXT("CrateTitle", "推箱接力"),
		LOCTEXT("CrateHint", "推箱必须踩进同伴留下的印记。先让 A 腾出推箱位，再由 B 反向接手。"),
		{TEXT("   oo"), TEXT(" AoouBa"), TEXT("   bo")});
	Crate->Crates = {{3, 1}};

	UGridLevelDataAsset* Ice = Add(LOCTEXT("IceTitle", "借你的滑道"),
		LOCTEXT("IceHint", "踏入 ICE 后沿连续同伴脚印滑行，直到下一格不可走或离开冰面。一次 Z 撤销整段滑行。"),
		{TEXT("    o"), TEXT("bAoouBa")});
	Ice->IceTiles = {{2, 1}, {3, 1}};

	UGridLevelDataAsset* Rhythm = Add(LOCTEXT("RhythmTitle", "给你留一拍"),
		LOCTEXT("RhythmHint", "BEAT 每三次成功落脚关闭一次。让同伴移动来让拍，切换角色不耗拍。"),
		{TEXT("    oo"), TEXT("bAoouBa")});
	Rhythm->RhythmTiles = {{3, 1}};

	UGridLevelDataAsset* Patrol = Add(LOCTEXT("PatrolTitle", "交替穿过巡逻"),
		LOCTEXT("PatrolHint", "红色巡逻点每次成功落脚前进一格，NEXT 是下一格。先让同伴脱离危险再沿他的脚印走。"),
		{TEXT("  ##o"), TEXT("bAoouBa")});
	Patrol->PatrolRoute = {{2, 0}, {3, 0}, {3, 1}, {2, 1}};
	return Result;
}

TArray<UGridLevelDataAsset*> AGridManager::CreateCampaignLevels()
{
	// 旧版资产可以是十四张原图，也可以是五个章节容器。新版直接生成五张棋盘。
	// 机关坐标和地形成对设计，采用配套的原始布局作为底图。
	const auto Original = CreateDefaultLevels();
	TArray<UGridLevelDataAsset*> Maps;
	const auto Map = [&](int32 Base, FText Title, FText Hint)
	{
		UGridLevelDataAsset* Board = Original[Base];
		Board->Title = MoveTemp(Title);
		Board->Hint = MoveTemp(Hint);
		Maps.Add(Board);
		return Board;
	};
	constexpr auto A = EFootprintOwner::PlayerA;
	constexpr auto B = EFootprintOwner::PlayerB;
	const auto Send = [](UGridLevelDataAsset* Board, FIntPoint Trigger, EFootprintOwner RequiredOwner,
		FIntPoint Source, FIntPoint Destination, int32 Delay = 0)
	{
		auto& Effect = Board->PrintEffects.AddDefaulted_GetRef();
		Effect.Trigger = Trigger;
		Effect.TriggerOwner = RequiredOwner;
		Effect.Links.Emplace(Source, Destination);
		Effect.DelayMoves = Delay;
		Effect.bOnce = true;
	};

	auto* First = Map(2, LOCTEXT("Map1", "第一章 · 回来接我"),
		LOCTEXT("Map1Hint", "B 先给 A 写下入口印。A 走到尽头开锁，B 再沿原路把 A 的回程改成橙印。"));
	First->Rows = {TEXT("x"), TEXT("Auooo"), TEXT(" B  o"), TEXT("    oooos"), TEXT("       b")};

	auto* Second = Map(4, LOCTEXT("Map2", "第二章 · 薄冰交班"),
		LOCTEXT("Map2Hint", "两人要在同一块脆石上交班。把上岸的脚印留好，最后从冰道滑过一次性石板。"));
	Second->Rows[0] = TEXT("  mocca");
	Second->IceTiles = {{3, 0}};
	Send(Second, {2, 4}, B, {2, 4}, {2, 0});

	auto* Third = Map(6, LOCTEXT("Map3", "第三章 · 印记中转站"),
		LOCTEXT("Map3Hint", "两岸同时留下正确印色。A 沿冰面去推箱，把蓝印寄到南岸；B 在南岸回寄橙印，A 才能出站。"));
	Third->Rows[0] = TEXT("        o");     // 箱子的停放点
	Third->Rows[1] = TEXT("        oma");  // 推箱位、等回信的空印、A 出口
	Third->Rows[7] = TEXT("        m");
	Third->Crates = {{8, 1}};
	Third->IceTiles = {{1, 2}};
	Third->RhythmTiles = {{2, 6}};
	Send(Third, {8, 1}, A, {8, 1}, {8, 7});
	Send(Third, {8, 7}, B, {8, 7}, {9, 1});

	auto* Fourth = Map(7, LOCTEXT("Map4", "第四章 · 隔岸同拍"),
		LOCTEXT("Map4Hint", "录下转弯的三步给 B。A 到东岸后在桥头留印、退回开关转桥，B 才能踩着这枚印走完回放后的路。"));
	Fourth->Rows[3] = TEXT("        o");
	Fourth->Rows[5] = TEXT("      mu b");
	Fourth->Rows[6] = TEXT("Bmuuumm");
	Fourth->TurnPivot = {7, 4};
	Fourth->InitialDeck = {{8, 3}};
	Fourth->AlternateDeck = {{8, 5}};
	Fourth->TurnControls = {{8, 2}};
	Fourth->RhythmTiles = {{1, 2}};

	auto* Fifth = Map(11, LOCTEXT("Map5", "第五章 · 一路接住你"),
		LOCTEXT("Map5Hint", "看巡逻点让步，互寄脚印到会合处。换位后把桥转给同伴；南岸保存的拓印，要等 A 到岸才会寄回北边。"));
	Fifth->Rows[1] = TEXT(" ##     m");
	Fifth->Rows[6] += TEXT("m"); // 隔空碑保存 B 的印色
	Fifth->PatrolRoute = {{1, 1}, {2, 1}, {2, 2}, {1, 2}};
	// 北岸门前的蓝印延时送达。南岸则取出此前寄存的橙印。
	Fifth->PrintEffects[0].DelayMoves = 2;
	Fifth->PrintEffects[1].Links[0].Target = {9, 6};
	Send(Fifth, {8, 6}, A, {9, 6}, {8, 7}, 1);
	return Maps;
}

#undef LOCTEXT_NAMESPACE
