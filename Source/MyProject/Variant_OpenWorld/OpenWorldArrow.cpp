// Arrow projectile for the open world game.

#include "OpenWorldArrow.h"
#include "OpenWorldAnimal.h"
#include "OpenWorldGameMode.h"
#include "OpenWorldGenerator.h"
#include "OpenWorldShapes.h"
#include "Engine/World.h"

using namespace OpenWorldShapes;

AOpenWorldArrow::AOpenWorldArrow()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;
}

void AOpenWorldArrow::BeginPlay()
{
	Super::BeginPlay();

	// built along +X: tip at +Length/2, fletching at the back
	const FColor Shaft(186, 150, 104), Head(90, 92, 96), Feather(220, 60, 50), FeatherLight(240, 236, 226);
	UOpenWorldShapeLibrary* Shapes = GetWorld()->GetSubsystem<UOpenWorldShapeLibrary>();
	Shapes->AddPart(this, Root, EShape::Cylinder, Shaft, FVector::ZeroVector, FRotator(-90.f, 0.f, 0.f), FVector(1.4f, 1.4f, Length), false);
	Shapes->AddPart(this, Root, EShape::Cone, Head, FVector(Length * 0.5f + 3.f, 0.f, 0.f), FRotator(-90.f, 0.f, 0.f), FVector(3.f, 3.f, 7.f), false);
	for (int32 i = 0; i < 3; ++i)
	{
		Shapes->AddPart(this, Root, EShape::Cube, i == 0 ? FeatherLight : Feather, FVector(-Length * 0.5f + 7.f, 0.f, 0.f), FRotator(0.f, 0.f, i * 120.f),
			FVector(10.f, 0.4f, 5.f), false);
	}
}

void AOpenWorldArrow::DropFromCarcass()
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	bInAnimal = false;
	Age = 0.f;
	if (const AOpenWorldGameMode* GameMode = GetWorld()->GetAuthGameMode<AOpenWorldGameMode>(); GameMode && GameMode->GetGenerator())
	{
		// lies on the ground where the carcass was
		const FVector Loc = GetActorLocation();
		SetActorLocationAndRotation(FVector(Loc.X, Loc.Y, GameMode->GetGenerator()->GetGroundZ(Loc.X, Loc.Y) + 2.f), FRotator(0.f, GetActorRotation().Yaw, 0.f));
	}
}

void AOpenWorldArrow::Launch(const FVector& InVelocity, AActor* InShooter)
{
	Velocity = InVelocity;
	Shooter = InShooter;
	LaunchOrigin = GetActorLocation();
	bFlying = true;
	SetActorRotation(Velocity.Rotation());
}

void AOpenWorldArrow::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;

	const AOpenWorldGameMode* GameMode = GetWorld()->GetAuthGameMode<AOpenWorldGameMode>();
	const AOpenWorldGenerator* Generator = GameMode ? GameMode->GetGenerator() : nullptr;

	if (!bFlying)
	{
		// the carcass it was stuck in is gone (scavenged or swept away): the arrow goes with it
		if (bInAnimal && !GetAttachParentActor())
		{
			Destroy();
			return;
		}
		// stuck arrows stay around for a while, then get lost
		if (Age > 240.f && !bInAnimal)
		{
			Destroy();
		}
		return;
	}

	// sub-step so fast arrows don't skip through thin targets on a long frame
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(DeltaSeconds / 0.02f), 1, 4);
	const float Dt = DeltaSeconds / Steps;
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		const FVector Start = GetActorLocation() + Velocity.GetSafeNormal() * Length * 0.5f;

		// gravity and quadratic air drag
		Velocity.Z -= 980.f * Dt;
		Velocity -= Velocity * Velocity.Size() * 4.0e-6f * Dt;
		const FVector Move = Velocity * Dt;
		const FVector End = Start + Move;

		FCollisionQueryParams Params(SCENE_QUERY_STAT(ArrowTrace), false, this);
		if (Shooter.IsValid())
		{
			Params.AddIgnoredActor(Shooter.Get());
		}
		FHitResult Hit;
		if (GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(2.f), Params))
		{
			const FVector Dir = Velocity.GetSafeNormal();
			const float Speed = Velocity.Size();
			bFlying = false;
			bStuck = true;
			Age = 0.f;

			// bury the tip a little, deeper the faster it was going
			const float Penetration = FMath::Lerp(4.f, 22.f, FMath::Clamp(Speed / 6000.f, 0.f, 1.f));
			SetActorLocationAndRotation(Hit.ImpactPoint - Dir * (Length * 0.5f - Penetration), Dir.Rotation());

			if (AOpenWorldAnimal* Animal = Cast<AOpenWorldAnimal>(Hit.GetActor()))
			{
				bInAnimal = true;
				AttachToComponent(Hit.GetComponent(), FAttachmentTransformRules::KeepWorldTransform);
				if (!Animal->IsDead())
				{
					Animal->ReceiveArrowHit(Hit, Speed, LaunchOrigin);
				}
			}
			else if (AOpenWorldGameMode* MutableMode = GetWorld()->GetAuthGameMode<AOpenWorldGameMode>())
			{
				// the thud of a miss spooks nearby game
				MutableMode->AlertAnimals(Hit.ImpactPoint, 700.f);
			}
			return;
		}

		SetActorLocationAndRotation(GetActorLocation() + Move, Velocity.Rotation());

		// into the sea: lost
		if (Generator && GetActorLocation().Z < Generator->GetWaterSurfaceZ() - 30.f)
		{
			Destroy();
			return;
		}
	}

	if (Age > 10.f)
	{
		Destroy();
	}
}
