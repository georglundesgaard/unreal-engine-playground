// Random blocky open world generator.

#include "OpenWorldGenerator.h"
#include "OpenWorldCrystal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"

namespace OpenWorldColors
{
	const FColor Sand(226, 207, 140);
	const FColor Seabed(190, 172, 118);
	const FColor Grass(106, 176, 72);
	const FColor Forest(70, 142, 58);
	const FColor Rock(128, 124, 118);
	const FColor Snow(240, 244, 250);
	const FColor Water(48, 118, 200);
	const FColor DeepWater(24, 70, 150);
	const FColor Trunk(110, 78, 50);
	const FColor Leaves(62, 146, 62);
	const FColor LeavesLight(122, 178, 60);
	const FColor LeavesAutumn(224, 138, 48);
	const FColor Pine(36, 98, 62);
	const FColor Boulder(146, 140, 134);
	const FColor FlowerRed(224, 64, 64);
	const FColor FlowerYellow(244, 214, 64);
	const FColor Cloud(255, 255, 255);
}

AOpenWorldGenerator::AOpenWorldGenerator()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Water = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Water"));
	Water->SetupAttachment(Root);
	Water->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Water->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	CubeMesh = CubeFinder.Object;
	ConeMesh = ConeFinder.Object;
	SphereMesh = SphereFinder.Object;
	CylinderMesh = CylinderFinder.Object;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TerrainMaterialFinder(TEXT("/Game/Variant_OpenWorld/M_TerrainVertexColor.M_TerrainVertexColor"));
	TerrainMaterial = TerrainMaterialFinder.Object;
	BaseMaterial = MaterialFinder.Object;

	Water->SetStaticMesh(CubeMesh);
}

FVector AOpenWorldGenerator::TileCenter(int32 X, int32 Y) const
{
	const float Half = GetHalfExtent();
	return FVector((X + 0.5f) * TileSize - Half, (Y + 0.5f) * TileSize - Half, 0.f);
}

FVector2D AOpenWorldGenerator::WorldToMap(const FVector& WorldLocation) const
{
	const float Extent = GridSize * TileSize;
	const float Half = Extent * 0.5f;
	return FVector2D((WorldLocation.Y + Half) / Extent, 1.f - (WorldLocation.X + Half) / Extent);
}

FColor AOpenWorldGenerator::BiomeColor(EOpenWorldBiome Biome)
{
	switch (Biome)
	{
	case EOpenWorldBiome::Seabed: return OpenWorldColors::Seabed;
	case EOpenWorldBiome::Sand:   return OpenWorldColors::Sand;
	case EOpenWorldBiome::Grass:  return OpenWorldColors::Grass;
	case EOpenWorldBiome::Forest: return OpenWorldColors::Forest;
	case EOpenWorldBiome::Rock:   return OpenWorldColors::Rock;
	case EOpenWorldBiome::Snow:   return OpenWorldColors::Snow;
	}
	checkNoEntry();
	return OpenWorldColors::Snow;
}

float AOpenWorldGenerator::SurfaceZ(float WorldX, float WorldY) const
{
	const float Half = GetHalfExtent();
	const float GX = FMath::Clamp((WorldX + Half) / TileSize, 0.f, float(GridSize) - KINDA_SMALL_NUMBER);
	const float GY = FMath::Clamp((WorldY + Half) / TileSize, 0.f, float(GridSize) - KINDA_SMALL_NUMBER);
	const int32 X = FMath::FloorToInt(GX);
	const int32 Y = FMath::FloorToInt(GY);

	if (TerrainStyle == EOpenWorldTerrainStyle::Blocky || VertexHeights.Num() == 0)
	{
		return TileTop(X, Y);
	}

	// interpolate across the same two triangles the mesh uses for this quad
	const int32 V = GridSize + 1;
	const float FX = GX - X;
	const float FY = GY - Y;
	const float H00 = VertexHeights[Y * V + X];
	const float H10 = VertexHeights[Y * V + X + 1];
	const float H01 = VertexHeights[(Y + 1) * V + X];
	const float H11 = VertexHeights[(Y + 1) * V + X + 1];
	return FX >= FY
		? H00 + FX * (H10 - H00) + FY * (H11 - H10)
		: H00 + FY * (H01 - H00) + FX * (H11 - H01);
}

float AOpenWorldGenerator::TileSurface(int32 X, int32 Y) const
{
	const FVector C = TileCenter(X, Y);
	return SurfaceZ(C.X, C.Y);
}

