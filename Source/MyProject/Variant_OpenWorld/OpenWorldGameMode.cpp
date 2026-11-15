// Game mode for the random open world game.

#include "OpenWorldGameMode.h"
#include "OpenWorldGenerator.h"
#include "OpenWorldCrystal.h"
#include "OpenWorldAnimal.h"
#include "OpenWorldArrow.h"
#include "OpenWorldBow.h"
#include "OpenWorldHUD.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/PlayerStartPIE.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
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

namespace
{
	/** Sun height above the horizon (degrees) for a clock time: up at 6, highest (60) at noon, down at 18 */
	float SunElevation(float Hours)
	{
		return 60.f * FMath::Sin((Hours - 6.f) / 24.f * UE_TWO_PI);
	}
}

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

	TimeOfDay = FMath::Clamp(StartTimeOfDay, 0.f, 24.f);
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
	// the generator may fall back to Blocky (e.g. missing terrain material): keep the toggle in sync
	TerrainStyle = Generator->GetTerrainStyle();
	Total = Generator->GetCrystals().Num();
	LastSafeLocation = Generator->GetSpawnLocation();

	SpawnEnvironment();
}

void AOpenWorldGameMode::SpawnEnvironment()
{
	UWorld* World = GetWorld();

	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		Sun = *It;
		break;
	}
	if (!Sun)
	{
		const FTransform SunXf(FRotator(-40.f, 35.f, 0.f), FVector(0.f, 0.f, 3000.f));
		Sun = World->SpawnActorDeferred<ADirectionalLight>(ADirectionalLight::StaticClass(), SunXf);
		Sun->GetRootComponent()->SetMobility(EComponentMobility::Movable);
		Sun->FinishSpawning(SunXf);
		if (UDirectionalLightComponent* SunLight = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			SunLight->SetAtmosphereSunLight(true);
			SunLight->SetIntensity(9.f);
		}
	}
	// the sun moves with the clock
	Sun->GetRootComponent()->SetMobility(EComponentMobility::Movable);
	if (UDirectionalLightComponent* SunLight = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
	{
		SunIntensity = SunLight->Intensity > 0.f ? SunLight->Intensity : 9.f;
		SunLight->SetAtmosphereSunLight(true);
		SunLight->SetAtmosphereSunLightIndex(0);
	}

	// a faint, cool moon opposite the sun keeps nights playable. Not an atmosphere light: the night sky stays dark.
	{
		const FTransform MoonXf(FRotator(-30.f, 0.f, 0.f), FVector(0.f, 0.f, 3000.f));
		Moon = World->SpawnActorDeferred<ADirectionalLight>(ADirectionalLight::StaticClass(), MoonXf);
		Moon->GetRootComponent()->SetMobility(EComponentMobility::Movable);
		Moon->FinishSpawning(MoonXf);
		if (UDirectionalLightComponent* MoonLight = Cast<UDirectionalLightComponent>(Moon->GetLightComponent()))
		{
			MoonLight->SetAtmosphereSunLight(false);
			MoonLight->SetLightColor(FLinearColor(0.55f, 0.65f, 1.f));
			MoonLight->SetIntensity(0.f);
		}
	}

	if (!TActorIterator<ASkyAtmosphere>(World))
	{
		World->SpawnActor<ASkyAtmosphere>(ASkyAtmosphere::StaticClass(), FTransform::Identity);
	}

	ASkyLight* Sky = nullptr;
	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		Sky = *It;
		break;
	}
	if (!Sky)
	{
		const FTransform SkyXf(FVector(0.f, 0.f, 3000.f));
		Sky = World->SpawnActorDeferred<ASkyLight>(ASkyLight::StaticClass(), SkyXf);
		USkyLightComponent* SkyComp = Sky->GetLightComponent();
		SkyComp->SetMobility(EComponentMobility::Movable);
		SkyComp->bRealTimeCapture = true;
		Sky->FinishSpawning(SkyXf);
	}
	else if (USkyLightComponent* SkyComp = Sky->GetLightComponent(); SkyComp && !SkyComp->bRealTimeCapture)
	{
		// ambient light has to follow the sun, otherwise nights stay bright
		SkyComp->SetMobility(EComponentMobility::Movable);
		SkyComp->bRealTimeCapture = true;
		SkyComp->MarkRenderStateDirty();
	}

	// night grading: auto exposure would otherwise brighten the night back to day
	NightGrading = World->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FTransform::Identity);
	NightGrading->bUnbound = true;
	NightGrading->Priority = 10.f;

	if (!TActorIterator<AExponentialHeightFog>(World))
	{
		AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(AExponentialHeightFog::StaticClass(), FTransform::Identity);
		Fog->GetComponent()->SetFogDensity(0.006f);
		Fog->GetComponent()->SetFogHeightFalloff(0.05f);
	}

	UpdateDayNight(0.f);
}

