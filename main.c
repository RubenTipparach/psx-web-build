/*
* PSX Lander Model Viewer - Bare Metal Version
* Ported from PSn00bSDK to ps1-bare-metal
*/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "gpu.h"
#include "ps1/cop0.h"
#include "ps1/gpucmd.h"
#include "ps1/gte.h"
#include "ps1/registers.h"
#include "trig.h"
#include "lander_model.h"

/* Texture data embedded by CMake */
extern const uint8_t textureData[];

/* Texture dimensions */
#define TEXTURE_WIDTH  64
#define TEXTURE_HEIGHT 64

/* GTE uses 20.12 fixed-point format */
#define ONE (1 << 12)

/* Screen resolution */
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

/* Screen center position */
#define CENTERX (SCREEN_WIDTH  / 2)
#define CENTERY (SCREEN_HEIGHT / 2)

/* Initialize the GTE for 3D rendering */
static void setupGTE(int width, int height) {
	/* Enable coprocessor 2 (GTE) */
	cop0_setReg(COP0_STATUS, cop0_getReg(COP0_STATUS) | COP0_STATUS_CU2);
	
	/* Set screen offset (center of screen) - 16.16 fixed-point */
	gte_setControlReg(GTE_OFX, (width  << 16) / 2);
	gte_setControlReg(GTE_OFY, (height << 16) / 2);
	
	/* Set projection plane distance (FOV control) */
	int focalLength = (width < height) ? width : height;
	gte_setControlReg(GTE_H, focalLength / 2);
	
	/* Set Z averaging scale factors for ordering table sorting */
	gte_setControlReg(GTE_ZSF3, ORDERING_TABLE_SIZE / 3);
	gte_setControlReg(GTE_ZSF4, ORDERING_TABLE_SIZE / 4);
}

/* Matrix multiplication helper */
static void multiplyCurrentMatrixByVectors(GTEMatrix *output) {
	gte_command(GTE_CMD_MVMVA | GTE_SF | GTE_MX_RT | GTE_V_V0 | GTE_CV_NONE);
	output->values[0][0] = gte_getDataReg(GTE_IR1);
	output->values[1][0] = gte_getDataReg(GTE_IR2);
	output->values[2][0] = gte_getDataReg(GTE_IR3);
	
	gte_command(GTE_CMD_MVMVA | GTE_SF | GTE_MX_RT | GTE_V_V1 | GTE_CV_NONE);
	output->values[0][1] = gte_getDataReg(GTE_IR1);
	output->values[1][1] = gte_getDataReg(GTE_IR2);
	output->values[2][1] = gte_getDataReg(GTE_IR3);
	
	gte_command(GTE_CMD_MVMVA | GTE_SF | GTE_MX_RT | GTE_V_V2 | GTE_CV_NONE);
	output->values[0][2] = gte_getDataReg(GTE_IR1);
	output->values[1][2] = gte_getDataReg(GTE_IR2);
	output->values[2][2] = gte_getDataReg(GTE_IR3);
}

/* Rotate the current GTE matrix */
static void rotateCurrentMatrix(int yaw, int pitch, int roll) {
	static GTEMatrix multiplied;
	int s, c;
	
	/* Yaw rotation (Y axis) */
	if (yaw) {
		s = isin(yaw);
		c = icos(yaw);
		
		gte_setColumnVectors(
			c, -s,   0,
			s,  c,   0,
			0,  0, ONE
		);
		multiplyCurrentMatrixByVectors(&multiplied);
		gte_loadRotationMatrix(&multiplied);
	}
	
	/* Pitch rotation (X axis) */
	if (pitch) {
		s = isin(pitch);
		c = icos(pitch);
		
		gte_setColumnVectors(
			c,   0, s,
			0, ONE, 0,
			-s,   0, c
		);
		multiplyCurrentMatrixByVectors(&multiplied);
		gte_loadRotationMatrix(&multiplied);
	}
	
	/* Roll rotation (Z axis) */
	if (roll) {
		s = isin(roll);
		c = icos(roll);
		
		gte_setColumnVectors(
			ONE, 0,  0,
			0, c, -s,
			0, s,  c
		);
		multiplyCurrentMatrixByVectors(&multiplied);
		gte_loadRotationMatrix(&multiplied);
	}
}

