#include "GridPuzzleVisuals.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	constexpr float FootHeight = 4.f;
	const FLinearColor DarkSeam(.036f, .068f, .091f);
	const FLinearColor BridgeLight(.29f, .84f, .76f);
	const FLinearColor BridgeDim(.21f, .33f, .37f);

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
		Label->RegisterComponent();
		return Label;
	}
}

FLinearColor GridVisuals::RoleColor(EPlayerRole Role)
{
	return Role == EPlayerRole::PlayerA ? FLinearColor(.14f, .66f, 1.f) : FLinearColor(1.f, .36f, .105f);
}

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
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetRelativeTransform(FTransform(Rotation, Position, Scale));
	Mesh->SetCastShadow(false);
	Mesh->RegisterComponent();
	Mesh->CreateDynamicMaterialInstance(0, Material);
	Paint(Mesh, Color, Glow);
	return Mesh;
}

void GridVisuals::Paint(UStaticMeshComponent* Mesh, FLinearColor Color, float Glow)
{
	if (UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)))
	{
		Dynamic->SetVectorParameterValue(TEXT("Tint"), Color);
		Dynamic->SetVectorParameterValue(TEXT("Color"), Color);
		Dynamic->SetScalarParameterValue(TEXT("Glow"), Glow);
	}
}

AGridTile::AGridTile()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Pieces = CreateDefaultSubobject<USceneComponent>(TEXT("Pieces"));
	Pieces->SetupAttachment(RootComponent);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

void AGridTile::Setup(const FTileData& Data, UMaterialInterface* Material)
{
	using namespace GridVisuals;
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

void AGridTile::FaceMarker(const FRotator& Rotation)
{
	MarkerRotation = Rotation;
	if (MarkerText) { MarkerText->SetWorldRotation(Rotation); }
	if (BridgeReadout) { BridgeReadout->SetWorldRotation(Rotation); }
	if (ActionMarker) { ActionMarker->SetWorldRotation(Rotation); }
}

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

void AGridTile::AddBridgeLink(const FVector& SourceOffset, UMaterialInterface* Material)
{
	BridgeLinks.Add(GridVisuals::Shape(*this, RootComponent, Material, EGridShape::Cube,
		SourceOffset * .5f + FVector(0,0,-13), FVector(SourceOffset.Size() / 100.f,.023f,.018f),
		BridgeDim * .55f, .14f, SourceOffset.Rotation()));
}

void AGridTile::AddPrintLink(const FVector& TargetOffset, UMaterialInterface* Material)
{
	GridVisuals::Shape(*this, RootComponent, Material, EGridShape::Cube,
		TargetOffset * .5f + FVector(0,0,-12), FVector(TargetOffset.Size() / 100.f,.017f,.018f),
		FLinearColor(.14f,.41f,.51f), .42f, TargetOffset.Rotation());
}

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

void AGridTile::Update(const FTileData& Data, bool bPulse)
{
	using namespace GridVisuals;
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

void AGridTile::AnimateOutro(float Strength)
{
	if (TileData.HasFootprints() && !TileData.bIsCollapsed && TileData.FootprintOwner != EFootprintOwner::None)
	{
		const FLinearColor Ink = GridVisuals::RoleColor(TileData.FootprintOwner == EFootprintOwner::PlayerA ?
			EPlayerRole::PlayerA : EPlayerRole::PlayerB);
		GridVisuals::Paint(Trim, Ink, 1.f + Strength * 2.8f);
	}
}

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
	SetActorTickEnabled(DeckDuration > 0.f || bLastStep || TileData.bPrintPending || (Ripple && Ripple->IsVisible()));
}

AGridPuzzleCharacter::AGridPuzzleCharacter()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Figure = CreateDefaultSubobject<USceneComponent>(TEXT("Figure"));
	Figure->SetupAttachment(RootComponent);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

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
}

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

void AGridPuzzleCharacter::RejectMove(FVector WorldDirection)
{
	From = To = GetActorLocation();
	Nudge = WorldDirection.GetSafeNormal() * 8.f;
	CurveOffset = FVector::ZeroVector;
	Elapsed = 0.f;
	Duration = .14f;
	SetActorTickEnabled(true);
}

void AGridPuzzleCharacter::SetControlState(bool bControlled, bool bFinished)
{
	Selection->SetVisibility(bControlled && !bFinished);
	GridVisuals::Paint(Shadow, bFinished ? GridVisuals::RoleColor(TravelerRole) * .5f : DarkSeam, bFinished ? 1.f : .25f);
	OnControlChanged(bControlled, bFinished);
}

void AGridPuzzleCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += DeltaSeconds;
	const float Alpha = FMath::Min(1.f, Elapsed / Duration);
	const float Hop = FMath::Sin(PI * Alpha);
	SetActorLocation(FMath::Lerp(From, To, FMath::SmoothStep(0.f,1.f,Alpha)) + (Nudge + CurveOffset) * Hop);
	Figure->SetRelativeLocation(FVector(0,0,Nudge.IsZero() ? 9.f * Hop : 0.f));
	if (Alpha >= 1.f)
	{
		SetActorLocation(To);
		Figure->SetRelativeLocation(FVector::ZeroVector);
		SetActorTickEnabled(false);
	}
}