AActor* AOpenWorldGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	EnsureWorld();

	// the editor's "Play from here" start wins, like in the default game mode
	for (TActorIterator<APlayerStartPIE> It(GetWorld()); It; ++It)
	{
		return *It;
	}

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

void AOpenWorldGameMode::RestartPlayer(AController* NewPlayer)
{
	Super::RestartPlayer(NewPlayer);
	if (NewPlayer && NewPlayer->IsPlayerController())
	{
		EquipPlayer(NewPlayer->GetPawn());
	}
}

void AOpenWorldGameMode::EquipPlayer(APawn* Pawn)
{
	ACharacter* Character = Cast<ACharacter>(Pawn);
	if (!Character)
	{
		return;
	}
	DefaultWalkSpeed = Character->GetCharacterMovement()->MaxWalkSpeed;
	if (!Bow)
	{
		Bow = GetWorld()->SpawnActor<AOpenWorldBow>(AOpenWorldBow::StaticClass(), Character->GetActorTransform());
	}
	if (Bow)
	{
		Bow->Equip(Character);
	}
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
		WinMessage = TEXT("This island has no crystals. Press N for a new world.");
		ShowMessage(WinMessage, 6.f);
		return;
	}
	ShowMessage(FString::Printf(TEXT("Find all %d crystals! Hunt with your bow: hold RMB to aim, LMB to draw."), Total), 7.f);
}

void AOpenWorldGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateDayNight(DeltaSeconds);

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
	auto TeleportPawn = [this, Pawn, Character](const FVector& Location)
	{
		Pawn->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		if (Character)
		{
			Character->GetCharacterMovement()->StopMovementImmediately();
		}
		// a teleport is not a fall
		bWasFalling = false;
		FallSpeed = 0.f;
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

	if (Character)
	{
		UpdateSurvival(DeltaSeconds, Character, PC);
	}
	UpdateHunting(DeltaSeconds, Pawn, PC);
}

void AOpenWorldGameMode::UpdateDayNight(float DeltaSeconds)
{
	TimeOfDay += DeltaSeconds * 24.f / (FMath::Max(DayLengthMinutes, 1.f) * 60.f);
	while (TimeOfDay >= 24.f)
	{
		TimeOfDay -= 24.f;
		++Day;
	}

	// the sun rises in the east (+Y) and sets in the west; +X is north
	const float Elevation = SunElevation(TimeOfDay);
	const float Azimuth = (TimeOfDay - 12.f) * 15.f;
	if (Sun)
	{
		Sun->SetActorRotation(FRotator(-Elevation, Azimuth, 0.f));
		if (UDirectionalLightComponent* SunLight = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			// fade out just below the horizon so the sun never lights the world from underneath
			SunLight->SetIntensity(SunIntensity * FMath::SmoothStep(-4.f, 6.f, Elevation));
		}
	}
	if (Moon)
	{
		const float MoonElevation = -Elevation * 0.8f;
		Moon->SetActorRotation(FRotator(-MoonElevation, Azimuth + 180.f, 0.f));
		if (UDirectionalLightComponent* MoonLight = Cast<UDirectionalLightComponent>(Moon->GetLightComponent()))
		{
			MoonLight->SetIntensity(0.8f * FMath::SmoothStep(-4.f, 8.f, MoonElevation));
		}
	}

	// clouds stop glowing after sunset (only update when it visibly changes)
	const float Daylight = GetDaylight();
	if (Generator && FMath::Abs(Daylight - AppliedCloudGlow) > 0.02f)
	{
		AppliedCloudGlow = Daylight;
		Generator->SetCloudGlow(FMath::Lerp(0.04f, 1.f, Daylight));
	}

	// darker, bluer and less saturated as the light goes
	const float Night = 1.f - Daylight;
	if (NightGrading)
	{
		{
			FPostProcessSettings& PP = NightGrading->Settings;
			PP.bOverride_AutoExposureBias = true;
			PP.AutoExposureBias = -3.f * Night;
			PP.bOverride_ColorSaturation = true;
			PP.ColorSaturation = FVector4(FMath::Lerp(1.f, 0.55f, Night), FMath::Lerp(1.f, 0.55f, Night), FMath::Lerp(1.f, 0.65f, Night), 1.f);
			PP.bOverride_ColorGain = true;
			PP.ColorGain = FVector4(FMath::Lerp(1.f, 0.8f, Night), FMath::Lerp(1.f, 0.9f, Night), FMath::Lerp(1.f, 1.15f, Night), 1.f);
		}
	}
}

