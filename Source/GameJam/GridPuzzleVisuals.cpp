#include "GridPuzzleVisuals.h"

#include "Components/SceneComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	constexpr float FootHeight = 4.f;
	const FLinearColor DarkSeam(.036f, .068f, .091f);
	const FLinearColor BridgeLight(.29f, .84f, .76f);
	const FLinearColor BridgeDim(.21f, .33f, .37f);

	// 世界空间中的机关文字挂到所属 Actor，并注册组件后加入场景显示。
	UTextRenderComponent* GridLabel(AActor& Owner, USceneComponent* Parent, const FString& Text,
		FVector Position, float Size)
	{
		UTextRenderComponent* Label = NewObject<UTextRenderComponent>(&Owner);
		Label->SetupAttachment(Parent);
		Label->SetRelativeLocation(Position);
		Label->SetHorizontalAlignment(EHTA_Center);
		Label->SetVerticalAlignment(EVRTA_TextCenter);
		Label->SetWorldSize(Size);
		Label->SetTextRenderColor(FColor(218, 244, 244));
		Label->SetText(FText::FromString(Text));
		static UFont* WorldLabelFont = LoadObject<UFont>(nullptr,
			TEXT("/Game/OneStep/Fonts/NotoSansSC-Bold.NotoSansSC-Bold"));
		if (WorldLabelFont) { Label->SetFont(WorldLabelFont); }
		Label->RegisterComponent();
		return Label;
	}
}

// A 使用蓝色，B 使用橙色，人物、脚印和界面保持相同的识别关系。
FLinearColor GridVisuals::RoleColor(EPlayerRole Role)
{
	return Role == EPlayerRole::PlayerA ? FLinearColor(.14f, .66f, 1.f) : FLinearColor(1.f, .36f, .105f);
}

// 运行时从 UE 基础模型创建网格组件，并为每个组件生成可独立调色的材质实例。
UStaticMeshComponent* GridVisuals::Shape(AActor& Actor, USceneComponent* Parent, UMaterialInterface* Material,
	EGridShape Type, FVector Position, FVector Scale, FLinearColor Color, float Glow, FRotator Rotation)
{
	static const TCHAR* Paths[] = {
		TEXT("/Engine/BasicShapes/Cube.Cube"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
		TEXT("/Engine/BasicShapes/Cone.Cone"), TEXT("/Engine/BasicShapes/Sphere.Sphere")
	};
	UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(&Actor);
	Mesh->SetupAttachment(Parent);
	Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Paths[static_cast<int32>(Type)]));
	// 移动合法性由网格规则决定，显示模型关闭物理碰撞和导航影响。
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetRelativeTransform(FTransform(Rotation, Position, Scale));
	Mesh->SetCastShadow(false);
	Mesh->RegisterComponent();
	Mesh->CreateDynamicMaterialInstance(0, Material);
	Paint(Mesh, Color, Glow);
	return Mesh;
}

// 调整动态材质的颜色与自发光，兼容项目材质和基础材质使用的颜色参数名。
void GridVisuals::Paint(UStaticMeshComponent* Mesh, FLinearColor Color, float Glow)
{
	if (UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)))
	{
		Dynamic->SetVectorParameterValue(TEXT("Tint"), Color);
		Dynamic->SetVectorParameterValue(TEXT("Color"), Color);
		Dynamic->SetScalarParameterValue(TEXT("Glow"), Glow);
	}
}

// 构造阶段只建立根层级，具体外观等管理器提供地块类型后再创建。
AGridTile::AGridTile()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Pieces = CreateDefaultSubobject<USceneComponent>(TEXT("Pieces"));
	Pieces->SetupAttachment(RootComponent);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

