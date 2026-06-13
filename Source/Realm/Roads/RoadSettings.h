// Copyright Asamoto.
// All road tunables in one UDeveloperSettings:
// editable under Project Settings -> Game -> Realm Roads, saved to
// DefaultGame.ini. Distances are centimeters (project convention); the spec's
// meter values are converted here.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RoadSettings.generated.h"

class UMaterialInterface;

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Realm Roads"))
class REALM_API URoadSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// --- Placement / snapping ---

	// Snap-to-node and snap-onto-edge radius (spec: 2.5 m).
	UPROPERTY(EditAnywhere, config, Category = "Placement", meta = (ClampMin = "0"))
	float SnapRadius = 250.f;

	// Default ribbon width (spec: 3 m).
	UPROPERTY(EditAnywhere, config, Category = "Placement", meta = (ClampMin = "10"))
	float DefaultWidth = 300.f;

	// Reject segments shorter than this (spec: 1.5 m) — avoids degenerate ribbons.
	UPROPERTY(EditAnywhere, config, Category = "Placement", meta = (ClampMin = "0"))
	float MinSegmentLength = 150.f;

	// Max slope between consecutive polyline samples before a segment turns
	// red and blocks commit.
	UPROPERTY(EditAnywhere, config, Category = "Placement", meta = (ClampMin = "1", ClampMax = "89"))
	float MaxSlopeDegrees = 35.f;

	// Shift-held angle snapping increment, relative to the previous segment.
	UPROPERTY(EditAnywhere, config, Category = "Placement", meta = (ClampMin = "1"))
	float AngleSnapDegrees = 15.f;

	// Ctrl + mouse wheel curvature step (curvature clamps to [0..1]).
	UPROPERTY(EditAnywhere, config, Category = "Placement", meta = (ClampMin = "0.01"))
	float CurvatureStep = 0.1f;

	// --- Snapping & Placement (road_snapping_todos.md §7) ---

	// Master switch for both snap directions (building→road and road→building).
	UPROPERTY(EditAnywhere, config, Category = "Snapping & Placement")
	bool bPlacementSnappingEnabled = true;

	// Activation distance: cursor↔road for the building snap, point↔wall for the
	// road snap.
	UPROPERTY(EditAnywhere, config, Category = "Snapping & Placement", meta = (ClampMin = "0"))
	float SnapTriggerRadiusCm = 150.f;

	// Final clearance left between a building wall and the road edge when snapped.
	UPROPERTY(EditAnywhere, config, Category = "Snapping & Placement", meta = (ClampMin = "0"))
	float SnapGapCm = 10.f;

	// Manual building rotation per mouse-wheel notch.
	UPROPERTY(EditAnywhere, config, Category = "Snapping & Placement", meta = (ClampMin = "1"))
	float RotationStepDegrees = 15.f;

	// --- Sampling / geometry ---

	// Arc-length resample spacing for edge polylines (spec: 0.5-1.0 m).
	UPROPERTY(EditAnywhere, config, Category = "Geometry", meta = (ClampMin = "10"))
	float SampleSpacing = 75.f;

	// Lift applied to polyline samples above the height provider, keeping the
	// VT-only rasterization clean on steep terrain (spec: 5 cm).
	UPROPERTY(EditAnywhere, config, Category = "Geometry", meta = (ClampMin = "0"))
	float RibbonZOffset = 5.f;

	// Fraction of the half-width that fades to alpha 0 at the ribbon edge
	// (spec: outer 25-35%).
	UPROPERTY(EditAnywhere, config, Category = "Geometry", meta = (ClampMin = "0.05", ClampMax = "0.6"))
	float FeatherFraction = 0.3f;

	// World length of one V-repeat of the road texture (UV tiling independent
	// of segment count).
	UPROPERTY(EditAnywhere, config, Category = "Geometry", meta = (ClampMin = "10"))
	float TilingLength = 400.f;

	// Junction disc radius = max incident width * this factor.
	UPROPERTY(EditAnywhere, config, Category = "Geometry", meta = (ClampMin = "0.1"))
	float JunctionDiscRadiusFactor = 0.6f;

	// --- Materials ---
	// Roads are standalone meshes above the ground (never baked into the
	// terrain; the ground and its materials are not touched). Missing assets
	// fall back to tinted engine materials so the system stays testable.

	UPROPERTY(EditAnywhere, config, Category = "Rendering")
	TSoftObjectPtr<UMaterialInterface> RoadMaterial =
		TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Realm/Roads/M_Road.M_Road")));

	UPROPERTY(EditAnywhere, config, Category = "Rendering")
	TSoftObjectPtr<UMaterialInterface> PreviewMaterial =
		TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Realm/Roads/M_RoadPreview.M_RoadPreview")));

	static const URoadSettings* Get() { return GetDefault<URoadSettings>(); }
};
