// Huntable wild animal for the open world game.

#include "OpenWorldAnimal.h"
#include "OpenWorldGameMode.h"
#include "OpenWorldGenerator.h"
#include "OpenWorldShapes.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

using namespace OpenWorldShapes;

namespace
{
	/** Everything that makes one kind of animal different. Lengths in cm, speeds in cm/s. */
	struct FAnimalSpec
	{
		float Health;
		float WalkSpeed;
		float RunSpeed;
		/** Distance at which a sprinting player is heard */
		float HearRange;
		/** Distance at which a moving player is seen in daylight */
		float SightRange;
		float CapsuleRadius;
		float CapsuleHalfHeight;
		/** Highest ledge it will climb (blocky terrain), jumping if it has to */
		float MaxClimb;
		bool bJumper;
		float TurnRate;
		float MeatKg;
		bool bAggressive;

		FVector Torso;
		float LegLength;
		float LegRadius;
		float NeckLength;
		float NeckRadius;
		/** Neck angle above horizontal */
		float NeckAngle;
		FVector Head;
		/** Distance covered by one full gait cycle */
		float Stride;
		float LegSwing;
		float GrazeNeck;
		float GrazeHead;

		FColor Coat;
		FColor Belly;
		FColor Dark;
	};

	const FAnimalSpec& GetSpec(EOpenWorldAnimalKind Kind)
	{
		static const FAnimalSpec Deer = {
			100.f, 150.f, 1000.f, 2600.f, 4500.f, 34.f, 75.f, 85.f, true, 300.f, 35.f, false,
			FVector(115.f, 40.f, 48.f), 72.f, 4.5f, 62.f, 8.f, 58.f, FVector(38.f, 17.f, 20.f),
			150.f, 32.f, -78.f, -50.f,
			FColor(150, 98, 58), FColor(226, 212, 190), FColor(48, 38, 32) };
		static const FAnimalSpec Boar = {
			170.f, 120.f, 820.f, 2000.f, 1600.f, 34.f, 44.f, 42.f, false, 260.f, 45.f, true,
			FVector(105.f, 48.f, 54.f), 30.f, 6.f, 20.f, 18.f, 0.f, FVector(40.f, 30.f, 30.f),
			80.f, 30.f, -30.f, -20.f,
			FColor(78, 62, 52), FColor(96, 80, 66), FColor(38, 30, 26) };
		static const FAnimalSpec Rabbit = {
			22.f, 80.f, 760.f, 1500.f, 2200.f, 15.f, 18.f, 42.f, true, 720.f, 1.5f, false,
			FVector(30.f, 17.f, 18.f), 7.f, 2.6f, 6.f, 5.f, 40.f, FVector(15.f, 11.f, 11.f),
			55.f, 40.f, -30.f, -20.f,
			FColor(150, 128, 104), FColor(232, 224, 212), FColor(70, 58, 50) };

		switch (Kind)
		{
		case EOpenWorldAnimalKind::Boar:   return Boar;
		case EOpenWorldAnimalKind::Rabbit: return Rabbit;
		default:                           return Deer;
		}
	}

	const FName TagHead(TEXT("Head"));
	const FName TagNeck(TEXT("Neck"));
	const FName TagLeg(TEXT("Leg"));

	const FColor BloodColor(120, 8, 10);
}

AOpenWorldAnimal::AOpenWorldAnimal()
{
	PrimaryActorTick.bCanEverTick = true;

	// no controller needed: behaviour lives in Tick and the movement component runs on its own
	AutoPossessAI = EAutoPossessAI::Disabled;
	AIControllerClass = nullptr;

	UCapsuleComponent* Capsule = GetCapsuleComponent();
	// arrows are traced against the body parts, not the rough capsule
	Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Capsule->SetCanEverAffectNavigation(false);

	// the inherited skeletal mesh is unused: the body is built from basic shapes
	GetMesh()->SetVisibility(false);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetComponentTickEnabled(false);

	Body = CreateDefaultSubobject<USceneComponent>(TEXT("Body"));
	Body->SetupAttachment(Capsule);

	BloodTrail = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BloodTrail"));
	BloodTrail->SetupAttachment(Capsule);
	BloodTrail->SetUsingAbsoluteLocation(true);
	BloodTrail->SetUsingAbsoluteRotation(true);
	BloodTrail->SetUsingAbsoluteScale(true);
	BloodTrail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BloodTrail->SetCastShadow(false);
	BloodTrail->SetCanEverAffectNavigation(false);

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bRunPhysicsWithNoController = true;
	Move->bOrientRotationToMovement = true;
	Move->bUseControllerDesiredRotation = false;
	Move->BrakingDecelerationWalking = 700.f;
	Move->GroundFriction = 6.f;
	Move->JumpZVelocity = 520.f;
	Move->bCanWalkOffLedges = true;
	Move->SetCanEverAffectNavigation(false);
	bUseControllerRotationYaw = false;
}

void AOpenWorldAnimal::Setup(EOpenWorldAnimalKind InKind, int32 InHerdId, int32 InSeed)
{
	Kind = InKind;
	HerdId = InHerdId;
	Stream.Initialize(InSeed);

	const FAnimalSpec& Spec = GetSpec(Kind);
	GetCapsuleComponent()->SetCapsuleSize(Spec.CapsuleRadius, Spec.CapsuleHalfHeight);
	GetCharacterMovement()->MaxStepHeight = FMath::Min(Spec.MaxClimb, 45.f);
	GetCharacterMovement()->RotationRate = FRotator(0.f, Spec.TurnRate, 0.f);
	GetCharacterMovement()->MaxWalkSpeed = Spec.WalkSpeed;
	Health = Spec.Health;
	bStag = Kind == EOpenWorldAnimalKind::Deer && Stream.FRand() < 0.4f;
}

