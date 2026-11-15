// Hunting bow for the open world game.

#include "OpenWorldBow.h"
#include "OpenWorldArrow.h"
#include "OpenWorldGameMode.h"
#include "OpenWorldShapes.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

using namespace OpenWorldShapes;

namespace
{
	const FName HandSocket(TEXT("hand_l"));

	// bow shape in its own space: +X towards the target, +Z up, grip at the origin
	const FVector LimbRoot(0.f, 0.f, 6.f);
	const FVector LimbMid(4.f, 0.f, 34.f);
	const FVector LimbTip(-5.f, 0.f, 64.f);
	constexpr float MaxDrawLength = 50.f;

	FVector MirrorZ(const FVector& V) { return FVector(V.X, V.Y, -V.Z); }
}

AOpenWorldBow::AOpenWorldBow()
{
	PrimaryActorTick.bCanEverTick = true;
	// after the character has moved and animated, so the bow follows the hand without lag
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;
}

void AOpenWorldBow::BeginPlay()
{
	Super::BeginPlay();

	const FColor Wood(132, 86, 46), Grip(58, 40, 30), String(236, 230, 214);
	const FColor Shaft(186, 150, 104), Head(90, 92, 96), Feather(220, 60, 50);
	UOpenWorldShapeLibrary* Shapes = GetWorld()->GetSubsystem<UOpenWorldShapeLibrary>();

	Shapes->AddPart(this, Root, EShape::Cylinder, Grip, FVector::ZeroVector, FRotator::ZeroRotator, FVector(5.f, 5.f, 16.f));
	for (int32 i = 0; i < 4; ++i)
	{
		Limbs.Add(Shapes->AddPart(this, Root, EShape::Cylinder, Wood, FVector::ZeroVector, FRotator::ZeroRotator, FVector(1.f)));
	}
	for (int32 i = 0; i < 2; ++i)
	{
		Strings.Add(Shapes->AddPart(this, Root, EShape::Cylinder, String, FVector::ZeroVector, FRotator::ZeroRotator, FVector(1.f), false));
	}

	// the arrow on the string, shown while aiming
	NockedArrow = NewObject<USceneComponent>(this, TEXT("NockedArrow"));
	NockedArrow->SetupAttachment(Root);
	NockedArrow->RegisterComponent();
	AddInstanceComponent(NockedArrow);
	const float L = AOpenWorldArrow::Length;
	Shapes->AddPart(this, NockedArrow, EShape::Cylinder, Shaft, FVector(L * 0.5f, 0.f, 0.f), FRotator(-90.f, 0.f, 0.f), FVector(1.4f, 1.4f, L), false);
	Shapes->AddPart(this, NockedArrow, EShape::Cone, Head, FVector(L + 3.f, 0.f, 0.f), FRotator(-90.f, 0.f, 0.f), FVector(3.f, 3.f, 7.f), false);
	Shapes->AddPart(this, NockedArrow, EShape::Cube, Feather, FVector(7.f, 0.f, 0.f), FRotator::ZeroRotator, FVector(10.f, 0.4f, 5.f), false);

	UpdateVisual();
}

void AOpenWorldBow::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetAiming(false);
	AimBlend = 0.f;
	UpdateCamera(0.f);
	Super::EndPlay(EndPlayReason);
}

void AOpenWorldBow::Equip(ACharacter* InHolder)
{
	Holder = InHolder;
	SetOwner(InHolder);
	bCameraSaved = false;
	if (!InHolder)
	{
		return;
	}
	AddTickPrerequisiteActor(InHolder);

	if (const USpringArmComponent* Boom = InHolder->FindComponentByClass<USpringArmComponent>())
	{
		DefaultArmLength = Boom->TargetArmLength;
		DefaultSocketOffset = Boom->SocketOffset;
		bCameraSaved = true;
	}
	if (const UCameraComponent* Camera = InHolder->FindComponentByClass<UCameraComponent>())
	{
		DefaultFOV = Camera->FieldOfView;
	}
}

void AOpenWorldBow::SetForcedDraw(float InDraw)
{
	ForcedDraw = InDraw;
	if (InDraw < 0.f)
	{
		bDrawing = false;
		Draw = 0.f;
	}
}

int32 AOpenWorldBow::AddArrows(int32 Count)
{
	const int32 Added = FMath::Clamp(MaxArrows - Arrows, 0, Count);
	Arrows += Added;
	return Added;
}

void AOpenWorldBow::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!Holder.IsValid())
	{
		return;
	}
	UpdateInput(DeltaSeconds);
	UpdateCamera(DeltaSeconds);
	UpdateVisual();
}

