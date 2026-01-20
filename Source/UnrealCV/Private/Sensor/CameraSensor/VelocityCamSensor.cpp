// Velocity Camera Sensor for UnrealCV
#include "VelocityCamSensor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/TextureRenderTarget2D.h"

UVelocityCamSensor::UVelocityCamSensor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 1. Enable Motion Blur ShowFlag (this enables velocity calculation)
	// Note: In UE5.5, there's no separate Velocity ShowFlag
	ShowFlags.SetMotionBlur(true);
	ShowFlags.SetPostProcessing(true);
	
	// 2. Try to use a capture source that might support velocity
	// Note: There's no direct SCS_Velocity, so we use FinalColorHDR with post-processing
	CaptureSource = ESceneCaptureSource::SCS_FinalColorHDR;
	
	// 3. Enable Motion Blur to trigger Velocity calculation
	// Note: MotionBlurAmount must be > 0 to enable velocity calculation
	PostProcessSettings.bOverride_MotionBlurAmount = true;
	PostProcessSettings.MotionBlurAmount = 0.5f;
	
	// 4. Try to load Velocity reading material (optional)
	// If material doesn't exist, we'll try to access velocity buffer directly
	FString MaterialPath = TEXT("Material'/UnrealCV/VelocityMaterial.VelocityMaterial'");
	ConstructorHelpers::FObjectFinder<UMaterial> Material(*MaterialPath);
	
	if (Material.Object)
	{
		VelocityMaterial = Material.Object;
		
		// Create dynamic material instance
		VelocityMaterialInstance = UMaterialInstanceDynamic::Create(VelocityMaterial, this);
		
		if (VelocityMaterialInstance)
		{
			// Set as post-process material
			PostProcessSettings.WeightedBlendables.Array.Empty();
			PostProcessSettings.WeightedBlendables.Array.Add(
				FWeightedBlendable(1.0f, VelocityMaterialInstance)
			);
			UE_LOG(LogTemp, Log, TEXT("VelocityCamSensor: Using VelocityMaterial for capture"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("VelocityMaterial not found. Will attempt direct velocity buffer access."));
		UE_LOG(LogTemp, Warning, TEXT("For better results, create Material'/UnrealCV/VelocityMaterial.VelocityMaterial'"));
	}
	
	UE_LOG(LogTemp, Log, TEXT("VelocityCamSensor initialized"));
}

void UVelocityCamSensor::BeginPlay()
{
	Super::BeginPlay();
	
	// Verify configuration
	UE_LOG(LogTemp, Log, TEXT("VelocityCamSensor BeginPlay"));
	UE_LOG(LogTemp, Log, TEXT("  ShowFlags.MotionBlur: %d"), ShowFlags.MotionBlur);
	
	// Check engine console variables
	static IConsoleVariable* VelocityOutputPass = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VelocityOutputPass"));
	static IConsoleVariable* VertexDeformation = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Velocity.EnableVertexDeformation"));
	
	if (VelocityOutputPass)
	{
		int32 Value = VelocityOutputPass->GetInt();
		UE_LOG(LogTemp, Log, TEXT("  r.VelocityOutputPass: %d"), Value);
		if (Value == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("  WARNING: r.VelocityOutputPass is 0. Set it to 1 in DefaultEngine.ini for velocity capture."));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("  r.VelocityOutputPass console variable not found"));
	}
	
	if (VertexDeformation)
	{
		int32 Value = VertexDeformation->GetInt();
		UE_LOG(LogTemp, Log, TEXT("  r.Velocity.EnableVertexDeformation: %d"), Value);
		if (Value == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("  WARNING: r.Velocity.EnableVertexDeformation is 0. Skeletal animation velocity may not be captured."));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("  r.Velocity.EnableVertexDeformation console variable not found"));
	}
}

void UVelocityCamSensor::SetFilmSize(int Width, int Height)
{
	// Call parent to set basic parameters
	// But we need to override the texture format for velocity
	this->FilmWidth = Width;
	this->FilmHeight = Height;
	
	// Initialize texture target with correct format for velocity
	if (!TextureTarget)
	{
		TextureTarget = NewObject<UTextureRenderTarget2D>(this);
	}
	
	// Use Float16 RGBA format for velocity data
	// This matches what the engine expects for velocity buffer
	bool bUseLinearGamma = true;
	TextureTarget->InitCustomFormat(Width, Height, PF_FloatRGBA, bUseLinearGamma);
	TextureTarget->TargetGamma = GEngine->GetDisplayGamma();
	
	UE_LOG(LogTemp, Log, TEXT("VelocityCamSensor::SetFilmSize: %dx%d, Format: PF_FloatRGBA"), Width, Height);
}

void UVelocityCamSensor::CaptureVelocity(TArray<FVector2D>& VelocityData, int& Width, int& Height)
{
	Width = GetFilmWidth();
	Height = GetFilmHeight();

	// Capture the scene
	// This triggers:
	// 1. Velocity Pass rendering (if r.VelocityOutputPass=1)
	// 2. Camera motion velocity calculation
	// 3. Object motion velocity calculation
	// 4. Vertex deformation velocity (if r.Velocity.EnableVertexDeformation=1)
	this->CaptureScene();

	// Read render result (Float16 format for velocity)
	TArray<FFloat16Color> RawData;
	TextureTarget->GameThread_GetRenderTargetResource()->ReadFloat16Pixels(RawData);

	// Convert to FVector2D
	VelocityData.SetNum(Width * Height);
	for (int32 i = 0; i < RawData.Num(); i++)
	{
		VelocityData[i] = FVector2D(
			RawData[i].R.GetFloat(),  // X direction velocity (horizontal)
			RawData[i].G.GetFloat()   // Y direction velocity (vertical)
		);
	}
	
	// Debug: Count non-zero pixels
	int32 NonZeroCount = 0;
	float MaxMagnitude = 0.0f;
	for (const FVector2D& Vel : VelocityData)
	{
		float Magnitude = Vel.Size();
		if (Magnitude > 0.001f)
		{
			NonZeroCount++;
			MaxMagnitude = FMath::Max(MaxMagnitude, Magnitude);
		}
	}
	
	UE_LOG(LogTemp, Verbose, TEXT("Velocity captured: %d/%d non-zero pixels, max magnitude: %f"), 
	       NonZeroCount, VelocityData.Num(), MaxMagnitude);
	
	if (NonZeroCount == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("No velocity detected. Check if:"));
		UE_LOG(LogTemp, Warning, TEXT("  1. r.VelocityOutputPass=1 in DefaultEngine.ini"));
		UE_LOG(LogTemp, Warning, TEXT("  2. Camera or objects are moving"));
		UE_LOG(LogTemp, Warning, TEXT("  3. VelocityMaterial is correctly set up"));
	}
}