void AOpenWorldGenerator::BuildSmoothTerrain()
{
	const int32 V = GridSize + 1;
	const float Half = GetHalfExtent();

	// corner heights and colours: average of the tiles touching each corner
	VertexHeights.SetNumZeroed(V * V);
	TArray<FColor> VertexColors;
	VertexColors.SetNumZeroed(V * V);
	TArray<bool> TouchesLand;
	TouchesLand.Init(false, V * V);
	for (int32 VY = 0; VY < V; ++VY)
	{
		for (int32 VX = 0; VX < V; ++VX)
		{
			float Sum = 0.f;
			FLinearColor ColorSum(0.f, 0.f, 0.f, 0.f);
			int32 Count = 0;
			for (int32 TY = VY - 1; TY <= VY; ++TY)
			{
				for (int32 TX = VX - 1; TX <= VX; ++TX)
				{
					if (TX >= 0 && TY >= 0 && TX < GridSize && TY < GridSize)
					{
						Sum += TileTop(TX, TY);
						ColorSum += FLinearColor::FromSRGBColor(BiomeColor(Biomes[Index(TX, TY)]));
						TouchesLand[VY * V + VX] |= IsLand(TX, TY);
						++Count;
					}
				}
			}
			VertexHeights[VY * V + VX] = Sum / Count;
			// the material reads vertex colour as linear, so store linear values
			VertexColors[VY * V + VX] = (ColorSum / float(Count)).ToFColor(false);
		}
	}

	// gaussian-ish blur to round off the remaining terraces
	TArray<float> Temp;
	for (int32 Pass = 0; Pass < SmoothPasses; ++Pass)
	{
		Temp = VertexHeights;
		for (int32 VY = 0; VY < V; ++VY)
		{
			for (int32 VX = 0; VX < V; ++VX)
			{
				float Sum = 0.f, Weight = 0.f;
				for (int32 DY = -1; DY <= 1; ++DY)
				{
					for (int32 DX = -1; DX <= 1; ++DX)
					{
						const int32 NX = VX + DX, NY = VY + DY;
						if (NX >= 0 && NY >= 0 && NX < V && NY < V)
						{
							const float W = (DX == 0 ? 2.f : 1.f) * (DY == 0 ? 2.f : 1.f);
							Sum += Temp[NY * V + NX] * W;
							Weight += W;
						}
					}
				}
				VertexHeights[VY * V + VX] = Sum / Weight;
			}
		}
	}

	// taller hills: stretch everything above the water line (the coastline stays put),
	// then add small bumps that fade in away from the shore
	const float WaterZ = GetWaterSurfaceZ();
	for (int32 VY = 0; VY < V; ++VY)
	{
		for (int32 VX = 0; VX < V; ++VX)
		{
			float& Hgt = VertexHeights[VY * V + VX];
			if (Hgt > WaterZ)
			{
				const float Above = Hgt - WaterZ;
				const float Fade = FMath::Clamp(Above / 300.f, 0.f, 1.f);
				const float Bumps = Fbm(VX * 0.15f, VY * 0.15f, 3, 12) * DetailNoise * Fade;
				Hgt = WaterZ + Above * HillScale + Bumps;
			}
		}
	}

	// averaging drags coastal land towards the seabed: keep every corner that touches
	// a land tile above the water, so all land the game treats as dry stays dry
	const float MinLandZ = GetWaterSurfaceZ() + 12.f;
	for (int32 I = 0; I < V * V; ++I)
	{
		if (TouchesLand[I])
		{
			VertexHeights[I] = FMath::Max(VertexHeights[I], MinLandZ);
		}
	}

	auto H =[&](int32 VX, int32 VY)
	{
		return VertexHeights[FMath::Clamp(VY, 0, V - 1) * V + FMath::Clamp(VX, 0, V - 1)];
	};

	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	Vertices.Reserve(V * V);
	Normals.Reserve(V * V);
	UVs.Reserve(V * V);

	for (int32 VY = 0; VY < V; ++VY)
	{
		for (int32 VX = 0; VX < V; ++VX)
		{
			Vertices.Add(FVector(VX * TileSize - Half, VY * TileSize - Half, H(VX, VY)));

			// smooth normal from central differences (one-sided at the mesh border)
			const int32 X0 = FMath::Max(VX - 1, 0), X1 = FMath::Min(VX + 1, V - 1);
			const int32 Y0 = FMath::Max(VY - 1, 0), Y1 = FMath::Min(VY + 1, V - 1);
			const float DZDX = (H(X1, VY) - H(X0, VY)) / ((X1 - X0) * TileSize);
			const float DZDY = (H(VX, Y1) - H(VX, Y0)) / ((Y1 - Y0) * TileSize);
			Normals.Add(FVector(-DZDX, -DZDY, 1.f).GetSafeNormal());

			UVs.Add(FVector2D(VX, VY));
		}
	}

	TArray<int32> Triangles;
	Triangles.Reserve(GridSize * GridSize * 6);
	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 V00 = Y * V + X;
			const int32 V10 = V00 + 1;
			const int32 V01 = V00 + V;
			const int32 V11 = V01 + 1;
			// split along V00-V11 (matches SurfaceZ)
			Triangles.Append({ V00, V11, V10 });
			Triangles.Append({ V00, V01, V11 });
		}
	}

	TerrainMesh = NewObject<UProceduralMeshComponent>(this, TEXT("SmoothTerrain"));
	TerrainMesh->SetupAttachment(Root);
	TerrainMesh->bUseAsyncCooking = false;
	TerrainMesh->RegisterComponent();
	TerrainMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, VertexColors, TArray<FProcMeshTangent>(), true);
	TerrainMesh->SetMaterial(0, TerrainMaterial);
	TerrainMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	AddInstanceComponent(TerrainMesh);
}

float AOpenWorldGenerator::Fbm(float X, float Y, int32 Octaves, int32 OffsetIndex) const
{
	float Sum = 0.f, Amp = 1.f, Freq = 1.f, Norm = 0.f;
	for (int32 i = 0; i < Octaves; ++i)
	{
		const FVector2D& Off = NoiseOffsets[(OffsetIndex + i) % NoiseOffsets.Num()];
		Sum += Amp * FMath::PerlinNoise2D(FVector2D(X * Freq + Off.X, Y * Freq + Off.Y));
		Norm += Amp;
		Amp *= 0.5f;
		Freq *= 2.f;
	}
	return Sum / Norm;
}

AOpenWorldGenerator::FLayerBuild& AOpenWorldGenerator::Layer(FName Name, UStaticMesh* Mesh, const FColor& SRGB, bool bCollision, bool bShadows, float Roughness, float CullDistance)
{
	FLayerBuild* Found = PendingLayers.Find(Name);
	if (!Found)
	{
		Found = &PendingLayers.Add(Name);
		Found->Mesh = Mesh;
		Found->Color = FLinearColor::FromSRGBColor(SRGB);
		Found->bCollision = bCollision;
		Found->bShadows = bShadows;
		Found->Roughness = Roughness;
		Found->CullDistance = CullDistance;
	}
	return *Found;
}

void AOpenWorldGenerator::Generate(int32 InSeed)
{
	if (bGenerated)
	{
		return;
	}
	bGenerated = true;
	Seed = InSeed;

	// guard against editor values that would break the height mapping
	GridSize = FMath::Clamp(GridSize, 16, 256);
	WaterLevel = FMath::Max(WaterLevel, 1);
	MaxSteps = FMath::Max(MaxSteps, WaterLevel + 4);
	MaxClimbSteps = FMath::Max(MaxClimbSteps, 1);
	NumCrystals = FMath::Max(NumCrystals, 1);
	HillScale = FMath::Clamp(HillScale, 1.f, 4.f);
	DetailNoise = FMath::Max(DetailNoise, 0.f);

	// the terrain never moves: static root so static instance layers can attach to it
	Root->SetMobility(EComponentMobility::Static);
	Water->SetMobility(EComponentMobility::Movable);

	FRandomStream Stream(Seed);

	NoiseOffsets.Reset();
	for (int32 i = 0; i < 16; ++i)
	{
		NoiseOffsets.Add(FVector2D(Stream.FRandRange(-4000.f, 4000.f), Stream.FRandRange(-4000.f, 4000.f)));
	}

	BuildHeights(Stream);
	if (TerrainStyle == EOpenWorldTerrainStyle::Smooth && TerrainMaterial)
	{
		BuildSmoothTerrain();
	}
	else
	{
		TerrainStyle = EOpenWorldTerrainStyle::Blocky;
		BuildTerrain();
	}
	PickSpawnAndCrystals(Stream);
	if (TerrainStyle == EOpenWorldTerrainStyle::Smooth)
	{
		BuildDetailedDecorations(Stream);
	}
	else
	{
		BuildDecorations(Stream);
	}
	BuildClouds(Stream);
	FlushLayers();
	BuildMapTexture();

	// water surface: a thin slab covering the island and beyond
	if (BaseMaterial)
	{
		UMaterialInstanceDynamic* WaterMat = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		WaterMat->SetVectorParameterValue(TEXT("Color"), FLinearColor::FromSRGBColor(OpenWorldColors::Water));
		WaterMat->SetScalarParameterValue(TEXT("Roughness"), 0.15f);
		Water->SetMaterial(0, WaterMat);
	}
	const float WaterSize = GridSize * TileSize * 4.f;
	Water->SetWorldLocation(FVector(0.f, 0.f, GetWaterSurfaceZ() - 5.f));
	Water->SetWorldScale3D(FVector(WaterSize / 100.f, WaterSize / 100.f, 0.1f));
}