// 按地块类型拼装石板、桥、出口或开关，再创建脚印和提示组件。
void AGridTile::Setup(const FTileData& Data, UMaterialInterface* Material)
{
	using namespace GridVisuals;
	VisualMaterial = Material;
	const bool bObstacle = Data.TileType == ETileType::Obstacle;
	const bool bFragile = Data.TileType == ETileType::Fragile;
	const bool bSingleUse = Data.TileType == ETileType::SingleUse;
	if (Data.TileType == ETileType::Bridge)
	{
		Top = Shape(*this, Pieces, Material, EGridShape::Cube, FVector(0,0,-6),
			FVector(.83f,.83f,.12f), BridgeLight, .3f);
		for (float Side : {-37.f, 37.f})
		{
			for (float Along : {-27.f, 27.f})
			{
				Shape(*this, Pieces, Material, EGridShape::Cube, FVector(Side,Along,1),
					FVector(.028f,.18f,.026f), BridgeDim, .35f);
				Shape(*this, Pieces, Material, EGridShape::Cube, FVector(Along,Side,1),
					FVector(.18f,.028f,.026f), BridgeDim, .35f);
			}
		}
		MarkerText = GridLabel(*this, RootComponent, Data.Marker, FVector(23,0,16), 25.f);
		BridgeReadout = GridLabel(*this, RootComponent, FString(), FVector(-23,0,16), 15.f);
		Update(Data, false);
		return;
	}
	const float Surface = bObstacle ? 45.f : 0.f;
	const float Width = bObstacle ? .82f : .92f;
	Shape(*this, Pieces, Material, EGridShape::Cube, FVector(0,0,Surface-16),
		FVector(Width,Width,bObstacle ? 1.22f : .29f), DarkSeam);
	Top = Shape(*this, Pieces, Material, EGridShape::Cube, FVector(0,0,Surface-5),
		FVector(Width+.015f,Width+.015f,.10f), NormalStone);
	Top->SetCastShadow(true);
	if (bObstacle)
	{
		Shape(*this, Pieces, Material, EGridShape::Cube, FVector(0,0,Surface+1),
			FVector(.57f,.57f,.035f), FLinearColor(.055f,.10f,.14f), .05f, FRotator(0,45,0));
	}
	else if (Data.HasFootprints())
	{
		const bool bImprint = Data.TileType == ETileType::Imprint;
		const FLinearColor Edge = bFragile || bImprint || bSingleUse ?
			FLinearColor(.74f,.61f,.35f) : FLinearColor(.52f,.66f,.72f);
		Trim = Shape(*this, Pieces, Material, bSingleUse ? EGridShape::Cube : EGridShape::Cylinder, FVector(0,0,2),
			FVector(.68f,.68f,.02f), Edge, .2f);
		Shape(*this, Pieces, Material, EGridShape::Cylinder, FVector(0,0,4),
			FVector(.61f,.61f,.024f), DarkSeam, .15f);
		Ripple = Shape(*this, Pieces, Material, EGridShape::Cylinder, FVector(0,0,1.4f),
			FVector(.68f,.68f,.013f), Edge);
		Ripple->SetVisibility(false);
		MakeFootprints(Material);
		if (bImprint || Data.bPrintTrigger || Data.bPrintReceiver || bSingleUse || !Data.InteractionLabel.IsEmpty())
		{
			MarkerText = GridLabel(*this, RootComponent, Data.Marker, FVector(27,0,22), 20.f);
		}
		if (bFragile)
		{
			for (int32 Index = 0; Index < Data.RemainingMoves; ++Index)
			{
				const float Segment = 56.f / Data.RemainingMoves;
				const float Side = (Index - (Data.RemainingMoves - 1) * .5f) * Segment;
				Countdown.Add(Shape(*this, Pieces, Material, EGridShape::Cube, FVector(-39, Side, 2),
					FVector(.048f, Segment * .0077f, .026f), Edge, .75f));
			}
		}
	}
	else if (Data.TileType == ETileType::Plate)
	{
		const FLinearColor Ink = RoleColor(Data.SwitchRole);
		Trim = Shape(*this, Pieces, Material, EGridShape::Cube, FVector(0,0,2),
			FVector(.72f,.72f,.045f), Ink, .45f);
		Portal = Shape(*this, Pieces, Material, EGridShape::Cube, FVector(0,0,5),
			FVector(.57f,.57f,.035f), DarkSeam, .2f);
		MarkerText = GridLabel(*this, RootComponent, Data.Marker +
			(Data.SwitchRole == EPlayerRole::PlayerA ? TEXT(" / A") : TEXT(" / B")), FVector(25,0,17), 19.f);
	}
	else if (Data.TileType == ETileType::Goal || Data.TileType == ETileType::Switch)
	{
		const bool bGoal = Data.TileType == ETileType::Goal;
		const EPlayerRole MarkerRole = bGoal ? Data.GoalOwner : Data.SwitchRole;
		const FLinearColor Ink = RoleColor(MarkerRole);
		Trim = Shape(*this, Pieces, Material, EGridShape::Cube, FVector(0,0,1),
			FVector(.62f,.62f,.035f), Ink, .85f, FRotator(0,45,0));
		Shape(*this, Pieces, Material, EGridShape::Cube, FVector(0,0,4),
			FVector(.53f,.53f,.035f), DarkSeam, .15f, FRotator(0,45,0));
		Portal = Shape(*this, Pieces, Material, bGoal ? EGridShape::Cube : EGridShape::Cylinder,
			FVector(0,0,7), FVector(.31f,.31f,.04f), Ink, .7f, FRotator(0,45,0));
		if (bGoal)
		{
			for (const float Side : {-31.f, 31.f})
			{
				Shape(*this, Pieces, Material, EGridShape::Cube, FVector(30,Side,19), 
					FVector(.11f,.10f,.36f), Ink * .40f, .55f);
				Shape(*this, Pieces, Material, EGridShape::Sphere, FVector(30,Side,39),
					FVector(.13f), Ink, 1.4f);
			}
		}
	}
	if (!MarkerText && !Data.InteractionLabel.IsEmpty())
	{
		MarkerText = GridLabel(*this, RootComponent, Data.InteractionLabel, FVector(27,0,22), 19.f);
	}
	Update(Data, false);
}