int main(int argc, const char **argv) {
	/* Initialize serial for debugging */
	initSerialIO(115200);
	
	/* Setup GPU based on region */
	if ((GPU_GP1 & GP1_STAT_FB_MODE_BITMASK) == GP1_STAT_FB_MODE_PAL) {
		puts("Using PAL mode");
		setupGPU(GP1_MODE_PAL, SCREEN_WIDTH, SCREEN_HEIGHT);
	} else {
		puts("Using NTSC mode");
		setupGPU(GP1_MODE_NTSC, SCREEN_WIDTH, SCREEN_HEIGHT);
	}
	
	/* Initialize GTE */
	setupGTE(SCREEN_WIDTH, SCREEN_HEIGHT);
	
	/* Enable DMA channels */
	DMA_DPCR |= 0
	| DMA_DPCR_CH_ENABLE(DMA_GPU)
	| DMA_DPCR_CH_ENABLE(DMA_OTC);
	
	GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_GP0_WRITE);
	GPU_GP1 = gp1_dispBlank(false);
	
	/* Upload texture to VRAM (placed after framebuffers at 640,0) */
	TextureInfo texture;
	uploadTexture(
		&texture,
		textureData,
		SCREEN_WIDTH * 2,  /* X position in VRAM */
		0,                  /* Y position in VRAM */
		TEXTURE_WIDTH,
		TEXTURE_HEIGHT
	);
	puts("Texture uploaded to VRAM");
	
	/* Double buffering */
	DMAChain dmaChains[2];
	bool     usingSecondFrame = false;
	int      frameCounter     = 0;
	
	puts("Lander model viewer starting...");
	
	/* Main loop */
	for (;;) {
		int bufferX = usingSecondFrame ? SCREEN_WIDTH : 0;
		int bufferY = 0;
		
		DMAChain *chain  = &dmaChains[usingSecondFrame];
		usingSecondFrame = !usingSecondFrame;
		
		uint32_t *ptr;
		
		GPU_GP1 = gp1_fbOffset(bufferX, bufferY);
		
		clearOrderingTable(chain->orderingTable, ORDERING_TABLE_SIZE);
		chain->nextPacket = chain->data;
		
		/* Reset GTE translation vector and rotation matrix */
		gte_setControlReg(GTE_TRX,    0);
		gte_setControlReg(GTE_TRY,    0);
		gte_setControlReg(GTE_TRZ, 300);  /* Distance from camera (closer = larger) */
		gte_setRotationMatrix(
			ONE,   0,   0,
			0, ONE,   0,
			0,   0, ONE
		);
		
		/* Rotate the model */
		rotateCurrentMatrix(0, frameCounter * 16, 0);
		frameCounter++;
		
		/* Draw model faces */
		for (int i = 0; i < MODEL_FACES; i++) {
			const Face *face = &model_faces[i];
			
			/* Load vertices into GTE */
			gte_loadV0(&model_verts[face->v0]);
			gte_loadV1(&model_verts[face->v1]);
			gte_loadV2(&model_verts[face->v2]);
			
			/* Perspective transformation (3 vertices) */
			gte_command(GTE_CMD_RTPT | GTE_SF);
			
			/* Normal clipping for backface culling */
			gte_command(GTE_CMD_NCLIP);
			
			/* Get clipping result */
			int nclip = gte_getDataReg(GTE_MAC0);
			if (nclip <= 0)
			continue;  /* Face is backfacing, skip it */
			
			/* Calculate average Z for depth sorting */
			gte_command(GTE_CMD_AVSZ3 | GTE_SF);
			int zIndex = gte_getDataReg(GTE_OTZ);
			
			if ((zIndex < 0) || (zIndex >= ORDERING_TABLE_SIZE))
			continue;
			
			/* Get UV coordinates for this face */
			const UV *uv0 = &model_uvs[face->uv0];
			const UV *uv1 = &model_uvs[face->uv1];
			const UV *uv2 = &model_uvs[face->uv2];
			
			/* Allocate packet for textured triangle (7 words) */
			ptr    = allocatePacket(chain, zIndex, 7);
			ptr[0] = gp0_rgb(128, 128, 128) | gp0_shadedTriangle(false, true, false);
			gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);  /* XY0 */
			ptr[2] = gp0_uv(uv0->u, uv0->v, texture.clut);  /* UV0 + CLUT */
			gte_storeDataReg(GTE_SXY1, 3 * 4, ptr);  /* XY1 */
			ptr[4] = gp0_uv(uv1->u, uv1->v, texture.page);  /* UV1 + TPAGE */
			gte_storeDataReg(GTE_SXY2, 5 * 4, ptr);  /* XY2 */
			ptr[6] = gp0_uv(uv2->u, uv2->v, 0);            /* UV2 */
		}
		
		/* Clear background (at the back of ordering table) */
		ptr    = allocatePacket(chain, ORDERING_TABLE_SIZE - 1, 3);
		ptr[0] = gp0_rgb(60, 120, 150) | gp0_vramFill();
		ptr[1] = gp0_xy(bufferX, bufferY);
		ptr[2] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);
		
		/* Set drawing area attributes */
		ptr    = allocatePacket(chain, ORDERING_TABLE_SIZE - 1, 4);
		ptr[0] = gp0_texpage(0, true, false);
		ptr[1] = gp0_fbOffset1(bufferX, bufferY);
		ptr[2] = gp0_fbOffset2(
			bufferX + SCREEN_WIDTH  - 1,
			bufferY + SCREEN_HEIGHT - 2
		);
		ptr[3] = gp0_fbOrigin(bufferX, bufferY);
		
		/* Wait for GPU and VSync, then draw */
		waitForGP0Ready();
		waitForVSync();
		sendLinkedList(&(chain->orderingTable)[ORDERING_TABLE_SIZE - 1]);
	}
	
	return 0;
}