float AOpenWorldAnimal::GetCapsuleHalfHeight(EOpenWorldAnimalKind InKind)
{
	return GetSpec(InKind).CapsuleHalfHeight;
}

FString AOpenWorldAnimal::GetKindName(EOpenWorldAnimalKind InKind)
{
	switch (InKind)
	{
	case EOpenWorldAnimalKind::Boar:   return TEXT("Boar");
	case EOpenWorldAnimalKind::Rabbit: return TEXT("Rabbit");
	default:                           return TEXT("Deer");
	}
}

float AOpenWorldAnimal::GetMeatKg() const
{
	return GetSpec(Kind).MeatKg * (bStag ? 1.3f : 1.f);
}

void AOpenWorldAnimal::BeginPlay()
{
	Super::BeginPlay();

	BuildBody();

	if (UMaterialInstanceDynamic* BloodMat = MakeColorMaterial(this, BloodColor, 0.3f))
	{
		BloodTrail->SetStaticMesh(OpenWorldShapes::GetMesh(EShape::Sphere));
		BloodTrail->SetMaterial(0, BloodMat);
	}
	BloodTrail->SetWorldTransform(FTransform::Identity);

	SetActorRotation(FRotator(0.f, Stream.FRandRange(0.f, 360.f), 0.f));
	SetState(EOpenWorldAnimalState::Graze, Stream.FRandRange(1.f, 6.f));
	SenseTimer = Stream.FRandRange(0.f, 0.2f);
	GaitPhase = Stream.FRandRange(0.f, 6.f);
}

