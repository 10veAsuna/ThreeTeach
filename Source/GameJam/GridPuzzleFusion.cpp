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
	UGridLevelDataAsset* Crate = Add(LOCTEXT("CrateTitle", "推箱"),
		LOCTEXT("CrateHint", "箱子下面要有对方的脚印，才能推过去。也看看这样推会不会堵住同伴的路。"),
		{TEXT("   oo"), TEXT(" AoouBa"), TEXT("   bo")});
	Crate->Crates = {{3, 1}};

	UGridLevelDataAsset* Ice = Add(LOCTEXT("IceTitle", "冰面"),
		LOCTEXT("IceHint", "踩上 ICE 会继续滑。先看看一路上有哪些脚印。按 Z 可以退回上冰前。"),
		{TEXT("    o"), TEXT("bAoouBa")});
	Ice->IceTiles = {{2, 1}, {3, 1}};

	UGridLevelDataAsset* Rhythm = Add(LOCTEXT("RhythmTitle", "节拍"),
		LOCTEXT("RhythmHint", "BEAT 显示 WAIT 时，先让同伴走一步。按 Tab 换人不算一步。"),
		{TEXT("    oo"), TEXT("bAoouBa")});
	Rhythm->RhythmTiles = {{3, 1}};

	UGridLevelDataAsset* Patrol = Add(LOCTEXT("PatrolTitle", "巡逻"),
		LOCTEXT("PatrolHint", "每走一步，红点也走一格。走之前看一眼 NEXT，也看看同伴正站在哪。"),
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

	auto* First = Map(2, LOCTEXT("Map1", "初见"),
		LOCTEXT("Map1Hint", "窄路上怎么错身？看看上面那一格。"));
	First->Rows = {TEXT("    s"), TEXT("yAoouBx")};

	auto* Second = Map(4, LOCTEXT("Map2", "薄冰"),
		LOCTEXT("Map2Hint", "冰道入口还空着。谁能先送一枚脚印过去？"));
	Second->Rows[0] = TEXT("  mocca");
	Second->IceTiles = {{3, 0}};
	Send(Second, {2, 4}, B, {2, 4}, {2, 0});
	Second->Rows[5] = TEXT("  m");
	Second->Rows.Add(TEXT("  b"));
	Send(Second, {3, 0}, A, {3, 0}, {2, 5});

	auto* Third = Map(6, LOCTEXT("Map3", "来信"),
		LOCTEXT("Map3Hint", "看看 OUT 在等哪种颜色。收到脚印的人，也能把路送回去。"));
	Third->Rows[0] = TEXT("        o");     // 箱子的停放点
	Third->Rows[1] = TEXT("        ooa");  // 推箱位、等回信的空印、A 出口
	Third->Rows[7] = TEXT("        mmb");
	Third->Rows[8] = TEXT("        o");
	Third->Crates = {{8, 1}};
	Third->IceTiles = {{1, 2}};
	Third->RhythmTiles = {{2, 6}};
	Send(Third, {8, 1}, A, {8, 1}, {8, 7});
	Send(Third, {8, 7}, B, {8, 7}, {8, 1});
	Send(Third, {8, 1}, A, {8, 1}, {9, 7});
	// 必须等 B 在南碑上留下橙印，A 折返原碑时才可打开最后的路。
	Third->PrintEffects.Last().Requirements.Add({{8, 7}, EPlayerRole::PlayerB});
	Third->PrintEffects.Last().Links.Emplace(FIntPoint(8, 7), FIntPoint(9, 1));

	auto* Fourth = Map(7, LOCTEXT("Map4", "回声"),
		LOCTEXT("Map4Hint", "想想 A 录下这三步后，B 在对面能不能踩上脚印。"));
	Fourth->Rows[3] = TEXT("        o");
	Fourth->Rows[5] = TEXT("      mu umb");
	Fourth->Rows[6] = TEXT("Bmuuumm");
	Fourth->TurnPivot = {7, 4};
	Fourth->InitialDeck = {{8, 3}};
	Fourth->AlternateDeck = {{8, 5}};
	Fourth->TurnControls = {{8, 2}};
	// B 第一次上岸只恢复前半条旧路，最后一格等第二轮借印后才会送达。
	Fourth->PrintEffects.Last().Links.Pop();
	Send(Fourth, {8, 3}, A, {8, 3}, {10, 5});
	Fourth->PrintEffects.Last().Requirements.Add({{9, 5}, EPlayerRole::PlayerB});
	Send(Fourth, {10, 5}, B, {10, 5}, {7, 2});
	Fourth->RhythmTiles = {{1, 2}};

	auto* Fifth = Map(11, LOCTEXT("Map5", "同行"),
		LOCTEXT("Map5Hint", "走之前看一眼 NEXT，也看看同伴正站在哪。"));
	Fifth->Rows[1] = TEXT("   ###  m");
	Fifth->Rows[6] += TEXT("m"); // 隔空碑保存 B 的印色
	Fifth->PatrolRoute = {{5, 2}, {4, 2}, {3, 2}, {3, 1}, {4, 1}, {5, 1}};
	// 北岸门前的蓝印延时送达。南岸则取出此前寄存的橙印。
	Fifth->PrintEffects[0].DelayMoves = 2;
	Fifth->PrintEffects[1].Links[0].Target = {9, 6};
	Send(Fifth, {8, 6}, A, {9, 6}, {8, 7}, 1);
	return Maps;
}

#undef LOCTEXT_NAMESPACE
