// Game mode for the random open world game.

#include "OpenWorldGameMode.h"
#include "OpenWorldGenerator.h"
#include "OpenWorldCrystal.h"
#include "OpenWorldHUD.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "InputCoreTypes.h"
#include "HAL/IConsoleManager.h"

// developer overrides, handy for comparing worlds in the editor (URL options still win)
static TAutoConsoleVariable<int32> CVarOpenWorldSeed(
	TEXT("OpenWorld.Seed"), 0,
	TEXT("Force the open world seed (0 = random)."));
static TAutoConsoleVariable<int32> CVarOpenWorldTerrainStyle(
	TEXT("OpenWorld.TerrainStyle"), -1,
	TEXT("Force the open world terrain style: -1 = game mode default, 0 = Blocky, 1 = Smooth."));

AOpenWorldGameMode::AOpenWorldGameMode()
{
	PrimaryActorTick.bCanEverTick = true;

	// reuse the third person template's character and controller (they carry the input setup)
	static ConstructorHelpers::FClassFinder<APawn> PawnClassFinder(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PawnClassFinder.Class)
	{
		DefaultPawnClass = PawnClassFinder.Class;
	}

	static ConstructorHelpers::FClassFinder<APlayerController> ControllerClassFinder(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonPlayerController"));
	if (ControllerClassFinder.Class)
	{
		PlayerControllerClass = ControllerClassFinder.Class;
	}

	HUDClass = AOpenWorldHUD::StaticClass();
}

void AOpenWorldGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	Seed = CVarOpenWorldSeed.GetValueOnGameThread() != 0 ? CVarOpenWorldSeed.GetValueOnGameThread() : FixedSeed;
	const FString SeedOption = UGameplayStatics::ParseOption(Options, TEXT("Seed"));
	if (!SeedOption.IsEmpty())
	{
		Seed = FCString::Atoi(*SeedOption);
	}
	TerrainStyle = DefaultTerrainStyle;
	const int32 StyleOverride = CVarOpenWorldTerrainStyle.GetValueOnGameThread();
	if (StyleOverride == 0 || StyleOverride == 1)
	{
		TerrainStyle = StyleOverride == 0 ? EOpenWorldTerrainStyle::Blocky : EOpenWorldTerrainStyle::Smooth;
	}
	const FString StyleOption = UGameplayStatics::ParseOption(Options, TEXT("Style"));
	if (StyleOption.Equals(TEXT("Blocky"), ESearchCase::IgnoreCase))
	{
		TerrainStyle = EOpenWorldTerrainStyle::Blocky;
	}
	else if (StyleOption.Equals(TEXT("Smooth"), ESearchCase::IgnoreCase))
	{
		TerrainStyle = EOpenWorldTerrainStyle::Smooth;
	}

	if (Seed == 0)
	{
		Seed = int32((FDateTime::Now().GetTicks() ^ FPlatformTime::Cycles64()) % 999983) + 1;
	}

	EnsureWorld();
}

void AOpenWorldGameMode::EnsureWorld()
{
	if (Generator)
	{
		return;
	}

	UWorld* World = GetWorld();

	// use a generator placed in the level if there is one, otherwise make our own
	for (TActorIterator<AOpenWorldGenerator> It(World); It; ++It)
	{
		Generator = *It;
		break;
	}
	if (!Generator)
	{
		Generator = World->SpawnActor<AOpenWorldGenerator>(AOpenWorldGenerator::StaticClass(), FTransform::Identity);
	}

	Generator->TerrainStyle = TerrainStyle;
	Generator->Generate(Seed);
	Total = Generator->GetCrystals().Num();
	LastSafeLocation = Generator->GetSpawnLocation();

	SpawnEnvironment();
}

void AOpenWorldGameMode::SpawnEnvironment()
{
	UWorld* World = GetWorld();

	if (!TActorIterator<ADirectionalLight>(World))
	{
		const FTransform SunXf(FRotator(-40.f, 35.f, 0.f), FVector(0.f, 0.f, 3000.f));
		ADirectionalLight* Sun = World->SpawnActorDeferred<ADirectionalLight>(ADirectionalLight::StaticClass(), SunXf);
		Sun->GetRootComponent()->SetMobility(EComponentMobility::Movable);
		Sun->FinishSpawning(SunXf);
		if (UDirectionalLightComponent* SunLight = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			SunLight->SetAtmosphereSunLight(true);
			SunLight->SetIntensity(9.f);
		}
	}

	if (!TActorIterator<ASkyAtmosphere>(World))
	{
		World->SpawnActor<ASkyAtmosphere>(ASkyAtmosphere::StaticClass(), FTransform::Identity);
	}

	if (!TActorIterator<ASkyLight>(World))
	{
		const FTransform SkyXf(FVector(0.f, 0.f, 3000.f));
		ASkyLight* Sky = World->SpawnActorDeferred<ASkyLight>(ASkyLight::StaticClass(), SkyXf);
		USkyLightComponent* SkyComp = Sky->GetLightComponent();
		SkyComp->SetMobility(EComponentMobility::Movable);
		SkyComp->bRealTimeCapture = true;
		Sky->FinishSpawning(SkyXf);
	}

	if (!TActorIterator<AExponentialHeightFog>(World))
	{
		AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(AExponentialHeightFog::StaticClass(), FTransform::Identity);
		Fog->GetComponent()->SetFogDensity(0.006f);
		Fog->GetComponent()->SetFogHeightFalloff(0.05f);
	}
}