// 摄像机取景变化后同步标识朝向，使机关文字仍朝向观察者。
void AGridTile::FaceMarker(const FRotator& Rotation)
{
	MarkerRotation = Rotation;
	if (MarkerText) { MarkerText->SetWorldRotation(Rotation); }
	if (BridgeReadout) { BridgeReadout->SetWorldRotation(Rotation); }
	if (ActionMarker) { ActionMarker->SetWorldRotation(Rotation); }
	if (FusionLabel) { FusionLabel->SetWorldRotation(Rotation); }
}

// 机关预览的落点标记独立于实际脚印，取消预览时可以单独隐藏。
void AGridTile::ShowActionMarker(const FText& Label, FColor Color)
{
	if (!ActionMarker && !Label.IsEmpty())
	{
		ActionMarker = GridLabel(*this, RootComponent, FString(), FVector(0,0,79), 36.f);
		ActionMarker->SetWorldRotation(MarkerRotation);
	}
	if (ActionMarker)
	{
		ActionMarker->SetText(Label);
		ActionMarker->SetTextRenderColor(Color);
		ActionMarker->SetVisibility(!Label.IsEmpty());
	}
	if (MarkerText) { MarkerText->SetVisibility(Label.IsEmpty() && !TileData.bFoldedAway); }
}

// 新朝向的 Actor 已在终点，Pieces 从旧桥面位置绕轴转入，逻辑状态保持在终点。
void AGridTile::AnimateDeckFrom(FVector Center, FVector Start, float Duration)
{
	DeckPivot = Center - GetActorLocation();
	DeckStart = Start - Center;
	const FVector Destination = -DeckPivot;
	DeckAngle = FMath::RadiansToDegrees(FMath::Atan2(FVector::CrossProduct(DeckStart, Destination).Z,
		FVector::DotProduct(DeckStart, Destination)));
	DeckElapsed = 0.f;
	DeckDuration = Duration;
	Pieces->SetRelativeLocation(DeckPivot + DeckStart);
	SetActorTickEnabled(true);
}

// 绘制桥与条件源之间的空间连接，帮助玩家找到对应压板或印板。
void AGridTile::AddBridgeLink(const FVector& SourceOffset, UMaterialInterface* Material)
{
	BridgeLinks.Add(GridVisuals::Shape(*this, RootComponent, Material, EGridShape::Cube,
		SourceOffset * .5f + FVector(0,0,-13), FVector(SourceOffset.Size() / 100.f,.023f,.018f),
		BridgeDim * .55f, .14f, SourceOffset.Rotation()));
}

// 寄印发送端与接收端之间的连线，方向和长度由管理器提供的相对坐标决定。
void AGridTile::AddPrintLink(const FVector& TargetOffset, UMaterialInterface* Material)
{
	GridVisuals::Shape(*this, RootComponent, Material, EGridShape::Cube,
		TargetOffset * .5f + FVector(0,0,-12), FVector(TargetOffset.Size() / 100.f,.017f,.018f),
		FLinearColor(.14f,.41f,.51f), .42f, TargetOffset.Rotation());
	// 沿寄送方向放置箭头，长距离连线和双向回信也可分辨去向。
	GridVisuals::Shape(*this, RootComponent, Material, EGridShape::Cone,
		TargetOffset * .76f + FVector(0, 0, -10), FVector(.10f, .10f, .22f),
		FLinearColor(.38f, .76f, .83f), .8f, FRotationMatrix::MakeFromZ(TargetOffset).Rotator());
}

// 更新条件读数与连线高亮，显示桥当前还缺哪些角色或印记。
void AGridTile::UpdateBridgeStatus(const TArray<FString>& Labels, const TArray<bool>& Matches)
{
	if (!BridgeReadout) { return; }
	BridgeReadout->SetText(FText::FromString(FString::Join(Labels, TEXT("  "))));
	for (int32 Index = 0; Index < BridgeLinks.Num(); ++Index)
	{
		const bool bMet = Matches[Index];
		GridVisuals::Paint(BridgeLinks[Index], bMet ? BridgeLight * .75f : BridgeDim * .55f, bMet ? .72f : .14f);
	}
}

