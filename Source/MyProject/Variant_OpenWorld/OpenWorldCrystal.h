// Collectible crystal for the open world game.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OpenWorldCrystal.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInterface;

/**
 *  A spinning, glowing crystal with a tall light beam so it can be found from afar.
 *  Collected when the player walks into it.
 */
UCLASS()
class AOpenWorldCrystal : public AActor
{
	GENERATED_BODY()

public:

	AOpenWorldCrystal();

	virtual void Tick(float DeltaSeconds) override;

protected:

	virtual void BeginPlay() override;

	UFUNCTION()
	void OnTriggerOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> Pivot;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Gem;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Beam;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY(EditAnywhere, Category="Crystal")
	FLinearColor CrystalColor = FLinearColor(0.05f, 0.75f, 1.f);

	float Age = 0.f;
	bool bCollected = false;
};
