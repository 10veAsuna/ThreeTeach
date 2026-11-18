#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GridPuzzleRules.generated.h"

UENUM(BlueprintType)
enum class EPlayerRole : uint8 { PlayerA, PlayerB };

UENUM(BlueprintType)
enum class ETileType : uint8 { Normal, Memory, Fragile, Obstacle, Switch, Goal, Plate, Imprint, Bridge, SingleUse };

UENUM(BlueprintType)
enum class EFootprintOwner : uint8 { None, PlayerA, PlayerB };

UENUM(BlueprintType)
enum class EGridPrototype : uint8 { None, TurnBridge, RecordRoute, SwapPlaces, TurnBridgeAndSwap };

USTRUCT(BlueprintType)
struct FTileData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FIntPoint GridPosition = FIntPoint::ZeroValue;
	UPROPERTY(BlueprintReadOnly) ETileType TileType = ETileType::Normal;
	UPROPERTY(BlueprintReadOnly) EFootprintOwner FootprintOwner = EFootprintOwner::None;
	UPROPERTY(BlueprintReadOnly) bool bIsOccupied = false;
	UPROPERTY(BlueprintReadOnly) bool bIsCollapsed = false;
	UPROPERTY(BlueprintReadOnly) bool bIsActivated = false;
	UPROPERTY(BlueprintReadOnly) bool bIsLocked = false;
	UPROPERTY(BlueprintReadOnly) int32 RemainingMoves = 0;
	UPROPERTY(BlueprintReadOnly) EPlayerRole GoalOwner = EPlayerRole::PlayerA;
	UPROPERTY(BlueprintReadOnly) EPlayerRole SwitchRole = EPlayerRole::PlayerA;
	UPROPERTY(BlueprintReadOnly) FString Marker;
	UPROPERTY(BlueprintReadOnly) FString InteractionLabel;
	UPROPERTY(BlueprintReadOnly) bool bPrintTrigger = false;
	UPROPERTY(BlueprintReadOnly) bool bPrintReceiver = false;
	UPROPERTY(BlueprintReadOnly) bool bPrintPending = false;
	UPROPERTY(BlueprintReadOnly) bool bFreshPrint = false;
	UPROPERTY(BlueprintReadOnly) bool bFoldedAway = false;

	bool HasFootprints() const
	{
		return TileType == ETileType::Memory || TileType == ETileType::Fragile ||
			TileType == ETileType::Imprint || TileType == ETileType::SingleUse;
	}
};

USTRUCT(BlueprintType)
struct FGridBridgeCondition
{
	GENERATED_BODY()

	FGridBridgeCondition() = default;
	FGridBridgeCondition(FIntPoint InSource, EPlayerRole InRole) : Source(InSource), RequiredRole(InRole) {}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bridge") FIntPoint Source = FIntPoint::ZeroValue;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bridge") EPlayerRole RequiredRole = EPlayerRole::PlayerA;
};

USTRUCT(BlueprintType)
struct FGridBridgeDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bridge") FString Marker;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bridge") TArray<FGridBridgeCondition> Conditions;
};

USTRUCT(BlueprintType)
struct FGridPrintLink
{
	GENERATED_BODY()

	FGridPrintLink() = default;
	FGridPrintLink(FIntPoint InSource, FIntPoint InTarget) : Source(InSource), Target(InTarget) {}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Footprint") FIntPoint Source = FIntPoint::ZeroValue;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Footprint") FIntPoint Target = FIntPoint::ZeroValue;
};

USTRUCT(BlueprintType)
struct FGridPrintEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Footprint") FIntPoint Trigger = FIntPoint::ZeroValue;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Footprint") EFootprintOwner TriggerOwner = EFootprintOwner::None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Footprint") TArray<FGridPrintLink> Links;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Footprint") TArray<FGridBridgeCondition> Requirements;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Footprint", meta=(ClampMin="0")) int32 DelayMoves = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Footprint") bool bMovePrint = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Footprint") bool bOnce = false;
};

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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Level") TArray<FString> Rows;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Level", meta=(ClampMin="1"))
	int32 FragileLifetime = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Level") TArray<FGridBridgeDefinition> Bridges;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Level") TArray<FGridPrintEffect> PrintEffects;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Level") bool bRequireBorrowedPrints = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Level") bool bChallenge = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prototype") EGridPrototype Prototype = EGridPrototype::None;
	// Turning bridge: controls, then one array per orientation with corresponding stones.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prototype") TArray<FIntPoint> TurnControls;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prototype") FIntPoint TurnPivot = FIntPoint::ZeroValue;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prototype") TArray<FIntPoint> InitialDeck;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prototype") TArray<FIntPoint> AlternateDeck;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prototype") FIntPoint RecordStart = FIntPoint::ZeroValue;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prototype") FIntPoint ReplayStart = FIntPoint::ZeroValue;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prototype") TArray<FIntPoint> ReplayTiles;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prototype") TArray<FIntPoint> SwapTiles;
};

enum class EGridMoveResult : uint8
{
	Moved, InvalidDirection, Finished, Void, Blocked, Occupied, OwnFootprint, Collapsed, OtherGoal, LockedGoal, ClosedBridge, EmptyPrint, ReplayOnly
};

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
};

class FGridPuzzleRules
{
public:
	bool Initialize(const UGridLevelDataAsset& Level);
	FGridMove TryMove(EPlayerRole Role, FIntPoint Direction);
	bool StartRecording(EPlayerRole Role);
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
	const TArray<FGridPrintEffect>& GetPrintEffects() const { return PrintEffects; }

	static int32 RoleIndex(EPlayerRole Role) { return Role == EPlayerRole::PlayerA ? 0 : 1; }

private:
	struct FPrintWrite
	{
		FIntPoint Target;
		EFootprintOwner Owner;
		bool bFresh;
	};

	struct FPendingPrint
	{
		int32 EffectIndex;
		int32 Steps;
		TArray<FPrintWrite> Writes;
	};

	void UpdatePrintEffects(FGridMove& Move, EFootprintOwner Owner);
	bool InitializePrototype(const UGridLevelDataAsset& Level);
	FGridMove MoveCharacter(EPlayerRole Role, FIntPoint Direction, bool bReplaying);
	void ResolveStep(FGridMove& Move, EFootprintOwner Owner);
	bool ArePrintTargetsFree(const TArray<FPrintWrite>& Writes) const;
	void ApplyPrintWrites(const TArray<FPrintWrite>& Writes, FGridMove& Move);

#if WITH_DEV_AUTOMATION_TESTS
	friend struct FGridTestSearch;
#endif
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
};
