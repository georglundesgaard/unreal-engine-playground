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
class UProceduralMeshComponent;

/** How the terrain surface is rendered */
UENUM(BlueprintType)
enum class EOpenWorldTerrainStyle : uint8
{
	/** One instanced cube column per tile: stepped voxel look */
	Blocky,
	/** One continuous mesh with smoothed heights, smooth normals and blended biome colours */
	Smooth
};

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

	/** Max height difference (in steps) between neighbouring land tiles; 2 steps = 80cm, within the character's jump */
	UPROPERTY(EditAnywhere, Category="World", meta=(ClampMin=1))
	int32 MaxClimbSteps = 2;

	/** Terrain step that sits just under the water surface */
	UPROPERTY(EditAnywhere, Category="World")
	int32 WaterLevel = 8;

	/** Fraction of tiles that end up underwater */
	UPROPERTY(EditAnywhere, Category="World", meta=(ClampMin=0.05, ClampMax=0.9))
	float WaterFraction = 0.42f;

	/** Base noise frequency per tile */
	UPROPERTY(EditAnywhere, Category="World")
	float NoiseScale = 0.022f;

	/** Terrain rendering style. Set before Generate(). */
	UPROPERTY(EditAnywhere, Category="World")
	EOpenWorldTerrainStyle TerrainStyle = EOpenWorldTerrainStyle::Smooth;

	/** Smoothing passes applied to the smooth terrain's vertex heights */
	UPROPERTY(EditAnywhere, Category="World", meta=(ClampMin=0, ClampMax=8))
	int32 SmoothPasses = 2;

	/** Smooth terrain: vertical stretch of everything above the water line (1 = same as blocky). Keeps slopes walkable up to 4. */
	UPROPERTY(EditAnywhere, Category="World", meta=(ClampMin=1, ClampMax=4))
	float HillScale = 2.6f;

	/** Smooth terrain: amplitude (cm) of small-scale bumps added to land, fading out towards the coast */
	UPROPERTY(EditAnywhere, Category="World", meta=(ClampMin=0, ClampMax=200))
	float DetailNoise = 70.f;

	/** Number of crystals to collect */
	UPROPERTY(EditAnywhere, Category="Gameplay")
	int32 NumCrystals = 8;

	/** Builds the world for the given seed. Safe to call only once. */
	void Generate(int32 InSeed);

	int32 GetSeed() const { return Seed; }
	EOpenWorldTerrainStyle GetTerrainStyle() const { return TerrainStyle; }
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
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	/** Material that renders vertex colours (used by the smooth terrain) */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> TerrainMaterial;

	/** Material for clouds: lit, with a soft emissive glow so undersides facing away from the sun don't go dark */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> CloudMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> TerrainMesh;

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
		/** Instances beyond this distance (cm) are culled; 0 = never */
		float CullDistance = 0.f;
		/** Parent material for this layer; BasicShapeMaterial when null. Must expose a "Color" parameter. */
		UMaterialInterface* Material = nullptr;
		TArray<FTransform> Instances;
	};

	FLayerBuild& Layer(FName Name, UStaticMesh* Mesh, const FColor& SRGB, bool bCollision, bool bShadows = true, float Roughness = 0.85f, float CullDistance = 0.f);
	void FlushLayers();

	float Fbm(float X, float Y, int32 Octaves, int32 OffsetIndex) const;
	int32 Index(int32 X, int32 Y) const { return Y * GridSize + X; }
	FVector TileCenter(int32 X, int32 Y) const;
	float TileTop(int32 X, int32 Y) const { return Heights[Index(X, Y)] * StepHeight; }
	bool IsLand(int32 X, int32 Y) const { return Heights[Index(X, Y)] > WaterLevel; }

	/** Ground height under a world XY position for the current terrain style */
	float SurfaceZ(float WorldX, float WorldY) const;
	/** Ground height at a tile's centre */
	float TileSurface(int32 X, int32 Y) const;
	static FColor BiomeColor(EOpenWorldBiome Biome);

	void BuildHeights(FRandomStream& Stream);
	void BuildTerrain();
	void BuildSmoothTerrain();
	void PickSpawnAndCrystals(FRandomStream& Stream);
	/** Simple cube-based props, used with the blocky terrain */
	void BuildDecorations(FRandomStream& Stream);
	/** Detailed vegetation and props built from cylinders, spheres and cones, used with the smooth terrain */
	void BuildDetailedDecorations(FRandomStream& Stream);
	void BuildClouds(FRandomStream& Stream);
	void BuildMapTexture();

	TMap<FName, FLayerBuild> PendingLayers;
	TArray<FVector2D> NoiseOffsets;
	TArray<int32> Heights;
	/** Smooth terrain: heights at tile corners, (GridSize + 1)^2 entries */
	TArray<float> VertexHeights;
	TArray<EOpenWorldBiome> Biomes;
	TArray<bool> HasTree;
	TSet<int32> ReservedTiles;

	FVector SpawnLocation = FVector::ZeroVector;
	int32 Seed = 0;
	bool bGenerated = false;
};