void AOpenWorldAnimal::BuildBody()
{
	const FAnimalSpec& S = GetSpec(Kind);
	const FVector T = S.Torso;
	const float TorsoZ = S.LegLength + T.Z * 0.4f;
	TorsoHalfWidth = T.Y * 0.5f;

	// Body sits at the torso centre so the carcass can roll over around it
	BodyBaseZ = -S.CapsuleHalfHeight + TorsoZ;
	Body->SetRelativeLocation(FVector(0.f, 0.f, BodyBaseZ));

	// every visible part blocks arrow traces (visibility channel) and nothing else
	auto Hittable = [](UStaticMeshComponent* Part, FName Tag)
	{
		Part->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Part->SetCollisionResponseToAllChannels(ECR_Ignore);
		Part->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		if (!Tag.IsNone())
		{
			Part->ComponentTags.Add(Tag);
		}
		return Part;
	};
	auto Part = [this](USceneComponent* Parent, EShape Shape, const FColor& Color, const FVector& Loc, const FRotator& Rot, const FVector& Size, bool bShadow = true)
	{
		return AddPart(this, Parent, Shape, Color, Loc, Rot, Size, MaterialCache, bShadow);
	};
	auto NewPivot = [this](USceneComponent* Parent, const FVector& Loc, const TCHAR* Name)
	{
		USceneComponent* Pivot = NewObject<USceneComponent>(this, Name);
		Pivot->SetupAttachment(Parent);
		Pivot->SetRelativeLocation(Loc);
		Pivot->RegisterComponent();
		AddInstanceComponent(Pivot);
		return Pivot;
	};

	// torso: main ellipsoid plus a lighter belly
	Hittable(Part(Body, EShape::Sphere, S.Coat, FVector::ZeroVector, FRotator::ZeroRotator, T), NAME_None);
	Part(Body, EShape::Sphere, S.Belly, FVector(0.f, 0.f, -T.Z * 0.18f), FRotator::ZeroRotator, FVector(T.X * 0.8f, T.Y * 0.85f, T.Z * 0.7f));
	if (Kind == EOpenWorldAnimalKind::Boar)
	{
		// bristly dark ridge and a big shoulder hump
		Part(Body, EShape::Sphere, S.Dark, FVector(T.X * 0.05f, 0.f, T.Z * 0.38f), FRotator::ZeroRotator, FVector(T.X * 0.8f, T.Y * 0.3f, T.Z * 0.35f));
		Hittable(Part(Body, EShape::Sphere, S.Coat, FVector(T.X * 0.25f, 0.f, T.Z * 0.1f), FRotator::ZeroRotator, FVector(T.X * 0.55f, T.Y * 1.05f, T.Z * 1.1f)), NAME_None);
	}
	else if (Kind == EOpenWorldAnimalKind::Deer)
	{
		// white rump patch
		Part(Body, EShape::Sphere, S.Belly, FVector(-T.X * 0.43f, 0.f, T.Z * 0.08f), FRotator::ZeroRotator, FVector(T.X * 0.18f, T.Y * 0.75f, T.Z * 0.6f));
	}

	// legs: pivot at the hip/shoulder, cylinder hanging down, dark hoof
	LegPivots.Reset();
	const float HipZ = -T.Z * 0.2f;
	const float LegLen = S.LegLength + T.Z * 0.2f;
	const float HoofH = FMath::Max(S.LegLength * 0.08f, 1.5f);
	const FVector LegOffsets[4] = {
		FVector(T.X * 0.32f, -T.Y * 0.3f, HipZ), FVector(T.X * 0.32f, T.Y * 0.3f, HipZ),
		FVector(-T.X * 0.32f, -T.Y * 0.3f, HipZ), FVector(-T.X * 0.32f, T.Y * 0.3f, HipZ) };
	static const TCHAR* LegNames[4] = { TEXT("LegFL"), TEXT("LegFR"), TEXT("LegBL"), TEXT("LegBR") };
	for (int32 i = 0; i < 4; ++i)
	{
		USceneComponent* Pivot = NewPivot(Body, LegOffsets[i], LegNames[i]);
		const bool bBack = i >= 2;
		// back legs: thicker thigh
		const float Thigh = S.LegRadius * (bBack ? 2.4f : 1.9f);
		Hittable(Part(Pivot, EShape::Sphere, S.Coat, FVector(0.f, 0.f, -LegLen * 0.12f), FRotator::ZeroRotator, FVector(Thigh * 2.2f, Thigh * 1.6f, LegLen * 0.45f)), TagLeg);
		Hittable(Part(Pivot, EShape::Cylinder, S.Coat, FVector(0.f, 0.f, -(LegLen - HoofH) * 0.5f), FRotator::ZeroRotator,
			FVector(S.LegRadius * 2.f, S.LegRadius * 2.f, LegLen - HoofH)), TagLeg);
		Part(Pivot, EShape::Cylinder, S.Dark, FVector(0.f, 0.f, -LegLen + HoofH * 0.5f), FRotator::ZeroRotator,
			FVector(S.LegRadius * 2.3f, S.LegRadius * 2.3f, HoofH));
		LegPivots.Add(Pivot);
	}

	// neck and head
	NeckPivot = NewPivot(Body, FVector(T.X * 0.38f, 0.f, T.Z * 0.18f), TEXT("NeckPivot"));
	const FVector NeckDir = FRotator(S.NeckAngle, 0.f, 0.f).Vector();
	Hittable(Part(NeckPivot, EShape::Cylinder, S.Coat, NeckDir * S.NeckLength * 0.5f, FRotator(S.NeckAngle - 90.f, 0.f, 0.f),
		FVector(S.NeckRadius * 2.f, S.NeckRadius * 2.f, S.NeckLength + S.NeckRadius)), TagNeck);

	HeadPivot = NewPivot(NeckPivot, NeckDir * S.NeckLength, TEXT("HeadPivot"));
	const FVector H = S.Head;
	const FRotator HeadTilt(-20.f, 0.f, 0.f);
	const FVector HeadFwd = HeadTilt.Vector();
	Hittable(Part(HeadPivot, EShape::Sphere, S.Coat, HeadFwd * H.X * 0.3f, HeadTilt, H), TagHead);

	switch (Kind)
	{
	case EOpenWorldAnimalKind::Deer:
	{
		// muzzle, black nose, big ears, antlers on stags
		Part(HeadPivot, EShape::Sphere, S.Belly, HeadFwd * H.X * 0.62f + FVector(0.f, 0.f, -2.f), HeadTilt, FVector(H.X * 0.45f, H.Y * 0.7f, H.Z * 0.6f));
		Part(HeadPivot, EShape::Sphere, S.Dark, HeadFwd * H.X * 0.82f, HeadTilt, FVector(5.f, 6.f, 5.f), false);
		for (float Side : { -1.f, 1.f })
		{
			Part(HeadPivot, EShape::Sphere, S.Coat, FVector(-2.f, Side * H.Y * 0.6f, H.Z * 0.45f), FRotator(10.f, 0.f, Side * 50.f), FVector(5.f, 4.f, 16.f));
			Part(HeadPivot, EShape::Sphere, S.Dark, FVector(H.X * 0.2f, Side * H.Y * 0.38f, H.Z * 0.2f), FRotator::ZeroRotator, FVector(4.f, 3.f, 4.f), false);
			if (bStag)
			{
				const FColor Antler(214, 196, 160);
				const FVector Base(0.f, Side * 5.f, H.Z * 0.45f);
				const FVector Tip = Base + FVector(-8.f, Side * 18.f, 38.f);
				UStaticMeshComponent* Beam = Part(HeadPivot, EShape::Cylinder, Antler, FVector::ZeroVector, FRotator::ZeroRotator, FVector(1.f));
				SetCylinderBetween(Beam, Base, Tip, 1.8f);
				for (float F : { 0.35f, 0.7f })
				{
					const FVector Root = FMath::Lerp(Base, Tip, F);
					UStaticMeshComponent* Tine = Part(HeadPivot, EShape::Cylinder, Antler, FVector::ZeroVector, FRotator::ZeroRotator, FVector(1.f));
					SetCylinderBetween(Tine, Root, Root + FVector(10.f, Side * 4.f, 12.f), 1.3f);
				}
			}
		}
		// short white tail
		TailPivot = NewPivot(Body, FVector(-T.X * 0.5f, 0.f, T.Z * 0.25f), TEXT("TailPivot"));
		Part(TailPivot, EShape::Sphere, S.Belly, FVector(-3.f, 0.f, -5.f), FRotator(20.f, 0.f, 0.f), FVector(7.f, 6.f, 14.f));
		break;
	}
	case EOpenWorldAnimalKind::Boar:
	{
		// flat snout disc, tusks, small ears
		UStaticMeshComponent* Snout = Part(HeadPivot, EShape::Cylinder, S.Belly, FVector::ZeroVector, FRotator::ZeroRotator, FVector(1.f));
		SetCylinderBetween(Snout, HeadFwd * H.X * 0.5f, HeadFwd * H.X * 0.78f, H.Z * 0.24f);
		Part(HeadPivot, EShape::Cylinder, FColor(180, 120, 116), HeadFwd * H.X * 0.79f, FRotator(HeadTilt.Pitch - 90.f, 0.f, 0.f), FVector(H.Z * 0.42f, H.Z * 0.42f, 1.f), false);
		for (float Side : { -1.f, 1.f })
		{
			UStaticMeshComponent* Tusk = Part(HeadPivot, EShape::Cone, FColor(236, 230, 210), FVector::ZeroVector, FRotator::ZeroRotator, FVector(1.f), false);
			const FVector Root = HeadFwd * H.X * 0.55f + FVector(0.f, Side * H.Y * 0.28f, -H.Z * 0.2f);
			SetCylinderBetween(Tusk, Root, Root + FVector(3.f, Side * 4.f, 11.f), 2.2f);
			Part(HeadPivot, EShape::Cone, S.Dark, FVector(-H.X * 0.15f, Side * H.Y * 0.32f, H.Z * 0.5f), FRotator(0.f, 0.f, Side * 25.f), FVector(8.f, 5.f, 12.f));
			Part(HeadPivot, EShape::Sphere, FColor(20, 16, 14), FVector(H.X * 0.22f, Side * H.Y * 0.36f, H.Z * 0.12f), FRotator::ZeroRotator, FVector(3.5f), false);
		}
		TailPivot = NewPivot(Body, FVector(-T.X * 0.5f, 0.f, T.Z * 0.2f), TEXT("TailPivot"));
		Part(TailPivot, EShape::Cylinder, S.Dark, FVector(-3.f, 0.f, -9.f), FRotator(-20.f, 0.f, 0.f), FVector(2.5f, 2.5f, 18.f), false);
		break;
	}
	case EOpenWorldAnimalKind::Rabbit:
	{
		// long upright ears, white cotton tail
		for (float Side : { -1.f, 1.f })
		{
			Hittable(Part(HeadPivot, EShape::Sphere, S.Coat, FVector(-3.f, Side * 2.5f, H.Z * 0.5f + 7.f), FRotator(-15.f, 0.f, Side * 12.f), FVector(3.5f, 2.f, 16.f)), TagHead);
			Part(HeadPivot, EShape::Sphere, FColor(20, 16, 14), FVector(H.X * 0.25f, Side * H.Y * 0.4f, H.Z * 0.15f), FRotator::ZeroRotator, FVector(2.5f), false);
		}
		Part(HeadPivot, EShape::Sphere, FColor(200, 140, 140), HeadFwd * H.X * 0.68f, FRotator::ZeroRotator, FVector(2.5f), false);
		TailPivot = NewPivot(Body, FVector(-T.X * 0.5f, 0.f, T.Z * 0.15f), TEXT("TailPivot"));
		Part(TailPivot, EShape::Sphere, FColor(246, 244, 240), FVector(-2.f, 0.f, 0.f), FRotator::ZeroRotator, FVector(8.f));
		break;
	}
	}
}