float AOpenWorldGameMode::GetDaylight() const
{
	return FMath::SmoothStep(-6.f, 8.f, SunElevation(TimeOfDay));
}

void AOpenWorldGameMode::SetTimeOfDay(float Hours)
{
	TimeOfDay = FMath::Fmod(FMath::Max(Hours, 0.f), 24.f);
	UpdateDayNight(0.f);
}

void AOpenWorldGameMode::UpdateSurvival(float DeltaSeconds, ACharacter* Character, APlayerController* PC)
{
	UCharacterMovementComponent* Move = Character->GetCharacterMovement();
	const float Speed2D = Character->GetVelocity().Size2D();
	const bool bAiming = Bow && Bow->IsAiming();

	// sneak (C or Ctrl) and sprint (Shift); sprinting uses stamina and an empty bar must recover a bit first
	bSneaking = PC->IsInputKeyDown(EKeys::C) || PC->IsInputKeyDown(EKeys::LeftControl);
	const bool bWantsSprint = PC->IsInputKeyDown(EKeys::LeftShift) && Speed2D > 50.f && !bAiming && !bSneaking;
	bSprinting = bWantsSprint && !bExhausted && Move->IsMovingOnGround();
	if (bSprinting)
	{
		Stamina = FMath::Max(Stamina - 18.f * DeltaSeconds, 0.f);
		if (Stamina <= 0.f)
		{
			bExhausted = true;
			ShowMessage(TEXT("Out of breath..."), 1.5f);
		}
	}
	else
	{
		Stamina = FMath::Min(Stamina + (Speed2D > 50.f ? 9.f : 16.f) * DeltaSeconds, 100.f);
	}
	if (bExhausted && Stamina >= 30.f)
	{
		bExhausted = false;
	}

	float TargetSpeed = DefaultWalkSpeed;
	if (bAiming)
	{
		TargetSpeed = DefaultWalkSpeed * 0.5f;
	}
	else if (bSneaking)
	{
		TargetSpeed = DefaultWalkSpeed * 0.45f;
	}
	else if (bSprinting)
	{
		TargetSpeed = DefaultWalkSpeed * 1.65f;
	}
	Move->MaxWalkSpeed = TargetSpeed;

	// how far the player's footsteps carry, for the animals' hearing
	if (Move->IsFalling())
	{
		PlayerNoise = 0.8f;
	}
	else if (Speed2D < 30.f)
	{
		PlayerNoise = 0.1f;
	}
	else if (bSneaking)
	{
		PlayerNoise = 0.25f;
	}
	else
	{
		PlayerNoise = Speed2D > DefaultWalkSpeed * 1.2f ? 1.f : 0.6f;
	}

	// hard landings hurt
	if (Move->IsFalling())
	{
		FallSpeed = FMath::Max(FallSpeed, -Character->GetVelocity().Z);
		bWasFalling = true;
	}
	else if (bWasFalling)
	{
		bWasFalling = false;
		if (FallSpeed > 1100.f)
		{
			DamagePlayer((FallSpeed - 1100.f) * 0.09f, TEXT("Ouch! Hard landing."));
		}
		FallSpeed = 0.f;
	}

	// slow natural healing once out of danger
	if (Health > 0.f && GetWorld()->GetTimeSeconds() - LastDamageTime > 6.f)
	{
		Health = FMath::Min(Health + 4.f * DeltaSeconds, 100.f);
	}
}

void AOpenWorldGameMode::DamagePlayer(float Amount, const FString& Cause, const FVector& From)
{
	if (Amount <= 0.f || Health <= 0.f)
	{
		return;
	}
	Health = FMath::Max(Health - Amount, 0.f);
	LastDamageTime = GetWorld()->GetTimeSeconds();

	if (Health <= 0.f)
	{
		// no knockback: it would be applied after the respawn teleport and fling the player away from camp
		HandlePlayerDeath(Cause);
		return;
	}

	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	ACharacter* Character = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
	if (Character && !From.IsZero())
	{
		const FVector Away = (Character->GetActorLocation() - From).GetSafeNormal2D();
		Character->LaunchCharacter(Away * 700.f + FVector(0.f, 0.f, 320.f), true, true);
	}

	ShowMessage(Cause, 2.f);
}

