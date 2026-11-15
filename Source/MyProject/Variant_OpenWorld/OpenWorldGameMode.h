// Game mode for the random open world game.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "OpenWorldGenerator.h"
#include "OpenWorldGameMode.generated.h"

class AOpenWorldAnimal;
class AOpenWorldBow;
class AOpenWorldCrystal;
class ADirectionalLight;
class APostProcessVolume;
class ACharacter;

/**
 *  Generates a random island at startup, spawns the player on it and
 *  tracks the "collect every crystal" objective and the hunt.
 *
 *  Controls (in addition to the third person template ones):
 *   RMB - aim the bow, LMB - hold to draw, release to shoot
 *   Shift - sprint (uses stamina), C / Ctrl - sneak
 *   E - field dress a downed animal (meat + arrows back)
 *   N - generate a brand new world
 *   R - return to the starting point
 *   T - switch between blocky and smooth terrain (same world seed)
 *
 *  Pass ?Seed=1234 in the map URL to replay a specific world,
 *  and ?Style=Blocky or ?Style=Smooth to pick the terrain style.
 */
UCLASS()
class AOpenWorldGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:

	AOpenWorldGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Called by a crystal when the player picks it up */
	void OnCrystalCollected(AOpenWorldCrystal* Crystal);

	/** Called by an animal when it dies. bInstant = killed by the arrow, false = bled out later. */
	void OnAnimalKilled(AOpenWorldAnimal* Animal, bool bInstant);

	/** Hurts the player. A non-zero From knocks them back, away from it. */
	void DamagePlayer(float Amount, const FString& Cause, const FVector& From = FVector::ZeroVector);

	/** Field dresses a carcass: meat for the hunt log, arrows stuck in it go back in the quiver */
	void HarvestCarcass(AOpenWorldAnimal* Carcass);

	/** A loud noise at Location (arrow impact, bow string): animals that hear it react */
	void AlertAnimals(const FVector& Location, float Radius);

	/** Reloads the level with a new random seed */
	UFUNCTION(BlueprintCallable, Category="Open World")
	void NewWorld();

	/** Reloads the same world with the other terrain style */
	UFUNCTION(BlueprintCallable, Category="Open World")
	void SwitchTerrainStyle();

	/** Test helper: walks the player up to the nearest living animal (deer first) and shoots it with a full draw */
	UFUNCTION(Exec, BlueprintCallable, Category="Open World")
	void HuntTest(int32 KindIndex = 0, float Distance = 1500.f);

	/** Logs every animal's kind, state and health */
	UFUNCTION(Exec, BlueprintCallable, Category="Open World")
	void HuntStatus();

	/** Test helper: holds the bow aimed at this draw (0..1) as if the mouse buttons were held; negative releases */
	UFUNCTION(Exec, BlueprintCallable, Category="Open World")
	void HuntAim(float Draw = 1.f);

	/** Sets the clock, in hours (0-24) */
	UFUNCTION(Exec, BlueprintCallable, Category="Open World")
	void SetTimeOfDay(float Hours);

	void ShowMessage(const FString& Text, float Duration = 3.f);

	AOpenWorldGenerator* GetGenerator() const { return Generator; }
	AOpenWorldBow* GetBow() const { return Bow; }
	int32 GetCollected() const { return Collected; }
	int32 GetTotal() const { return Total; }
	bool HasWon() const { return bWon; }
	float GetElapsedTime() const;

	float GetHealth() const { return Health; }
	float GetStamina() const { return Stamina; }
	bool IsSneaking() const { return bSneaking; }
	bool IsExhausted() const { return bExhausted; }
	int32 GetKills(EOpenWorldAnimalKind Kind) const { return Kills[static_cast<int32>(Kind)]; }
	float GetMeatKg() const { return MeatKg; }
	AOpenWorldAnimal* GetHarvestTarget() const { return HarvestTarget.Get(); }

	/** How loud the player is right now: 0.1 standing still .. 1 sprinting */
	float GetPlayerNoise() const { return PlayerNoise; }
	/** Hours, 0-24 */
	float GetTimeOfDay() const { return TimeOfDay; }
	int32 GetDay() const { return Day; }
	/** 0 at night, 1 in full daylight */
	float GetDaylight() const;

	/** Returns the current status message, or an empty string once it has expired */
	FString GetMessage() const;

protected:

	/** Fixed world seed. 0 = random every time. */
	UPROPERTY(EditAnywhere, Category="Open World")
	int32 FixedSeed = 0;

	/** Terrain style used unless the map URL has ?Style= */
	UPROPERTY(EditAnywhere, Category="Open World")
	EOpenWorldTerrainStyle DefaultTerrainStyle = EOpenWorldTerrainStyle::Smooth;

	/** Real minutes for a full day and night */
	UPROPERTY(EditAnywhere, Category="Open World", meta=(ClampMin=1))
	float DayLengthMinutes = 24.f;

	/** Clock time when the game starts (hours) */
	UPROPERTY(EditAnywhere, Category="Open World", meta=(ClampMin=0, ClampMax=24))
	float StartTimeOfDay = 8.f;

	EOpenWorldTerrainStyle TerrainStyle = EOpenWorldTerrainStyle::Smooth;

	/** Reloads the current level with the given seed and terrain style */
	void ReloadWorld(int32 InSeed, EOpenWorldTerrainStyle InStyle);

	void EnsureWorld();
	void SpawnEnvironment();
	void EquipPlayer(APawn* Pawn);
	void UpdateDayNight(float DeltaSeconds);
	void UpdateSurvival(float DeltaSeconds, ACharacter* Character, APlayerController* PC);
	void UpdateHunting(float DeltaSeconds, APawn* Pawn, APlayerController* PC);
	void HandlePlayerDeath(const FString& Cause);

	UPROPERTY(Transient)
	TObjectPtr<AOpenWorldGenerator> Generator;

	UPROPERTY(Transient)
	TObjectPtr<AActor> SpawnPoint;

	UPROPERTY(Transient)
	TObjectPtr<AOpenWorldBow> Bow;

	UPROPERTY(Transient)
	TObjectPtr<ADirectionalLight> Sun;

	UPROPERTY(Transient)
	TObjectPtr<ADirectionalLight> Moon;

	/** Unbound post-process volume that darkens and cools the picture at night */
	UPROPERTY(Transient)
	TObjectPtr<APostProcessVolume> NightGrading;

	int32 Seed = 0;
	int32 Collected = 0;
	int32 Total = 0;
	bool bWon = false;
	float StartTime = 0.f;
	float FinishTime = 0.f;

	FString Message;
	float MessageExpires = 0.f;
	/** Shown whenever no other message is, once every crystal is found */
	FString WinMessage;

	FVector LastSafeLocation = FVector::ZeroVector;
	float SafeSampleTimer = 0.f;

	// survival
	float Health = 100.f;
	float Stamina = 100.f;
	float LastDamageTime = -100.f;
	float DefaultWalkSpeed = 500.f;
	float PlayerNoise = 0.6f;
	bool bSneaking = false;
	bool bSprinting = false;
	bool bExhausted = false;
	bool bWasFalling = false;
	float FallSpeed = 0.f;

	// day and night
	float TimeOfDay = 8.f;
	int32 Day = 1;
	float SunIntensity = 9.f;
	float AppliedCloudGlow = -1.f;

	// hunting
	int32 Kills[3] = { 0, 0, 0 };
	float MeatKg = 0.f;
	float PickupTimer = 0.f;
	float ReplenishTimer = 30.f;
	TWeakObjectPtr<AOpenWorldAnimal> HarvestTarget;
};
