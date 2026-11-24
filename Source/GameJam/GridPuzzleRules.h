#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GridPuzzleRules.generated.h"

// 规则层的数据与接口。网格坐标、脚印、机关都保存在这里，场景 Actor 根据结果更新外观。
// 建议先读 UGridLevelDataAsset，再看 FGridPuzzleRules::MoveCharacter 理解每一步的判定。
UENUM(BlueprintType)
enum class EPlayerRole : uint8 { PlayerA, PlayerB };

// 地块用途与脚印归属独立保存，例如易碎地块也能带有 A 或 B 的脚印。
UENUM(BlueprintType)
enum class ETileType : uint8 { Normal, Memory, Fragile, Obstacle, Switch, Goal, Plate, Imprint, Bridge, SingleUse };

UENUM(BlueprintType)
enum class EFootprintOwner : uint8 { None, PlayerA, PlayerB };

// Prototype 字段也用于正式关卡，指定该关支持的转桥、录制回放或换位操作。
UENUM(BlueprintType)
enum class EGridPrototype : uint8 { None, TurnBridge, RecordRoute, SwapPlaces, TurnBridgeAndSwap };

// 一个格子的完整运行状态。复制规则快照时，这些状态会一起复制，供撤销与试算使用。
USTRUCT(BlueprintType)
struct FTileData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FIntPoint GridPosition = FIntPoint::ZeroValue;
	UPROPERTY(BlueprintReadOnly) ETileType TileType = ETileType::Normal;
	// 进入前检查是否属于同伴，成功落脚后改成当前角色，供同伴下一次借用。
	UPROPERTY(BlueprintReadOnly) EFootprintOwner FootprintOwner = EFootprintOwner::None;
	UPROPERTY(BlueprintReadOnly) bool bIsOccupied = false;
	UPROPERTY(BlueprintReadOnly) bool bIsCollapsed = false;
	UPROPERTY(BlueprintReadOnly) bool bIsActivated = false;
	UPROPERTY(BlueprintReadOnly) bool bIsLocked = false;
	// 易碎地块按成功行动扣除剩余次数，角色站在上面时暂停倒计时。
	UPROPERTY(BlueprintReadOnly) int32 RemainingMoves = 0;
	UPROPERTY(BlueprintReadOnly) EPlayerRole GoalOwner = EPlayerRole::PlayerA;
	UPROPERTY(BlueprintReadOnly) EPlayerRole SwitchRole = EPlayerRole::PlayerA;
	UPROPERTY(BlueprintReadOnly) FString Marker;
	UPROPERTY(BlueprintReadOnly) FString InteractionLabel;
	UPROPERTY(BlueprintReadOnly) bool bPrintTrigger = false;
	UPROPERTY(BlueprintReadOnly) bool bPrintReceiver = false;
	UPROPERTY(BlueprintReadOnly) bool bPrintPending = false;
	UPROPERTY(BlueprintReadOnly) bool bStoredPrintSource = false;
	UPROPERTY(BlueprintReadOnly) int32 PrintDelayMoves = 0;
	UPROPERTY(BlueprintReadOnly) int32 PendingPrintSteps = 0;
	UPROPERTY(BlueprintReadOnly) EFootprintOwner PendingPrintOwner = EFootprintOwner::None;
	// 标记这枚脚印是否由本次游玩的角色写出，寄送与转桥会携带这个标记。
	UPROPERTY(BlueprintReadOnly) bool bFreshPrint = false;
	// 转桥在两个朝向各保留一组网格槽位，当前朝向之外的槽位视为虚空。
	UPROPERTY(BlueprintReadOnly) bool bFoldedAway = false;
	// 融合玩法使用地形叠加，落脚仍通过同一套脚印判定。
	UPROPERTY(BlueprintReadOnly) bool bIce = false;
	UPROPERTY(BlueprintReadOnly) bool bCrate = false;
	UPROPERTY(BlueprintReadOnly) bool bRhythm = false;
	UPROPERTY(BlueprintReadOnly) bool bRhythmOpen = true;
	UPROPERTY(BlueprintReadOnly) bool bPatrol = false;
	UPROPERTY(BlueprintReadOnly) bool bPatrolNext = false;

	// 只有这些地块承载可被角色改写的脚印，起点、终点等地块另行处理。
	bool HasFootprints() const
	{
		return TileType == ETileType::Memory || TileType == ETileType::Fragile ||
			TileType == ETileType::Imprint || TileType == ETileType::SingleUse;
	}
};

