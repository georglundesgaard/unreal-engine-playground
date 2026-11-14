// Random blocky open world generator.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OpenWorldGenerator.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UTexture2D;
class AOpenWorldCrystal;

/** Surface type of a terrain tile */
UENUM()
enum class EOpenWorldBiome : uint8
{
	Seabed,
	Sand,
	Grass,
	Forest,
	Rock,
	Snow
};

/**
 *  Generates a small random island out of instanced cubes:
 *  stepped terrain, water, trees, rocks, flowers, clouds and collectible crystals.
 *  Also builds a minimap texture of the result.
 */
UCLASS()
class AOpenWorldGenerator : public AActor
{
	GENERATED_BODY()

public:

	AOpenWorldGenerator();

	/** Number of tiles along each side of the world */
	UPROPERTY(EditAnywhere, Category="World", meta=(ClampMin=16, ClampMax=256))
	int32 GridSize = 128;

	/** Width of a single tile (cm) */
	UPROPERTY(EditAnywhere, Category="World")
	float TileSize = 400.f;

	/** Height of one terrain step (cm). Keep below the character's max step height so slopes stay walkable. */
	UPROPERTY(EditAnywhere, Category="World")
	float StepHeight = 40.f;

	/** Highest terrain step */
	UPROPERTY(EditAnywhere, Category="World")
	int32 MaxSteps = 50;

	/** Terrain step that sits just under the water surface */
	UPROPERTY(EditAnywhere, Category="World")
	int32 WaterLevel = 8;

	/** Fraction of tiles that end up underwater */
	UPROPERTY(EditAnywhere, Category="World", meta=(ClampMin=0.05, ClampMax=0.9))
	float WaterFraction = 0.42f;

	/** Base noise frequency per tile */
	UPROPERTY(EditAnywhere, Category="World")
	float NoiseScale = 0.022f;

	/** Number of crystals to collect */
	UPROPERTY(EditAnywhere, Category="Gameplay")
	int32 NumCrystals = 8;

	/** Builds the world for the given seed. Safe to call only once. */
	void Generate(int32 InSeed);

	bool IsGenerated() const { return bGenerated; }
	int32 GetSeed() const { return Seed; }
	FVector GetSpawnLocation() const { return SpawnLocation; }
	float GetWaterSurfaceZ() const { return WaterLevel * StepHeight + StepHeight * 0.6f; }
	float GetHalfExtent() const { return GridSize * TileSize * 0.5f; }
	UTexture2D* GetMapTexture() const { return MapTexture; }
	const TArray<TObjectPtr<AOpenWorldCrystal>>& GetCrystals() const { return Crystals; }

	/** Converts a world location to 0..1 minimap coordinates (x = right/+Y, y = down/-X) */
	FVector2D WorldToMap(const FVector& WorldLocation) const;

protected:

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Water;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> ConeMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> MeshLayers;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> MapTexture;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AOpenWorldCrystal>> Crystals;

private:

	/** Instances waiting to be added to a layer */
	struct FLayerBuild
	{
		UStaticMesh* Mesh = nullptr;
		FLinearColor Color;
		bool bCollision = true;
		bool bShadows = true;
		float Roughness = 0.8f;
		TArray<FTransform> Instances;
	};

	FLayerBuild& Layer(FName Name, UStaticMesh* Mesh, const FColor& SRGB, bool bCollision, bool bShadows = true, float Roughness = 0.85f);
	void FlushLayers();

	float Fbm(float X, float Y, int32 Octaves, int32 OffsetIndex) const;
	int32 Index(int32 X, int32 Y) const { return Y * GridSize + X; }
	FVector TileCenter(int32 X, int32 Y) const;
	float TileTop(int32 X, int32 Y) const { return Heights[Index(X, Y)] * StepHeight; }
	bool IsLand(int32 X, int32 Y) const { return Heights[Index(X, Y)] > WaterLevel; }

	void BuildHeights(FRandomStream& Stream);
	void BuildTerrain();
	void PickSpawnAndCrystals(FRandomStream& Stream);
	void BuildDecorations(FRandomStream& Stream);
	void BuildClouds(FRandomStream& Stream);
	void BuildMapTexture();

	TMap<FName, FLayerBuild> PendingLayers;
	TArray<FVector2D> NoiseOffsets;
	TArray<int32> Heights;
	TArray<EOpenWorldBiome> Biomes;
	TArray<bool> HasTree;
	TSet<int32> ReservedTiles;
	TArray<FIntPoint> CrystalTiles;

	FVector SpawnLocation = FVector::ZeroVector;
	int32 Seed = 0;
	bool bGenerated = false;
};