void AOpenWorldGenerator::BuildHeights(FRandomStream& Stream)
{
	const int32 Num = GridSize * GridSize;
	TArray<float> Raw;
	Raw.SetNumUninitialized(Num);

	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			const float Nx = X * NoiseScale;
			const float Ny = Y * NoiseScale;

			// rolling hills + sharp mountain ridges
			const float Hills = Fbm(Nx, Ny, 5, 0);
			float Ridge = 1.f - FMath::Abs(Fbm(Nx * 0.7f, Ny * 0.7f, 4, 5));
			Ridge = Ridge * Ridge * Ridge;

			// island falloff towards the edges
			const float Dx = (X + 0.5f) / GridSize * 2.f - 1.f;
			const float Dy = (Y + 0.5f) / GridSize * 2.f - 1.f;
			const float Dist = FMath::Sqrt(Dx * Dx + Dy * Dy);
			const float Falloff = FMath::SmoothStep(0.45f, 0.98f, Dist);

			Raw[Index(X, Y)] = Hills + Ridge * 0.45f - Falloff * 1.6f;
		}
	}

	// normalise with percentiles so every seed has a similar land/water balance
	TArray<float> Sorted = Raw;
	Sorted.Sort();
	const float MinV = Sorted[0];
	const float WaterV = Sorted[FMath::Clamp(int32(Num * WaterFraction), 0, Num - 1)];
	const float MaxV = Sorted[FMath::Clamp(int32(Num * 0.997f), 0, Num - 1)];

	Heights.SetNumUninitialized(Num);
	Biomes.SetNumUninitialized(Num);
	HasTree.Init(false, Num);

	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 I = Index(X, Y);
			const float V = Raw[I];
			int32 Steps;
			if (V < WaterV)
			{
				const float T = (V - MinV) / FMath::Max(WaterV - MinV, KINDA_SMALL_NUMBER);
				Steps = FMath::Clamp(FMath::RoundToInt(T * WaterLevel), 0, WaterLevel);
			}
			else
			{
				float T = FMath::Clamp((V - WaterV) / FMath::Max(MaxV - WaterV, KINDA_SMALL_NUMBER), 0.f, 1.f);
				T = FMath::Pow(T, 1.6f); // flatter lowlands, steeper peaks
				Steps = WaterLevel + 1 + FMath::RoundToInt(T * (MaxSteps - WaterLevel - 1));
			}
			Heights[I] = Steps;
		}
	}

	// limit land slopes so every cliff can be jumped: no land tile may be more than
	// MaxClimbSteps above a land neighbour (only lowers tiles, so it always converges)
	for (int32 Pass = 0; Pass < 256; ++Pass)
	{
		bool bChanged = false;
		for (int32 Y = 0; Y < GridSize; ++Y)
		{
			for (int32 X = 0; X < GridSize; ++X)
			{
				const int32 I = Index(X, Y);
				if (Heights[I] <= WaterLevel)
				{
					continue;
				}
				const FIntPoint Dirs[4] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
				for (const FIntPoint& D : Dirs)
				{
					const int32 NX = X + D.X, NY = Y + D.Y;
					if (NX < 0 || NY < 0 || NX >= GridSize || NY >= GridSize)
					{
						continue;
					}
					const int32 N = Heights[Index(NX, NY)];
					if (N > WaterLevel && Heights[I] > N + MaxClimbSteps)
					{
						Heights[I] = N + MaxClimbSteps;
						bChanged = true;
					}
				}
			}
		}
		if (!bChanged)
		{
			break;
		}
	}

	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 I = Index(X, Y);
			const int32 Steps = Heights[I];

			// biome from altitude + moisture
			const float Moisture = Fbm(X * NoiseScale * 1.8f, Y * NoiseScale * 1.8f, 3, 10);
			const float Alt = float(Steps - WaterLevel) / float(MaxSteps - WaterLevel);
			const float Jitter = Moisture * 0.08f;

			EOpenWorldBiome Biome;
			if (Steps <= WaterLevel)               Biome = EOpenWorldBiome::Seabed;
			else if (Steps <= WaterLevel + 1)      Biome = EOpenWorldBiome::Sand;
			else if (Alt < 0.58f + Jitter)         Biome = Moisture > 0.05f ? EOpenWorldBiome::Forest : EOpenWorldBiome::Grass;
			else if (Alt < 0.86f + Jitter)         Biome = EOpenWorldBiome::Rock;
			else                                   Biome = EOpenWorldBiome::Snow;
			Biomes[I] = Biome;
		}
	}
}

void AOpenWorldGenerator::BuildTerrain()
{
	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			const float Top = TileTop(X, Y);

			// extend each column down to its lowest neighbour so no gaps show
			float MinNeighbour = Top;
			const FIntPoint Dirs[4] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
			for (const FIntPoint& D : Dirs)
			{
				const int32 NX = X + D.X, NY = Y + D.Y;
				MinNeighbour = (NX < 0 || NY < 0 || NX >= GridSize || NY >= GridSize)
					? FMath::Min(MinNeighbour, -StepHeight * 2.f)
					: FMath::Min(MinNeighbour, TileTop(NX, NY));
			}
			const float Bottom = FMath::Min(Top - StepHeight, MinNeighbour) - 2.f;
			const float Height = Top - Bottom;

			FVector Center = TileCenter(X, Y);
			Center.Z = (Top + Bottom) * 0.5f;
			const FTransform Xf(FRotator::ZeroRotator, Center, FVector(TileSize / 100.f, TileSize / 100.f, Height / 100.f));

			switch (Biomes[Index(X, Y)])
			{
			case EOpenWorldBiome::Seabed: Layer("Seabed", CubeMesh, BiomeColor(EOpenWorldBiome::Seabed), true, false).Instances.Add(Xf); break;
			case EOpenWorldBiome::Sand:   Layer("Sand", CubeMesh, BiomeColor(EOpenWorldBiome::Sand), true).Instances.Add(Xf); break;
			case EOpenWorldBiome::Grass:  Layer("Grass", CubeMesh, BiomeColor(EOpenWorldBiome::Grass), true).Instances.Add(Xf); break;
			case EOpenWorldBiome::Forest: Layer("Forest", CubeMesh, BiomeColor(EOpenWorldBiome::Forest), true).Instances.Add(Xf); break;
			case EOpenWorldBiome::Rock:   Layer("Rock", CubeMesh, BiomeColor(EOpenWorldBiome::Rock), true).Instances.Add(Xf); break;
			case EOpenWorldBiome::Snow:   Layer("Snow", CubeMesh, BiomeColor(EOpenWorldBiome::Snow), true, true, 0.5f).Instances.Add(Xf); break;
			}
		}
	}
}

