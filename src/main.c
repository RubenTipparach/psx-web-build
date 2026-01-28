/*
* PSX Lander Model Viewer - Bare Metal Version
* Ported from PSn00bSDK to ps1-bare-metal
*/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "gpu.h"
#include "spu.h"
#include "bios.h"
#include "model.h"
#include "font.h"
#include "ps1/cop0.h"
#include "ps1/gpucmd.h"
#include "ps1/gte.h"
#include "ps1/registers.h"
#include "trig.h"

/* Texture data embedded by CMake */
extern const uint8_t textureData[];

/* Model data embedded by CMake */
extern const uint8_t modelData[];
extern const uint32_t modelData_size;

/* Font data embedded by CMake */
extern const uint8_t fontTexture[];
extern const uint8_t fontPalette[];

/* Music data embedded by CMake (SPU-ADPCM format) */
extern const uint8_t musicData[];
extern const uint32_t musicData_size;


/* Font dimensions */
#define FONT_WIDTH        96
#define FONT_HEIGHT       56
#define FONT_COLOR_DEPTH  GP0_COLOR_4BPP

/* Controller button definitions (directly from response bytes) */
#define PAD_SELECT   (1 << 0)
#define PAD_L3       (1 << 1)
#define PAD_R3       (1 << 2)
#define PAD_START    (1 << 3)
#define PAD_UP       (1 << 4)
#define PAD_RIGHT    (1 << 5)
#define PAD_DOWN     (1 << 6)
#define PAD_LEFT     (1 << 7)
#define PAD_L2       (1 << 8)
#define PAD_R2       (1 << 9)
#define PAD_L1       (1 << 10)
#define PAD_R1       (1 << 11)
#define PAD_TRIANGLE (1 << 12)
#define PAD_CIRCLE   (1 << 13)
#define PAD_X        (1 << 14)
#define PAD_SQUARE   (1 << 15)

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

/* Controller communication */
static void delayMicroseconds(int time) {
	time = ((time * 271) + 4) / 8;
	__asm__ volatile(
		".set push\n"
		".set noreorder\n"
		"bgtz  %0, .\n"
		"addiu %0, -2\n"
		".set pop\n"
		: "+r"(time)
	);
}

static void initControllerBus(void) {
	SIO_CTRL(0) = SIO_CTRL_RESET;
	SIO_MODE(0) = SIO_MODE_BAUD_DIV1 | SIO_MODE_DATA_8;
	SIO_BAUD(0) = F_CPU / 250000;
	SIO_CTRL(0) = SIO_CTRL_TX_ENABLE | SIO_CTRL_RX_ENABLE | SIO_CTRL_DSR_IRQ_ENABLE;
}

static bool waitForAcknowledge(int timeout) {
	for (; timeout > 0; timeout -= 10) {
		if (IRQ_STAT & (1 << IRQ_SIO0)) {
			IRQ_STAT = ~(1 << IRQ_SIO0);
			SIO_CTRL(0) |= SIO_CTRL_ACKNOWLEDGE;
			return true;
		}
		delayMicroseconds(10);
	}
	return false;
}

static uint8_t exchangeByte(uint8_t value) {
	while (!(SIO_STAT(0) & SIO_STAT_TX_NOT_FULL))
		__asm__ volatile("");
	SIO_DATA(0) = value;
	while (!(SIO_STAT(0) & SIO_STAT_RX_NOT_EMPTY))
		__asm__ volatile("");
	return SIO_DATA(0);
}

static uint8_t exchangeByteWithTimeout(uint8_t value, int timeout) {
	/* Wait for TX ready with timeout */
	while (!(SIO_STAT(0) & SIO_STAT_TX_NOT_FULL)) {
		if (--timeout <= 0) return 0xFF;
		__asm__ volatile("");
	}
	SIO_DATA(0) = value;

	/* Wait for RX ready with timeout */
	timeout = 10000;
	while (!(SIO_STAT(0) & SIO_STAT_RX_NOT_EMPTY)) {
		if (--timeout <= 0) return 0xFF;
		__asm__ volatile("");
	}
	return SIO_DATA(0);
}

/* Controller state structure with analog support */
typedef struct {
	uint16_t buttons;    /* Digital buttons (active high after inversion) */
	uint8_t  leftX;      /* Left stick X (0x00=left, 0x80=center, 0xFF=right) */
	uint8_t  leftY;      /* Left stick Y (0x00=up, 0x80=center, 0xFF=down) */
	uint8_t  rightX;     /* Right stick X */
	uint8_t  rightY;     /* Right stick Y */
	bool     isAnalog;   /* True if analog controller detected */
} ControllerState;