AOpenWorldGenerator* AOpenWorldAnimal::GetGenerator() const
{
	const AOpenWorldGameMode* GameMode = GetWorld()->GetAuthGameMode<AOpenWorldGameMode>();
	return GameMode ? GameMode->GetGenerator() : nullptr;
}

APawn* AOpenWorldAnimal::GetPlayerPawn() const
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	return PC ? PC->GetPawn() : nullptr;
}

void AOpenWorldAnimal::SetState(EOpenWorldAnimalState NewState, float Duration)
{
	State = NewState;
	StateTime = 0.f;
	StateDuration = Duration;
	StuckTime = 0.f;
}

void AOpenWorldAnimal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (State == EOpenWorldAnimalState::Dead)
	{
		DeadTime += DeltaSeconds;
		UpdatePose(DeltaSeconds);
		// unharvested carcasses are eventually taken by scavengers
		if (DeadTime > 300.f)
		{
			Destroy();
		}
		return;
	}

	// swept into deep water (shouldn't happen, steering avoids it)
	if (const AOpenWorldGenerator* Generator = GetGenerator())
	{
		const float FeetZ = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		if (FeetZ < Generator->GetWaterSurfaceZ() - 120.f)
		{
			Destroy();
			return;
		}
	}

	UpdateBleeding(DeltaSeconds);
	if (State == EOpenWorldAnimalState::Dead)
	{
		return;
	}

	SenseTimer -= DeltaSeconds;
	if (SenseTimer <= 0.f)
	{
		UpdateSenses(0.2f - SenseTimer);
		SenseTimer = 0.2f;
	}

	UpdateBehaviour(DeltaSeconds);
	UpdatePose(DeltaSeconds);
}