void AOpenWorldGenerator::PickSpawnAndCrystals(FRandomStream& Stream)
{
	// spawn: a random grassy tile near the middle of the island
	TArray<FIntPoint> Central;
	FIntPoint Best(GridSize / 2, GridSize / 2);
	float BestDist = TNumericLimits<float>::Max();
	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			if (!IsLand(X, Y))
			{
				continue;
			}
			const float Dist = FVector2D(X - GridSize * 0.5f, Y - GridSize * 0.5f).Size();
			const EOpenWorldBiome B = Biomes[Index(X, Y)];
			if (Dist < GridSize * 0.22f && (B == EOpenWorldBiome::Grass || B == EOpenWorldBiome::Forest))
			{
				Central.Add(FIntPoint(X, Y));
			}
			if (Dist < BestDist)
			{
				BestDist = Dist;
				Best = FIntPoint(X, Y);
			}
		}
	}
	const FIntPoint SpawnTile = Central.Num() > 0 ? Central[Stream.RandRange(0, Central.Num() - 1)] : Best;
	SpawnLocation = TileCenter(SpawnTile.X, SpawnTile.Y);
	SpawnLocation.Z = TileSurface(SpawnTile.X, SpawnTile.Y) + 120.f;

	// keep the spawn area clear of trees
	for (int32 DY = -2; DY <= 2; ++DY)
	{
		for (int32 DX = -2; DX <= 2; ++DX)
		{
			ReservedTiles.Add(Index(FMath::Clamp(SpawnTile.X + DX, 0, GridSize - 1), FMath::Clamp(SpawnTile.Y + DY, 0, GridSize - 1)));
		}
	}

	// crystals: only on land the player can walk/jump to from the spawn,
	// spread out over the island and away from the spawn
	TArray<FIntPoint> Candidates;
	TArray<bool> Reachable;
	Reachable.Init(false, GridSize * GridSize);
	TArray<FIntPoint> Queue;
	Queue.Add(SpawnTile);
	Reachable[Index(SpawnTile.X, SpawnTile.Y)] = true;
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const FIntPoint Cur = Queue[Head];
		if (Biomes[Index(Cur.X, Cur.Y)] != EOpenWorldBiome::Sand)
		{
			Candidates.Add(Cur);
		}
		const FIntPoint Dirs[4] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
		for (const FIntPoint& D : Dirs)
		{
			const FIntPoint N = Cur + D;
			if (N.X < 0 || N.Y < 0 || N.X >= GridSize || N.Y >= GridSize)
			{
				continue;
			}
			const int32 NI = Index(N.X, N.Y);
			if (!Reachable[NI] && IsLand(N.X, N.Y) && Heights[NI] - Heights[Index(Cur.X, Cur.Y)] <= MaxClimbSteps)
			{
				Reachable[NI] = true;
				Queue.Add(N);
			}
		}
	}

	TArray<FIntPoint> CrystalTiles;
	for (int32 i = Candidates.Num() - 1; i > 0; --i)
	{
		Candidates.Swap(i, Stream.RandRange(0, i));
	}

	float MinSpacing = GridSize * 0.16f;
	while (CrystalTiles.Num() < NumCrystals && MinSpacing > 2.f)
	{
		for (const FIntPoint& C : Candidates)
		{
			if (CrystalTiles.Num() >= NumCrystals)
			{
				break;
			}
			if (FVector2D(C - SpawnTile).Size() < MinSpacing)
			{
				continue;
			}
			bool bTooClose = false;
			for (const FIntPoint& Other : CrystalTiles)
			{
				if (FVector2D(C - Other).Size() < MinSpacing)
				{
					bTooClose = true;
					break;
				}
			}
			if (!bTooClose)
			{
				CrystalTiles.Add(C);
			}
		}
		MinSpacing *= 0.75f; // relax if the island is too small
	}

	for (const FIntPoint& C : CrystalTiles)
	{
		ReservedTiles.Add(Index(C.X, C.Y));
		FVector Loc = TileCenter(C.X, C.Y);
		Loc.Z = TileSurface(C.X, C.Y) + 110.f;
		if (AOpenWorldCrystal* Crystal = GetWorld()->SpawnActor<AOpenWorldCrystal>(AOpenWorldCrystal::StaticClass(), Loc, FRotator::ZeroRotator))
		{
			Crystals.Add(Crystal);
		}
	}
}