void AOpenWorldGameMode::HandlePlayerDeath(const FString& Cause)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (ACharacter* Character = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr)
	{
		Character->SetActorLocation(Generator->GetSpawnLocation(), false, nullptr, ETeleportType::TeleportPhysics);
		Character->GetCharacterMovement()->StopMovementImmediately();
		Character->GetCharacterMovement()->PendingLaunchVelocity = FVector::ZeroVector;
	}
	bWasFalling = false;
	FallSpeed = 0.f;
	Health = 100.f;
	Stamina = 100.f;
	// some of the meat is lost on the way back to camp
	MeatKg *= 0.5f;
	ShowMessage(FString::Printf(TEXT("%s You passed out and woke up back at camp."), *Cause), 4.f);
}

void AOpenWorldGameMode::AlertAnimals(const FVector& Location, float Radius)
{
	if (!Generator)
	{
		return;
	}
	for (AOpenWorldAnimal* Animal : Generator->GetAnimals())
	{
		if (IsValid(Animal))
		{
			Animal->HearNoise(Location, Radius);
		}
	}
}

void AOpenWorldGameMode::UpdateHunting(float DeltaSeconds, APawn* Pawn, APlayerController* PC)
{
	const FVector Loc = Pawn->GetActorLocation();

	// walk over arrows lying around to pick them up
	PickupTimer -= DeltaSeconds;
	if (PickupTimer <= 0.f && Bow)
	{
		PickupTimer = 0.2f;
		int32 PickedUp = 0;
		for (TActorIterator<AOpenWorldArrow> It(GetWorld()); It; ++It)
		{
			if (It->CanBePickedUp() && FVector::Dist(It->GetActorLocation(), Loc) < 170.f && Bow->AddArrows(1) > 0)
			{
				It->Destroy();
				++PickedUp;
			}
		}
		if (PickedUp > 0)
		{
			ShowMessage(FString::Printf(TEXT("Arrow recovered (%d in the quiver)"), Bow->GetArrows()), 1.5f);
		}
	}

	// field dressing: E next to a carcass
	HarvestTarget = nullptr;
	float BestDist = 280.f;
	for (AOpenWorldAnimal* Animal : Generator->GetAnimals())
	{
		if (IsValid(Animal) && Animal->IsDead())
		{
			const float Dist = FVector::Dist2D(Animal->GetActorLocation(), Loc);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				HarvestTarget = Animal;
			}
		}
	}
	if (HarvestTarget.IsValid() && PC->WasInputKeyJustPressed(EKeys::E))
	{
		HarvestCarcass(HarvestTarget.Get());
	}

	// new animals wander in from out of sight now and then
	ReplenishTimer -= DeltaSeconds;
	if (ReplenishTimer <= 0.f)
	{
		ReplenishTimer = 45.f;
		Generator->ReplenishAnimals(Loc, 6000.f);
	}
}

void AOpenWorldGameMode::HarvestCarcass(AOpenWorldAnimal* Target)
{
	if (!IsValid(Target) || !Target->IsDead())
	{
		return;
	}

	// arrows stuck in the carcass come back
	TArray<AActor*> Attached;
	Target->GetAttachedActors(Attached, true, true);
	int32 Recovered = 0;
	for (AActor* Actor : Attached)
	{
		if (AOpenWorldArrow* Arrow = Cast<AOpenWorldArrow>(Actor))
		{
			if (Bow && Bow->AddArrows(1) > 0)
			{
				++Recovered;
				Arrow->Destroy();
			}
			else
			{
				// quiver full: leave it on the ground to pick up later
				Arrow->DropFromCarcass();
			}
		}
	}
	const FString Name = AOpenWorldAnimal::GetKindName(Target->GetKind()).ToLower();
	const float Meat = Target->Harvest();
	MeatKg += Meat;
	HarvestTarget = nullptr;
	ShowMessage(FString::Printf(TEXT("Field dressed the %s: +%.1f kg of meat%s"), *Name, Meat,
		Recovered > 0 ? *FString::Printf(TEXT(", %d arrow%s recovered"), Recovered, Recovered == 1 ? TEXT("") : TEXT("s")) : TEXT("")), 3.f);
}

