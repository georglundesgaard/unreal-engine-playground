// Shared helpers for building props out of the engine's basic shapes at runtime.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "OpenWorldShapes.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

namespace OpenWorldShapes
{
	enum class EShape : uint8
	{
		Cube,
		Sphere,
		Cylinder,
		Cone,
		Count
	};

	/** Places a cylinder (axis = local Z) so it spans A to B in its parent's space */
	void SetCylinderBetween(UStaticMeshComponent* Cylinder, const FVector& A, const FVector& B, float Radius);
}

/**
 *  Per-world library for runtime-built props (animals, arrows, the bow).
 *  Loads the basic shape meshes and BasicShapeMaterial once and shares one coloured
 *  material instance per colour, so a hundred animals don't make a hundred copies of "brown".
 */
UCLASS()
class UOpenWorldShapeLibrary : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Engine basic shape meshes are 100 units across */
	UStaticMesh* GetMesh(OpenWorldShapes::EShape Shape) const;

	/** Shared coloured instance of BasicShapeMaterial (colour is sRGB) */
	UMaterialInstanceDynamic* GetColorMaterial(const FColor& SRGB, float Roughness = 0.85f);

	/**
	 *  Adds a coloured basic shape to Owner, attached to Parent.
	 *  SizeCm is the full size along each local axis. Parts have no collision unless the caller enables it.
	 */
	UStaticMeshComponent* AddPart(AActor* Owner, USceneComponent* Parent, OpenWorldShapes::EShape Shape, const FColor& SRGB,
		const FVector& Location, const FRotator& Rotation, const FVector& SizeCm, bool bCastShadow = true);

private:

	UPROPERTY()
	TArray<TObjectPtr<UStaticMesh>> Meshes;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	/** Keyed by colour and roughness */
	UPROPERTY()
	TMap<uint64, TObjectPtr<UMaterialInstanceDynamic>> ColorMaterials;
};