// 回响外观按需创建，拾取后隐藏并停止对应的悬浮闪光。
void AGridTile::SetEchoAvailable(bool bAvailable, UMaterialInterface* Material)
{
	bEchoAvailable = bAvailable;
	if (bAvailable && !EchoMarker)
	{
		const FLinearColor EchoGold(.92f,.58f,.16f);
		EchoBeam = GridVisuals::Shape(*this, RootComponent, Material, EGridShape::Cylinder,
			FVector(0,0,37), FVector(.022f,.022f,.58f), EchoGold, 2.2f);
		EchoHalo = GridVisuals::Shape(*this, RootComponent, Material, EGridShape::Cylinder,
			FVector(0,0,16), FVector(.38f,.38f,.016f), EchoGold, 1.8f);
		EchoMarker = GridVisuals::Shape(*this, RootComponent, Material, EGridShape::Cube,
			FVector(0,0,77), FVector(.14f,.14f,.14f), EchoGold, 2.6f, FRotator(0,45,45));
		for (int32 Index = 0; Index < 3; ++Index)
		{
			EchoShards.Add(GridVisuals::Shape(*this, RootComponent, Material, EGridShape::Cone,
				FVector(27.f,0,61), FVector(.055f,.055f,.12f), EchoGold, 2.2f,
				FRotator(180,Index * 120.f,0)));
		}
		EchoLight = NewObject<UPointLightComponent>(this, TEXT("EchoLight"));
		EchoLight->SetupAttachment(RootComponent);
		EchoLight->SetRelativeLocation(FVector(0,0,58));
		EchoLight->SetMobility(EComponentMobility::Movable);
		EchoLight->SetLightColor(EchoGold);
		EchoLight->SetAttenuationRadius(220.f);
		EchoLight->SetCastShadows(false);
		EchoLight->RegisterComponent();
	}
	if (EchoBeam) { EchoBeam->SetVisibility(bAvailable); }
	if (EchoHalo) { EchoHalo->SetVisibility(bAvailable); }
	if (EchoMarker) { EchoMarker->SetVisibility(bAvailable); }
	for (UStaticMeshComponent* Shard : EchoShards) { Shard->SetVisibility(bAvailable); }
	if (EchoLight) { EchoLight->SetVisibility(bAvailable); }
	EchoTime = 0.f;
	if (bAvailable) { SetActorTickEnabled(true); }
}

// 两色脚印都预先拼好，由 Update 选择当前需要显示的一组。
void AGridTile::MakeFootprints(UMaterialInterface* Material)
{
	using namespace GridVisuals;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const float Sign = Index == 0 ? -1.f : 1.f;
		const FVector Position(Sign*10, Sign*10, FootHeight+3);
		const FRotator Rotation(0,-12,0);
		const FLinearColor Blue = RoleColor(EPlayerRole::PlayerA);
		const FLinearColor Orange = RoleColor(EPlayerRole::PlayerB);
		BluePrints.Add(Shape(*this, Pieces, Material, EGridShape::Sphere, Position,
			FVector(.24f,.115f,.033f), Blue, 1.1f, Rotation));
		BluePrints.Add(Shape(*this, Pieces, Material, EGridShape::Sphere, Position+FVector(10,-1.5,0),
			FVector(.055f,.083f,.026f), Blue, 1.1f, Rotation));
		OrangePrints.Add(Shape(*this, Pieces, Material, EGridShape::Cube, Position,
			FVector(.14f,.14f,.023f), Orange, 1.1f, FRotator(0,45,0)));
		OrangePrints.Add(Shape(*this, Pieces, Material, EGridShape::Cube, Position+FVector(10,0,0),
			FVector(.075f,.075f,.023f), Orange, 1.1f, FRotator(0,45,0)));
	}
}

