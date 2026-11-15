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

	/** How a part's surface is shaded */
	enum class ESurface : uint8
	{
		/** BasicShapeMaterial */
		Plain,
		/** Soft, matte with a light rim sheen (M_Natural): coats and hides */
		Fur,
		/** Wet and shiny: eyes, noses */
		Glossy
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

	/** Shared coloured material instance (colour is sRGB) */
	UMaterialInstanceDynamic* GetColorMaterial(const FColor& SRGB, float Roughness = 0.85f, OpenWorldShapes::ESurface Surface = OpenWorldShapes::ESurface::Plain);

	/**
	 *  Adds a coloured basic shape to Owner, attached to Parent.
	 *  SizeCm is the full size along each local axis. Parts have no collision unless the caller enables it.
	 */
	UStaticMeshComponent* AddPart(AActor* Owner, USceneComponent* Parent, OpenWorldShapes::EShape Shape, const FColor& SRGB,
		const FVector& Location, const FRotator& Rotation, const FVector& SizeCm, bool bCastShadow = true,
		OpenWorldShapes::ESurface Surface = OpenWorldShapes::ESurface::Plain);

private:

	UPROPERTY()
	TArray<TObjectPtr<UStaticMesh>> Meshes;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	/** M_Natural, for fur; falls back to BaseMaterial when missing */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> FurMaterial;

	/** Keyed by colour, roughness and surface */
	UPROPERTY()
	TMap<uint64, TObjectPtr<UMaterialInstanceDynamic>> ColorMaterials;
};
