// Canvas HUD for the random open world game.

#include "OpenWorldHUD.h"
#include "OpenWorldGameMode.h"
#include "OpenWorldGenerator.h"
#include "OpenWorldCrystal.h"
#include "OpenWorldAnimal.h"
#include "OpenWorldBow.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

namespace
{
	const FLinearColor CrystalBlue(0.25f, 0.85f, 1.f);

	FString CompassName(const FVector& Delta)
	{
		// +X is north, +Y is east
		static const TCHAR* Names[8] = { TEXT("N"), TEXT("NE"), TEXT("E"), TEXT("SE"), TEXT("S"), TEXT("SW"), TEXT("W"), TEXT("NW") };
		float Angle = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
		if (Angle < 0.f)
		{
			Angle += 360.f;
		}
		return Names[FMath::RoundToInt(Angle / 45.f) % 8];
	}
}

void AOpenWorldHUD::DrawShadowedText(const FString& Text, float X, float Y, const FLinearColor& Color, UFont* Font, float Scale)
{
	DrawText(Text, FLinearColor(0.f, 0.f, 0.f, 0.75f), X + 2.f * Scale, Y + 2.f * Scale, Font, Scale);
	DrawText(Text, Color, X, Y, Font, Scale);
}

void AOpenWorldHUD::DrawCenteredText(const FString& Text, float Y, const FLinearColor& Color, UFont* Font, float Scale)
{
	float W = 0.f, H = 0.f;
	GetTextSize(Text, W, H, Font, Scale);
	DrawShadowedText(Text, (Canvas->ClipX - W) * 0.5f, Y, Color, Font, Scale);
}

void AOpenWorldHUD::DrawHUD()
{
	Super::DrawHUD();

	const AOpenWorldGameMode* GameMode = GetWorld()->GetAuthGameMode<AOpenWorldGameMode>();
	if (!GameMode || !GameMode->GetGenerator() || !Canvas)
	{
		return;
	}

	const float UIScale = FMath::Max(Canvas->ClipY / 1080.f, 0.5f);
	UFont* Large = GEngine->GetLargeFont();
	UFont* Medium = GEngine->GetMediumFont();
	UFont* Small = GEngine->GetSmallFont();
	const float Pad = 24.f * UIScale;

	// objective panel
	const int32 Secs = FMath::FloorToInt(GameMode->GetElapsedTime());
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.35f), Pad - 10.f * UIScale, Pad - 8.f * UIScale, 420.f * UIScale, 200.f * UIScale);
	DrawShadowedText(FString::Printf(TEXT("Crystals  %d / %d"), GameMode->GetCollected(), GameMode->GetTotal()), Pad, Pad, CrystalBlue, Large, 1.4f * UIScale);
	const float Clock = GameMode->GetTimeOfDay();
	const int32 ClockMinutes = FMath::FloorToInt(Clock * 60.f);
	DrawShadowedText(FString::Printf(TEXT("Time  %d:%02d      Day %d  %02d:%02d %s"), Secs / 60, Secs % 60,
		GameMode->GetDay(), ClockMinutes / 60, ClockMinutes % 60, GameMode->GetDaylight() > 0.5f ? TEXT("day") : TEXT("night")),
		Pad, Pad + 44.f * UIScale, FLinearColor::White, Medium, 1.3f * UIScale);
	const bool bSmooth = GameMode->GetGenerator()->GetTerrainStyle() == EOpenWorldTerrainStyle::Smooth;
	DrawShadowedText(FString::Printf(TEXT("World seed  %d   Terrain  %s"), GameMode->GetGenerator()->GetSeed(), bSmooth ? TEXT("Smooth") : TEXT("Blocky")),
		Pad, Pad + 78.f * UIScale, FLinearColor(0.8f, 0.8f, 0.8f), Small, 1.2f * UIScale);

	// hunting log
	const AOpenWorldBow* Bow = GameMode->GetBow();
	DrawShadowedText(FString::Printf(TEXT("Hunt  deer %d  boar %d  rabbit %d   meat %.1f kg"),
		GameMode->GetKills(EOpenWorldAnimalKind::Deer), GameMode->GetKills(EOpenWorldAnimalKind::Boar), GameMode->GetKills(EOpenWorldAnimalKind::Rabbit), GameMode->GetMeatKg()),
		Pad, Pad + 108.f * UIScale, FLinearColor(0.95f, 0.75f, 0.5f), Small, 1.2f * UIScale);
	if (Bow)
	{
		DrawShadowedText(FString::Printf(TEXT("Arrows  %d / %d"), Bow->GetArrows(), Bow->GetMaxArrows()),
			Pad, Pad + 134.f * UIScale, Bow->GetArrows() > 0 ? FLinearColor(0.95f, 0.75f, 0.5f) : FLinearColor(1.f, 0.35f, 0.3f), Small, 1.2f * UIScale);
	}

	// hint towards the nearest crystal
	const APawn* Pawn = PlayerOwner ? PlayerOwner->GetPawn() : nullptr;
	if (Pawn && !GameMode->HasWon())
	{
		float BestDist = TNumericLimits<float>::Max();
		FVector BestDelta = FVector::ZeroVector;
		for (const AOpenWorldCrystal* Crystal : GameMode->GetGenerator()->GetCrystals())
		{
			if (IsValid(Crystal))
			{
				const FVector Delta = Crystal->GetActorLocation() - Pawn->GetActorLocation();
				const float Dist = Delta.Size2D();
				if (Dist < BestDist)
				{
					BestDist = Dist;
					BestDelta = Delta;
				}
			}
		}
		if (BestDist < TNumericLimits<float>::Max())
		{
			DrawShadowedText(FString::Printf(TEXT("Nearest crystal: %d m %s"), FMath::RoundToInt(BestDist / 100.f), *CompassName(BestDelta)),
				Pad, Pad + 160.f * UIScale, FLinearColor(1.f, 0.95f, 0.6f), Medium, 1.2f * UIScale);
		}
	}

	// status message
	const FString Message = GameMode->GetMessage();
	if (!Message.IsEmpty())
	{
		DrawCenteredText(Message, Canvas->ClipY * 0.22f, GameMode->HasWon() ? FLinearColor(1.f, 0.85f, 0.2f) : FLinearColor::White, Large, 1.6f * UIScale);
	}

	// controls
	DrawShadowedText(TEXT("RMB aim   LMB hold to draw, release to shoot   Shift sprint   C sneak   E field dress"),
		Pad, Canvas->ClipY - Pad - 44.f * UIScale, FLinearColor(1.f, 1.f, 1.f, 0.8f), Small, 1.2f * UIScale);
	DrawShadowedText(TEXT("WASD move   Space jump   Mouse look   R return to start   N new world   T blocky/smooth"),
		Pad, Canvas->ClipY - Pad - 20.f * UIScale, FLinearColor(1.f, 1.f, 1.f, 0.8f), Small, 1.2f * UIScale);

	DrawVitals(UIScale);
	DrawCrosshair(UIScale);

	if (const AOpenWorldAnimal* Carcass = GameMode->GetHarvestTarget())
	{
		DrawCenteredText(FString::Printf(TEXT("E  Field dress the %s (%.1f kg)"), *AOpenWorldAnimal::GetKindName(Carcass->GetKind()).ToLower(), Carcass->GetMeatKg()),
			Canvas->ClipY * 0.62f, FLinearColor(1.f, 0.9f, 0.6f), Medium, 1.4f * UIScale);
	}

	DrawMinimap(UIScale);
}