void AOpenWorldGenerator::BuildDecorations(FRandomStream& Stream)
{
	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 I = Index(X, Y);
			if (!IsLand(X, Y) || ReservedTiles.Contains(I))
			{
				continue;
			}

			const EOpenWorldBiome B = Biomes[I];
			const FVector Center = TileCenter(X, Y);
			// ground point at an offset (in tiles) from the tile centre
			auto Ground = [this, &Center](float OffX, float OffY)
			{
				const float WX = Center.X + OffX * TileSize;
				const float WY = Center.Y + OffY * TileSize;
				return FVector(WX, WY, SurfaceZ(WX, WY));
			};
			const float Alt = float(Heights[I] - WaterLevel) / float(MaxSteps - WaterLevel);

			// trees
			const float TreeChance = B == EOpenWorldBiome::Forest ? 0.2f : (B == EOpenWorldBiome::Grass ? 0.025f : 0.f);
			if (Stream.FRand() < TreeChance)
			{
				HasTree[I] = true;
				const float OffX = Stream.FRandRange(-0.25f, 0.25f);
				const float OffY = Stream.FRandRange(-0.25f, 0.25f);
				// sink slightly so trunks never float on slopes
				const float Sink = TerrainStyle == EOpenWorldTerrainStyle::Smooth ? 10.f : 0.f;
				const FVector Base = Ground(OffX, OffY) - FVector(0.f, 0.f, Sink);
				const float Yaw = Stream.FRandRange(0.f, 90.f);
				const bool bPine = Alt > 0.3f || Stream.FRand() < 0.25f;

				if (bPine)
				{
					const float TrunkH = Stream.FRandRange(120.f, 180.f);
					Layer("Trunk", CubeMesh, OpenWorldColors::Trunk, true).Instances.Add(
						FTransform(FRotator(0, Yaw, 0), Base + FVector(0, 0, TrunkH * 0.5f), FVector(0.3f, 0.3f, TrunkH / 100.f)));
					const float S = Stream.FRandRange(0.85f, 1.25f);
					Layer("Pine", ConeMesh, OpenWorldColors::Pine, false).Instances.Add(
						FTransform(FRotator(0, Yaw, 0), Base + FVector(0, 0, TrunkH + 110.f * S), FVector(2.4f * S, 2.4f * S, 2.4f * S)));
					Layer("Pine", ConeMesh, OpenWorldColors::Pine, false).Instances.Add(
						FTransform(FRotator(0, Yaw, 0), Base + FVector(0, 0, TrunkH + 250.f * S), FVector(1.7f * S, 1.7f * S, 2.0f * S)));
				}
				else
				{
					const float TrunkH = Stream.FRandRange(200.f, 320.f);
					Layer("Trunk", CubeMesh, OpenWorldColors::Trunk, true).Instances.Add(
						FTransform(FRotator(0, Yaw, 0), Base + FVector(0, 0, TrunkH * 0.5f), FVector(0.35f, 0.35f, TrunkH / 100.f)));

					const float Roll = Stream.FRand();
					const FName LeafLayer = Roll < 0.12f ? FName("LeavesAutumn") : (Roll < 0.4f ? FName("LeavesLight") : FName("Leaves"));
					const FColor LeafColor = Roll < 0.12f ? OpenWorldColors::LeavesAutumn : (Roll < 0.4f ? OpenWorldColors::LeavesLight : OpenWorldColors::Leaves);
					const float S = Stream.FRandRange(1.8f, 2.6f);
					Layer(LeafLayer, CubeMesh, LeafColor, false).Instances.Add(
						FTransform(FRotator(0, Yaw, 0), Base + FVector(0, 0, TrunkH + S * 30.f), FVector(S, S, S * 0.85f)));
					Layer(LeafLayer, CubeMesh, LeafColor, false).Instances.Add(
						FTransform(FRotator(0, Yaw + 45.f, 0), Base + FVector(0, 0, TrunkH + S * 85.f), FVector(S * 0.6f, S * 0.6f, S * 0.5f)));
				}
				continue;
			}

			// boulders
			const float RockChance = B == EOpenWorldBiome::Rock ? 0.1f : (B == EOpenWorldBiome::Snow ? 0.03f : 0.008f);
			if (Stream.FRand() < RockChance)
			{
				const float S = Stream.FRandRange(0.7f, 1.8f);
				const float OffX = Stream.FRandRange(-0.2f, 0.2f);
				const float OffY = Stream.FRandRange(-0.2f, 0.2f);
				const FVector Pos = Ground(OffX, OffY) + FVector(0.f, 0.f, S * 30.f);
				const FRotator Rot(Stream.FRandRange(-20.f, 20.f), Stream.FRandRange(0.f, 90.f), Stream.FRandRange(-20.f, 20.f));
				Layer("Boulder", CubeMesh, OpenWorldColors::Boulder, true).Instances.Add(
					FTransform(Rot, Pos, FVector(S * Stream.FRandRange(0.9f, 1.4f), S, S * 0.8f)));
				continue;
			}

			// flowers
			if ((B == EOpenWorldBiome::Grass || B == EOpenWorldBiome::Forest) && Stream.FRand() < 0.08f)
			{
				const bool bRed = (Stream.FRand() < 0.5f);
				const int32 Count = Stream.RandRange(3, 6);
				for (int32 F = 0; F < Count; ++F)
				{
					const float OffX = Stream.FRandRange(-0.4f, 0.4f);
					const float OffY = Stream.FRandRange(-0.4f, 0.4f);
					const FVector Pos = Ground(OffX, OffY) + FVector(0.f, 0.f, 8.f);
					Layer(bRed ? "FlowerRed" : "FlowerYellow", CubeMesh, bRed ? OpenWorldColors::FlowerRed : OpenWorldColors::FlowerYellow, false, false).Instances.Add(
						FTransform(FRotator(0, Stream.FRandRange(0.f, 90.f), 0), Pos, FVector(0.14f)));
				}
			}
		}
	}
}

