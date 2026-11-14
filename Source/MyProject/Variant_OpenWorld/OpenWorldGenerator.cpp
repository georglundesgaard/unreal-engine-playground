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

	CubeMesh = CubeFinder.Object;
	ConeMesh = ConeFinder.Object;
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

AOpenWorldGenerator::FLayerBuild& AOpenWorldGenerator::Layer(FName Name, UStaticMesh* Mesh, const FColor& SRGB, bool bCollision, bool bShadows, float Roughness)
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
	BuildTerrain();
	PickSpawnAndCrystals(Stream);
	BuildDecorations(Stream);
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
			case EOpenWorldBiome::Seabed: Layer("Seabed", CubeMesh, OpenWorldColors::Seabed, true, false).Instances.Add(Xf); break;
			case EOpenWorldBiome::Sand:   Layer("Sand", CubeMesh, OpenWorldColors::Sand, true).Instances.Add(Xf); break;
			case EOpenWorldBiome::Grass:  Layer("Grass", CubeMesh, OpenWorldColors::Grass, true).Instances.Add(Xf); break;
			case EOpenWorldBiome::Forest: Layer("Forest", CubeMesh, OpenWorldColors::Forest, true).Instances.Add(Xf); break;
			case EOpenWorldBiome::Rock:   Layer("Rock", CubeMesh, OpenWorldColors::Rock, true).Instances.Add(Xf); break;
			case EOpenWorldBiome::Snow:   Layer("Snow", CubeMesh, OpenWorldColors::Snow, true, true, 0.5f).Instances.Add(Xf); break;
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
	SpawnLocation.Z = TileTop(SpawnTile.X, SpawnTile.Y) + 120.f;

	// keep the spawn area clear of trees
	for (int32 DY = -2; DY <= 2; ++DY)
	{
		for (int32 DX = -2; DX <= 2; ++DX)
		{
			ReservedTiles.Add(Index(FMath::Clamp(SpawnTile.X + DX, 0, GridSize - 1), FMath::Clamp(SpawnTile.Y + DY, 0, GridSize - 1)));
		}
	}

	// crystals: spread out over the island, away from the spawn
	TArray<FIntPoint> Candidates;
	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			if (IsLand(X, Y) && Biomes[Index(X, Y)] != EOpenWorldBiome::Sand)
			{
				Candidates.Add(FIntPoint(X, Y));
			}
		}
	}
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
		Loc.Z = TileTop(C.X, C.Y) + 110.f;
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
			const float Top = TileTop(X, Y);
			const FVector Center = TileCenter(X, Y);
			const float Alt = float(Heights[I] - WaterLevel) / float(MaxSteps - WaterLevel);

			// trees
			const float TreeChance = B == EOpenWorldBiome::Forest ? 0.2f : (B == EOpenWorldBiome::Grass ? 0.025f : 0.f);
			if (Stream.FRand() < TreeChance)
			{
				HasTree[I] = true;
				const FVector Base = Center + FVector(Stream.FRandRange(-0.25f, 0.25f) * TileSize, Stream.FRandRange(-0.25f, 0.25f) * TileSize, Top);
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
				const FVector Pos = Center + FVector(Stream.FRandRange(-0.2f, 0.2f) * TileSize, Stream.FRandRange(-0.2f, 0.2f) * TileSize, Top + S * 30.f);
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
					const FVector Pos = Center + FVector(Stream.FRandRange(-0.4f, 0.4f) * TileSize, Stream.FRandRange(-0.4f, 0.4f) * TileSize, Top + 8.f);
					Layer(bRed ? "FlowerRed" : "FlowerYellow", CubeMesh, bRed ? OpenWorldColors::FlowerRed : OpenWorldColors::FlowerYellow, false, false).Instances.Add(
						FTransform(FRotator(0, Stream.FRandRange(0.f, 90.f), 0), Pos, FVector(0.14f)));
				}
			}
		}
	}
}

void AOpenWorldGenerator::BuildClouds(FRandomStream& Stream)
{
	const float Half = GetHalfExtent() * 1.3f;
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
			FLinearColor C;
			switch (Biomes[I])
			{
			case EOpenWorldBiome::Seabed:
				C = FLinearColor::LerpUsingHSV(FLinearColor(OpenWorldColors::DeepWater), FLinearColor(OpenWorldColors::Water), float(Heights[I]) / float(FMath::Max(WaterLevel, 1)));
				break;
			case EOpenWorldBiome::Sand:   C = FLinearColor(OpenWorldColors::Sand); break;
			case EOpenWorldBiome::Grass:  C = FLinearColor(OpenWorldColors::Grass); break;
			case EOpenWorldBiome::Forest: C = FLinearColor(OpenWorldColors::Forest); break;
			case EOpenWorldBiome::Rock:   C = FLinearColor(OpenWorldColors::Rock); break;
			default:                      C = FLinearColor(OpenWorldColors::Snow); break;
			}
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
