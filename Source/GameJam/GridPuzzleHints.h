#pragma once

#include "CoreMinimal.h"
#include "GridPuzzleRules.h"

enum class EGridHintKind : uint8 { Move, Turn, Swap, Record, Replay };

struct FGridHintAction
{
	EPlayerRole Role = EPlayerRole::PlayerA;
	EGridHintKind Kind = EGridHintKind::Move;
	FIntPoint Direction = FIntPoint::ZeroValue;
	FIntPoint Target = FIntPoint::ZeroValue;
	TArray<FIntPoint> Path;
	bool bDeliversWaitingPrint = false;
};

// 缓存中的每一步都已经完整验证能到达双人终点，并非按当前步数读取攻略。
class FGridHintGuide
{
public:
	static TSharedPtr<FGridHintGuide> Build(const FGridPuzzleRules& Initial, int32 CampaignIndex);
	const FGridHintAction* Find(const FGridPuzzleRules& Rules) const;
	void Add(const FString& Key, const FGridHintAction& Action) { Steps.Add(Key, Action); }
private:
	TMap<FString, FGridHintAction> Steps;
};

// 分帧搜索任意当前局面；搜索资源耗尽和已证明无解是两个不同结果。
class FGridHintSearch
{
public:
	enum class EStatus { Searching, Found, Complete, NoSolution, LimitReached };
	FGridHintSearch(const FGridPuzzleRules& Current, TSharedPtr<FGridHintGuide> InGuide,
		int32 InNodeLimit = 20000);
	EStatus Advance(double Seconds = .002);
	EStatus GetStatus() const { return Status; }
	const FGridHintAction& GetAction() const { return Result; }
	static bool Apply(FGridPuzzleRules& Rules, FGridHintAction& Action);

private:
	struct FNode
	{
		FGridPuzzleRules Rules;
		int32 Parent = INDEX_NONE;
		FGridHintAction Action;
	};
	void Enqueue(FGridPuzzleRules&& Rules, int32 Parent, const FGridHintAction& Action);
	void Finish(int32 Index, const FGridHintAction* Suffix);
	TArray<FNode> Nodes;
	TSet<FString> Seen;
	TSharedPtr<FGridHintGuide> Guide;
	FGridHintAction Result;
	EStatus Status = EStatus::Searching;
	int32 Head = 0, NodeLimit;
	double StartedAt;
};