AActor* AOpenWorldGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	EnsureWorld();

	if (!SpawnPoint)
	{
		const FVector Loc = Generator->GetSpawnLocation();
		// face the middle of the island
		const FRotator Rot(0.f, (FVector::ZeroVector - Loc).GetSafeNormal2D().Rotation().Yaw, 0.f);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnPoint = GetWorld()->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), Loc, Rot, Params);
	}
	return SpawnPoint;
}

void AOpenWorldGameMode::StartPlay()
{
	EnsureWorld();
	Super::StartPlay();

	StartTime = GetWorld()->GetTimeSeconds();
	if (Total == 0)
	{
		// nothing could be placed (e.g. a tiny island): don't leave the player stuck
		bWon = true;
		FinishTime = StartTime;
		ShowMessage(TEXT("This island has no crystals. Press N for a new world."), 1.0e6f);
		return;
	}
	ShowMessage(FString::Printf(TEXT("Find all %d crystals! Follow the light beams."), Total), 6.f);
}

void AOpenWorldGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!PC || !Pawn || !Generator)
	{
		return;
	}

	if (PC->WasInputKeyJustPressed(EKeys::N))
	{
		NewWorld();
		return;
	}

	if (PC->WasInputKeyJustPressed(EKeys::T))
	{
		SwitchTerrainStyle();
		return;
	}

	ACharacter* Character = Cast<ACharacter>(Pawn);
	auto TeleportPawn = [Pawn, Character](const FVector& Location)
	{
		Pawn->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		if (Character)
		{
			Character->GetCharacterMovement()->StopMovementImmediately();
		}
	};

	if (PC->WasInputKeyJustPressed(EKeys::R))
	{
		TeleportPawn(Generator->GetSpawnLocation());
		ShowMessage(TEXT("Back to the start."), 2.f);
		return;
	}

	// deep water: put the player back on the last dry ground they stood on
	// work with the feet position so this holds for any capsule size
	const float WaterZ = Generator->GetWaterSurfaceZ();
	const FVector Loc = Pawn->GetActorLocation();
	const float HalfHeight = Pawn->GetSimpleCollisionHalfHeight();
	const float FeetZ = Loc.Z - HalfHeight;
	if (FeetZ < WaterZ - 140.f)
	{
		TeleportPawn(LastSafeLocation + FVector(0.f, 0.f, 40.f));
		ShowMessage(TEXT("Splash! The water is too deep."), 2.f);
		return;
	}

	SafeSampleTimer -= DeltaSeconds;
	if (SafeSampleTimer <= 0.f)
	{
		SafeSampleTimer = 0.5f;
		if (Character && Character->GetCharacterMovement()->IsMovingOnGround() && FeetZ > WaterZ)
		{
			LastSafeLocation = Loc;
		}
	}
}

void AOpenWorldGameMode::OnCrystalCollected(AOpenWorldCrystal* Crystal)
{
	if (bWon)
	{
		return;
	}

	++Collected;
	if (Collected >= Total)
	{
		bWon = true;
		FinishTime = GetWorld()->GetTimeSeconds();
		const int32 Secs = FMath::FloorToInt(GetElapsedTime());
		ShowMessage(FString::Printf(TEXT("You found every crystal in %d:%02d!  Press N for a new world."), Secs / 60, Secs % 60), 1.0e6f);
	}
	else
	{
		ShowMessage(FString::Printf(TEXT("Crystal found!  %d to go."), Total - Collected), 2.5f);
	}
}

void AOpenWorldGameMode::NewWorld()
{
	// pass an explicit fresh seed so this is a new world even when FixedSeed is set
	const int32 NewSeed = int32((FDateTime::Now().GetTicks() ^ FPlatformTime::Cycles64()) % 999983) + 1;
	ReloadWorld(NewSeed, TerrainStyle);
}

void AOpenWorldGameMode::SwitchTerrainStyle()
{
	ReloadWorld(Seed, TerrainStyle == EOpenWorldTerrainStyle::Smooth ? EOpenWorldTerrainStyle::Blocky : EOpenWorldTerrainStyle::Smooth);
}

void AOpenWorldGameMode::ReloadWorld(int32 InSeed, EOpenWorldTerrainStyle InStyle)
{
	const FString LevelPath = UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName());
	const TCHAR* StyleName = InStyle == EOpenWorldTerrainStyle::Smooth ? TEXT("Smooth") : TEXT("Blocky");
	UGameplayStatics::OpenLevel(this, FName(*LevelPath), true, FString::Printf(TEXT("Seed=%d?Style=%s"), InSeed, StyleName));
}

float AOpenWorldGameMode::GetElapsedTime() const
{
	const float End = bWon ? FinishTime : GetWorld()->GetTimeSeconds();
	return FMath::Max(0.f, End - StartTime);
}

void AOpenWorldGameMode::ShowMessage(const FString& Text, float Duration)
{
	Message = Text;
	MessageExpires = GetWorld()->GetTimeSeconds() + Duration;
}

FString AOpenWorldGameMode::GetMessage() const
{
	return GetWorld()->GetTimeSeconds() < MessageExpires ? Message : FString();
}