void AOpenWorldGenerator::BuildDetailedDecorations(FRandomStream& Stream)
{
	// palette
	const FColor Bark(96, 66, 44), BarkDark(70, 50, 36), StumpTop(176, 144, 100);
	const FColor BirchBark(228, 224, 212), BirchMark(52, 50, 48);
	const FColor LeafColors[3] = { FColor(58, 140, 56), FColor(86, 162, 60), FColor(40, 118, 52) };
	const FColor LeafAutumn(222, 132, 46), LeafBirch(150, 198, 82);
	const FColor PineColors[2] = { FColor(34, 96, 60), FColor(48, 112, 68) };
	const FColor SnowWhite(244, 248, 252);
	const FColor BushColors[2] = { FColor(50, 120, 48), FColor(74, 142, 54) };
	const FColor Berry(204, 32, 52);
	const FColor GrassColors[2] = { FColor(92, 170, 62), FColor(124, 186, 72) };
	const FColor Stem(70, 140, 60);
	const FColor Petals[4] = { FColor(228, 60, 72), FColor(248, 214, 60), FColor(150, 92, 212), FColor(250, 250, 250) };
	const FColor FlowerHeart(232, 176, 40);
	const FColor MushStem(236, 230, 214), MushCap(206, 40, 36);
	const FColor RockColors[3] = { FColor(138, 134, 128), FColor(116, 112, 108), FColor(158, 152, 144) };
	const FColor Reed(170, 162, 98), Cattail(96, 64, 40);

	// small props are culled by distance; trees, rocks and logs are always drawn
	constexpr float NearCull = 7000.f;
	constexpr float MidCull = 14000.f;

	// basic shapes are 100 units across: these helpers take real sizes in cm
	auto Add = [this](FName Name, UStaticMesh* Mesh, const FColor& Color, bool bCollision, bool bShadows, float Cull, const FTransform& Xf)
	{
		Layer(Name, Mesh, Color, bCollision, bShadows, 0.85f, Cull).Instances.Add(Xf);
	};
	// cylinder whose axis follows Rot, centred on Center
	auto Cyl = [&](FName Name, const FColor& Color, bool bCollision, bool bShadows, float Cull, const FVector& Center, const FRotator& Rot, float Radius, float Length)
	{
		Add(Name, CylinderMesh, Color, bCollision, bShadows, Cull, FTransform(Rot, Center, FVector(Radius / 50.f, Radius / 50.f, Length / 100.f)));
	};
	// ellipsoid with the given radii
	auto Ball = [&](FName Name, const FColor& Color, bool bCollision, bool bShadows, float Cull, const FVector& Center, const FVector& Radii, const FRotator& Rot = FRotator::ZeroRotator)
	{
		Add(Name, SphereMesh, Color, bCollision, bShadows, Cull, FTransform(Rot, Center, Radii / 50.f));
	};
	// upright cone standing on BaseCenter
	auto Cone = [&](FName Name, const FColor& Color, float Cull, const FVector& BaseCenter, float Radius, float Height, float Yaw)
	{
		Add(Name, ConeMesh, Color, false, true, Cull, FTransform(FRotator(0.f, Yaw, 0.f), BaseCenter + FVector(0.f, 0.f, Height * 0.5f), FVector(Radius / 50.f, Radius / 50.f, Height / 100.f)));
	};
	auto Ground = [this](float WX, float WY)
	{
		return FVector(WX, WY, SurfaceZ(WX, WY));
	};

	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 I = Index(X, Y);
			if (!IsLand(X, Y) || ReservedTiles.Contains(I))
			{
				continue;
			}

			const EOpenWorldBiome B = Biomes[I];
			const FVector Center = TileCenter(X, Y);
			const float Alt = float(Heights[I] - WaterLevel) / float(MaxSteps - WaterLevel);
			const bool bGreen = B == EOpenWorldBiome::Grass || B == EOpenWorldBiome::Forest;

			auto RandomGround = [&](float Range)
			{
				return Ground(Center.X + Stream.FRandRange(-Range, Range) * TileSize, Center.Y + Stream.FRandRange(-Range, Range) * TileSize);
			};

			// ---- trees ----
			const float TreeChance = B == EOpenWorldBiome::Forest ? 0.22f : (B == EOpenWorldBiome::Grass ? 0.03f : (B == EOpenWorldBiome::Rock ? 0.02f : 0.f));
			if (Stream.FRand() < TreeChance)
			{
				HasTree[I] = true;
				const FVector Base = RandomGround(0.25f) - FVector(0.f, 0.f, 15.f);
				const float Yaw = Stream.FRandRange(0.f, 360.f);
				const float Kind = Stream.FRand();

				// pines take over with altitude; lowlands are mostly oaks and birches
				const float PineChance = FMath::Lerp(0.2f, 1.f, FMath::Clamp((Alt - 0.25f) / 0.3f, 0.f, 1.f));
				if (B == EOpenWorldBiome::Rock || Kind < PineChance)
				{
					// pine: dark trunk and stacked cones, snow-capped high up
					const float S = Stream.FRandRange(0.8f, 1.3f);
					const float TrunkH = 160.f * S;
					Cyl("PineTrunk", BarkDark, true, true, 0.f, Base + FVector(0.f, 0.f, TrunkH * 0.5f), FRotator::ZeroRotator, 14.f * S, TrunkH);
					const int32 Variant = Stream.RandRange(0, 1);
					const int32 Tiers = Stream.RandRange(3, 4);
					float Z = TrunkH * 0.55f;
					float R = 140.f * S;
					for (int32 T = 0; T < Tiers; ++T)
					{
						const float H = R * 1.5f;
						Cone(FName(TEXT("PineNeedles"), Variant + 1), PineColors[Variant], 0.f, Base + FVector(0.f, 0.f, Z), R, H, Yaw + T * 20.f);
						Z += H * 0.45f;
						R *= 0.74f;
					}
					if (Alt > 0.55f)
					{
						Cone("PineSnow", SnowWhite, 0.f, Base + FVector(0.f, 0.f, Z), R * 1.05f, R * 1.5f, Yaw);
					}
				}
				else if (Kind < PineChance + (1.f - PineChance) * 0.3f)
				{
					// birch: slim white trunk with dark marks, tall light-green crown
					const float S = Stream.FRandRange(0.8f, 1.2f);
					const float TrunkH = 320.f * S;
					const float R = 10.f * S;
					Cyl("BirchTrunk", BirchBark, true, true, 0.f, Base + FVector(0.f, 0.f, TrunkH * 0.5f), FRotator::ZeroRotator, R, TrunkH);
					const int32 Marks = Stream.RandRange(3, 5);
					for (int32 M = 0; M < Marks; ++M)
					{
						Cyl("BirchMark", BirchMark, false, false, NearCull, Base + FVector(0.f, 0.f, Stream.FRandRange(0.12f, 0.8f) * TrunkH), FRotator::ZeroRotator, R * 1.08f, 6.f);
					}
					for (int32 C = 0; C < 3; ++C)
					{
						const FVector P = Base + FVector(Stream.FRandRange(-30.f, 30.f) * S, Stream.FRandRange(-30.f, 30.f) * S, TrunkH * 0.78f + C * 60.f * S);
						const float K = Stream.FRandRange(0.8f, 1.1f) * S;
						Ball("LeavesBirch", LeafBirch, false, true, 0.f, P, FVector(70.f, 70.f, 110.f) * K);
					}
				}
				else
				{
					// oak: thick trunk with root flare, two branches and a clustered crown
					const float S = Stream.FRandRange(0.85f, 1.25f);
					const float TrunkH = 230.f * S;
					const float TrunkR = 20.f * S;
					Cyl("OakTrunk", Bark, true, true, 0.f, Base + FVector(0.f, 0.f, TrunkH * 0.5f), FRotator::ZeroRotator, TrunkR, TrunkH);
					Cyl("OakTrunk", Bark, true, true, 0.f, Base + FVector(0.f, 0.f, 15.f), FRotator::ZeroRotator, TrunkR * 1.5f, 30.f);
					for (int32 Br = 0; Br < 2; ++Br)
					{
						const FRotator Rot(Stream.FRandRange(35.f, 50.f), Yaw + Br * 180.f + Stream.FRandRange(-30.f, 30.f), 0.f);
						const float Len = 110.f * S;
						const FVector Dir = Rot.RotateVector(FVector::UpVector);
						Cyl("OakBranch", Bark, false, true, MidCull, Base + FVector(0.f, 0.f, TrunkH * 0.7f) + Dir * Len * 0.5f, Rot, 7.f * S, Len);
					}

					const bool bAutumn = Stream.FRand() < 0.1f;
					const int32 Variant = Stream.RandRange(0, 2);
					const FName LeafName = bAutumn ? FName("LeavesAutumn") : FName(TEXT("Leaves"), Variant + 1);
					const FColor LeafColor = bAutumn ? LeafAutumn : LeafColors[Variant];
					const float R = 150.f * S;
					const FVector Crown = Base + FVector(0.f, 0.f, TrunkH + R * 0.35f);
					Ball(LeafName, LeafColor, false, true, 0.f, Crown, FVector(R, R, R * 0.85f));
					const int32 Lobes = Stream.RandRange(3, 5);
					for (int32 L = 0; L < Lobes; ++L)
					{
						const FVector Off(Stream.FRandRange(-0.7f, 0.7f) * R, Stream.FRandRange(-0.7f, 0.7f) * R, Stream.FRandRange(-0.2f, 0.45f) * R);
						const float LR = R * Stream.FRandRange(0.5f, 0.75f);
						Ball(LeafName, LeafColor, false, true, 0.f, Crown + Off, FVector(LR, LR, LR * 0.9f));
					}
				}
				continue;
			}

			// ---- rocks: a main boulder with smaller ones and pebbles around it ----
			const float RockChance = B == EOpenWorldBiome::Rock ? 0.12f : (B == EOpenWorldBiome::Snow ? 0.05f : (B == EOpenWorldBiome::Sand ? 0.02f : 0.015f));
			if (Stream.FRand() < RockChance)
			{
				const FVector P = RandomGround(0.25f);
				const float R = Stream.FRandRange(60.f, 150.f);
				const int32 Count = Stream.RandRange(1, 3);
				for (int32 K = 0; K < Count; ++K)
				{
					const float RR = K == 0 ? R : R * Stream.FRandRange(0.4f, 0.7f);
					const FVector Pos = P + (K == 0 ? FVector::ZeroVector : FVector(Stream.FRandRange(-R, R), Stream.FRandRange(-R, R), 0.f)) + FVector(0.f, 0.f, RR * 0.25f);
					const int32 Variant = Stream.RandRange(0, 2);
					const FRotator Rot(Stream.FRandRange(-12.f, 12.f), Stream.FRandRange(0.f, 360.f), Stream.FRandRange(-12.f, 12.f));
					Ball(FName(TEXT("Rock"), Variant + 1), RockColors[Variant], true, true, K == 0 ? 0.f : MidCull, Pos,
						FVector(RR * Stream.FRandRange(1.f, 1.4f), RR, RR * Stream.FRandRange(0.55f, 0.8f)), Rot);
					if (B == EOpenWorldBiome::Snow)
					{
						Ball("RockSnow", SnowWhite, false, false, MidCull, Pos + FVector(0.f, 0.f, RR * 0.38f), FVector(RR * 0.8f, RR * 0.7f, RR * 0.3f), Rot);
					}
				}
				const int32 Pebbles = Stream.RandRange(3, 6);
				for (int32 K = 0; K < Pebbles; ++K)
				{
					const float PR = Stream.FRandRange(8.f, 18.f);
					const int32 Variant = Stream.RandRange(0, 2);
					const FVector Pos = Ground(P.X + Stream.FRandRange(-2.f, 2.f) * R, P.Y + Stream.FRandRange(-2.f, 2.f) * R);
					Ball(FName(TEXT("Pebble"), Variant + 1), RockColors[Variant], false, false, NearCull, Pos, FVector(PR * 1.3f, PR, PR * 0.6f), FRotator(0.f, Stream.FRandRange(0.f, 360.f), 0.f));
				}
			}

			// ---- forest floor: fallen logs with a stump, and mushrooms ----
			if (B == EOpenWorldBiome::Forest && Stream.FRand() < 0.012f)
			{
				const FVector P = RandomGround(0.2f);
				const float Len = Stream.FRandRange(250.f, 380.f);
				const FRotator Lying(0.f, Stream.FRandRange(0.f, 360.f), 90.f);
				Cyl("Log", Bark, true, true, MidCull, P + FVector(0.f, 0.f, 18.f), Lying, 22.f, Len);
				const FVector StumpPos = RandomGround(0.35f);
				Cyl("Stump", Bark, true, true, MidCull, StumpPos + FVector(0.f, 0.f, 18.f), FRotator::ZeroRotator, 24.f, 40.f);
				Cyl("StumpTop", StumpTop, false, false, NearCull, StumpPos + FVector(0.f, 0.f, 38.5f), FRotator::ZeroRotator, 20.f, 1.5f);
			}
			if (B == EOpenWorldBiome::Forest && Stream.FRand() < 0.05f)
			{
				const int32 Count = Stream.RandRange(1, 3);
				for (int32 K = 0; K < Count; ++K)
				{
					const FVector P = RandomGround(0.4f);
					const float H = Stream.FRandRange(14.f, 24.f);
					Cyl("MushroomStem", MushStem, false, false, NearCull, P + FVector(0.f, 0.f, H * 0.5f), FRotator::ZeroRotator, 3.5f, H);
					Ball("MushroomCap", MushCap, false, false, NearCull, P + FVector(0.f, 0.f, H), FVector(12.f, 12.f, 7.f));
					for (int32 D = 0; D < 3; ++D)
					{
						Ball("MushroomDot", SnowWhite, false, false, NearCull, P + FVector(Stream.FRandRange(-6.f, 6.f), Stream.FRandRange(-6.f, 6.f), H + 5.5f), FVector(2.f));
					}
				}
			}

			// ---- bushes, some with berries ----
			if (bGreen && Stream.FRand() < (B == EOpenWorldBiome::Forest ? 0.12f : 0.06f))
			{
				const FVector P = RandomGround(0.35f);
				const float R = Stream.FRandRange(45.f, 75.f);
				const int32 Variant = Stream.RandRange(0, 1);
				for (int32 K = 0; K < 3; ++K)
				{
					const FVector Off(Stream.FRandRange(-0.6f, 0.6f) * R, Stream.FRandRange(-0.6f, 0.6f) * R, R * 0.35f);
					Ball(FName(TEXT("Bush"), Variant + 1), BushColors[Variant], false, true, MidCull, P + Off, FVector(R, R, R * 0.8f));
				}
				if (Stream.FRand() < 0.3f)
				{
					const int32 Berries = Stream.RandRange(4, 7);
					for (int32 K = 0; K < Berries; ++K)
					{
						const FVector Off(Stream.FRandRange(-0.9f, 0.9f) * R, Stream.FRandRange(-0.9f, 0.9f) * R, Stream.FRandRange(0.6f, 1.05f) * R);
						Ball("Berries", Berry, false, false, NearCull, P + Off, FVector(6.f));
					}
				}
			}

			// ---- grass tufts: a few thin tilted blades ----
			const float TuftChance = bGreen ? 0.8f : (B == EOpenWorldBiome::Rock ? 0.1f : 0.f);
			const int32 Tufts = bGreen ? Stream.RandRange(1, 3) : 1;
			for (int32 Tuft = 0; Tuft < Tufts; ++Tuft)
			{
				if (Stream.FRand() >= TuftChance)
				{
					continue;
				}
				const FVector P = RandomGround(0.45f) - FVector(0.f, 0.f, 4.f);
				const int32 Variant = Stream.RandRange(0, 1);
				const int32 Blades = Stream.RandRange(4, 6);
				for (int32 K = 0; K < Blades; ++K)
				{
					const float H = Stream.FRandRange(30.f, 55.f);
					const FRotator Rot(Stream.FRandRange(-18.f, 18.f), Stream.FRandRange(0.f, 360.f), Stream.FRandRange(-18.f, 18.f));
					const FVector BladeBase = P + FVector(Stream.FRandRange(-18.f, 18.f), Stream.FRandRange(-18.f, 18.f), 0.f);
					Add(FName(TEXT("GrassBlade"), Variant + 1), ConeMesh, GrassColors[Variant], false, false, NearCull,
						FTransform(Rot, BladeBase + Rot.RotateVector(FVector(0.f, 0.f, H * 0.5f)), FVector(0.07f, 0.07f, H / 100.f)));
				}
			}

			// ---- flowers on stems ----
			if (bGreen && Stream.FRand() < (B == EOpenWorldBiome::Grass ? 0.1f : 0.04f))
			{
				const int32 Colour = Stream.RandRange(0, 3);
				const int32 Count = Stream.RandRange(3, 6);
				for (int32 K = 0; K < Count; ++K)
				{
					const FVector P = RandomGround(0.4f);
					const float H = Stream.FRandRange(25.f, 45.f);
					Cyl("FlowerStem", Stem, false, false, NearCull, P + FVector(0.f, 0.f, H * 0.5f), FRotator::ZeroRotator, 1.5f, H);
					Ball(FName(TEXT("FlowerPetals"), Colour + 1), Petals[Colour], false, false, NearCull, P + FVector(0.f, 0.f, H), FVector(9.f, 9.f, 4.f));
					Ball("FlowerHeart", FlowerHeart, false, false, NearCull, P + FVector(0.f, 0.f, H + 3.f), FVector(3.5f));
				}
			}

			// ---- reeds and cattails along the shore ----
			if (B == EOpenWorldBiome::Sand && Stream.FRand() < 0.15f)
			{
				bool bShore = false;
				const FIntPoint Dirs[4] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
				for (const FIntPoint& D : Dirs)
				{
					const int32 NX = X + D.X, NY = Y + D.Y;
					bShore |= NX >= 0 && NY >= 0 && NX < GridSize && NY < GridSize && !IsLand(NX, NY);
				}
				if (bShore)
				{
					const int32 Stalks = Stream.RandRange(5, 9);
					for (int32 K = 0; K < Stalks; ++K)
					{
						const FVector P = RandomGround(0.4f);
						const float H = Stream.FRandRange(70.f, 130.f);
						const FRotator Rot(Stream.FRandRange(-10.f, 10.f), Stream.FRandRange(0.f, 360.f), Stream.FRandRange(-10.f, 10.f));
						const bool bGreenStalk = Stream.FRand() < 0.5f;
						Cyl(bGreenStalk ? FName("ReedGreen") : FName("ReedDry"), bGreenStalk ? Stem : Reed, false, false, NearCull,
							P + Rot.RotateVector(FVector(0.f, 0.f, H * 0.5f)), Rot, 2.f, H);
						if (Stream.FRand() < 0.4f)
						{
							Ball("Cattail", Cattail, false, false, NearCull, P + Rot.RotateVector(FVector(0.f, 0.f, H * 0.85f)), FVector(4.f, 4.f, 12.f), Rot);
						}
					}
				}
			}
		}
	}
}