void AOpenWorldGameMode::OnAnimalKilled(AOpenWorldAnimal* Animal, bool bInstant)
{
	++Kills[static_cast<int32>(Animal->GetKind())];
	const FString Name = AOpenWorldAnimal::GetKindName(Animal->GetKind()).ToLower();

	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const int32 Meters = Pawn ? FMath::RoundToInt(FVector::Dist(Pawn->GetActorLocation(), Animal->GetActorLocation()) / 100.f) : 0;
	if (bInstant)
	{
		ShowMessage(FString::Printf(TEXT("Clean kill! %s down at %d m. Press E next to it to field dress."), *AOpenWorldAnimal::GetKindName(Animal->GetKind()), Meters), 4.f);
	}
	else
	{
		ShowMessage(FString::Printf(TEXT("The wounded %s has died %d m away. Follow the blood trail."), *Name, Meters), 4.f);
	}
}

void AOpenWorldGameMode::HuntTest(int32 KindIndex, float Distance)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	ACharacter* Character = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
	if (!Character || !Generator || !Bow)
	{
		UE_LOG(LogTemp, Warning, TEXT("HuntTest: no player, world or bow"));
		return;
	}

	// below -1: walk up to the nearest animal and watch it from -Distance cm, without shooting
	const bool bObserveOnly = Distance < -1.f;
	if (bObserveOnly)
	{
		Distance = -Distance;
	}

	// -1: field dress the carcass the player is standing next to (same as pressing E)
	if (Distance < 0.f)
	{
		UE_LOG(LogTemp, Log, TEXT("HuntTest: field dressing %s"), *GetNameSafe(HarvestTarget.Get()));
		HarvestCarcass(HarvestTarget.Get());
		UE_LOG(LogTemp, Log, TEXT("HuntTest: %s"), *Message);
		return;
	}

	// distance 0: walk up to the nearest carcass instead (to try field dressing with E)
	if (Distance <= 0.f)
	{
		const AOpenWorldAnimal* Carcass = nullptr;
		float CarcassDist = TNumericLimits<float>::Max();
		for (const AOpenWorldAnimal* Animal : Generator->GetAnimals())
		{
			const float Dist = IsValid(Animal) && Animal->IsDead() ? FVector::Dist(Animal->GetActorLocation(), Character->GetActorLocation()) : CarcassDist;
			if (Dist < CarcassDist)
			{
				CarcassDist = Dist;
				Carcass = Animal;
			}
		}
		if (!Carcass)
		{
			UE_LOG(LogTemp, Warning, TEXT("HuntTest: no carcass to walk to"));
			return;
		}
		FVector Stand = Carcass->GetActorLocation() + FVector(150.f, 0.f, 0.f);
		Stand.Z = Generator->GetGroundZ(Stand.X, Stand.Y) + Character->GetSimpleCollisionHalfHeight() + 5.f;
		Character->SetActorLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
		Character->GetCharacterMovement()->StopMovementImmediately();
		bWasFalling = false;
		FallSpeed = 0.f;
		PC->SetControlRotation(FRotator(-25.f, 180.f, 0.f));
		UE_LOG(LogTemp, Log, TEXT("HuntTest: next to the %s carcass %s"), *AOpenWorldAnimal::GetKindName(Carcass->GetKind()), *Carcass->GetName());
		return;
	}

	// nearest living animal of the requested kind (any kind if there is none)
	const EOpenWorldAnimalKind WantedKind = static_cast<EOpenWorldAnimalKind>(FMath::Clamp(KindIndex, 0, 2));
	AOpenWorldAnimal* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (int32 Pass = 0; Pass < 2 && !Best; ++Pass)
	{
		for (AOpenWorldAnimal* Animal : Generator->GetAnimals())
		{
			if (IsValid(Animal) && !Animal->IsDead() && (Pass == 1 || Animal->GetKind() == WantedKind))
			{
				const float Dist = FVector::Dist(Animal->GetActorLocation(), Character->GetActorLocation());
				if (Dist < BestDist)
				{
					BestDist = Dist;
					Best = Animal;
				}
			}
		}
	}
	if (!Best)
	{
		UE_LOG(LogTemp, Warning, TEXT("HuntTest: no living animals"));
		return;
	}

	// find a dry spot at the given distance with a clear line of fire
	const FVector Target = Best->GetActorLocation() + FVector(0.f, 0.f, 10.f);
	const float HalfHeight = Character->GetSimpleCollisionHalfHeight();
	FVector Stand = FVector::ZeroVector;
	for (int32 i = 0; i < 16 && Stand.IsZero(); ++i)
	{
		const FVector Dir = FRotator(0.f, i * 22.5f, 0.f).Vector();
		FVector Candidate = Best->GetActorLocation() + Dir * Distance;
		if (!Generator->IsOpenGround(Candidate.X, Candidate.Y))
		{
			continue;
		}
		Candidate.Z = Generator->GetGroundZ(Candidate.X, Candidate.Y) + HalfHeight + 5.f;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HuntTest), false, Character);
		Params.AddIgnoredActor(Best);
		if (!GetWorld()->LineTraceTestByChannel(Candidate + FVector(0.f, 0.f, 40.f), Target, ECC_Visibility, Params))
		{
			Stand = Candidate;
		}
	}
	if (Stand.IsZero())
	{
		UE_LOG(LogTemp, Warning, TEXT("HuntTest: no clear shot at %s"), *Best->GetName());
		return;
	}

	Character->SetActorLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
	Character->GetCharacterMovement()->StopMovementImmediately();
	bWasFalling = false;
	FallSpeed = 0.f;
	const FRotator Face = (Target - Stand).Rotation();
	PC->SetControlRotation(Face);
	Character->SetActorRotation(FRotator(0.f, Face.Yaw, 0.f));
	if (bObserveOnly)
	{
		UE_LOG(LogTemp, Log, TEXT("HuntTest: watching %s %s from %.0f m"), *AOpenWorldAnimal::GetKindName(Best->GetKind()), *Best->GetName(), FVector::Dist(Stand, Target) / 100.f);
		return;
	}
	Bow->AddArrows(1);

	// aim above the target to allow for the drop over the flight time
	const float Flight = FVector::Dist(Stand, Target) / AOpenWorldBow::ArrowSpeed(1.f);
	const FVector AimPoint = Target + FVector(0.f, 0.f, 0.5f * 980.f * Flight * Flight);
	const bool bFired = Bow->FireAt(AimPoint, 1.f);
	UE_LOG(LogTemp, Log, TEXT("HuntTest: %s %s at %.0f m (health %.0f), fired=%d"),
		*AOpenWorldAnimal::GetKindName(Best->GetKind()), *Best->GetName(), FVector::Dist(Stand, Target) / 100.f, Best->GetHealth(), bFired ? 1 : 0);
}