// 规则状态的显示入口，统一刷新脚印、倒计时、WAIT 标记、出口与坍塌效果。
void AGridTile::Update(const FTileData& Data, bool bPulse)
{
	using namespace GridVisuals;
	if (Data.bCrate && !CrateMesh)
	{
		CrateMesh = Shape(*this, Pieces, VisualMaterial, EGridShape::Cube, FVector(0,0,27),
			FVector(.52f,.52f,.50f), FLinearColor(.43f,.25f,.10f), .12f);
	}
	if (CrateMesh) { CrateMesh->SetVisibility(Data.bCrate); }
	const float PatrolHeight = Data.TileType == ETileType::Obstacle ? 132.f : 82.f;
	if (Data.bPatrol && !PatrolMesh)
	{
		PatrolMesh = Shape(*this, Pieces, VisualMaterial, EGridShape::Sphere, FVector(0,0,PatrolHeight),
			FVector(.23f), FLinearColor(1.f,.08f,.07f), 3.f);
	}
	if (PatrolMesh) { PatrolMesh->SetVisibility(Data.bPatrol); }
	if (Data.bPatrolNext && !PatrolWarning)
	{
		PatrolWarning = Shape(*this, Pieces, VisualMaterial, EGridShape::Cylinder, FVector(0,0,PatrolHeight - 20.f),
			FVector(.48f,.48f,.018f), FLinearColor(1.f,.38f,.12f), 1.4f);
	}
	if (PatrolWarning) { PatrolWarning->SetVisibility(Data.bPatrolNext); }
	if ((Data.bIce || Data.bCrate || Data.bRhythm || Data.bPatrolNext) && !FusionLabel)
	{
		FusionLabel = GridLabel(*this, RootComponent, FString(), FVector(-27,0,
			Data.TileType == ETileType::Obstacle ? 150.f : 24.f), 16.f);
		FusionLabel->SetWorldRotation(MarkerRotation);
	}
	if (FusionLabel)
	{
		const FString Label = Data.bPatrolNext ? TEXT("NEXT") : Data.bCrate ?
			(Data.FootprintOwner == EFootprintOwner::PlayerA ? TEXT("BOX / A") : TEXT("BOX / B")) : Data.bRhythm ?
			(Data.bRhythmOpen ? TEXT("BEAT OPEN") : TEXT("BEAT WAIT")) : Data.bIce ? TEXT("ICE") : TEXT("");
		FusionLabel->SetText(FText::FromString(Label));
		FusionLabel->SetTextRenderColor(Data.bRhythm && !Data.bRhythmOpen ? FColor(255,100,90) : FColor(130,235,255));
	}
	const bool bFirstCollapse = Data.bIsCollapsed && !TileData.bIsCollapsed;
	const bool bRestored = !Data.bIsCollapsed && TileData.bIsCollapsed;
	const bool bNewBridge = Data.TileType == ETileType::Bridge && TileData.TileType != ETileType::Bridge;
	TileData = Data;
	if (Data.bFoldedAway)
	{
		DeckDuration = 0.f;
		Pieces->SetRelativeLocation(FVector::ZeroVector);
	}
	Pieces->SetHiddenInGame(Data.bFoldedAway, true);
	if (MarkerText) { MarkerText->SetVisibility(!Data.bFoldedAway); }
	if (bRestored)
	{
		Pieces->SetRelativeTransform(FTransform::Identity);
		SetActorHiddenInGame(false);
		SetActorTickEnabled(false);
	}
	if (Data.TileType == ETileType::Bridge)
	{
		const float Target = Data.bIsActivated || Data.bIsOccupied ? 1.f : 0.f;
		if (bNewBridge)
		{
			BridgeDeck = Target;
			Top->SetVisibility(BridgeDeck > 0.f);
		}
		Paint(Top, Data.bIsActivated ? BridgeLight * .18f : FLinearColor(.33f,.23f,.10f),
			Data.bIsActivated ? .22f : .16f);
		MarkerText->SetTextRenderColor(Data.bIsActivated ? FColor(160, 254, 232) :
			Data.bIsOccupied ? FColor(255, 220, 165) : FColor(218, 234, 237));
		SetActorTickEnabled(!FMath::IsNearlyEqual(BridgeDeck, Target));
		OnTileUpdated(Data, false);
		return;
	}
	const bool bBlue = Data.FootprintOwner == EFootprintOwner::PlayerA;
	const bool bOrange = Data.FootprintOwner == EFootprintOwner::PlayerB;
	const FLinearColor Ink = bBlue ? RoleColor(EPlayerRole::PlayerA) : RoleColor(EPlayerRole::PlayerB);
	FLinearColor Color = NormalStone;
	if (Data.HasFootprints())
	{
		const bool bHasOwner = bBlue || bOrange;
		const bool bSpecial = Data.TileType == ETileType::Fragile || Data.TileType == ETileType::Imprint ||
			Data.TileType == ETileType::SingleUse;
		Color = bHasOwner ? FMath::Lerp(MemoryStone * .48f, Ink, .52f) :
			Data.bPrintReceiver || Data.bPrintTrigger ? DarkSeam : MemoryStone;
		Paint(Trim, bHasOwner ? Ink : bSpecial ? FLinearColor(.76f,.62f,.35f) :
			FLinearColor(.38f,.59f,.66f), bHasOwner ? .65f : .16f);
		if (MarkerText)
		{
			const TCHAR* Label = Data.bPrintPending ? TEXT("WAIT") : !Data.InteractionLabel.IsEmpty() ? *Data.InteractionLabel :
				Data.bPrintTrigger ? TEXT("OUT") :
				Data.bPrintReceiver ? TEXT("IN") :
				Data.TileType == ETileType::SingleUse ? TEXT("1") : *Data.Marker;
			MarkerText->SetText(FText::FromString(Label));
			MarkerText->SetTextRenderColor(Data.bPrintPending ? FColor(255,217,142) : FColor(219,245,245));
		}
		for (UStaticMeshComponent* Print : BluePrints) { Print->SetVisibility(bBlue); }
		for (UStaticMeshComponent* Print : OrangePrints) { Print->SetVisibility(bOrange); }
		for (int32 Index = 0; Index < Countdown.Num(); ++Index)
		{
			const bool bLive = Index < Data.RemainingMoves;
			Paint(Countdown[Index], bLive ? FLinearColor(.96f,.68f,.32f) : DarkSeam, bLive ? 1.f : .05f);
		}
		if (Data.bPrintPending ||
			(Data.TileType == ETileType::Fragile && Data.RemainingMoves == 1 && !Data.bIsOccupied))
		{
			SetActorTickEnabled(true);
		}
		if (bPulse && !Data.bIsCollapsed)
		{
			Ripple->SetVisibility(true);
			AnimationTime = 0.f;
			SetActorTickEnabled(true);
		}
	}
	else if (Data.TileType == ETileType::Obstacle)
	{
		Color = FLinearColor(.16f,.26f,.32f);
	}
	else if (Data.TileType == ETileType::Plate)
	{
		const FLinearColor Accent = RoleColor(Data.SwitchRole);
		Paint(Trim, Accent * (Data.bIsActivated ? 1.f : .48f), Data.bIsActivated ? 1.2f : .26f);
		Paint(Portal, Data.bIsActivated ? Accent * .35f : DarkSeam, .25f);
	}
	else if (Portal)
	{
		const bool bGoal = Data.TileType == ETileType::Goal;
		const bool bLit = bGoal ? !Data.bIsLocked : Data.bIsActivated;
		const FLinearColor Accent = RoleColor(bGoal ? Data.GoalOwner : Data.SwitchRole);
		Paint(Trim, Accent * (bLit ? 1.f : .42f), bLit ? .8f : .18f);
		Paint(Portal, bLit ? Accent : FLinearColor(.34f,.36f,.40f), bLit ? 1.45f : .1f);
	}
	Paint(Top, Color, .08f);
	if (bFirstCollapse)
	{
		Ripple->SetVisibility(false);
		AnimationTime = 0.f;
		SetActorTickEnabled(true);
		OnTileCollapsed();
	}
	OnTileUpdated(Data, bPulse);
}