// 桥的一个通行条件，要求指定角色站在压板上，或在指定印板留下对应脚印。
USTRUCT(BlueprintType)
struct FGridBridgeCondition
{
	GENERATED_BODY()

	FGridBridgeCondition() = default;
	FGridBridgeCondition(FIntPoint InSource, EPlayerRole InRole) : Source(InSource), RequiredRole(InRole) {}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bridge") FIntPoint Source = FIntPoint::ZeroValue;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bridge") EPlayerRole RequiredRole = EPlayerRole::PlayerA;
};

// 同一 Marker 的桥格共享条件列表，全部满足后开启。
USTRUCT(BlueprintType)
struct FGridBridgeDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bridge") FString Marker;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bridge") TArray<FGridBridgeCondition> Conditions;
};

// 把 Source 格的脚印送到 Target 格，坐标均使用关卡的网格坐标。
USTRUCT(BlueprintType)
struct FGridPrintLink
{
	GENERATED_BODY()

	FGridPrintLink() = default;
	FGridPrintLink(FIntPoint InSource, FIntPoint InTarget) : Source(InSource), Target(InTarget) {}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Footprint") FIntPoint Source = FIntPoint::ZeroValue;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Footprint") FIntPoint Target = FIntPoint::ZeroValue;
};

// 踩中 Trigger 时检查触发者与前置条件，再按 Links 复制或搬运脚印。
USTRUCT(BlueprintType)
struct FGridPrintEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Footprint") FIntPoint Trigger = FIntPoint::ZeroValue;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Footprint") EFootprintOwner TriggerOwner = EFootprintOwner::None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Footprint") TArray<FGridPrintLink> Links;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Footprint") TArray<FGridBridgeCondition> Requirements;
	// 延迟按后续成功行动计数，目标被占用时继续等待，避免覆盖角色脚下的印记。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Footprint", meta=(ClampMin="0")) int32 DelayMoves = 0;
	// 搬运会清除源印，复制会保留源印；一次性效果在触发后记入已用集合。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Footprint") bool bMovePrint = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Footprint") bool bOnce = false;
};

// 供蓝图和 Python 配置的关卡资源。Rows 描述布局，其余字段描述机关关系。
UCLASS(BlueprintType)
class GAMEJAM_API UGridLevelDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Level") FText Title;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Level", meta=(MultiLine=true)) FText Hint;

	// A/B: starts; a/b: goals; x/y: locked goals; s/t: switches for A/B.
	// .: stone; m: memory; f: fragile; #: obstacle; space: empty.
	// P/Q: pressure plates; I/J: imprints; G/H/K: bridge groups.
	// o/u: orange/blue print; c/d: single-use orange/blue; f starts orange in strict levels.
	// 每个字符串是一行，字符下标为 X，行下标为 Y，左上角是 (0,0)，Y 向下增加。
	// A/B 为起点，a/b 为终点，x/y 为上锁终点，s/t 为各自的开关。
	// 空格为虚空，. 为普通石板，m 为无初始印记的记忆板，# 为障碍。
	// o/u 为橙/蓝脚印，c/d 为橙/蓝一次性印板，f 为易碎板。
	// P/Q 为压板，I/J 为印板，G/H/K 为由 Bridges 配置条件的桥组。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Level") TArray<FString> Rows;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Level", meta=(ClampMin="1"))
	int32 FragileLifetime = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Level") TArray<FGridBridgeDefinition> Bridges;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Level") TArray<FGridPrintEffect> PrintEffects;
	// 严格模式下，承载脚印的地块必须已有同伴印记才能进入，空印格也会阻挡。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Level") bool bRequireBorrowedPrints = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Level") bool bChallenge = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chapter") int32 LegacyChapterIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prototype") EGridPrototype Prototype = EGridPrototype::None;
	// Turning bridge: controls, then one array per orientation with corresponding stones.
	// 两个朝向的数组按下标对应同一块实体石板，转动时一起搬运脚印归属与新印标记。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prototype") TArray<FIntPoint> TurnControls;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prototype") FIntPoint TurnPivot = FIntPoint::ZeroValue;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prototype") TArray<FIntPoint> InitialDeck;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prototype") TArray<FIntPoint> AlternateDeck;
	// A 从 REC 录下三次成功移动的方向，B 在 PLAY 复用这串方向。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prototype") FIntPoint RecordStart = FIntPoint::ZeroValue;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prototype") FIntPoint ReplayStart = FIntPoint::ZeroValue;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prototype") TArray<FIntPoint> ReplayTiles;
	// 换位要求两个相邻印板各站一人，并且两人都已在自己的脚下写出新印。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prototype") TArray<FIntPoint> SwapTiles;

	// 可组合的玩法配置，数组中的格子仍需在 Rows 中声明。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fusion") TArray<FIntPoint> IceTiles;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fusion") TArray<FIntPoint> Crates;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fusion") TArray<FIntPoint> RhythmTiles;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fusion", meta=(ClampMin="2")) int32 RhythmPeriod = 3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fusion", meta=(ClampMin="0")) int32 RhythmClosedPhase = 2;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fusion") TArray<FIntPoint> PatrolRoute;
	// 兼容 V6 的子关卡资源；正式主线使用独立棋盘，运行时会迁移旧容器。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chapter") TArray<TObjectPtr<UGridLevelDataAsset>> Sections;
};