void AOpenWorldBow::UpdateInput(float DeltaSeconds)
{
	ACharacter* Character = Holder.Get();
	APlayerController* PC = Cast<APlayerController>(Character->GetController());
	if (!PC)
	{
		return;
	}

	Cooldown -= DeltaSeconds;
	if (ForcedDraw >= 0.f)
	{
		Draw = FMath::Clamp(ForcedDraw, 0.f, 1.f);
		bDrawing = Draw > 0.f;
		SetAiming(true);
		return;
	}

	const bool bAimHeld = PC->IsInputKeyDown(EKeys::RightMouseButton);
	const bool bDrawHeld = PC->IsInputKeyDown(EKeys::LeftMouseButton);

	if (!bDrawing && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		if (Arrows <= 0)
		{
			if (AOpenWorldGameMode* GameMode = GetWorld()->GetAuthGameMode<AOpenWorldGameMode>())
			{
				GameMode->ShowMessage(TEXT("Out of arrows. Pick up the ones you shot."), 2.f);
			}
		}
		else if (Cooldown <= 0.f)
		{
			bDrawing = true;
			Draw = 0.f;
			FullDrawHold = 0.f;
		}
	}

	if (bDrawing)
	{
		if (bDrawHeld)
		{
			Draw = FMath::Min(Draw + DeltaSeconds / FMath::Max(FullDrawTime, 0.1f), 1.f);
			if (Draw >= 1.f)
			{
				FullDrawHold += DeltaSeconds;
			}
		}
		else
		{
			// a half-hearted twitch just lets the string down
			if (Draw >= 0.2f)
			{
				Fire(Draw);
			}
			bDrawing = false;
			Draw = 0.f;
		}
	}

	SetAiming(bAimHeld || bDrawing);

	// tired arms: holding a full draw makes the aim wander more and more
	FRotator Sway = FRotator::ZeroRotator;
	if (bDrawing)
	{
		SwayTime += DeltaSeconds;
		const float Amp = 0.06f + Draw * 0.08f + FMath::Clamp(FullDrawHold - 2.5f, 0.f, 4.f) * 0.35f;
		Sway = FRotator(FMath::Sin(SwayTime * 1.3f) * Amp, FMath::Sin(SwayTime * 0.85f + 1.f) * Amp, 0.f);
	}
	if (!Sway.Equals(AppliedSway))
	{
		PC->SetControlRotation(PC->GetControlRotation() + (Sway - AppliedSway));
		AppliedSway = Sway;
	}
}

void AOpenWorldBow::SetAiming(bool bNewAiming)
{
	if (bAiming == bNewAiming)
	{
		return;
	}
	bAiming = bNewAiming;
	if (ACharacter* Character = Holder.Get())
	{
		// face the crosshair while aiming, face the movement direction otherwise
		Character->bUseControllerRotationYaw = bAiming;
		Character->GetCharacterMovement()->bOrientRotationToMovement = !bAiming;
	}
}

void AOpenWorldBow::UpdateCamera(float DeltaSeconds)
{
	ACharacter* Character = Holder.Get();
	if (!Character)
	{
		return;
	}

	AimBlend = DeltaSeconds > 0.f ? FMath::FInterpTo(AimBlend, bAiming ? 1.f : 0.f, DeltaSeconds, 10.f) : AimBlend;

	// over-the-shoulder framing so the character doesn't hide the crosshair
	if (USpringArmComponent* Boom = Character->FindComponentByClass<USpringArmComponent>(); Boom && bCameraSaved)
	{
		Boom->TargetArmLength = FMath::Lerp(DefaultArmLength, 170.f, AimBlend);
		Boom->SocketOffset = FMath::Lerp(DefaultSocketOffset, FVector(0.f, 55.f, 55.f), AimBlend);
	}
	if (UCameraComponent* Camera = Character->FindComponentByClass<UCameraComponent>())
	{
		Camera->SetFieldOfView(FMath::Lerp(DefaultFOV, 72.f - 14.f * Draw, AimBlend));
	}
}

FVector AOpenWorldBow::GetNockLocal() const
{
	return FVector(LimbTip.X - 1.f - Draw * MaxDrawLength, 0.f, 0.f);
}

