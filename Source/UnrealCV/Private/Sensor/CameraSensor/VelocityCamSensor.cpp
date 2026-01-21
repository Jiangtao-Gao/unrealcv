// Velocity Camera Sensor for UnrealCV
#include "VelocityCamSensor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/TextureRenderTarget2D.h"

UVelocityCamSensor::UVelocityCamSensor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bHasPreviousFrame(false)
{
	// Enable ticking to track transform changes
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	
	// === CRITICAL: Use VelocityMaterial as bridge to read Velocity Buffer ===
	
	// 1. Manual capture (we call CaptureScene() manually)
	bCaptureEveryFrame = false;
	bCaptureOnMovement = false;
	
	// 2. Use capture source that supports post-processing
	CaptureSource = ESceneCaptureSource::SCS_FinalColorHDR;
	
	// 3. ShowFlags configuration
	ShowFlags.SetMotionBlur(true);          // Enable Motion Blur
	ShowFlags.SetPostProcessing(true);      // Enable Post Processing
	ShowFlags.SetTemporalAA(true);          // Enable TAA (forces Velocity calculation)
	
	// 4. Motion Blur settings (triggers Velocity Pass)
	PostProcessSettings.bOverride_MotionBlurAmount = true;
	PostProcessSettings.MotionBlurAmount = 1.0f;  // Must be > 0
	
	PostProcessSettings.bOverride_MotionBlurTargetFPS = true;
	PostProcessSettings.MotionBlurTargetFPS = 30.0f;
	
	PostProcessSettings.bOverride_MotionBlurMax = true;
	PostProcessSettings.MotionBlurMax = 1.0f;
	
	PostProcessSettings.bOverride_MotionBlurPerObjectSize = true;
	PostProcessSettings.MotionBlurPerObjectSize = 1.0f;
	
	// 5. Load VelocityMaterial (REQUIRED)
	// This material reads from SceneTexture:Velocity and outputs to Emissive Color
	FString MaterialPath = TEXT("Material'/UnrealCV/VelocityMaterial.VelocityMaterial'");
	ConstructorHelpers::FObjectFinder<UMaterial> Material(*MaterialPath);
	
	if (Material.Object)
	{
		VelocityMaterial = Material.Object;
		VelocityMaterialInstance = UMaterialInstanceDynamic::Create(VelocityMaterial, this);
		
		if (VelocityMaterialInstance)
		{
			// Set as post-process material
			PostProcessSettings.WeightedBlendables.Array.Empty();
			PostProcessSettings.WeightedBlendables.Array.Add(
				FWeightedBlendable(1.0f, VelocityMaterialInstance)
			);
			UE_LOG(LogTemp, Log, TEXT("VelocityCamSensor: VelocityMaterial loaded successfully"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("VelocityMaterial NOT FOUND!"));
		UE_LOG(LogTemp, Error, TEXT("Please create Material'/UnrealCV/VelocityMaterial.VelocityMaterial'"));
		UE_LOG(LogTemp, Error, TEXT("Material should:"));
		UE_LOG(LogTemp, Error, TEXT("  1. Material Domain = Post Process"));
		UE_LOG(LogTemp, Error, TEXT("  2. Blendable Location = After Tonemapping"));
		UE_LOG(LogTemp, Error, TEXT("  3. Shading Model = Unlit"));
		UE_LOG(LogTemp, Error, TEXT("  4. Nodes: [SceneTexture:Velocity] -> [Emissive Color]"));
	}
	
	UE_LOG(LogTemp, Log, TEXT("VelocityCamSensor initialized with full Velocity support"));
	UE_LOG(LogTemp, Log, TEXT("  bCaptureEveryFrame: %d"), bCaptureEveryFrame);
	UE_LOG(LogTemp, Log, TEXT("  ShowFlags.TemporalAA: %d"), ShowFlags.TemporalAA);
	UE_LOG(LogTemp, Log, TEXT("  MotionBlurAmount: %f"), PostProcessSettings.MotionBlurAmount);
}

void UVelocityCamSensor::BeginPlay()
{
	Super::BeginPlay();
	
	// Initialize previous frame transform
	PreviousFrameTransform = GetComponentTransform();
	bHasPreviousFrame = false;
	
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

void UVelocityCamSensor::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	// NOTE: We do NOT update PreviousFrameTransform here
	// It's only updated in CaptureVelocity() after capturing
	// This ensures PreviousFrameTransform represents the position of the last capture
}

void UVelocityCamSensor::UpdatePreviousFrameTransform()
{
	// This function is now only called from CaptureVelocity()
	// after capturing, to save the current position for the next capture
	FTransform CurrentTransform = GetComponentTransform();
	
	if (bHasPreviousFrame)
	{
		float LocationDiff = (CurrentTransform.GetLocation() - PreviousFrameTransform.GetLocation()).Size();
		FRotator CurrentRot = CurrentTransform.GetRotation().Rotator();
		FRotator PreviousRot = PreviousFrameTransform.GetRotation().Rotator();
		float RotationDiff = CurrentRot.GetManhattanDistance(PreviousRot);
		
		if (LocationDiff > 0.01f || RotationDiff > 0.01f)
		{
			UE_LOG(LogTemp, VeryVerbose, TEXT("Saving new PreviousFrameTransform: Location diff=%f, Rotation diff=%f"), 
			       LocationDiff, RotationDiff);
		}
	}
	
	// Save current as previous for next capture
	PreviousFrameTransform = CurrentTransform;
	bHasPreviousFrame = true;
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

	// Get current transform (user's new position)
	FTransform CurrentTransform = GetComponentTransform();
	
	// === CORRECT APPROACH: Single capture at current position ===
	// Unreal's Velocity Buffer automatically compares:
	//   - Current frame position (this capture)
	//   - Previous frame position (last CaptureScene() call)
	// The engine stores previous frame data in GPU buffers
	
	// Ensure transform is updated
	UpdateComponentToWorld();
	MarkRenderStateDirty();
	
	// Capture scene - this will generate velocity based on movement since last capture
	this->CaptureScene();
	FlushRenderingCommands();
	
	// Read render result (Float16 format for velocity)
	TArray<FFloat16Color> RawData;
	TextureTarget->GameThread_GetRenderTargetResource()->ReadFloat16Pixels(RawData);

	// Convert to FVector2D
	VelocityData.SetNum(Width * Height);
	for (int32 i = 0; i < RawData.Num(); i++)
	{
		float VX = RawData[i].R.GetFloat();
		float VY = RawData[i].G.GetFloat();
		
		// VelocityMaterial maps Velocity from [-1, 1] to [0, 1] using:
		// Output = Velocity * 0.5 + 0.5
		// We need to reverse this mapping: [0, 1] → [-1, 1]
		VX = (VX - 0.5f) * 2.0f;
		VY = (VY - 0.5f) * 2.0f;
		
		VelocityData[i] = FVector2D(VX, VY);
	}
	
	// Debug: Count non-zero pixels and analyze velocity
	int32 NonZeroCount = 0;
	float MaxMagnitude = 0.0f;
	float SumMagnitude = 0.0f;
	FVector2D MinVel(FLT_MAX, FLT_MAX);
	FVector2D MaxVel(-FLT_MAX, -FLT_MAX);
	
	for (const FVector2D& Vel : VelocityData)
	{
		float Magnitude = Vel.Size();
		if (Magnitude > 0.001f)
		{
			NonZeroCount++;
			MaxMagnitude = FMath::Max(MaxMagnitude, Magnitude);
			SumMagnitude += Magnitude;
		}
		
		MinVel.X = FMath::Min(MinVel.X, Vel.X);
		MinVel.Y = FMath::Min(MinVel.Y, Vel.Y);
		MaxVel.X = FMath::Max(MaxVel.X, Vel.X);
		MaxVel.Y = FMath::Max(MaxVel.Y, Vel.Y);
	}
	
	float AvgMagnitude = NonZeroCount > 0 ? SumMagnitude / NonZeroCount : 0.0f;
	
	// Detailed logging
	if (bHasPreviousFrame)
	{
		// Calculate actual transform difference
		float LocationDiff = (CurrentTransform.GetLocation() - PreviousFrameTransform.GetLocation()).Size();
		FRotator CurrentRot = CurrentTransform.GetRotation().Rotator();
		FRotator PreviousRot = PreviousFrameTransform.GetRotation().Rotator();
		float RotationDiff = CurrentRot.GetManhattanDistance(PreviousRot);
		
		UE_LOG(LogTemp, Log, TEXT("Velocity captured: %d/%d non-zero pixels (%.1f%%), max=%.4f, avg=%.4f"), 
		       NonZeroCount, VelocityData.Num(), 
		       100.0f * NonZeroCount / FMath::Max(1, VelocityData.Num()),
		       MaxMagnitude, AvgMagnitude);
		UE_LOG(LogTemp, Log, TEXT("  Velocity range: X[%.4f, %.4f], Y[%.4f, %.4f]"), 
		       MinVel.X, MaxVel.X, MinVel.Y, MaxVel.Y);
		UE_LOG(LogTemp, Log, TEXT("  Transform change: Location=%.2f cm, Rotation=%.2f deg"), 
		       LocationDiff, RotationDiff);
		
		if (NonZeroCount == 0 && (LocationDiff > 1.0f || RotationDiff > 1.0f))
		{
			UE_LOG(LogTemp, Warning, TEXT("  WARNING: No velocity despite significant transform change!"));
			UE_LOG(LogTemp, Warning, TEXT("  Possible causes:"));
			UE_LOG(LogTemp, Warning, TEXT("    1. VelocityMaterial not found or incorrectly configured"));
			UE_LOG(LogTemp, Warning, TEXT("    2. r.VelocityOutputPass=0 (should be 1)"));
			UE_LOG(LogTemp, Warning, TEXT("    3. Motion Blur disabled in ShowFlags"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("First frame - velocity is zero (no previous frame for comparison)"));
		UE_LOG(LogTemp, Log, TEXT("  Velocity range: X[%.4f, %.4f], Y[%.4f, %.4f]"), 
		       MinVel.X, MaxVel.X, MinVel.Y, MaxVel.Y);
	}
	
	// Save current transform as "previous" for next call
	// This is for logging/debugging only - the actual velocity calculation
	// is done by Unreal's rendering pipeline using GPU buffers
	PreviousFrameTransform = CurrentTransform;
	bHasPreviousFrame = true;
}