void AOpenWorldAnimal::UpdateSenses(float Interval)
{
	const APawn* Player = GetPlayerPawn();
	if (!Player)
	{
		return;
	}

	const FAnimalSpec& S = GetSpec(Kind);
	const AOpenWorldGameMode* GameMode = GetWorld()->GetAuthGameMode<AOpenWorldGameMode>();
	const float Noise = GameMode ? GameMode->GetPlayerNoise() : 0.6f;
	const float Daylight = GameMode ? GameMode->GetDaylight() : 1.f;

	const FVector ToPlayer = Player->GetActorLocation() - GetActorLocation();
	const float Dist = ToPlayer.Size();
	float Stimulus = 0.f;

	// hearing: footsteps carry further the faster you move
	const float HearDist = S.HearRange * Noise;
	if (Dist < HearDist)
	{
		Stimulus = 1.f - Dist / HearDist;
	}

	// sight: prey animals see almost all around, but mostly notice movement; worse in the dark
	const float Motion = FMath::Clamp(Player->GetVelocity().Size() / 500.f, 0.15f, 1.f);
	const float SightDist = S.SightRange * Motion * FMath::Lerp(0.35f, 1.f, Daylight);
	if (Dist < SightDist && FVector::DotProduct(GetActorForwardVector(), ToPlayer.GetSafeNormal2D()) > -0.5f)
	{
		const FVector Eye = HeadPivot ? HeadPivot->GetComponentLocation() : GetActorLocation();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AnimalSight), false, this);
		Params.AddIgnoredActor(Player);
		if (!GetWorld()->LineTraceTestByChannel(Eye, Player->GetActorLocation() + FVector(0.f, 0.f, 50.f), ECC_Visibility, Params))
		{
			Stimulus = FMath::Max(Stimulus, 1.f - Dist / SightDist);
		}
	}

	Awareness = FMath::Clamp(Awareness + (Stimulus * 2.2f - 0.12f) * Interval, 0.f, 1.2f);
	if (Stimulus > 0.f && State != EOpenWorldAnimalState::Flee && State != EOpenWorldAnimalState::Charge)
	{
		Threat = Player->GetActorLocation();
	}
}

void AOpenWorldAnimal::HearNoise(const FVector& Location, float Radius)
{
	if (IsDead())
	{
		return;
	}
	const float Range = Radius * GetSpec(Kind).HearRange / 2000.f;
	const float Dist = FVector::Dist(Location, GetActorLocation());
	if (Dist > Range)
	{
		return;
	}
	Threat = Location;
	if (Dist < Range * 0.6f)
	{
		Startle(Location, true);
	}
	else
	{
		Awareness = FMath::Max(Awareness, 0.6f);
	}
}

void AOpenWorldAnimal::Startle(const FVector& From, bool bWarnHerd)
{
	if (IsDead())
	{
		return;
	}
	const FAnimalSpec& S = GetSpec(Kind);
	Awareness = 1.f;
	Threat = From;

	const APawn* Player = GetPlayerPawn();
	const float PlayerDist = Player ? FVector::Dist(Player->GetActorLocation(), GetActorLocation()) : TNumericLimits<float>::Max();
	if (S.bAggressive && Player && ChargeCooldown <= 0.f && (IsWounded() || PlayerDist < 700.f))
	{
		// a hurt or cornered boar goes for the hunter
		Threat = Player->GetActorLocation();
		SetState(EOpenWorldAnimalState::Charge, 6.f);
	}
	else if (State != EOpenWorldAnimalState::Flee)
	{
		SetState(EOpenWorldAnimalState::Flee, Stream.FRandRange(6.f, 10.f));
	}

	if (bWarnHerd)
	{
		if (const AOpenWorldGenerator* Generator = GetGenerator())
		{
			for (AOpenWorldAnimal* Other : Generator->GetAnimals())
			{
				if (IsValid(Other) && Other != this && Other->HerdId == HerdId && !Other->IsDead()
					&& Other->State != EOpenWorldAnimalState::Flee && Other->PendingStartleTime < 0.f
					&& FVector::Dist(Other->GetActorLocation(), GetActorLocation()) < 3000.f)
				{
					// the rest of the herd bolts a moment later
					Other->PendingStartleTime = Other->Stream.FRandRange(0.15f, 0.6f);
					Other->PendingStartleFrom = From;
				}
			}
		}
	}
}