void AOpenWorldBow::UpdateVisual()
{
	const ACharacter* Character = Holder.Get();
	if (!Character || Limbs.Num() < 4 || Strings.Num() < 2)
	{
		return;
	}

	const AController* Controller = Character->GetController();
	const FRotator View = Controller ? Controller->GetControlRotation() : Character->GetActorRotation();

	FVector Location;
	FRotator Rotation;
	const USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (bAiming)
	{
		// held out in front of the chest, pointing where the camera looks, canted a little
		const FRotator Yaw(0.f, View.Yaw, 0.f);
		Location = Character->GetActorLocation() + Yaw.RotateVector(FVector(38.f, -12.f, 46.f));
		Rotation = FRotator(FRotator::NormalizeAxis(View.Pitch), View.Yaw, -12.f);
	}
	else if (Mesh && Mesh->DoesSocketExist(HandSocket))
	{
		// carried upright in the left hand
		Location = Mesh->GetSocketLocation(HandSocket);
		Rotation = FRotator(-8.f, Character->GetActorRotation().Yaw, 0.f);
	}
	else
	{
		Location = Character->GetActorLocation() + Character->GetActorRotation().RotateVector(FVector(10.f, -32.f, -8.f));
		Rotation = FRotator(-8.f, Character->GetActorRotation().Yaw, 0.f);
	}
	SetActorLocationAndRotation(Location, Rotation);

	// limbs bend back and the string comes back to the nock as the bow is drawn
	const FVector Bend(-Draw * 8.f, 0.f, -Draw * 2.f);
	const FVector Mid = LimbMid + Bend * 0.4f;
	const FVector Tip = LimbTip + Bend;
	SetCylinderBetween(Limbs[0], LimbRoot, Mid, 2.2f);
	SetCylinderBetween(Limbs[1], Mid, Tip, 1.6f);
	SetCylinderBetween(Limbs[2], MirrorZ(LimbRoot), MirrorZ(Mid), 2.2f);
	SetCylinderBetween(Limbs[3], MirrorZ(Mid), MirrorZ(Tip), 1.6f);

	const FVector Nock = GetNockLocal();
	SetCylinderBetween(Strings[0], Tip, Nock, 0.35f);
	SetCylinderBetween(Strings[1], Nock, MirrorZ(Tip), 0.35f);

	if (NockedArrow)
	{
		NockedArrow->SetVisibility(bAiming && Arrows > 0 && Cooldown <= 0.f, true);
		NockedArrow->SetRelativeLocation(Nock);
	}
}

bool AOpenWorldBow::Fire(float DrawAmount)
{
	ACharacter* Character = Holder.Get();
	APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!PC || Arrows <= 0)
	{
		return false;
	}

	// what is under the crosshair? start the trace past the character so it can't hit itself
	FVector CamLoc;
	FRotator CamRot;
	PC->GetPlayerViewPoint(CamLoc, CamRot);
	const FVector ViewDir = CamRot.Vector();
	const float PastHolder = FMath::Max(FVector::DotProduct(Character->GetActorLocation() - CamLoc, ViewDir) + 60.f, 0.f);
	const FVector TraceStart = CamLoc + ViewDir * PastHolder;
	const FVector TraceEnd = CamLoc + ViewDir * 20000.f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BowAim), false, this);
	Params.AddIgnoredActor(Character);
	FHitResult Hit;
	const FVector Target = GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params) ? Hit.ImpactPoint : TraceEnd;
	return FireAt(Target, DrawAmount);
}

bool AOpenWorldBow::FireAt(const FVector& Target, float DrawAmount)
{
	ACharacter* Character = Holder.Get();
	if (!Character || Arrows <= 0)
	{
		return false;
	}

	// leave from the arrow rest, already pointing at the target
	Draw = DrawAmount;
	UpdateVisual();
	const FVector Spawn = GetActorTransform().TransformPosition(GetNockLocal() + FVector(AOpenWorldArrow::Length * 0.5f, 0.f, 0.f));
	const FVector Dir = (Target - Spawn).GetSafeNormal();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Character;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AOpenWorldArrow* Arrow = GetWorld()->SpawnActor<AOpenWorldArrow>(AOpenWorldArrow::StaticClass(), Spawn, Dir.Rotation(), SpawnParams);
	if (!Arrow)
	{
		return false;
	}
	Arrow->Launch(Dir * ArrowSpeed(DrawAmount), Character);

	--Arrows;
	Cooldown = 0.45f;
	Draw = 0.f;

	// the twang of the string can be heard close by
	if (AOpenWorldGameMode* GameMode = GetWorld()->GetAuthGameMode<AOpenWorldGameMode>())
	{
		GameMode->AlertAnimals(Character->GetActorLocation(), 500.f);
	}
	return true;
}
