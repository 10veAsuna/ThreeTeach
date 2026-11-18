#include "GridPuzzleGame.h"

#define LOCTEXT_NAMESPACE "GridPuzzleChaptersV4"

TArray<UGridLevelDataAsset*> AGridManager::CreateDefaultLevels()
{
	TArray<UGridLevelDataAsset*> Result;
	const auto Add = [&Result](FText Title, FText Hint, TArray<FString> Rows, int32 Lifetime = 3, bool bChallenge = false)
	{
		UGridLevelDataAsset* Level = NewObject<UGridLevelDataAsset>();
		Level->Title = MoveTemp(Title);
		Level->Hint = MoveTemp(Hint);
		Level->Rows = MoveTemp(Rows);
		Level->FragileLifetime = Lifetime;
		Level->bChallenge = bChallenge;
		Level->bRequireBorrowedPrints = true;
		Result.Add(Level);
		return Level;
	};
	const auto Print = [](UGridLevelDataAsset& Level, FIntPoint Trigger, EFootprintOwner RequiredPrint,
		TArray<FGridPrintLink> Links, TArray<FGridBridgeCondition> Requirements = {},
		int32 Delay = 0, bool bOnce = false, bool bMove = false)
	{
		FGridPrintEffect& Effect = Level.PrintEffects.AddDefaulted_GetRef();
		Effect.Trigger = Trigger;
		Effect.TriggerOwner = RequiredPrint;
		Effect.Links = MoveTemp(Links);
		Effect.Requirements = MoveTemp(Requirements);
		Effect.DelayMoves = Delay;
		Effect.bOnce = bOnce;
		Effect.bMovePrint = bMove;
	};
	constexpr EFootprintOwner A = EFootprintOwner::PlayerA;
	constexpr EFootprintOwner B = EFootprintOwner::PlayerB;
	constexpr EFootprintOwner Any = EFootprintOwner::None;
	constexpr EPlayerRole RoleA = EPlayerRole::PlayerA;
	constexpr EPlayerRole RoleB = EPlayerRole::PlayerB;

	Add(LOCTEXT("Chapter01", "把下一步留给你"),
		LOCTEXT("Hint01", "看脚印的主人。A 先走中间的橙印，离开后 B 才能跟上。"), {
		TEXT("  a"),
		TEXT("  o"),
		TEXT("AoouB"),
		TEXT("  u"),
		TEXT("  b")
	});

	Add(LOCTEXT("Chapter02", "借你一格"),
		LOCTEXT("Hint02", "让进侧袋时也会留下自己的印。先让同伴经过，再回来借他留下的脚印。"), {
		TEXT("    o"),
		TEXT("bAoouBa")
	});

	Add(LOCTEXT("Chapter03", "回来接我"),
		LOCTEXT("Hint03", "请 B 先开入口。A 到尽头以后，B 还得把整条回程路改写一遍。"), {
		TEXT("x"),
		TEXT("Auooos"),
		TEXT(" B  b")
	});

	Add(LOCTEXT("Chapter04", "岔路留给谁"),
		LOCTEXT("Hint04", "南边的蓝印要留给 B。先看他怎么过河，再选 A 的支路。"), {
		TEXT("  uoooo"),
		TEXT("Aou   oa"),
		TEXT("  uooou"),
		TEXT("  B b")
	});

	Add(LOCTEXT("Chapter05", "慢半步"),
		LOCTEXT("Hint05", "先让同伴改好两侧的脚印。离开脆弱石板后，接手只有两次成功移动的时间。"), {
		TEXT("  a"),
		TEXT("  u"),
		TEXT("  .  #"),
		TEXT("AofuuB"),
		TEXT("  u"),
		TEXT("  b")
	}, 2);

	{
		UGridLevelDataAsset* Level = Add(LOCTEXT("Chapter06", "隔岸寄步"),
			LOCTEXT("Hint06Turn", "A 走过桥后把蓝印转给 B。B 从下岸过桥，再把橙印转回去。"), {
			TEXT("        b"),
			TEXT("        u"),
			TEXT("Aoooooouu"),
			TEXT("      o o"),
			TEXT("      too"),
			TEXT("        o"),
			TEXT("Buuuuu  o"),
			TEXT("        o"),
			TEXT("        x")
		});
		Level->Prototype = EGridPrototype::TurnBridge;
		Level->TurnPivot = {6, 4};
		Level->InitialDeck = {{7, 4}, {8, 4}};
		Level->AlternateDeck = {{6, 5}, {6, 6}};
		Level->TurnControls = {{8, 3}, {6, 4}};
	}

	{
		UGridLevelDataAsset* Level = Add(LOCTEXT("Chapter07", "拼一条路"),
			LOCTEXT("Hint07", "两枚印同时满足，才会在中段拓出路。你还要借同伴的脚印跨过两道缺口。"), {
			TEXT("        a"),
			TEXT("        o"),
			TEXT("Aooooumoo"),
			TEXT("    o u"),
			TEXT("    oou"),
			TEXT("    u o"),
			TEXT("Buuuuomuu"),
			TEXT("        u"),
			TEXT("        b")
		});
		Print(*Level, { 4, 2 }, Any, { { { 4, 2 }, { 6, 2 } }, { { 4, 6 }, { 6, 6 } } }, { { { 4, 2 }, RoleA }, { { 4, 6 }, RoleB } });
		Print(*Level, { 4, 6 }, Any, { { { 4, 2 }, { 6, 2 } }, { { 4, 6 }, { 6, 6 } } }, { { { 4, 2 }, RoleA }, { { 4, 6 }, RoleB } });
	}

	{
		UGridLevelDataAsset* Level = Add(LOCTEXT("Chapter08", "脚印渡口"),
			LOCTEXT("Hint08Replay", "先互送入口印。A 在 REC 录下对岸需要的转弯，B 在 PLAY 回放并寄回橙印。"), {
			TEXT(""),
			TEXT("x    oo"),
			TEXT("Aoomoooos"),
			TEXT(""),
			TEXT(""),
			TEXT("      muu"),
			TEXT("Bmuuumm ub")
		});
		Level->Prototype = EGridPrototype::RecordRoute;
		Level->RecordStart = {3, 2};
		Level->ReplayStart = {4, 6};
		Level->ReplayTiles = {{5, 6}, {6, 6}, {6, 5}};
		Print(*Level, { 1, 2 }, A, { { { 1, 2 }, { 1, 6 } } }, {}, 0, true);
		Print(*Level, { 2, 6 }, B, { { { 2, 6 }, { 3, 2 } } }, {}, 0, true);
		Print(*Level, { 4, 2 }, A, { { { 4, 2 }, { 5, 6 } } }, {}, 0, true);
		Print(*Level, { 5, 2 }, A, { { { 5, 2 }, { 6, 6 } } }, {}, 0, true);
		Print(*Level, { 5, 1 }, A, { { { 5, 1 }, { 6, 5 } } }, {}, 0, true);
		Print(*Level, { 7, 5 }, B, { { { 7, 5 }, { 1, 2 } }, { { 7, 5 }, { 2, 2 } }, { { 7, 5 }, { 3, 2 } }, { { 7, 5 }, { 4, 2 } }, { { 7, 5 }, { 5, 2 } }, { { 7, 5 }, { 6, 2 } }, { { 7, 5 }, { 7, 2 } } });
	}

	Add(LOCTEXT("Chapter09", "只能借一次"),
		LOCTEXT("Hint09", "先改写共用路，再分开去出口。标着 1 的印板，离开就会落下。"), {
		TEXT("         a"),
		TEXT("         c"),
		TEXT("Auuooooooo"),
		TEXT(" Bo   d"),
		TEXT("      u"),
		TEXT("      b")
	});

	{
		UGridLevelDataAsset* Level = Add(LOCTEXT("Chapter10", "一枚印，两条路"),
			LOCTEXT("Hint10", "每次改写中间的碑，开放的是另一人的路。留意碑两侧的退路。"), {
			TEXT("   a"),
			TEXT("  um"),
			TEXT("Aooooouub"),
			TEXT("   uo"),
			TEXT("   m"),
			TEXT("   u"),
			TEXT("   B")
		});
		Print(*Level, { 3, 2 }, A, { { { 3, 2 }, { 3, 4 } } });
		Print(*Level, { 3, 2 }, B, { { { 3, 2 }, { 3, 1 } } });
	}

	{
		UGridLevelDataAsset* Level = Add(LOCTEXT("Chapter11", "留一张拓本"),
			LOCTEXT("Hint11", "先保存蓝印。随后原印会多次改色，别把留给同伴的拓本走掉。"), {
			TEXT("        b"),
			TEXT("Aooooomom"),
			TEXT("  u    ."),
			TEXT("  u oooua"),
			TEXT("  B")
		});
		Print(*Level, { 3, 1 }, Any, { { { 3, 1 }, { 8, 1 } } }, {}, 0, true);
		Print(*Level, { 3, 1 }, B, { { { 3, 1 }, { 6, 1 } } });
	}

	{
		UGridLevelDataAsset* Level = Add(LOCTEXT("Chapter12", "最后一步给你"),
			LOCTEXT("Hint12SwapTurn", "先互寄进桥的脚印，到 SWAP 处一起换位。等 A 离桥，B 再转桥上岸。"), {
			TEXT("        b"),
			TEXT("        m"),
			TEXT("Aooomm  u"),
			TEXT("     u"),
			TEXT("     u"),
			TEXT("     u"),
			TEXT("Buuummooo"),
			TEXT("        m"),
			TEXT("        a")
		});
		Level->Prototype = EGridPrototype::TurnBridgeAndSwap;
		Level->TurnPivot = {5, 2};
		Level->InitialDeck = {{5, 3}, {5, 4}};
		Level->AlternateDeck = {{6, 2}, {7, 2}};
		Level->TurnControls = {{5, 2}};
		Level->SwapTiles = {{5, 2}, {5, 3}};
		Print(*Level, { 3, 2 }, A, { { { 3, 2 }, { 8, 1 } }, { { 3, 2 }, { 4, 6 } } });
		Print(*Level, { 3, 6 }, B, { { { 3, 6 }, { 8, 7 } }, { { 3, 6 }, { 4, 2 } } });
		Print(*Level, { 4, 2 }, A, { { { 4, 2 }, { 5, 6 } } });
		Print(*Level, { 5, 5 }, B, { { { 5, 5 }, { 5, 2 } } });
	}

	{
		UGridLevelDataAsset* Level = Add(LOCTEXT("Chapter13", "旧路重现"),
			LOCTEXT("Hint13", "先封存 B 的橙印。等 A 写完去路，再用旧印恢复回程。A 回去时会替 B 补最后一印。"), {
			TEXT("   x"),
			TEXT("Aoomoos"),
			TEXT(""),
			TEXT(""),
			TEXT(""),
			TEXT("Bmuumuuumb"),
			TEXT(""),
			TEXT("        m")
		}, 3, true);
		Print(*Level, { 1, 1 }, A, { { { 1, 1 }, { 1, 5 } } }, {}, 0, true);
		Print(*Level, { 2, 5 }, B, { { { 2, 5 }, { 8, 7 } }, { { 2, 5 }, { 3, 1 } } }, {}, 0, true);
		Print(*Level, { 4, 1 }, A, { { { 4, 1 }, { 4, 5 } } });
		Print(*Level, { 4, 5 }, B, { { { 8, 7 }, { 3, 1 } }, { { 8, 7 }, { 4, 1 } }, { { 8, 7 }, { 5, 1 } } }, { { { 5, 1 }, RoleA } });
		Print(*Level, { 3, 1 }, A, { { { 3, 1 }, { 8, 5 } } }, { { { 4, 5 }, RoleB } });
	}

	{
		UGridLevelDataAsset* Level = Add(LOCTEXT("Chapter14", "迟到的脚印"),
			LOCTEXT("Hint14", "两轮寄印都有三步延迟。先换岸再折返，留意同伴何时能接下一枚。"), {
			TEXT("        b"),
			TEXT("        u"),
			TEXT("Aooomoomu"),
			TEXT("      o"),
			TEXT("      .o"),
			TEXT("      u"),
			TEXT("Buuumuumo"),
			TEXT("        o"),
			TEXT("        a")
		}, 3, true);
		Print(*Level, { 2, 2 }, A, { { { 2, 2 }, { 4, 6 } } }, {}, 3);
		Print(*Level, { 2, 6 }, B, { { { 2, 6 }, { 4, 2 } } }, {}, 3);
		Print(*Level, { 5, 2 }, A, { { { 5, 2 }, { 7, 6 } } }, {}, 3);
		Print(*Level, { 5, 6 }, B, { { { 5, 6 }, { 7, 2 } } }, {}, 3);
	}
	return Result;
}

