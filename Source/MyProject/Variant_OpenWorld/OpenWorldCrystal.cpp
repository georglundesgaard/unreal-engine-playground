// Collectible crystal for the open world game.

#include "OpenWorldCrystal.h"
#include "OpenWorldGameMode.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

AOpenWorldCrystal::AOpenWorldCrystal()
{
	PrimaryActorTick.bCanEverTick = true;

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(160.f);
	Trigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Trigger->SetGenerateOverlapEvents(true);
	RootComponent = Trigger;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BaseMaterial = MaterialFinder.Object;

	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	Pivot->SetupAttachment(Trigger);

	// a cube balanced on its corner reads as a gem
	Gem = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Gem"));
	Gem->SetupAttachment(Pivot);
	Gem->SetStaticMesh(CubeFinder.Object);
	Gem->SetRelativeRotation(FRotator(45.f, 0.f, 35.264f));
	Gem->SetRelativeScale3D(FVector(0.55f));
	Gem->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// tall thin beam visible from across the island
	Beam = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Beam"));
	Beam->SetupAttachment(Trigger);
	Beam->SetStaticMesh(CubeFinder.Object);
	Beam->SetRelativeLocation(FVector(0.f, 0.f, 2500.f));
	Beam->SetRelativeScale3D(FVector(0.12f, 0.12f, 50.f));
	Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beam->SetCastShadow(false);

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Trigger);
	Light->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
	Light->SetIntensity(8000.f);
	Light->SetAttenuationRadius(900.f);
	Light->SetCastShadows(false);
}

void AOpenWorldCrystal::BeginPlay()
{
	Super::BeginPlay();

	Light->SetLightColor(CrystalColor);

	if (BaseMaterial)
	{
		UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		Mat->SetVectorParameterValue(TEXT("Color"), CrystalColor);
		Mat->SetScalarParameterValue(TEXT("Roughness"), 0.1f);
		Gem->SetMaterial(0, Mat);
		Beam->SetMaterial(0, Mat);
	}

	// stagger the animation so crystals don't bob in sync
	Age = FMath::FRandRange(0.f, 10.f);

	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AOpenWorldCrystal::OnTriggerOverlap);
}

void AOpenWorldCrystal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Age += DeltaSeconds;
	Pivot->SetRelativeLocation(FVector(0.f, 0.f, FMath::Sin(Age * 2.f) * 20.f));
	Pivot->SetRelativeRotation(FRotator(0.f, Age * 90.f, 0.f));
}

void AOpenWorldCrystal::OnTriggerOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (bCollected || !Pawn || !Pawn->IsPlayerControlled())
	{
		return;
	}

	bCollected = true;
	if (AOpenWorldGameMode* GameMode = GetWorld()->GetAuthGameMode<AOpenWorldGameMode>())
	{
		GameMode->OnCrystalCollected(this);
	}
	Destroy();
}
