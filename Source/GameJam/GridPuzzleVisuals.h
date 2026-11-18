#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GridPuzzleRules.h"
#include "GridPuzzleVisuals.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTextRenderComponent;

enum class EGridShape : uint8 { Cube, Cylinder, Cone, Sphere };

namespace GridVisuals
{
	FLinearColor RoleColor(EPlayerRole Role);
	UStaticMeshComponent* Shape(AActor& Actor, USceneComponent* Parent, UMaterialInterface* Material,
		EGridShape Type, FVector Position, FVector Scale, FLinearColor Color, float Glow = 0.f,
		FRotator Rotation = FRotator::ZeroRotator);
	void Paint(UStaticMeshComponent* Mesh, FLinearColor Color, float Glow = 0.f);
}

UCLASS(Blueprintable)
class GAMEJAM_API AGridTile : public AActor
{
	GENERATED_BODY()

public:
	AGridTile();
	void Setup(const FTileData& Data, UMaterialInterface* Material);
	void Update(const FTileData& Data, bool bPulse);
	void FaceMarker(const FRotator& Rotation);
	void ShowActionMarker(const FText& Label, FColor Color);
	void AnimateDeckFrom(FVector Center, FVector Start, float Duration);
	void AddBridgeLink(const FVector& SourceOffset, UMaterialInterface* Material);
	void AddPrintLink(const FVector& TargetOffset, UMaterialInterface* Material);
	void UpdateBridgeStatus(const TArray<FString>& Labels, const TArray<bool>& Matches);
	void AnimateOutro(float Strength);

	UPROPERTY(BlueprintReadOnly, Category="Puzzle") FTileData TileData;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Look") FLinearColor MemoryStone = FLinearColor(.28f,.38f,.43f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Look") FLinearColor NormalStone = FLinearColor(.13f,.21f,.27f);

	UFUNCTION(BlueprintImplementableEvent, Category="Puzzle") void OnTileUpdated(FTileData State, bool bFootprintChanged);
	UFUNCTION(BlueprintImplementableEvent, Category="Puzzle") void OnTileCollapsed();

private:
	void MakeFootprints(UMaterialInterface* Material);
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY() TObjectPtr<USceneComponent> Pieces;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Top;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Trim;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Portal;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Ripple;
	UPROPERTY() TObjectPtr<UTextRenderComponent> MarkerText;
	UPROPERTY() TObjectPtr<UTextRenderComponent> BridgeReadout;
	UPROPERTY() TObjectPtr<UTextRenderComponent> ActionMarker;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> BluePrints;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> OrangePrints;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Countdown;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> BridgeLinks;
	float AnimationTime = 0.f;
	float BridgeDeck = 0.f;
	FRotator MarkerRotation = FRotator::ZeroRotator;
	FVector DeckPivot = FVector::ZeroVector;
	FVector DeckStart = FVector::ZeroVector;
	float DeckAngle = 0.f;
	float DeckElapsed = 0.f;
	float DeckDuration = 0.f;
};

UCLASS(Blueprintable)
class GAMEJAM_API AGridPuzzleCharacter : public AActor
{
	GENERATED_BODY()

public:
	AGridPuzzleCharacter();
	void Setup(EPlayerRole NewRole, FIntPoint Start, UMaterialInterface* Material);
	void MoveTo(FIntPoint Destination, FVector WorldDestination, float Duration, bool bCurve = false);
	void RejectMove(FVector WorldDirection);
	void SetControlState(bool bControlled, bool bFinished);

	UPROPERTY(BlueprintReadOnly, Category="Puzzle") EPlayerRole TravelerRole = EPlayerRole::PlayerA;
	UPROPERTY(BlueprintReadOnly, Category="Puzzle") FIntPoint GridPosition = FIntPoint::ZeroValue;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Look") FLinearColor ScarfColor = FLinearColor(.59f,.56f,.42f);

	UFUNCTION(BlueprintImplementableEvent, Category="Puzzle") void OnCharacterMoved(FIntPoint Destination);
	UFUNCTION(BlueprintImplementableEvent, Category="Puzzle") void OnControlChanged(bool bControlled, bool bFinished);

private:
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY() TObjectPtr<USceneComponent> Figure;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Selection;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Shadow;
	FVector From = FVector::ZeroVector;
	FVector To = FVector::ZeroVector;
	FVector Nudge = FVector::ZeroVector;
	FVector CurveOffset = FVector::ZeroVector;
	float Duration = 0.f;
	float Elapsed = 0.f;
};