void AOpenWorldAnimal::UpdateBehaviour(float DeltaSeconds)
{
	const FAnimalSpec& S = GetSpec(Kind);
	StateTime += DeltaSeconds;
	ChargeCooldown -= DeltaSeconds;

	if (PendingStartleTime >= 0.f)
	{
		PendingStartleTime -= DeltaSeconds;
		if (PendingStartleTime < 0.f)
		{
			Startle(PendingStartleFrom, false);
		}
	}

	const bool bCalm = State == EOpenWorldAnimalState::Graze || State == EOpenWorldAnimalState::Wander || State == EOpenWorldAnimalState::Alert;
	if (bCalm && Awareness >= 1.f)
	{
		Startle(Threat, true);
	}
	else if (bCalm && State != EOpenWorldAnimalState::Alert && Awareness >= 0.3f)
	{
		SetState(EOpenWorldAnimalState::Alert, 0.f);
	}

	const float Wounded = IsWounded() ? FMath::Lerp(0.6f, 0.9f, Health / S.Health) : 1.f;
	const FVector Loc = GetActorLocation();

	switch (State)
	{
	case EOpenWorldAnimalState::Graze:
		if (StateTime > StateDuration)
		{
			if (PickWanderTarget())
			{
				SetState(EOpenWorldAnimalState::Wander, Stream.FRandRange(8.f, 14.f));
			}
			else
			{
				SetState(EOpenWorldAnimalState::Graze, Stream.FRandRange(2.f, 5.f));
			}
		}
		break;

	case EOpenWorldAnimalState::Wander:
	{
		const FVector Delta = WanderTarget - Loc;
		if (Delta.Size2D() < 120.f || StateTime > StateDuration)
		{
			SetState(EOpenWorldAnimalState::Graze, Stream.FRandRange(4.f, 10.f));
		}
		else
		{
			// rabbits move in short hops between pauses
			Steer(Delta, S.WalkSpeed * Wounded, DeltaSeconds);
		}
		break;
	}

	case EOpenWorldAnimalState::Alert:
	{
		// freeze and stare at whatever caught our attention
		const FRotator Look(0.f, (Threat - Loc).GetSafeNormal2D().Rotation().Yaw, 0.f);
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), Look, DeltaSeconds, 4.f));
		if (Awareness < 0.12f && StateTime > 1.5f)
		{
			SetState(EOpenWorldAnimalState::Graze, Stream.FRandRange(2.f, 5.f));
		}
		break;
	}

	case EOpenWorldAnimalState::Flee:
	{
		// keep running from the hunter while we can still hear or see them
		if (const APawn* Player = GetPlayerPawn())
		{
			if (Awareness > 0.5f)
			{
				Threat = Player->GetActorLocation();
			}
		}
		FVector Away = (Loc - Threat).GetSafeNormal2D();
		if (Away.IsNearlyZero())
		{
			Away = GetActorForwardVector();
		}
		if (Kind == EOpenWorldAnimalKind::Rabbit)
		{
			ZigZagTimer -= DeltaSeconds;
			if (ZigZagTimer <= 0.f)
			{
				ZigZagTimer = Stream.FRandRange(0.35f, 0.8f);
				ZigZagSign = -ZigZagSign;
			}
			Away = Away.RotateAngleAxis(35.f * ZigZagSign, FVector::UpVector);
		}
		Steer(Away, S.RunSpeed * Wounded, DeltaSeconds);

		const float SafeDist = S.HearRange * 1.2f;
		if (StateTime > StateDuration && FVector::Dist2D(Loc, Threat) > SafeDist)
		{
			Awareness = 0.5f;
			SetState(EOpenWorldAnimalState::Alert, 0.f);
		}
		else if (StateTime > StateDuration + 10.f)
		{
			// cornered against the coast: calm down eventually
			Awareness = 0.6f;
			SetState(EOpenWorldAnimalState::Alert, 0.f);
		}
		break;
	}

	case EOpenWorldAnimalState::Charge:
	{
		APawn* Player = GetPlayerPawn();
		if (!Player || StateTime > StateDuration)
		{
			ChargeCooldown = 8.f;
			SetState(EOpenWorldAnimalState::Flee, Stream.FRandRange(5.f, 8.f));
			break;
		}
		const FVector ToPlayer = Player->GetActorLocation() - Loc;
		Steer(ToPlayer, S.RunSpeed * 0.9f * Wounded, DeltaSeconds);
		if (ToPlayer.Size2D() < S.CapsuleRadius + 110.f && FMath::Abs(ToPlayer.Z) < 160.f)
		{
			if (AOpenWorldGameMode* GameMode = GetWorld()->GetAuthGameMode<AOpenWorldGameMode>())
			{
				GameMode->DamagePlayer(22.f, TEXT("A boar gored you!"), Loc);
			}
			ChargeCooldown = 8.f;
			Threat = Player->GetActorLocation();
			SetState(EOpenWorldAnimalState::Flee, Stream.FRandRange(4.f, 6.f));
		}
		break;
	}

	case EOpenWorldAnimalState::Dead:
		break;
	}
}

bool AOpenWorldAnimal::PickWanderTarget()
{
	const AOpenWorldGenerator* Generator = GetGenerator();
	if (!Generator)
	{
		return false;
	}

	// drift towards the rest of the herd so groups stay together
	FVector Centre = GetActorLocation();
	int32 Count = 1;
	for (const AOpenWorldAnimal* Other : Generator->GetAnimals())
	{
		if (IsValid(Other) && Other != this && Other->HerdId == HerdId && !Other->IsDead())
		{
			Centre += Other->GetActorLocation();
			++Count;
		}
	}
	Centre /= float(Count);

	const float Range = Kind == EOpenWorldAnimalKind::Rabbit ? 500.f : 1400.f;
	for (int32 Try = 0; Try < 8; ++Try)
	{
		const FVector Base = Count > 1 ? Centre : GetActorLocation();
		const FVector Candidate = Base + FVector(Stream.FRandRange(-Range, Range), Stream.FRandRange(-Range, Range), 0.f);
		if (Generator->IsOpenGround(Candidate.X, Candidate.Y))
		{
			WanderTarget = Candidate;
			return true;
		}
	}
	return false;
}

