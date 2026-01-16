// Weichao Qiu @ 2017
#include "DepthCamSensor.h"
#include "AnnotationCamSensor.h"
#include "TextureResource.h"
#include "Runtime/Core/Public/Async/ParallelFor.h"

UDepthCamSensor::UDepthCamSensor(const FObjectInitializer& ObjectInitializer) :
	Super(ObjectInitializer)
{
	// this->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	this->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
	bIgnoreTransparentObjects = false;
}

void UDepthCamSensor::InitTextureTarget(int filmWidth, int filmHeight)
{
	EPixelFormat PixelFormat = EPixelFormat::PF_FloatRGBA;
	bool bUseLinearGamma = true;
 	TextureTarget->InitCustomFormat(filmWidth, filmHeight, EPixelFormat::PF_FloatRGBA, bUseLinearGamma);
}

void UDepthCamSensor::CaptureDepth(TArray<float>& DepthData, int& Width, int& Height)
{
	// FIX: Use default scene depth rendering instead of ShowOnlyComponents
	// 
	// Problem: When using ShowOnlyComponents with PRM_UseShowOnlyList mode,
	// newly spawned cameras would have corrupted depth data (left half of image invalid).
	// This was because the ShowOnlyComponents list was not properly synchronized
	// with the new camera's rendering state.
	//
	// Solution: Use default scene depth rendering (PRM_RenderScenePrimitives) which
	// renders all scene primitives without filtering.
	this->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	this->ShowOnlyComponents.Empty();

	if (!CheckTextureTarget()) return;
	this->CaptureScene();
	Width = this->TextureTarget->SizeX, Height = TextureTarget->SizeY;
	DepthData.AddZeroed(Width * Height); // or AddUninitialized(FloatColorDepthData.Num());
	FTextureRenderTargetResource* RenderTargetResource = this->TextureTarget->GameThread_GetRenderTargetResource();
	TArray<FFloat16Color> FloatColorDepthData;
	RenderTargetResource->ReadFloat16Pixels(FloatColorDepthData);

    ParallelFor(FloatColorDepthData.Num(), [&](int32 i)
    {
        if (i >= 0 && i < FloatColorDepthData.Num() && i < DepthData.Num())
        {
            FFloat16Color& FloatColor = FloatColorDepthData[i];
            DepthData[i] = FloatColor.R;
        }
    });
}
