
// cookiedough -- voxel landscape

#include "main.h"
// #include "landscape.h"
#include "image.h"
#include "cspan.h"
#include "bilinear.h"
#include "polar.h"
#include "boxblur.h"
#include "voxel-shared.h"
#include "gamepad.h"
#include "rocket.h"

static uint8_t *s_pHeightMap = nullptr;
static uint32_t *s_pColorMap = nullptr;
static uint32_t *s_pFogGradient = nullptr;

static __m128i s_fogGradientUnp[256];

// --- Sync. tracks ---

SyncTrack trackVoxelScapeForward;
SyncTrack trackVoxelScapeTilt;
SyncTrack trackWarpSpeed, trackWarpStrength;

// --------------------

// -- voxel renderer --

// adjust to map (FIXME: parametrize, document)
constexpr float kMapViewLenScale = kAspect*(kPI*0.1f);
// constexpr int kMapViewHeight = 100;
constexpr int kMapTilt = 90;
constexpr float kMaxTiltDiff = 90.f;
constexpr int kMapScale = 512;

// adjust to map resolution
constexpr unsigned kMapSize = 1024;
constexpr unsigned kMapAnd = kMapSize-1;                                          
constexpr unsigned kMapShift = 10;

// trace depth
const unsigned int kRayLength = 512;

static int s_mapTilt = kMapTilt;

// sample height (filtered)
VIZ_INLINE unsigned int vscape_shf(int U, int V)
{
	unsigned int U0, V0, U1, V1, fracU, fracV;
	bsamp_prepUVs(U, V, kMapAnd, kMapShift, U0, V0, U1, V1, fracU, fracV);
	return bsamp8(s_pHeightMap, U0, V0, U1, V1, fracU, fracV);
}

static void vscape_ray(uint32_t *pDest, int curX, int curY, int dX, int dY, float fishMul)
{
	int lastHeight = kResY;
	int lastDrawnHeight = kResY;

	const unsigned int U = (curX>>8) & kMapAnd, V = ((curY>>8) & kMapAnd) << kMapShift;
	__m128i lastColor = c2vISSE16(s_pColorMap[U|V]);

	const int fpFishMul = ftofp24(fabsf(fishMul));
	
	for (unsigned int iStep = 0; iStep < kRayLength; ++iStep)
	{
		// advance! (FIXME: in this case I think the direction is sort of irrelevant)
		curX += dX;
		curY += dY;

		// prepare UVs
		unsigned int U0, V0, U1, V1, fracU, fracV;
		bsamp_prepUVs(curX, curY, kMapAnd, kMapShift, U0, V0, U1, V1, fracU, fracV);

		// fetch height & color
		const unsigned int mapHeight = bsamp8(s_pHeightMap, U0, V0, U1, V1, fracU, fracV);
		 __m128i color = bsamp32_16(s_pColorMap, U0, V0, U1, V1, fracU, fracV);


		// apply fog (additive/subtractive, no clamp: can overflow)
///		color = _mm_adds_epu16(color, s_fogGradientUnp[(iStep)>>1]);
		color = _mm_subs_epu16(color, s_fogGradientUnp[(iStep)>>1]);

		// FIXME
		int height = 255-mapHeight;		
		height <<= 16;
		height /= fpFishMul*(iStep+1);
		height *= kMapScale;
		height >>= 8;
		height += s_mapTilt;

		VIZ_ASSERT(height >= 0);

		// voxel visible?
		if (height < lastDrawnHeight)
		{
			// draw span (vertical)
			const unsigned int drawLength = lastDrawnHeight - height;
			cspanISSE16(pDest + height*kResX, kResX, lastHeight - height, drawLength, color, lastColor);
			lastDrawnHeight = height;
		}

		lastHeight = height;
		lastColor = color;
	}
}