// 结局沿保留下来的脚印提高边框亮度，Strength 由管理器的演出计时决定。
void AGridTile::AnimateOutro(float Strength)
{
	if (TileData.HasFootprints() && !TileData.bIsCollapsed && TileData.FootprintOwner != EFootprintOwner::None)
	{
		const FLinearColor Ink = GridVisuals::RoleColor(TileData.FootprintOwner == EFootprintOwner::PlayerA ?
			EPlayerRole::PlayerA : EPlayerRole::PlayerB);
		GridVisuals::Paint(Trim, Ink, 1.f + Strength * 2.8f);
	}
}

// Tick 只推进视觉过渡，地块状态与脚印可通行性由规则层维护。
void AGridTile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (DeckDuration > 0.f)
	{
		DeckElapsed += DeltaSeconds;
		const float Alpha = FMath::Min(DeckElapsed / DeckDuration, 1.f);
		const float Angle = FMath::SmoothStep(0.f, DeckAngle, Alpha);
		Pieces->SetRelativeLocation(Alpha < 1.f ?
			DeckPivot + DeckStart.RotateAngleAxis(Angle, FVector::UpVector) : FVector::ZeroVector);
		if (Alpha >= 1.f) { DeckDuration = 0.f; }
	}
	// 有人站在桥上时保留脚下的可见桥面，直到离开后再完成收拢。
	if (TileData.TileType == ETileType::Bridge)
	{
		const float Target = TileData.bIsActivated || TileData.bIsOccupied ? 1.f : 0.f;
		BridgeDeck = FMath::FInterpConstantTo(BridgeDeck, Target, DeltaSeconds, 6.f);
		Top->SetVisibility(BridgeDeck > 0.f);
		Top->SetRelativeScale3D(FVector(.83f, .83f * FMath::Max(BridgeDeck, .01f), .12f));
		SetActorTickEnabled(!FMath::IsNearlyEqual(BridgeDeck, Target));
		return;
	}
	AnimationTime += DeltaSeconds;
	if (TileData.bIsCollapsed)
	{
		const float Alpha = FMath::Min(1.f, AnimationTime / .40f);
		Pieces->SetRelativeLocation(FVector(0,0,-120.f * Alpha * Alpha));
		Pieces->SetRelativeRotation(FRotator(31*Alpha,-12*Alpha,18*Alpha));
		Pieces->SetRelativeScale3D(FVector(1.f-.70f*Alpha));
		if (Alpha >= 1.f)
		{
			SetActorHiddenInGame(true);
			SetActorTickEnabled(false);
		}
		return;
	}
	if (Ripple && Ripple->IsVisible())
	{
		const float Alpha = FMath::Min(1.f, AnimationTime / .27f);
		const FLinearColor Ink = GridVisuals::RoleColor(TileData.FootprintOwner == EFootprintOwner::PlayerA ?
			EPlayerRole::PlayerA : EPlayerRole::PlayerB);
		Ripple->SetRelativeScale3D(FVector(.65f + .28f*Alpha,.65f + .28f*Alpha,.013f));
		GridVisuals::Paint(Ripple, Ink, .9f * (1.f - Alpha));
		Ripple->SetVisibility(Alpha < 1.f);
	}
	const bool bLastStep = TileData.TileType == ETileType::Fragile &&
		TileData.RemainingMoves == 1 && !TileData.bIsOccupied;
	if (TileData.bPrintPending)
	{
		const float Pulse = .5f + .5f * FMath::Sin(GetWorld()->GetTimeSeconds() * 6.f);
		GridVisuals::Paint(Trim, FLinearColor(.94f,.62f,.24f), .38f + .65f * Pulse);
	}
	if (bLastStep)
	{
		const bool bFlash = FMath::Fmod(GetWorld()->GetTimeSeconds(), .65f) < .36f;
		GridVisuals::Paint(Countdown[0], bFlash ? FLinearColor(1.f,.64f,.28f) : DarkSeam, bFlash ? 1.7f : .1f);
	}
	if (bEchoAvailable && EchoMarker && EchoHalo && EchoBeam)
	{
		EchoTime += DeltaSeconds;
		const float Bob = FMath::Sin(EchoTime * 4.f) * 6.f;
		const float Pulse = .65f + .35f * (0.5f + 0.5f * FMath::Sin(EchoTime * 5.5f));
		const FLinearColor EchoGold(.92f,.58f,.16f);
		EchoMarker->SetRelativeLocation(FVector(0,0,77.f + Bob));
		EchoMarker->SetRelativeRotation(FRotator(EchoTime * 35.f,EchoTime * 92.f,45.f));
		EchoMarker->SetRelativeScale3D(FVector(.135f + Pulse * .035f));
		EchoHalo->SetRelativeScale3D(FVector(.36f + Pulse * .10f,.36f + Pulse * .10f,.016f));
		EchoBeam->SetRelativeScale3D(FVector(.020f,.020f,.52f + Pulse * .14f));
		for (int32 Index = 0; Index < EchoShards.Num(); ++Index)
		{
			const float Angle = FMath::DegreesToRadians(EchoTime * 58.f + Index * 120.f);
			EchoShards[Index]->SetRelativeLocation(FVector(
				FMath::Cos(Angle) * 27.f, FMath::Sin(Angle) * 27.f,
				62.f + FMath::Sin(EchoTime * 4.6f + Index * 2.1f) * 5.f));
			EchoShards[Index]->SetRelativeRotation(FRotator(180, FMath::RadiansToDegrees(Angle), 0));
		}
		GridVisuals::Paint(EchoMarker, EchoGold, 1.4f + Pulse * 1.5f);
		GridVisuals::Paint(EchoHalo, EchoGold, .9f + Pulse * 1.1f);
		GridVisuals::Paint(EchoBeam, EchoGold, .55f + Pulse * .9f);
		for (UStaticMeshComponent* Shard : EchoShards) { GridVisuals::Paint(Shard, EchoGold, 1.2f + Pulse * 1.2f); }
		if (EchoLight) { EchoLight->SetIntensity(650.f + Pulse * 650.f); }
	}
	// 转动、闪烁、寄印等待和回响动画结束后关闭 Tick，减少静止格子的逐帧开销。
	SetActorTickEnabled(DeckDuration > 0.f || bLastStep || TileData.bPrintPending ||
		(Ripple && Ripple->IsVisible()) || bEchoAvailable);
}