// 判定结果交给管理器转换成玩家提示与反馈动画。
enum class EGridMoveResult : uint8
{
	Moved, InvalidDirection, Finished, Void, Blocked, Occupied, OwnFootprint, Collapsed, OtherGoal, LockedGoal, ClosedBridge, EmptyPrint, ReplayOnly,
	CrateBlocked, RhythmClosed, PatrolBlocked
};

// 实际寄印的发件位置、落点与颜色，表现层据此生成对应的声场反馈。
struct FGridPrintTransfer
{
	FIntPoint From;
	FIntPoint To;
	EFootprintOwner Owner;
};

// 单次行动的结果。ChangedTiles 包括落脚之外受倒计时、寄印、开关影响的所有格子。
struct FGridMove
{
	EGridMoveResult Result = EGridMoveResult::InvalidDirection;
	FIntPoint From = FIntPoint::ZeroValue;
	FIntPoint To = FIntPoint::ZeroValue;
	TArray<FIntPoint> ChangedTiles;
	bool bFootprintTransferred = false;
	bool bSwitchActivated = false;
	bool bReachedGoal = false;
	bool bSentPrint = false;
	TArray<FIntPoint> Path;
	int32 BorrowedPrintCount = 0;
	int32 FreshBorrowedPrintCount = 0;
	bool bRecordedStep = false;
	bool bReplayStep = false;
	TArray<FGridPrintTransfer> PrintDispatches;
	TArray<FGridPrintTransfer> PrintArrivals;
};

// 可按值复制的规则状态，供正常移动、机关预览、撤销和自动化测试共用。
class FGridPuzzleRules
{
public:
	// 所有配置校验成功后才替换当前状态，失败时保留原状态。
	bool Initialize(const UGridLevelDataAsset& Level);
	FGridMove TryMove(EPlayerRole Role, FIntPoint Direction);
	bool StartRecording(EPlayerRole Role);
	// 在副本中预演整段回放，返回每一步的结果与快照，供预览和逐步播放。
	bool PlanReplay(EPlayerRole Role, TArray<FGridMove>& Steps, TArray<FGridPuzzleRules>& States) const;
	bool CanReplayFrom(EPlayerRole Role) const;
	bool IsAtTurnControl(EPlayerRole Role) const;
	bool CanSwap() const;
	bool TryTurnBridge(EPlayerRole Role, TArray<FIntPoint>& Changed);
	bool TrySwap(EPlayerRole Role, FGridMove& Move);
	const TArray<FIntPoint>& GetNextDeck() const { return bAlternateDeck ? InitialDeck : AlternateDeck; }
	const TArray<FIntPoint>& GetRecording() const { return Recording; }
	bool IsRecording() const { return bRecording; }
	static constexpr int32 RecordedLength = 3;