static void vscape(uint32_t *pDest, float time, float delta)
{
	// grab gamepad input
	PadState pad;
	Gamepad_Update(pad);

	// tilt (pad & sync.)
	static float padTilt = 0.f;
	const float tiltStep = std::min(delta, 1.f);
	if (padTilt < kMaxTiltDiff && pad.rightY > 0.f)
		padTilt += tiltStep;
	if (padTilt > -kMaxTiltDiff && pad.rightY < 0.f)
		padTilt -= tiltStep;

	const float tilt = clampf(-kMaxTiltDiff, kMaxTiltDiff, Rocket::getf(trackVoxelScapeTilt) + padTilt);
	s_mapTilt = kMapTilt + int(tilt);

	// calc. view angle + it's sine & cosine
	static float viewAngle = 0.f;
//	constexpr float maxAng = kPI*2.f;
	float viewMul = 1.f/kAspect;
	viewMul *= delta*0.01f;
//	if (viewAngle < maxAng)
		viewAngle += viewMul*pad.lShoulder;
//	if (viewAngle > -maxAng)
		viewAngle -= viewMul*pad.rShoulder;

	const float viewCos = cosf(viewAngle);
	const float viewSin = sinf(viewAngle);

	// strafe
	static float strafeX = 0.f;
	static float strafeY = 0.f;
	if (pad.rightX != 0.f)
	{
		const float strafe = pad.rightX*delta;
		strafeX += viewCos*strafe;
		strafeY += viewSin*strafe;
	}

	// move (pad & sync.)
	static float padMoveX = 0.f;
	static float padMoveY = 0.f;
	const float padMove = (pad.leftY != 0.f) ? -pad.leftY*delta : 0.f;
	padMoveX += -viewSin*padMove;
	padMoveY +=  viewCos*padMove;
	
	const float syncvMoveFwd = Rocket::getf(trackVoxelScapeForward);
	float moveX = -viewSin*syncvMoveFwd;
	float moveY =  viewCos*syncvMoveFwd;

	moveX += padMoveX;
	moveY += padMoveY;

	// origin
	float X1 = moveX+strafeX;
	float Y1 = moveY+strafeY;
	const int fpX1 = ftofp24(X1);
	const int fpY1 = ftofp24(Y1);

	constexpr float rayY = kMapSize*kMapViewLenScale;

	#pragma omp parallel for schedule(static)
	for (int iRay = 0; iRay < kResX; ++iRay)
	{
		// FIXME: subpixel accuracy adj.

		const float rayX = 0.25f*(iRay - kResX*0.5f); // FIXME: parameter?

		// FIXME: simplify
		float rotRayX = rayX, rotRayY = rayY;
		voxel::vrot2D(viewCos, viewSin, rotRayX, rotRayY);
		float X2 = X1+rotRayX;
		float Y2 = Y1+rotRayY;
		float dX = X2-X1;
		float dY = Y2-Y1;
		voxel::vnorm2D(dX, dY);

		// counteract fisheye effect
		/* const */ float fishMul = rayY / sqrtf(rotRayX*rotRayX + rotRayY*rotRayY);
	
		vscape_ray(pDest+iRay, fpX1, fpY1, ftofp24(dX), ftofp24(dY), fishMul);
	}
}

// -- composition --

bool Landscape_Create()
{
	// load maps
//	s_pHeightMap = Image_Load8("assets/scape/comanche-maps/D19.png");
//	s_pColorMap = Image_Load32("assets/scape/comanche-maps/C19w.png");
	s_pHeightMap = Image_Load8("assets/scape/D17.png");
	s_pColorMap = Image_Load32("assets/scape/C17W-edit.png");
	if (nullptr == s_pHeightMap || nullptr == s_pColorMap)
		return false;

	// load fog gradient (8-bit LUT)
	s_pFogGradient = Image_Load32("assets/scape/foggradient.jpg");
	if (s_pFogGradient == NULL)
		return false;
		
	// unpack fog gradient pixels
	for (int iPixel = 0; iPixel < 256; ++iPixel)
		s_fogGradientUnp[iPixel] = c2vISSE16(s_pFogGradient[iPixel]);

	// init. sync.
	trackVoxelScapeForward = Rocket::AddTrack("voxelScape:Forward");
	trackVoxelScapeTilt = Rocket::AddTrack("voxelScape:Tilt");
	trackWarpSpeed = Rocket::AddTrack("voxelScape:WarpSpeed");
	trackWarpStrength = Rocket::AddTrack("voxelScape:WarpStrength");

	return true;
}

void Landscape_Destroy()
{
}

void Landscape_Draw(uint32_t *pDest, float time, float delta)
{
	// render landscape
	memset32(g_renderTarget[0], s_pFogGradient[0], kResX*kResY);
	vscape(g_renderTarget[0], time, delta);

	const float warpStrength = Rocket::getf(trackWarpStrength);
	if (0.f != warpStrength)
		TapeWarp32(pDest, g_renderTarget[0], kResX, kResY, Rocket::getf(trackWarpSpeed), warpStrength);
	else
		memcpy(pDest, g_renderTarget[0], kOutputBytes);
}

