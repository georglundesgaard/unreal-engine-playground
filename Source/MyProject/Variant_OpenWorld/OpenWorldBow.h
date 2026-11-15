// Hunting bow for the open world game.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OpenWorldBow.generated.h"

class ACharacter;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/**
 *  A simple recurve bow carried by the player.
 *
 *  Right mouse button aims (over-the-shoulder camera, the character faces the crosshair).
 *  Hold the left mouse button to draw and release to shoot: a longer draw (up to ~1 s) gives a faster,
 *  flatter, harder-hitting arrow. Holding a full draw for long makes the aim sway.
 *  Built from basic shapes; the string and limbs bend with the draw.
 */
UCLASS()
class AOpenWorldBow : public AActor
{
	GENERATED_BODY()

public:

	AOpenWorldBow();

	/** Gives the bow to a character */
	void Equip(ACharacter* InHolder);

	virtual void Tick(float DeltaSeconds) override;

	/** Shoots an arrow toward the crosshair. DrawAmount 0..1. Returns false when there is nothing to shoot. */
	bool Fire(float DrawAmount);

	/** Shoots an arrow from the bow straight at Target (no aiming needed; used by the test command) */
	bool FireAt(const FVector& Target, float DrawAmount);

	/** Test aid: holds the bow aimed at this draw (0..1) regardless of the mouse; negative releases it without shooting */
	void SetForcedDraw(float InDraw);

	/** Adds arrows to the quiver; returns how many fit */
	int32 AddArrows(int32 Count);

	int32 GetArrows() const { return Arrows; }
	int32 GetMaxArrows() const { return MaxArrows; }
	bool IsAiming() const { return bAiming; }
	bool IsDrawing() const { return bDrawing; }
	float GetDraw() const { return Draw; }

	/** Arrow speed (cm/s) for a draw amount */
	static float ArrowSpeed(float DrawAmount) { return FMath::Lerp(2200.f, 6200.f, FMath::Clamp(DrawAmount, 0.f, 1.f)); }

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void UpdateInput(float DeltaSeconds);
	void UpdateCamera(float DeltaSeconds);
	void UpdateVisual();
	void SetAiming(bool bNewAiming);
	FVector GetNockLocal() const;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Limbs;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Strings;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> NockedArrow;

	UPROPERTY(EditAnywhere, Category="Bow")
	int32 MaxArrows = 24;

	/** Seconds to reach a full draw */
	UPROPERTY(EditAnywhere, Category="Bow")
	float FullDrawTime = 0.9f;

	TWeakObjectPtr<ACharacter> Holder;

	int32 Arrows = 12;
	float Draw = 0.f;
	float ForcedDraw = -1.f;
	float FullDrawHold = 0.f;
	float Cooldown = 0.f;
	bool bAiming = false;
	bool bDrawing = false;

	// camera framing restored when aiming stops
	bool bCameraSaved = false;
	float DefaultArmLength = 400.f;
	FVector DefaultSocketOffset = FVector::ZeroVector;
	float DefaultFOV = 90.f;
	float AimBlend = 0.f;

	FRotator AppliedSway = FRotator::ZeroRotator;
	float SwayTime = 0.f;
};
