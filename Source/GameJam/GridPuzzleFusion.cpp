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

TArray<UGridLevelDataAsset*> AGridManager::CreateCampaignLevels(const TArray<UGridLevelDataAsset*>& SourceLevels)
{
	if (SourceLevels.Num() != 14) { return {}; }
	for (int32 Index = 0; Index < SourceLevels.Num(); ++Index)
	{
		if (!SourceLevels[Index]) { return {}; }
		SourceLevels[Index]->LegacyChapterIndex = Index;
	}
	TArray<UGridLevelDataAsset*> Chapters;
	const auto Add = [&Chapters, &SourceLevels](FText Title, TArray<int32> Indices)
	{
		UGridLevelDataAsset* Chapter = NewObject<UGridLevelDataAsset>();
		Chapter->Title = MoveTemp(Title);
		for (int32 Index : Indices) { Chapter->Sections.Add(SourceLevels[Index]); }
		Chapters.Add(Chapter);
		return Chapter;
	};
	const TArray<UGridLevelDataAsset*> Fusion = CreateFusionLevels();
	Add(LOCTEXT("Chapter1", "第一章 · 交替留路"), {0, 1, 2});
	UGridLevelDataAsset* Second = Add(LOCTEXT("Chapter2", "第二章 · 把路借给你"), {3, 4, 8});
	Second->Sections.Add(Fusion[1]);
	UGridLevelDataAsset* Third = Add(LOCTEXT("Chapter3", "第三章 · 印记中转站"), {6, 9, 10});
	Third->Sections.Add(Fusion[0]);
	// 同一块地图中组合推箱、冰面与节拍，滑行会逐格推箱和推进拍点。
	UGridLevelDataAsset* Combined = DuplicateObject<UGridLevelDataAsset>(Fusion[0], GetTransientPackage());
	Combined->Title = LOCTEXT("CombinedTitle", "滑行搬运站");
	Combined->Hint = LOCTEXT("CombinedHint", "先滑行推箱让出路，再让同伴借印反推。BEAT 格需配合另一人的落脚时机。");
	Combined->IceTiles = {{2, 1}};
	Combined->RhythmTiles = {{4, 1}};
	Third->Sections.Last() = Combined;
	UGridLevelDataAsset* Fourth = Add(LOCTEXT("Chapter4", "第四章 · 隔岸同拍"), {5, 7});
	Fourth->Sections.Add(Fusion[2]);
	UGridLevelDataAsset* Fifth = Add(LOCTEXT("Chapter5", "第五章 · 一路接住你"), {11, 12, 13});
	Fifth->Sections.Add(Fusion[3]);
	return Chapters;
}

#undef LOCTEXT_NAMESPACE