void AOpenWorldGameMode::HuntAim(float Draw)
{
	if (Bow)
	{
		Bow->SetForcedDraw(Draw);
	}
}

void AOpenWorldGameMode::HuntStatus()
{
	if (!Generator)
	{
		return;
	}
	static const TCHAR* States[] = { TEXT("Graze"), TEXT("Wander"), TEXT("Alert"), TEXT("Flee"), TEXT("Charge"), TEXT("Dead") };
	int32 Count = 0;
	for (const AOpenWorldAnimal* Animal : Generator->GetAnimals())
	{
		if (!IsValid(Animal))
		{
			continue;
		}
		++Count;
		const FVector L = Animal->GetActorLocation();
		const float Ground = Generator->GetGroundZ(L.X, L.Y);
		UE_LOG(LogTemp, Log, TEXT("HuntStatus: %-6s herd %2d %-6s hp %5.1f speed %5.0f at (%.0f, %.0f, %.0f) feet-ground %.0f"),
			*AOpenWorldAnimal::GetKindName(Animal->GetKind()), Animal->GetHerdId(), States[static_cast<int32>(Animal->GetState())],
			Animal->GetHealth(), Animal->GetVelocity().Size2D(), L.X, L.Y, L.Z, L.Z - Animal->GetSimpleCollisionHalfHeight() - Ground);
	}
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (const APawn* Pawn = PC ? PC->GetPawn() : nullptr)
	{
		UE_LOG(LogTemp, Log, TEXT("HuntStatus: player at (%.0f, %.0f, %.0f), noise %.2f"), Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y, Pawn->GetActorLocation().Z, PlayerNoise);
	}
	UE_LOG(LogTemp, Log, TEXT("HuntStatus: %d animals, kills deer %d boar %d rabbit %d, meat %.1f kg, arrows %d, time %.2f h, health %.0f"),
		Count, Kills[0], Kills[1], Kills[2], MeatKg, Bow ? Bow->GetArrows() : 0, TimeOfDay, Health);
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
		WinMessage = FString::Printf(TEXT("You found every crystal in %d:%02d!  Press N for a new world."), Secs / 60, Secs % 60);
		ShowMessage(WinMessage, 6.f);
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
	return GetWorld()->GetTimeSeconds() < MessageExpires ? Message : WinMessage;
}
