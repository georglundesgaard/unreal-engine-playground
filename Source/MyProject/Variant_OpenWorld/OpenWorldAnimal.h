// Huntable wild animal for the open world game.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "OpenWorldAnimal.generated.h"

class AOpenWorldGenerator;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

UENUM()
enum class EOpenWorldAnimalKind : uint8
{
	/** Keen senses, lives in herds, very fast */
	Deer,
	/** Poor eyesight, tough, charges when hurt or cornered */
	Boar,
	/** Small and nervous, zig-zags when it flees */
	Rabbit
};

UENUM()
enum class EOpenWorldAnimalState : uint8
{
	Graze,
	Wander,
	Alert,
	Flee,
	Charge,
	Dead
};

/**
 *  A wild animal built from basic shapes, with procedural leg, head and body animation.
 *
 *  Behaviour (no navmesh needed, steering samples the generator's terrain):
 *   - grazes and wanders, staying close to its herd
 *   - notices the player by hearing (depends on how fast the player moves) and
 *     sight (field of view, line of sight, worse at night), builds up awareness,
 *     looks up when suspicious and flees when sure; the herd follows
 *   - arrows deal damage by hit zone; wounded animals bleed, slow down and leave a blood trail
 *   - boars charge the player when hurt or approached too closely
 */
UCLASS()
class AOpenWorldAnimal : public ACharacter
{
	GENERATED_BODY()

public:

	AOpenWorldAnimal();

	/** Call between SpawnActorDeferred and FinishSpawning */
	void Setup(EOpenWorldAnimalKind InKind, int32 InHerdId, int32 InSeed);

	/** Capsule half height for a kind, so spawners can place it on the ground */
	static float GetCapsuleHalfHeight(EOpenWorldAnimalKind InKind);
	static FString GetKindName(EOpenWorldAnimalKind InKind);

	virtual void Tick(float DeltaSeconds) override;

	/** An arrow hit one of our body parts */
	void ReceiveArrowHit(const FHitResult& Hit, float Speed, const FVector& ShotOrigin);

	/** A noise at Location, audible within Radius (cm) for an animal with average hearing */
	void HearNoise(const FVector& Location, float Radius);

	/** Removes the carcass. Returns the meat it yields (kg). */
	float Harvest();

	bool IsDead() const { return State == EOpenWorldAnimalState::Dead; }
	bool IsWounded() const { return BleedRate > 0.f; }
	EOpenWorldAnimalKind GetKind() const { return Kind; }
	EOpenWorldAnimalState GetState() const { return State; }
	int32 GetHerdId() const { return HerdId; }
	float GetHealth() const { return Health; }
	float GetMeatKg() const;

protected:

	virtual void BeginPlay() override;

	void BuildBody();
	void UpdateSenses(float DeltaSeconds);
	void UpdateBehaviour(float DeltaSeconds);
	void UpdatePose(float DeltaSeconds);
	void UpdateBleeding(float DeltaSeconds);

	/** Walks or runs in roughly Direction, turning away from water, trees, cliffs and the island edge */
	void Steer(const FVector& Direction, float Speed, float DeltaSeconds);
	bool PickWanderTarget();
	void SetState(EOpenWorldAnimalState NewState, float Duration);

	/** Something scary happened at From: flee (or charge, for a boar) and warn the herd */
	void Startle(const FVector& From, bool bWarnHerd);
	void Die(bool bInstant);
	void AddBlood(const FVector& Location, float Size);

	AOpenWorldGenerator* GetGenerator() const;
	APawn* GetPlayerPawn() const;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> Body;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> NeckPivot;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> HeadPivot;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> TailPivot;

	/** Front left, front right, back left, back right */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> LegPivots;

	/** Blood drops, in world space */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UInstancedStaticMeshComponent> BloodTrail;


	EOpenWorldAnimalKind Kind = EOpenWorldAnimalKind::Deer;
	EOpenWorldAnimalState State = EOpenWorldAnimalState::Graze;
	int32 HerdId = 0;
	FRandomStream Stream;
	bool bStag = false;

	float Health = 100.f;
	float BleedRate = 0.f;
	float Awareness = 0.f;
	float StateTime = 0.f;
	float StateDuration = 0.f;
	float SenseTimer = 0.f;
	float StuckTime = 0.f;
	float ChargeCooldown = 0.f;
	float PendingStartleTime = -1.f;
	FVector PendingStartleFrom = FVector::ZeroVector;
	FVector Threat = FVector::ZeroVector;
	FVector WanderTarget = FVector::ZeroVector;
	FVector LastBloodLocation = FVector::ZeroVector;
	float ZigZagTimer = 0.f;
	float ZigZagSign = 1.f;

	// animation
	float GaitPhase = 0.f;
	float HeadDown = 0.f;
	float DeathBlend = 0.f;
	float BodyBaseZ = 0.f;
	float TorsoHalfWidth = 0.f;
	float DeadTime = 0.f;
};
