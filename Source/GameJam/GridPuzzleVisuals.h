#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GridPuzzleRules.h"
#include "GridPuzzleVisuals.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UTextRenderComponent;

enum class EGridShape : uint8 { Cube, Cylinder, Cone, Sphere };

// 地块、人物与背景共用的几何体和材质工具。
namespace GridVisuals
{
	FLinearColor RoleColor(EPlayerRole Role);
	TArray<FVector> PrintLinkPath(FVector TargetOffset, int32 Lane);
	UStaticMeshComponent* Shape(AActor& Actor, USceneComponent* Parent, UMaterialInterface* Material,
		EGridShape Type, FVector Position, FVector Scale, FLinearColor Color, float Glow = 0.f,
		FRotator Rotation = FRotator::ZeroRotator);
	void Paint(UStaticMeshComponent* Mesh, FLinearColor Color, float Glow = 0.f);
}

// 一个格子的可视 Actor，接收规则层的 FTileData 来绘制石板、脚印和机关标识。
UCLASS(Blueprintable)
class GAMEJAM_API AGridTile : public AActor
{
	GENERATED_BODY()

public:
	AGridTile();
	// 管理器生成地块后调用 Setup 创建组件，之后用 Update 同步规则状态。
	void Setup(const FTileData& Data, UMaterialInterface* Material);
	void Update(const FTileData& Data, bool bPulse);
	void FaceMarker(const FRotator& Rotation);
	void ShowActionMarker(const FText& Label, FColor Color);
	void SetOpeningHighlight(bool bVisible);
	void PulseTile();
	void AnimateDeckFrom(FVector Center, FVector Start, float Duration);
	void AddBridgeLink(const FVector& SourceOffset, UMaterialInterface* Material);
	TArray<UStaticMeshComponent*> AddPrintLink(const FVector& TargetOffset, UMaterialInterface* Material, bool bSourceGuide = false, int32 Lane = 0);
	void ShowLinkBadge(const FString& Label, FColor Color);
	void UpdateBridgeStatus(const TArray<FString>& Labels, const TArray<bool>& Matches);
	void AnimateOutro(float Strength);
	void SetEchoAvailable(bool bAvailable, UMaterialInterface* Material);

	UPROPERTY(BlueprintReadOnly, Category="Puzzle") FTileData TileData;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Look") FLinearColor MemoryStone = FLinearColor(.28f,.38f,.43f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Look") FLinearColor NormalStone = FLinearColor(.13f,.21f,.27f);

	// 派生蓝图可以追加表现，地块判定仍通过管理器调用规则层。
	UFUNCTION(BlueprintImplementableEvent, Category="Puzzle") void OnTileUpdated(FTileData State, bool bFootprintChanged);
	UFUNCTION(BlueprintImplementableEvent, Category="Puzzle") void OnTileCollapsed();

private:
	void MakeFootprints(UMaterialInterface* Material);
	virtual void Tick(float DeltaSeconds) override;

	// 可见几何体集中挂在 Pieces 下，坍塌或转桥时可整体做局部变换。
	UPROPERTY() TObjectPtr<USceneComponent> Pieces;
	UPROPERTY() TObjectPtr<UMaterialInterface> VisualMaterial;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> CrateMesh;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> PatrolMesh;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> PatrolWarning;
	UPROPERTY() TObjectPtr<UTextRenderComponent> FusionLabel;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Top;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Trim;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Portal;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Ripple;
	UPROPERTY() TObjectPtr<UTextRenderComponent> MarkerText;
	UPROPERTY() TObjectPtr<UTextRenderComponent> BridgeReadout;
	UPROPERTY() TObjectPtr<UTextRenderComponent> ActionMarker;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> OpeningHalo;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> FeedbackPulse;
	float FeedbackPulseStart = 0.f;
	UPROPERTY() TObjectPtr<UTextRenderComponent> LinkBadge;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> EchoMarker;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> EchoHalo;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> EchoBeam;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> EchoShards;
	UPROPERTY() TObjectPtr<UPointLightComponent> EchoLight;
	// 两种颜色的脚印组件预先创建，刷新时按归属切换可见性。
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
	float EchoTime = 0.f;
	bool bEchoAvailable = false;
};

// 拼装式人物 Actor，逻辑坐标来自规则层，世界坐标通过短时插值呈现移动。
UCLASS(Blueprintable)
class GAMEJAM_API AGridPuzzleCharacter : public AActor
{
	GENERATED_BODY()

public:
	AGridPuzzleCharacter();
	void Setup(EPlayerRole NewRole, FIntPoint Start, UMaterialInterface* Material);
	// bCurve 用于换位时的弧线动画，普通行走沿两格之间的直线过渡。
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

	// 身体部件统一挂到 Figure，便于整体转向和添加行走起伏。
	UPROPERTY() TObjectPtr<USceneComponent> Figure;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Selection;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Shadow;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ControlHalo;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ControlDiamond;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ControlPointer;
	FVector From = FVector::ZeroVector;
	FVector To = FVector::ZeroVector;
	FVector Nudge = FVector::ZeroVector;
	FVector CurveOffset = FVector::ZeroVector;
	float Duration = 0.f;
	float Elapsed = 0.f;
	float BeaconTime = 0.f;
	bool bIsControlled = false;
	bool bIsFinished = false;
};
