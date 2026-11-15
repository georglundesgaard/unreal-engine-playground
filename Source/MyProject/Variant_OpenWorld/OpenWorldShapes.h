// Small helpers for building props out of the engine's basic shapes at runtime.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace OpenWorldShapes
{
	enum class EShape : uint8
	{
		Cube,
		Sphere,
		Cylinder,
		Cone
	};

	/** Engine basic shape meshes are 100 units across */
	inline UStaticMesh* GetMesh(EShape Shape)
	{
		static const TCHAR* Paths[] = {
			TEXT("/Engine/BasicShapes/Cube.Cube"),
			TEXT("/Engine/BasicShapes/Sphere.Sphere"),
			TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
			TEXT("/Engine/BasicShapes/Cone.Cone")
		};
		return LoadObject<UStaticMesh>(nullptr, Paths[static_cast<int32>(Shape)]);
	}

	inline UMaterialInterface* GetBaseMaterial()
	{
		return LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}

	/** Coloured instance of BasicShapeMaterial. Colour is sRGB. */
	inline UMaterialInstanceDynamic* MakeColorMaterial(UObject* Outer, const FColor& SRGB, float Roughness = 0.85f)
	{
		UMaterialInterface* Base = GetBaseMaterial();
		if (!Base)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(Base, Outer);
		Mat->SetVectorParameterValue(TEXT("Color"), FLinearColor::FromSRGBColor(SRGB));
		Mat->SetScalarParameterValue(TEXT("Roughness"), Roughness);
		return Mat;
	}

	/** Places a cylinder (axis = local Z) so it spans A to B in its parent's space */
	inline void SetCylinderBetween(UStaticMeshComponent* Cylinder, const FVector& A, const FVector& B, float Radius)
	{
		const FVector Axis = B - A;
		const float Length = FMath::Max(Axis.Size(), 0.1f);
		Cylinder->SetRelativeLocationAndRotation((A + B) * 0.5f, FRotationMatrix::MakeFromZ(Axis).Rotator());
		Cylinder->SetRelativeScale3D(FVector(Radius / 50.f, Radius / 50.f, Length / 100.f));
	}

	/**
	 *  Adds a coloured basic shape to Owner at runtime, attached to Parent.
	 *  SizeCm is the full size along each local axis. Materials are shared per colour through Cache.
	 *  Parts have no collision unless the caller enables it.
	 */
	inline UStaticMeshComponent* AddPart(AActor* Owner, USceneComponent* Parent, EShape Shape, const FColor& SRGB,
		const FVector& Location, const FRotator& Rotation, const FVector& SizeCm,
		TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>>& Cache, bool bCastShadow = true)
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

		TObjectPtr<UMaterialInstanceDynamic>& Mat = Cache.FindOrAdd(SRGB.DWColor());
		if (!Mat)
		{
			Mat = MakeColorMaterial(Owner, SRGB);
		}
		if (Mat)
		{
			Part->SetMaterial(0, Mat);
		}

		Part->RegisterComponent();
		Owner->AddInstanceComponent(Part);
		return Part;
	}
}