void AOpenWorldAnimal::Steer(const FVector& Direction, float Speed, float DeltaSeconds)
{
	const AOpenWorldGenerator* Generator = GetGenerator();
	const FVector Dir = Direction.GetSafeNormal2D();
	if (!Generator || Dir.IsNearlyZero())
	{
		return;
	}

	const FAnimalSpec& S = GetSpec(Kind);
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = Speed;

	const FVector Loc = GetActorLocation();
	const float Probe = FMath::Max(S.CapsuleRadius * 2.f + Speed * 0.35f, 120.f);
	// try straight ahead first, then fan out to either side
	static const float Angles[] = { 0.f, 25.f, -25.f, 50.f, -50.f, 80.f, -80.f, 115.f, -115.f, 150.f, -150.f, 180.f };
	FVector Chosen = FVector::ZeroVector;
	for (const float Angle : Angles)
	{
		const FVector Candidate = Dir.RotateAngleAxis(Angle * ZigZagSign, FVector::UpVector);
		if (Generator->CanWalk(Loc, Loc + Candidate * Probe, S.MaxClimb, 220.f))
		{
			Chosen = Candidate;
			break;
		}
	}
	if (Chosen.IsZero())
	{
		StuckTime += DeltaSeconds;
		return;
	}
	AddMovementInput(Chosen, 1.f);

	// jumpers hop up ledges the movement component can't step onto
	if (S.bJumper && Move->IsMovingOnGround())
	{
		const FVector Ahead = Loc + Chosen * (S.CapsuleRadius + 50.f);
		const float FeetZ = Loc.Z - S.CapsuleHalfHeight;
		if (Generator->GetGroundZ(Ahead.X, Ahead.Y) - FeetZ > Move->MaxStepHeight - 5.f)
		{
			Jump();
		}
	}

	// bumped into a rock or another animal: pick another way
	if (Move->IsMovingOnGround() && GetVelocity().Size2D() < Speed * 0.2f)
	{
		StuckTime += DeltaSeconds;
		if (StuckTime > 0.8f)
		{
			StuckTime = 0.f;
			ZigZagSign = -ZigZagSign;
			if (State == EOpenWorldAnimalState::Wander)
			{
				SetState(EOpenWorldAnimalState::Graze, Stream.FRandRange(1.f, 3.f));
			}
			else if (S.bJumper)
			{
				Jump();
			}
		}
	}
	else
	{
		StuckTime = 0.f;
	}
}

void AOpenWorldAnimal::UpdateBleeding(float DeltaSeconds)
{
	if (BleedRate <= 0.f)
	{
		return;
	}

	Health -= BleedRate * DeltaSeconds;
	const FVector Loc = GetActorLocation();
	const float Spacing = Kind == EOpenWorldAnimalKind::Rabbit ? 80.f : 110.f;
	if (FVector::Dist2D(Loc, LastBloodLocation) > Spacing)
	{
		LastBloodLocation = Loc;
		AddBlood(Loc + FVector(Stream.FRandRange(-20.f, 20.f), Stream.FRandRange(-20.f, 20.f), 0.f), Stream.FRandRange(14.f, 24.f));
	}

	if (Health <= 0.f)
	{
		Die(false);
	}
}

void AOpenWorldAnimal::AddBlood(const FVector& Location, float Size)
{
	const AOpenWorldGenerator* Generator = GetGenerator();
	if (!Generator)
	{
		return;
	}
	const float Z = Generator->GetGroundZ(Location.X, Location.Y);
	BloodTrail->AddInstance(FTransform(FRotator(0.f, Stream.FRandRange(0.f, 180.f), 0.f), FVector(Location.X, Location.Y, Z + 2.f),
		FVector(Size / 100.f, Size * 0.7f / 100.f, 0.012f)), true);
}

void AOpenWorldAnimal::ReceiveArrowHit(const FHitResult& Hit, float Speed, const FVector& ShotOrigin)
{
	if (IsDead())
	{
		return;
	}

	const FAnimalSpec& S = GetSpec(Kind);
	const UPrimitiveComponent* Part = Hit.GetComponent();
	float ZoneMultiplier = 1.f;
	if (Part && Part->ComponentHasTag(TagHead))
	{
		ZoneMultiplier = 2.5f;
	}
	else if (Part && Part->ComponentHasTag(TagNeck))
	{
		ZoneMultiplier = 1.6f;
	}
	else if (Part && Part->ComponentHasTag(TagLeg))
	{
		ZoneMultiplier = 0.4f;
	}

	// kinetic energy grows with the square of the arrow speed: a full draw hits much harder
	const float Energy = FMath::Square(Speed / 6000.f);
	const float Damage = 110.f * Energy * ZoneMultiplier;
	Health -= Damage;
	AddBlood(Hit.ImpactPoint, 22.f);

	if (Health <= 0.f)
	{
		Die(true);
		return;
	}

	// wounded: bleeds, and the herd scatters
	BleedRate += 1.5f + Damage * 0.04f;
	LastBloodLocation = GetActorLocation();
	Startle(ShotOrigin, true);
}