TArray<UGridLevelDataAsset*> AGridManager::CreatePrototypeLevels()
{
	TArray<UGridLevelDataAsset*> Result;
	const auto Add = [&Result](FText Title, FText Hint, EGridPrototype Prototype, TArray<FString> Rows)
	{
		UGridLevelDataAsset* Level = NewObject<UGridLevelDataAsset>();
		Level->Title = MoveTemp(Title);
		Level->Hint = MoveTemp(Hint);
		Level->Rows = MoveTemp(Rows);
		Level->Prototype = Prototype;
		Level->bRequireBorrowedPrints = true;
		Result.Add(Level);
		return Level;
	};

	UGridLevelDataAsset* Turn = Add(LOCTEXT("TurnPrototype", "把桥折给你"),
		LOCTEXT("TurnPrototypeHint", "先走过桥面，再到 TURN 碑按 E 预览。脚印会随桥转过去。"),
		EGridPrototype::TurnBridge, {TEXT(""), TEXT("    x"), TEXT("  Aoo."), TEXT(" B"), TEXT(""), TEXT("  t"), TEXT("  b")});
	Turn->InitialDeck = {{3, 2}, {4, 2}};
	Turn->AlternateDeck = {{2, 3}, {2, 4}};
	Turn->TurnControls = {{5, 2}, {2, 5}};
	Turn->TurnPivot = {2, 2};

	UGridLevelDataAsset* Record = Add(LOCTEXT("RecordPrototype", "借你三步"),
		LOCTEXT("RecordPrototypeHint", "A 在录印碑按 E，走三步后让开。B 到同一块碑按 E 预览回放。"),
		EGridPrototype::RecordRoute, {TEXT(""), TEXT("   a"), TEXT("   ob"), TEXT(" Aooo"), TEXT(" B")});
	Record->RecordStart = Record->ReplayStart = {1, 3};
	Record->ReplayTiles = {{2, 3}, {3, 3}, {3, 2}};

	UGridLevelDataAsset* Exchange = Add(LOCTEXT("SwapPrototype", "交换脚下这一步"),
		LOCTEXT("SwapPrototypeHint", "让两人分别站上相邻的 SWAP 印板。按 E 预览，再按 E 同时迈步。"),
		EGridPrototype::SwapPlaces, {TEXT("bAouBa")});
	Exchange->SwapTiles = {{2, 0}, {3, 0}};
	return Result;
}

#undef LOCTEXT_NAMESPACE