void AOpenWorldHUD::DrawMinimap(float UIScale)
{
	const AOpenWorldGameMode* GameMode = GetWorld()->GetAuthGameMode<AOpenWorldGameMode>();
	const AOpenWorldGenerator* Generator = GameMode ? GameMode->GetGenerator() : nullptr;
	UTexture2D* MapTexture = Generator ? Generator->GetMapTexture() : nullptr;
	if (!MapTexture)
	{
		return;
	}

	const float Size = 260.f * UIScale;
	const float Pad = 24.f * UIScale;
	const float X0 = Canvas->ClipX - Size - Pad;
	const float Y0 = Pad;
	const float Border = 4.f * UIScale;

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), X0 - Border, Y0 - Border, Size + Border * 2.f, Size + Border * 2.f);
	DrawTexture(MapTexture, X0, Y0, Size, Size, 0.f, 0.f, 1.f, 1.f);

	auto ToScreen = [&](const FVector& World)
	{
		const FVector2D UV = Generator->WorldToMap(World);
		return FVector2D(X0 + FMath::Clamp(UV.X, 0.f, 1.f) * Size, Y0 + FMath::Clamp(UV.Y, 0.f, 1.f) * Size);
	};

	// crystals
	const float Dot = 8.f * UIScale;
	for (const AOpenWorldCrystal* Crystal : Generator->GetCrystals())
	{
		if (IsValid(Crystal))
		{
			const FVector2D P = ToScreen(Crystal->GetActorLocation());
			DrawRect(FLinearColor::Black, P.X - Dot * 0.5f - 1.f, P.Y - Dot * 0.5f - 1.f, Dot + 2.f, Dot + 2.f);
			DrawRect(CrystalBlue, P.X - Dot * 0.5f, P.Y - Dot * 0.5f, Dot, Dot);
		}
	}

	// player position + view direction
	if (const APawn* Pawn = PlayerOwner ? PlayerOwner->GetPawn() : nullptr)
	{
		const FVector2D P = ToScreen(Pawn->GetActorLocation());
		const float Yaw = FMath::DegreesToRadians(PlayerOwner->GetControlRotation().Yaw);
		// world forward (cos, sin) -> map (right = +Y, down = -X)
		const FVector2D Dir(FMath::Sin(Yaw), -FMath::Cos(Yaw));
		const float Len = 18.f * UIScale;
		DrawLine(P.X, P.Y, P.X + Dir.X * Len, P.Y + Dir.Y * Len, FLinearColor::Red, 3.f * UIScale);
		DrawRect(FLinearColor::Black, P.X - 6.f * UIScale, P.Y - 6.f * UIScale, 12.f * UIScale, 12.f * UIScale);
		DrawRect(FLinearColor(1.f, 0.2f, 0.2f), P.X - 4.f * UIScale, P.Y - 4.f * UIScale, 8.f * UIScale, 8.f * UIScale);
	}

	DrawShadowedText(TEXT("N"), X0 + Size * 0.5f - 5.f * UIScale, Y0 + 2.f * UIScale, FLinearColor::White, GEngine->GetSmallFont(), 1.2f * UIScale);
}