void AOpenWorldAnimal::Die(bool bInstant)
{
	if (IsDead())
	{
		return;
	}
	SetState(EOpenWorldAnimalState::Dead, 0.f);
	Health = 0.f;
	BleedRate = 0.f;
	PendingStartleTime = -1.f;

	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// it may have died mid-leap: the carcass rests on the ground
	if (const AOpenWorldGenerator* Generator = GetGenerator())
	{
		const FVector Loc = GetActorLocation();
		SetActorLocation(FVector(Loc.X, Loc.Y, Generator->GetGroundZ(Loc.X, Loc.Y) + GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
	}

	// small pool under the carcass
	for (int32 i = 0; i < 3; ++i)
	{
		AddBlood(GetActorLocation() + FVector(Stream.FRandRange(-45.f, 45.f), Stream.FRandRange(-45.f, 45.f), 0.f), Stream.FRandRange(40.f, 65.f));
	}

	if (AOpenWorldGameMode* GameMode = GetWorld()->GetAuthGameMode<AOpenWorldGameMode>())
	{
		GameMode->OnAnimalKilled(this, bInstant);
	}

	// spook the rest of the herd
	if (const AOpenWorldGenerator* Generator = GetGenerator())
	{
		for (AOpenWorldAnimal* Other : Generator->GetAnimals())
		{
			if (IsValid(Other) && Other != this && !Other->IsDead() && FVector::Dist(Other->GetActorLocation(), GetActorLocation()) < 2500.f)
			{
				Other->Startle(GetActorLocation(), false);
			}
		}
	}
}

float AOpenWorldAnimal::Harvest()
{
	const float Meat = GetMeatKg();
	Destroy();
	return Meat;
}

void AOpenWorldAnimal::UpdatePose(float DeltaSeconds)
{
	const FAnimalSpec& S = GetSpec(Kind);

	if (State == EOpenWorldAnimalState::Dead)
	{
		// topple onto the side
		DeathBlend = FMath::Min(DeathBlend + DeltaSeconds * 2.5f, 1.f);
		const float Ease = 1.f - FMath::Square(1.f - DeathBlend);
		const float RestZ = -S.CapsuleHalfHeight + TorsoHalfWidth * 0.9f;
		Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, FMath::Lerp(BodyBaseZ, RestZ, Ease)), FRotator(0.f, 0.f, 90.f * Ease));
		for (int32 i = 0; i < LegPivots.Num(); ++i)
		{
			LegPivots[i]->SetRelativeRotation(FRotator(FMath::Lerp(LegPivots[i]->GetRelativeRotation().Pitch, (i < 2 ? 25.f : -20.f), Ease), 0.f, 0.f));
		}
		if (NeckPivot)
		{
			NeckPivot->SetRelativeRotation(FRotator(-25.f * Ease, 0.f, 0.f));
		}
		return;
	}

	const float Speed = GetVelocity().Size2D();
	const float Cycle = FMath::Max(S.Stride, 1.f);
	GaitPhase = FMath::Fmod(GaitPhase + Speed / Cycle * UE_TWO_PI * DeltaSeconds, UE_TWO_PI * 64.f);
	const float Amount = FMath::Clamp(Speed / (S.WalkSpeed * 1.5f), 0.f, 1.f);
	const bool bRunning = Speed > S.WalkSpeed * 2.f;
	const bool bHopper = Kind == EOpenWorldAnimalKind::Rabbit;

	// walk: diagonal pairs together; run and hop: front pair against back pair
	float Phases[4];
	if (bHopper || bRunning)
	{
		Phases[0] = 0.f; Phases[1] = 0.3f; Phases[2] = UE_PI; Phases[3] = UE_PI + 0.3f;
	}
	else
	{
		Phases[0] = 0.f; Phases[1] = UE_PI; Phases[2] = UE_PI; Phases[3] = 0.f;
	}
	const float Swing = S.LegSwing * (bRunning ? 1.3f : 1.f) * Amount;
	for (int32 i = 0; i < LegPivots.Num(); ++i)
	{
		LegPivots[i]->SetRelativeRotation(FRotator(FMath::Sin(GaitPhase + Phases[i]) * Swing, 0.f, 0.f));
	}

	// body bounce and rocking
	const float Bob = FMath::Abs(FMath::Sin(GaitPhase)) * Amount * (bHopper ? S.Torso.Z * 0.6f : (bRunning ? S.Torso.Z * 0.18f : S.Torso.Z * 0.04f));
	const float Rock = FMath::Cos(GaitPhase) * Amount * (bRunning || bHopper ? 6.f : 1.5f);
	Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, BodyBaseZ + Bob), FRotator(Rock, 0.f, 0.f));

	// head: down while grazing (with a nibble), held high when alert
	float HeadTarget = 0.f;
	if (State == EOpenWorldAnimalState::Graze && StateTime > 0.4f)
	{
		HeadTarget = 1.f;
	}
	else if (State == EOpenWorldAnimalState::Alert)
	{
		HeadTarget = -0.15f;
	}
	HeadDown = FMath::FInterpTo(HeadDown, HeadTarget, DeltaSeconds, 3.f);
	const float Nibble = HeadDown > 0.8f ? FMath::Sin(GetWorld()->GetTimeSeconds() * 7.f + HerdId) * 4.f : 0.f;
	if (NeckPivot)
	{
		NeckPivot->SetRelativeRotation(FRotator(S.GrazeNeck * HeadDown - Rock * 0.5f, 0.f, 0.f));
	}
	if (HeadPivot)
	{
		HeadPivot->SetRelativeRotation(FRotator(S.GrazeHead * HeadDown + Nibble, 0.f, 0.f));
	}

	// tails flick up when alarmed (a deer's white flag)
	if (TailPivot)
	{
		const bool bAlarm = State == EOpenWorldAnimalState::Flee || State == EOpenWorldAnimalState::Alert || State == EOpenWorldAnimalState::Charge;
		const float TailPitch = bAlarm ? -110.f : FMath::Sin(GetWorld()->GetTimeSeconds() * 2.f + HerdId) * 8.f;
		TailPivot->SetRelativeRotation(FMath::RInterpTo(TailPivot->GetRelativeRotation(), FRotator(TailPitch, 0.f, 0.f), DeltaSeconds, 8.f));
	}
}
