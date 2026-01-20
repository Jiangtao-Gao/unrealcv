// Velocity Camera Sensor for UnrealCV
// Captures engine's velocity buffer including camera and object motion
#pragma once

#include "BaseCameraSensor.h"
#include "VelocityCamSensor.generated.h"

/**
 * Velocity Camera Sensor
 * Captures the engine's Velocity Buffer which includes:
 * 1. Camera motion (translation, rotation)
 * 2. Object motion (translation, rotation, scale)
 * 3. Vertex deformation (skeletal animation, morph targets)
 * 
 * Requirements:
 * - DefaultEngine.ini should have:
 *   r.VelocityOutputPass=1
 *   r.Velocity.EnableVertexDeformation=1
 */
UCLASS()
class UNREALCV_API UVelocityCamSensor : public UBaseCameraSensor
{
	GENERATED_BODY()

public:
	UVelocityCamSensor(const FObjectInitializer& ObjectInitializer);

	/** Capture 2D Velocity (screen space motion vectors) */
	void CaptureVelocity(TArray<FVector2D>& VelocityData, int& Width, int& Height);

	void SetFilmSize(int Width, int Height);
	virtual void BeginPlay() override;

private:
	/** Material to read velocity buffer */
	UPROPERTY()
	UMaterial* VelocityMaterial;

	/** Material instance for velocity reading */
	UPROPERTY()
	UMaterialInstanceDynamic* VelocityMaterialInstance;
};
