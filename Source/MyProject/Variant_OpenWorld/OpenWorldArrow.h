// Arrow projectile for the open world game.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OpenWorldArrow.generated.h"

class UMaterialInstanceDynamic;

/**
 *  An arrow with simple ballistics: gravity drop and a little air drag, traced every frame.
 *  It sticks into whatever it hits. Arrows stuck in the ground or trees can be picked up again;
 *  arrows in an animal come back when the carcass is harvested.
 */
UCLASS()
class AOpenWorldArrow : public AActor
{
	GENERATED_BODY()

public:

	AOpenWorldArrow();

	virtual void Tick(float DeltaSeconds) override;

	void Launch(const FVector& InVelocity, AActor* InShooter);

	/** Leaves a carcass that is being field dressed and lies on the ground, ready to be picked up */
	void DropFromCarcass();

	/** Stuck in the ground or a tree, not in an animal */
	bool CanBePickedUp() const { return bStuck && !bInAnimal; }

	/** Length of the shaft (cm) */
	static constexpr float Length = 75.f;

protected:

	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> Root;


	TWeakObjectPtr<AActor> Shooter;
	FVector Velocity = FVector::ZeroVector;
	FVector LaunchOrigin = FVector::ZeroVector;
	bool bFlying = false;
	bool bStuck = false;
	bool bInAnimal = false;
	float Age = 0.f;
};