void AOpenWorldGenerator::BuildClouds(FRandomStream& Stream)
{
	const float Half = GetHalfExtent() * 1.3f;

	if (TerrainStyle == EOpenWorldTerrainStyle::Smooth)
	{
		// fluffy clouds: a flat-bottomed cluster of overlapping spheres
		for (int32 C = 0; C < 30; ++C)
		{
			const FVector Base(Stream.FRandRange(-Half, Half), Stream.FRandRange(-Half, Half), Stream.FRandRange(5200.f, 6400.f));
			const float Size = Stream.FRandRange(0.8f, 1.4f);
			const int32 Puffs = Stream.RandRange(5, 9);
			for (int32 P = 0; P < Puffs; ++P)
			{
				const float R = Stream.FRandRange(250.f, 450.f) * Size;
				const FVector Pos = Base + FVector(Stream.FRandRange(-700.f, 700.f) * Size, Stream.FRandRange(-400.f, 400.f) * Size, Stream.FRandRange(0.f, 0.5f) * R);
				Layer("CloudPuff", SphereMesh, OpenWorldColors::Cloud, false, false, 1.f).Instances.Add(
					FTransform(FRotator::ZeroRotator, Pos, FVector(R / 50.f, R / 50.f, R / 50.f * 0.6f)));
			}
		}
		return;
	}

	for (int32 C = 0; C < 36; ++C)
	{
		const FVector Base(Stream.FRandRange(-Half, Half), Stream.FRandRange(-Half, Half), Stream.FRandRange(4000.f, 5200.f));
		const int32 Puffs = Stream.RandRange(2, 4);
		for (int32 P = 0; P < Puffs; ++P)
		{
			const FVector Pos = Base + FVector(Stream.FRandRange(-600.f, 600.f), Stream.FRandRange(-400.f, 400.f), Stream.FRandRange(-60.f, 60.f));
			Layer("Cloud", CubeMesh, OpenWorldColors::Cloud, false, false, 1.f).Instances.Add(
				FTransform(FRotator::ZeroRotator, Pos, FVector(Stream.FRandRange(6.f, 12.f), Stream.FRandRange(4.f, 8.f), Stream.FRandRange(1.2f, 2.2f))));
		}
	}
}

