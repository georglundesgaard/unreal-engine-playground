// Shared helpers for building props out of the engine's basic shapes at runtime.

#include "OpenWorldShapes.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

void OpenWorldShapes::SetCylinderBetween(UStaticMeshComponent* Cylinder, const FVector& A, const FVector& B, float Radius)
{
	const FVector Axis = B - A;
	const float Length = FMath::Max(Axis.Size(), 0.1f);
	Cylinder->SetRelativeLocationAndRotation((A + B) * 0.5f, FRotationMatrix::MakeFromZ(Axis).Rotator());
	Cylinder->SetRelativeScale3D(FVector(Radius / 50.f, Radius / 50.f, Length / 100.f));
}

void UOpenWorldShapeLibrary::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	static const TCHAR* Paths[] = {
		TEXT("/Engine/BasicShapes/Cube.Cube"),
		TEXT("/Engine/BasicShapes/Sphere.Sphere"),
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
		TEXT("/Engine/BasicShapes/Cone.Cone")
	};
	static_assert(UE_ARRAY_COUNT(Paths) == static_cast<int32>(OpenWorldShapes::EShape::Count), "one path per shape");
	for (const TCHAR* Path : Paths)
	{
		Meshes.Add(LoadObject<UStaticMesh>(nullptr, Path));
	}
	BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
}

UStaticMesh* UOpenWorldShapeLibrary::GetMesh(OpenWorldShapes::EShape Shape) const
{
	const int32 Index = static_cast<int32>(Shape);
	return Meshes.IsValidIndex(Index) ? Meshes[Index].Get() : nullptr;
}

UMaterialInstanceDynamic* UOpenWorldShapeLibrary::GetColorMaterial(const FColor& SRGB, float Roughness)
{
	if (!BaseMaterial)
	{
		return nullptr;
	}
	const uint64 Key = (uint64(SRGB.DWColor()) << 32) | uint64(FMath::RoundToInt(FMath::Clamp(Roughness, 0.f, 1.f) * 1000.f));
	TObjectPtr<UMaterialInstanceDynamic>& Mat = ColorMaterials.FindOrAdd(Key);
	if (!Mat)
	{
		Mat = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		Mat->SetVectorParameterValue(TEXT("Color"), FLinearColor::FromSRGBColor(SRGB));
		Mat->SetScalarParameterValue(TEXT("Roughness"), Roughness);
	}
	return Mat;
}

UStaticMeshComponent* UOpenWorldShapeLibrary::AddPart(AActor* Owner, USceneComponent* Parent, OpenWorldShapes::EShape Shape, const FColor& SRGB,
	const FVector& Location, const FRotator& Rotation, const FVector& SizeCm, bool bCastShadow)
{
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner);
	Part->SetMobility(EComponentMobility::Movable);
	Part->SetStaticMesh(GetMesh(Shape));
	Part->SetupAttachment(Parent);
	Part->SetRelativeLocationAndRotation(Location, Rotation);
	Part->SetRelativeScale3D(SizeCm / 100.f);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetGenerateOverlapEvents(false);
	Part->SetCanEverAffectNavigation(false);
	Part->SetCastShadow(bCastShadow);
	if (UMaterialInstanceDynamic* Mat = GetColorMaterial(SRGB))
	{
		Part->SetMaterial(0, Mat);
	}
	Part->RegisterComponent();
	Owner->AddInstanceComponent(Part);
	return Part;
}