// 人物构造时只有层级，头、身体和围巾由 Setup 在运行时拼装。
AGridPuzzleCharacter::AGridPuzzleCharacter()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Figure = CreateDefaultSubobject<USceneComponent>(TEXT("Figure"));
	Figure->SetupAttachment(RootComponent);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

// 使用圆锥、球体和圆柱组合人物，颜色由角色身份与蓝图围巾配置决定。
void AGridPuzzleCharacter::Setup(EPlayerRole NewRole, FIntPoint Start, UMaterialInterface* Material)
{
	using namespace GridVisuals;
	TravelerRole = NewRole;
	GridPosition = Start;
	const FLinearColor Ink = RoleColor(TravelerRole);
	const FLinearColor Cloth = Ink * .46f + FLinearColor(.035f,.048f,.06f);
	const FLinearColor Cream(.77f,.84f,.82f);
	const bool bBlue = TravelerRole == EPlayerRole::PlayerA;
	Shadow = Shape(*this, RootComponent, Material, EGridShape::Cylinder, FVector(0,0,5),
		FVector(.54f,.54f,.014f), DarkSeam, .25f);
	Selection = Shape(*this, RootComponent, Material, bBlue ? EGridShape::Cylinder : EGridShape::Cube,
		FVector(0,0,4), FVector(.65f,.65f,.012f), Ink, 1.65f, FRotator(0,bBlue ? 0 : 45,0));
	Shape(*this, Figure, Material, EGridShape::Cone, FVector(0,0,32),
		FVector(.43f,.40f,.52f), Cloth)->SetCastShadow(true);
	Shape(*this, Figure, Material, EGridShape::Cylinder, FVector(0,0,10),
		FVector(.395f,.375f,.026f), Ink, .55f);
	Shape(*this, Figure, Material, EGridShape::Sphere, FVector(1,0,53),
		FVector(.305f,.29f,.315f), Cloth)->SetCastShadow(true);
	Shape(*this, Figure, Material, EGridShape::Sphere, FVector(11,0,52),
		FVector(.17f,.23f,.204f), FLinearColor(.018f,.039f,.055f), .18f);
	for (const float Side : {-4.8f, 4.8f})
	{
		Shape(*this, Figure, Material, EGridShape::Sphere, FVector(18,Side,54),
			FVector(.031f,.032f,.039f), Cream, 1.4f);
		Shape(*this, Figure, Material, EGridShape::Cube, FVector(6,Side*1.5f,5.5f),
			FVector(.118f,.09f,.08f), DarkSeam, .2f);
	}
	Shape(*this, Figure, Material, EGridShape::Cylinder, FVector(0,0,38),
		FVector(.305f,.29f,.053f), ScarfColor * Ink, .65f);
	Shape(*this, Figure, Material, EGridShape::Cube, FVector(-12,-7,34),
		FVector(.26f,.072f,.032f), ScarfColor * Ink, .57f, FRotator(14,17,-8));
	Figure->SetRelativeRotation(FRotator(0,155,0));

	// A floating beacon above the head cannot be confused with a footprint on the board.
	ControlHalo = Shape(*this, RootComponent, Material, EGridShape::Cylinder, FVector(0,0,91),
		FVector(.21f,.21f,.012f), Ink, 2.2f);
	ControlPointer = Shape(*this, RootComponent, Material, EGridShape::Cone, FVector(0,0,98),
		FVector(.08f,.08f,.16f), Ink, 3.1f, FRotator(180,0,0));
	ControlDiamond = Shape(*this, RootComponent, Material, EGridShape::Cube, FVector(0,0,111),
		FVector(.095f,.095f,.095f), Cream, 3.2f, FRotator(0,45,45));
	ControlHalo->SetVisibility(false);
	ControlPointer->SetVisibility(false);
	ControlDiamond->SetVisibility(false);
}