	const TMap<FIntPoint, FTileData>& GetTiles() const { return Tiles; }
	const TArray<FGridBridgeCondition>& GetBridgeConditions(const FString& Marker) const { return Bridges.FindChecked(Marker); }
	bool IsBridgeConditionMet(const FGridBridgeCondition& Condition) const;
	FIntPoint GetSize() const { return Size; }
	FIntPoint GetPosition(EPlayerRole Role) const { return Positions[RoleIndex(Role)]; }
	bool IsFinished(EPlayerRole Role) const { return Finished[RoleIndex(Role)]; }
	bool IsComplete() const { return Finished[0] && Finished[1]; }
	int32 GetMoveCount() const { return MoveCount; }
	int32 GetRhythmPeriod() const { return RhythmPeriod; }
	const TArray<FGridPrintEffect>& GetPrintEffects() const { return PrintEffects; }
	bool IsPrintEffectUsed(int32 Index) const { return UsedPrintEffects.Contains(Index); }
	bool IsPrintEffectPending(int32 Index) const;
	// 只编码会影响后续合法行动的状态，供提示搜索与路线缓存使用。
	FString GetHintStateKey() const;

	static int32 RoleIndex(EPlayerRole Role) { return Role == EPlayerRole::PlayerA ? 0 : 1; }

private:
	// 触发寄印时就记录要送出的颜色与新印状态，后续源格变色不会更改这次包裹。
	struct FPrintWrite
	{
		FIntPoint Target;
		EFootprintOwner Owner;
		bool bFresh;
	};

	// 等待送达的整组写入，只有所有目标都空闲时才一起应用。
	struct FPendingPrint
	{
		int32 EffectIndex;
		int32 Steps;
		TArray<FPrintWrite> Writes;
	};

	void UpdatePrintEffects(FGridMove& Move, EFootprintOwner Owner);
	bool InitializePrototype(const UGridLevelDataAsset& Level);
	bool InitializeFusion(const UGridLevelDataAsset& Level);
	void UpdateFusionTiles(TArray<FIntPoint>& Changed);
	FGridMove MoveWithTerrain(EPlayerRole Role, FIntPoint Direction, bool bReplaying);
	FGridMove MoveCharacter(EPlayerRole Role, FIntPoint Direction, bool bReplaying);
	void ResolveStep(FGridMove& Move, EFootprintOwner Owner);
	bool ArePrintTargetsFree(const TArray<FPrintWrite>& Writes) const;
	void ApplyPrintWrites(const TArray<FPrintWrite>& Writes, FGridMove& Move, FIntPoint From);

#if WITH_DEV_AUTOMATION_TESTS
	friend struct FGridTestSearch;
#endif
	// 这些成员共同组成一个局面，包含延迟寄印、已用机关和录制状态。
	TMap<FIntPoint, FTileData> Tiles;
	TMap<FString, TArray<FGridBridgeCondition>> Bridges;
	TArray<FGridPrintEffect> PrintEffects;
	TArray<FPendingPrint> PendingPrints;
	TSet<int32> UsedPrintEffects;
	FIntPoint Size = FIntPoint::ZeroValue;
	FIntPoint Positions[2] = {};
	bool Finished[2] = {};
	int32 FragileLifetime = 3;
	int32 MoveCount = 0;
	bool bRequireBorrowedPrints = false;
	EGridPrototype Prototype = EGridPrototype::None;
	TArray<FIntPoint> TurnControls;
	TArray<FIntPoint> InitialDeck;
	TArray<FIntPoint> AlternateDeck;
	TArray<FIntPoint> ReplayTiles;
	TArray<FIntPoint> SwapTiles;
	TArray<FIntPoint> Recording;
	FIntPoint RecordStart = FIntPoint::ZeroValue;
	FIntPoint ReplayStart = FIntPoint::ZeroValue;
	bool bRecording = false;
	bool bAlternateDeck = false;
	int32 RhythmPeriod = 3;
	int32 RhythmClosedPhase = 2;
	TArray<FIntPoint> PatrolRoute;
};
