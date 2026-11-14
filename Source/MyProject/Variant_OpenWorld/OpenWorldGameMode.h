// Game mode for the random open world game.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "OpenWorldGenerator.h"
#include "OpenWorldGameMode.generated.h"

class AOpenWorldCrystal;

/**
 *  Generates a random island at startup, spawns the player on it and
 *  tracks the "collect every crystal" objective.
 *
 *  Controls (in addition to the third person template ones):
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
	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Called by a crystal when the player picks it up */
	void OnCrystalCollected(AOpenWorldCrystal* Crystal);

	/** Reloads the level with a new random seed */
	UFUNCTION(BlueprintCallable, Category="Open World")
	void NewWorld();

	/** Reloads the same world with the other terrain style */
	UFUNCTION(BlueprintCallable, Category="Open World")
	void SwitchTerrainStyle();

	AOpenWorldGenerator* GetGenerator() const { return Generator; }
	int32 GetCollected() const { return Collected; }
	int32 GetTotal() const { return Total; }
	bool HasWon() const { return bWon; }
	float GetElapsedTime() const;

	/** Returns the current status message, or an empty string once it has expired */
	FString GetMessage() const;

protected:

	/** Fixed world seed. 0 = random every time. */
	UPROPERTY(EditAnywhere, Category="Open World")
	int32 FixedSeed = 0;

	/** Terrain style used unless the map URL has ?Style= */
	UPROPERTY(EditAnywhere, Category="Open World")
	EOpenWorldTerrainStyle DefaultTerrainStyle = EOpenWorldTerrainStyle::Smooth;

	EOpenWorldTerrainStyle TerrainStyle = EOpenWorldTerrainStyle::Smooth;

	/** Reloads the current level with the given seed and terrain style */
	void ReloadWorld(int32 InSeed, EOpenWorldTerrainStyle InStyle);

	void EnsureWorld();
	void SpawnEnvironment();
	void ShowMessage(const FString& Text, float Duration = 3.f);

	UPROPERTY(Transient)
	TObjectPtr<AOpenWorldGenerator> Generator;

	UPROPERTY(Transient)
	TObjectPtr<AActor> SpawnPoint;

	int32 Seed = 0;
	int32 Collected = 0;
	int32 Total = 0;
	bool bWon = false;
	float StartTime = 0.f;
	float FinishTime = 0.f;

	FString Message;
	float MessageExpires = 0.f;

	FVector LastSafeLocation = FVector::ZeroVector;
	float SafeSampleTimer = 0.f;
};