// 立即更新逻辑格坐标，并记录视觉起终点，由后续 Tick 平滑走完这一步。
void AGridPuzzleCharacter::MoveTo(FIntPoint Destination, FVector WorldDestination, float MoveDuration, bool bCurve)
{
	GridPosition = Destination;
	From = GetActorLocation();
	To = WorldDestination;
	Nudge = FVector::ZeroVector;
	CurveOffset = bCurve ? FVector::CrossProduct(FVector::UpVector, (To - From).GetSafeNormal()) * 22.f : FVector::ZeroVector;
	Elapsed = 0.f;
	Duration = MoveDuration;
	Figure->SetRelativeRotation((To-From).Rotation());
	SetActorTickEnabled(true);
	OnCharacterMoved(Destination);
}

// 被规则拒绝的输入只产生短促探身反馈，人物最终回到原世界位置。
void AGridPuzzleCharacter::RejectMove(FVector WorldDirection)
{
	From = To = GetActorLocation();
	Nudge = WorldDirection.GetSafeNormal() * 8.f;
	CurveOffset = FVector::ZeroVector;
	Elapsed = 0.f;
	Duration = .14f;
	SetActorTickEnabled(true);
}

// 头顶信标标出当前控制角色，抵达终点后的状态由管理器传入。
void AGridPuzzleCharacter::SetControlState(bool bControlled, bool bFinished)
{
	bIsControlled = bControlled && !bFinished;
	bIsFinished = bFinished;
	Selection->SetVisibility(bIsControlled);
	ControlHalo->SetVisibility(bIsControlled);
	ControlPointer->SetVisibility(bIsControlled);
	ControlDiamond->SetVisibility(bIsControlled);
	BeaconTime = 0.f;
	SetActorTickEnabled(bIsControlled || Duration > 0.f);
	GridVisuals::Paint(Shadow, bFinished ? GridVisuals::RoleColor(TravelerRole) * .5f : DarkSeam, bFinished ? 1.f : .25f);
	OnControlChanged(bControlled, bFinished);
}

// 按经过时间插值位置，叠加起伏、换位弧线和控制信标的动态效果。
void AGridPuzzleCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Duration > 0.f)
	{
		Elapsed += DeltaSeconds;
		const float Alpha = FMath::Min(1.f, Elapsed / Duration);
		const float Hop = FMath::Sin(PI * Alpha);
		SetActorLocation(FMath::Lerp(From, To, FMath::SmoothStep(0.f,1.f,Alpha)) + (Nudge + CurveOffset) * Hop);
		Figure->SetRelativeLocation(FVector(0,0,Nudge.IsZero() ? 9.f * Hop : 0.f));
		if (Alpha >= 1.f)
		{
			SetActorLocation(To);
			Figure->SetRelativeLocation(FVector::ZeroVector);
			Duration = 0.f;
		}
	}

	if (bIsControlled)
	{
		BeaconTime += DeltaSeconds;
		const float Bob = FMath::Sin(BeaconTime * 4.2f) * 3.2f;
		ControlDiamond->SetRelativeLocation(FVector(0,0,111.f + Bob));
		ControlDiamond->SetRelativeRotation(FRotator(0, BeaconTime * 95.f, 45.f));
		ControlPointer->SetRelativeLocation(FVector(0,0,98.f + Bob * .45f));
		const float Pulse = .70f + .30f * (0.5f + 0.5f * FMath::Sin(BeaconTime * 5.4f));
		GridVisuals::Paint(ControlDiamond, GridVisuals::RoleColor(TravelerRole), 2.2f + Pulse * 1.8f);
		GridVisuals::Paint(ControlPointer, GridVisuals::RoleColor(TravelerRole), 1.8f + Pulse * 1.4f);
	}

	SetActorTickEnabled(bIsControlled || Duration > 0.f);
}