void AOpenWorldGenerator::FlushLayers()
{
	for (TPair<FName, FLayerBuild>& Pair : PendingLayers)
	{
		FLayerBuild& L = Pair.Value;
		if (L.Instances.Num() == 0 || !L.Mesh)
		{
			continue;
		}

		UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this, Pair.Key);
		ISM->SetupAttachment(Root);
		ISM->SetStaticMesh(L.Mesh);
		ISM->SetMobility(EComponentMobility::Static);
		ISM->SetCastShadow(L.bShadows);
		if (L.CullDistance > 0.f)
		{
			// small props fade out with distance to keep the instance count on screen low
			ISM->SetCullDistances(int32(L.CullDistance * 0.8f), int32(L.CullDistance));
		}
		if (L.bCollision)
		{
			ISM->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		}
		else
		{
			ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}

		if (BaseMaterial)
		{
			UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(BaseMaterial, this);
			Mat->SetVectorParameterValue(TEXT("Color"), L.Color);
			Mat->SetScalarParameterValue(TEXT("Roughness"), L.Roughness);
			ISM->SetMaterial(0, Mat);
		}

		ISM->RegisterComponent();
		ISM->AddInstances(L.Instances, false, true);
		AddInstanceComponent(ISM);
		MeshLayers.Add(ISM);
	}
	PendingLayers.Empty();
}

void AOpenWorldGenerator::BuildMapTexture()
{
	MapTexture = UTexture2D::CreateTransient(GridSize, GridSize, PF_B8G8R8A8);
	if (!MapTexture)
	{
		return;
	}
	MapTexture->Filter = TF_Nearest;
	MapTexture->SRGB = true;

	FTexture2DMipMap& Mip = MapTexture->GetPlatformData()->Mips[0];
	FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));

	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 I = Index(X, Y);
			const float Alt = float(Heights[I]) / float(MaxSteps);
			// the minimap shows water depth instead of the seabed colour
			FLinearColor C = Biomes[I] == EOpenWorldBiome::Seabed
				? FLinearColor::LerpUsingHSV(FLinearColor(OpenWorldColors::DeepWater), FLinearColor(OpenWorldColors::Water), float(Heights[I]) / float(FMath::Max(WaterLevel, 1)))
				: FLinearColor(BiomeColor(Biomes[I]));
			if (Biomes[I] != EOpenWorldBiome::Seabed)
			{
				C *= 0.75f + 0.35f * Alt;
				if (HasTree[I])
				{
					C *= 0.7f;
				}
			}
			C.A = 1.f;

			// texture x = world +Y (right), texture y = world -X (north is up)
			const int32 TexX = Y;
			const int32 TexY = GridSize - 1 - X;
			Pixels[TexY * GridSize + TexX] = C.ToFColor(true);
		}
	}

	Mip.BulkData.Unlock();
	MapTexture->UpdateResource();
}