void AOpenWorldHUD::DrawVitals(float UIScale)
{
	const AOpenWorldGameMode* GameMode = GetWorld()->GetAuthGameMode<AOpenWorldGameMode>();
	if (!GameMode)
	{
		return;
	}

	const float Pad = 24.f * UIScale;
	const float W = 260.f * UIScale;
	const float H = 12.f * UIScale;
	const float X = Pad;
	float Y = Canvas->ClipY - Pad - 100.f * UIScale;

	auto Bar = [&](float Fraction, const FLinearColor& Color, const TCHAR* Label)
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), X - 2.f * UIScale, Y - 2.f * UIScale, W + 4.f * UIScale, H + 4.f * UIScale);
		DrawRect(Color, X, Y, W * FMath::Clamp(Fraction, 0.f, 1.f), H);
		DrawShadowedText(Label, X + W + 10.f * UIScale, Y - 4.f * UIScale, FLinearColor(1.f, 1.f, 1.f, 0.85f), GEngine->GetSmallFont(), 1.1f * UIScale);
		Y += H + 10.f * UIScale;
	};

	const float Health = GameMode->GetHealth() / 100.f;
	Bar(Health, Health > 0.3f ? FLinearColor(0.85f, 0.15f, 0.15f) : FLinearColor(1.f, 0.4f, 0.1f), TEXT("Health"));
	Bar(GameMode->GetStamina() / 100.f, GameMode->IsExhausted() ? FLinearColor(0.6f, 0.5f, 0.2f) : FLinearColor(0.95f, 0.8f, 0.2f),
		GameMode->IsSneaking() ? TEXT("Stamina  (sneaking)") : TEXT("Stamina"));
}

void AOpenWorldHUD::DrawCrosshair(float UIScale)
{
	const AOpenWorldGameMode* GameMode = GetWorld()->GetAuthGameMode<AOpenWorldGameMode>();
	const AOpenWorldBow* Bow = GameMode ? GameMode->GetBow() : nullptr;
	if (!Bow || !Bow->IsAiming())
	{
		return;
	}

	// four ticks that close in as the bow is drawn
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;
	const float Draw = Bow->GetDraw();
	const float Gap = FMath::Lerp(22.f, 5.f, Draw) * UIScale;
	const float Len = 10.f * UIScale;
	const float Thick = 2.f * UIScale;
	const FLinearColor Color = Draw >= 1.f ? FLinearColor(1.f, 0.85f, 0.2f) : FLinearColor(1.f, 1.f, 1.f, 0.9f);
	const FLinearColor Shadow(0.f, 0.f, 0.f, 0.6f);
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const FLinearColor& C = Pass == 0 ? Shadow : Color;
		const float O = Pass == 0 ? 1.5f * UIScale : 0.f;
		DrawLine(CX - Gap - Len + O, CY + O, CX - Gap + O, CY + O, C, Thick);
		DrawLine(CX + Gap + O, CY + O, CX + Gap + Len + O, CY + O, C, Thick);
		DrawLine(CX + O, CY - Gap - Len + O, CX + O, CY - Gap + O, C, Thick);
		DrawLine(CX + O, CY + Gap + O, CX + O, CY + Gap + Len + O, C, Thick);
	}
	DrawRect(Color, CX - 1.5f * UIScale, CY - 1.5f * UIScale, 3.f * UIScale, 3.f * UIScale);

	// draw strength meter under the crosshair
	if (Bow->IsDrawing())
	{
		const float W = 80.f * UIScale;
		const float Y = CY + 40.f * UIScale;
		DrawRect(Shadow, CX - W * 0.5f - 2.f, Y - 2.f, W + 4.f, 6.f * UIScale + 4.f);
		DrawRect(Color, CX - W * 0.5f, Y, W * Draw, 6.f * UIScale);
	}
}