static void pollController(int port, ControllerState *state) {
	/* Initialize to defaults (no input, centered sticks) */
	state->buttons = 0;
	state->leftX = 0x80;
	state->leftY = 0x80;
	state->rightX = 0x80;
	state->rightY = 0x80;
	state->isAnalog = false;

	/* Select port */
	if (port)
		SIO_CTRL(0) |= SIO_CTRL_CS_PORT_2;
	else
		SIO_CTRL(0) &= ~SIO_CTRL_CS_PORT_2;

	/* Reset IRQ and assert DTR */
	IRQ_STAT = ~(1 << IRQ_SIO0);
	SIO_CTRL(0) |= SIO_CTRL_DTR | SIO_CTRL_ACKNOWLEDGE;
	delayMicroseconds(60);

	/* Send address byte (0x01 = controller) */
	SIO_DATA(0) = 0x01;

	if (!waitForAcknowledge(120)) {
		SIO_CTRL(0) &= ~SIO_CTRL_DTR;
		return;  /* No controller */
	}

	/* Clear receive buffer with timeout */
	int clearTimeout = 1000;
	while ((SIO_STAT(0) & SIO_STAT_RX_NOT_EMPTY) && clearTimeout-- > 0)
		SIO_DATA(0);

	/* Send poll command - request up to 8 bytes for analog data */
	uint8_t response[8] = {0, 0, 0, 0, 0x80, 0x80, 0x80, 0x80};
	uint8_t request[] = { 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	/* First byte tells us the controller type and response length */
	response[0] = exchangeByteWithTimeout(request[0], 10000);
	if (!waitForAcknowledge(120)) goto done;

	/* Get controller type from upper nibble, length from lower nibble */
	int type = response[0] >> 4;
	int halfwords = response[0] & 0x0F;  /* Response length in 16-bit units */
	int responseLen = (halfwords + 1) * 2;  /* Convert to bytes */
	if (responseLen > 8) responseLen = 8;

	/* Read remaining bytes */
	for (int i = 1; i < responseLen; i++) {
		response[i] = exchangeByteWithTimeout(request[i], 10000);
		if (i < responseLen - 1 && !waitForAcknowledge(120))
			break;
	}

	/* Parse response */
	state->buttons = (response[2] | (response[3] << 8)) ^ 0xFFFF;

	/* Check if analog controller (type 0x7 = DualShock, 0x5 = Analog stick) */
	if (type == 0x7 || type == 0x5) {
		state->isAnalog = true;
		state->rightX = response[4];
		state->rightY = response[5];
		state->leftX = response[6];
		state->leftY = response[7];
	}

done:
	/* Release DTR */
	delayMicroseconds(60);
	SIO_CTRL(0) &= ~SIO_CTRL_DTR;
}

int main(int argc, const char **argv) {
	/* Initialize serial for debugging */
	initSerialIO(115200);

	/* Initialize controller bus */
	initControllerBus();

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

	/* Upload font to VRAM (placed after model texture) */
	TextureInfo font;
	uploadIndexedTexture(
		&font,
		fontTexture,
		fontPalette,
		SCREEN_WIDTH * 2,          /* Image X */
		TEXTURE_HEIGHT,            /* Image Y (below model texture) */
		SCREEN_WIDTH * 2,          /* Palette X */
		TEXTURE_HEIGHT + FONT_HEIGHT, /* Palette Y */
		FONT_WIDTH,
		FONT_HEIGHT,
		FONT_COLOR_DEPTH
	);
	puts("Font uploaded to VRAM");

	/* Load 3D model from embedded data */
	Model model;
	if (!loadModel(&model, modelData, modelData_size)) {
		puts("Failed to load model!");
		return 1;
	}
	printf("Model loaded: %d verts, %d faces\n", model.numVertices, model.numFaces);

	/* Initialize SPU FIRST (matches psyqo order - SPU::reset before BIOS events) */
	setupSPU();
	puts("SPU initialized");

	/* Initialize BIOS events for HLE compatibility (required for EmulatorJS) */
	/* This sets up DMA event handlers that HLE BIOS needs */
	biosInit();
	puts("BIOS events initialized");

	/* Upload and play music (SPU-ADPCM format) */
	if (musicData_size > 0) {
		uint32_t musicAddr = uploadVAG(musicData, musicData_size);
		spuUnmute();  /* Unmute after upload (psyqo order) */
		playSample(0, musicAddr, 22050, 0x3FFF);  /* Channel 0, 22kHz, max volume */
		puts("Music started on SPU");
	}

	/* Double buffering */
	DMAChain dmaChains[2];
	bool     usingSecondFrame = false;

	/* Rotation angles controlled by player */
	int rotationYaw   = 0;
	int rotationPitch = 0;
	int rotationRoll  = 0;

	puts("Lander model viewer starting...");
	puts("Use D-pad or left stick to rotate");
	puts("Use L1/R1 or right stick for roll");

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

		/* Poll controller and update rotation */
		ControllerState pad;
		pollController(0, &pad);

		/* Rotation speed (in fixed-point units per frame) */
		const int ROTATION_SPEED = 32;
		const int ANALOG_DEADZONE = 20;  /* Deadzone around center (0x80) */

		/* Check for analog stick input first */
		if (pad.isAnalog) {
			/* Left stick controls yaw and pitch */
			int stickX = (int)pad.leftX - 0x80;  /* -128 to +127 */
			int stickY = (int)pad.leftY - 0x80;

			/* Apply deadzone */
			if (stickX > ANALOG_DEADZONE || stickX < -ANALOG_DEADZONE)
				rotationYaw += stickX / 4;
			if (stickY > ANALOG_DEADZONE || stickY < -ANALOG_DEADZONE)
				rotationPitch += stickY / 4;

			/* Right stick controls roll (X axis only) */
			int rightX = (int)pad.rightX - 0x80;
			if (rightX > ANALOG_DEADZONE || rightX < -ANALOG_DEADZONE)
				rotationRoll += rightX / 4;
		}

		/* D-pad controls yaw and pitch (works with both digital and analog) */
		if (pad.buttons & PAD_LEFT)
			rotationYaw -= ROTATION_SPEED;
		if (pad.buttons & PAD_RIGHT)
			rotationYaw += ROTATION_SPEED;
		if (pad.buttons & PAD_UP)
			rotationPitch -= ROTATION_SPEED;
		if (pad.buttons & PAD_DOWN)
			rotationPitch += ROTATION_SPEED;

		/* L1/R1 control roll */
		if (pad.buttons & PAD_L1)
			rotationRoll -= ROTATION_SPEED;
		if (pad.buttons & PAD_R1)
			rotationRoll += ROTATION_SPEED;

		/* Reset GTE translation vector and rotation matrix */
		gte_setControlReg(GTE_TRX,    0);
		gte_setControlReg(GTE_TRY,    0);
		gte_setControlReg(GTE_TRZ, 300);  /* Distance from camera (closer = larger) */
		gte_setRotationMatrix(
			ONE,   0,   0,
			0, ONE,   0,
			0,   0, ONE
		);

		/* Rotate the model based on player input */
		rotateCurrentMatrix(rotationYaw, rotationPitch, rotationRoll);

		/* Draw model faces */
		for (int i = 0; i < model.numFaces; i++) {
			const Face *face = &model.faces[i];

			/* Load vertices into GTE */
			gte_loadV0(&model.vertices[face->v0]);
			gte_loadV1(&model.vertices[face->v1]);
			gte_loadV2(&model.vertices[face->v2]);

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
			const UV *uv0 = &model.uvs[face->uv0];
			const UV *uv1 = &model.uvs[face->uv1];
			const UV *uv2 = &model.uvs[face->uv2];

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

		/* ========================================
		 * Controller HUD - Text display
		 * Note: Don't add bufferX/bufferY - fbOrigin handles buffer offset
		 * ======================================== */
		{
			char hudText[128];

			/* Mode indicator */
			if (pad.isAnalog) {
				printString(chain, &font, 8, 8, "ANALOG");
			} else {
				printString(chain, &font, 8, 8, "DIGITAL");
			}

			/* D-pad direction */
			const char *dpadDir = "-";
			if (pad.buttons & PAD_UP)         dpadDir = "UP";
			else if (pad.buttons & PAD_DOWN)  dpadDir = "DOWN";
			else if (pad.buttons & PAD_LEFT)  dpadDir = "LEFT";
			else if (pad.buttons & PAD_RIGHT) dpadDir = "RIGHT";

			sprintf(hudText, "DPAD: %s", dpadDir);
			printString(chain, &font, 8, 20, hudText);

			/* Left analog stick values */
			sprintf(hudText, "L: X=%3d Y=%3d", pad.leftX, pad.leftY);
			printString(chain, &font, 8, SCREEN_HEIGHT - 30, hudText);

			/* Right analog stick values */
			sprintf(hudText, "R: X=%3d Y=%3d", pad.rightX, pad.rightY);
			printString(chain, &font, 8, SCREEN_HEIGHT - 18, hudText);
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
