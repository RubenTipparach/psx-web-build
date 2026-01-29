/*
* PSX Lander Model Viewer - Bare Metal Version
* Ported from PSn00bSDK to ps1-bare-metal
*/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "gpu.h"
#include "spu.h"
#include "cdda.h"
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

/* Starfield configuration - scrolling right to left */
#define NUM_STARS 80

/* Star structure - 2D scrolling with parallax layers */
typedef struct {
	int16_t x, y;
	uint8_t brightness;
	uint8_t speed;
	uint8_t size;
} Star;

/* 3D Shape types */
#define SHAPE_CUBE     0
#define SHAPE_PYRAMID  1
#define SHAPE_SPHERE   2  /* Low-poly octahedron */
#define NUM_SHAPES 6

/* 3D Shape structure */
typedef struct {
	int16_t x, y, z;
	int16_t rotX, rotY, rotZ;
	int16_t rotSpeedX, rotSpeedY, rotSpeedZ;
	int16_t moveSpeed;
	uint8_t type;
	uint8_t r, g, b;  /* Base color */
} Shape3D;

/* Global starfield and shapes */
static Star stars[NUM_STARS];
static Shape3D shapes[NUM_SHAPES];

/* Simple pseudo-random number generator */
static uint32_t randSeed = 12345;
static uint32_t fastRand(void) {
	randSeed = randSeed * 1103515245 + 12345;
	return (randSeed >> 16) & 0x7FFF;
}

/* Initialize a single star at right edge */
static void resetStar(Star *star, bool randomX) {
	star->x = randomX ? (int16_t)(fastRand() % SCREEN_WIDTH) : SCREEN_WIDTH + (fastRand() % 20);
	star->y = (int16_t)(fastRand() % SCREEN_HEIGHT);
	int layer = fastRand() % 3;
	if (layer == 0) {
		star->brightness = 60 + (fastRand() % 40);
		star->speed = 1;
		star->size = 1;
	} else if (layer == 1) {
		star->brightness = 120 + (fastRand() % 60);
		star->speed = 2;
		star->size = 1;
	} else {
		star->brightness = 200 + (fastRand() % 55);
		star->speed = 3 + (fastRand() % 2);
		star->size = 2;
	}
}

/* Calculate screen X position from world coordinates */
/* screenX = (worldX * focalLength) / worldZ + CENTERX */
/* focalLength = SCREEN_WIDTH/2 = 160 */
static int getScreenX(int worldX, int worldZ) {
	if (worldZ <= 0) return SCREEN_WIDTH + 100;  /* Off-screen if behind camera */
	return (worldX * (SCREEN_WIDTH / 2)) / worldZ + CENTERX;
}

/* Initialize a 3D shape - spawns off-screen to the right */
static void resetShape(Shape3D *shape, bool randomX) {
	shape->z = 350 + (fastRand() % 250);  /* Set Z first since X depends on it */
	shape->y = (int16_t)((fastRand() % 180) - 90);
	if (randomX) {
		/* Initial spawn: random position across screen */
		shape->x = (int16_t)((fastRand() % 600) - 150);
	} else {
		/* Respawn: start off-screen right. Need worldX > worldZ to be off right edge */
		/* Add some margin so shape is fully off-screen */
		shape->x = shape->z + 50 + (fastRand() % 100);
	}
	shape->rotX = fastRand() % 4096;
	shape->rotY = fastRand() % 4096;
	shape->rotZ = fastRand() % 4096;
	shape->rotSpeedX = (fastRand() % 40) - 20;
	shape->rotSpeedY = (fastRand() % 50) - 25;
	shape->rotSpeedZ = (fastRand() % 30) - 15;
	shape->moveSpeed = 2 + (fastRand() % 3);
	shape->type = fastRand() % 3;
	/* Vibrant colors */
	int colorType = fastRand() % 6;
	switch (colorType) {
		case 0: shape->r = 255; shape->g = 80;  shape->b = 80;  break;  /* Red */
		case 1: shape->r = 80;  shape->g = 255; shape->b = 80;  break;  /* Green */
		case 2: shape->r = 80;  shape->g = 80;  shape->b = 255; break;  /* Blue */
		case 3: shape->r = 255; shape->g = 255; shape->b = 80;  break;  /* Yellow */
		case 4: shape->r = 255; shape->g = 80;  shape->b = 255; break;  /* Magenta */
		default: shape->r = 80; shape->g = 255; shape->b = 255; break;  /* Cyan */
	}
}

/* Initialize starfield and shapes */
static void initStarfield(void) {
	for (int i = 0; i < NUM_STARS; i++) {
		resetStar(&stars[i], true);
	}
	for (int i = 0; i < NUM_SHAPES; i++) {
		resetShape(&shapes[i], true);
	}
}

/* Update starfield and shapes */
static void updateStarfield(void) {
	for (int i = 0; i < NUM_STARS; i++) {
		stars[i].x -= stars[i].speed;
		if (stars[i].x < -2) {
			resetStar(&stars[i], false);
		}
	}
	for (int i = 0; i < NUM_SHAPES; i++) {
		shapes[i].x -= shapes[i].moveSpeed;
		shapes[i].rotX += shapes[i].rotSpeedX;
		shapes[i].rotY += shapes[i].rotSpeedY;
		shapes[i].rotZ += shapes[i].rotSpeedZ;
		/* Check screen-space position - reset when fully off-screen left */
		/* Shape size on screen is roughly (20 * 160) / z pixels */
		int screenX = getScreenX(shapes[i].x, shapes[i].z);
		int screenSize = (20 * (SCREEN_WIDTH / 2)) / shapes[i].z;
		if (screenX < -screenSize) {
			resetShape(&shapes[i], false);
		}
	}
}

/* Draw a single flat-shaded triangle for background shapes */
/* Uses depth index to sort: higher zIdx = drawn first (further back) */
/* Background shapes should use indices from ORDERING_TABLE_SIZE/2 to ORDERING_TABLE_SIZE-3 */
static void drawBgTriangle(DMAChain *chain, uint8_t r, uint8_t g, uint8_t b, int zIdx) {
	/* Clamp zIdx to background range (behind main model, in front of stars) */
	/* Main model uses indices 0 to ~ORDERING_TABLE_SIZE/2 */
	/* Background shapes use ORDERING_TABLE_SIZE/2 to ORDERING_TABLE_SIZE-3 */
	int minIdx = ORDERING_TABLE_SIZE / 2;
	int maxIdx = ORDERING_TABLE_SIZE - 3;

	if (zIdx < minIdx) zIdx = minIdx;
	if (zIdx > maxIdx) zIdx = maxIdx;

	uint32_t *ptr = allocatePacket(chain, zIdx, 4);
	ptr[0] = gp0_rgb(r, g, b) | gp0_triangle(false, false);
	gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
	gte_storeDataReg(GTE_SXY1, 2 * 4, ptr);
	gte_storeDataReg(GTE_SXY2, 3 * 4, ptr);
}

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

	if (!waitForAcknowledge(500)) {
		SIO_CTRL(0) &= ~SIO_CTRL_DTR;
		return;  /* No controller */
	}

	/* Clear receive buffer with timeout */
	int clearTimeout = 2000;
	while ((SIO_STAT(0) & SIO_STAT_RX_NOT_EMPTY) && clearTimeout-- > 0)
		SIO_DATA(0);

	/* Send poll command - request up to 8 bytes for analog data */
	uint8_t response[8] = {0, 0, 0, 0, 0x80, 0x80, 0x80, 0x80};
	uint8_t request[] = { 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	/* First byte tells us the controller type and response length */
	response[0] = exchangeByteWithTimeout(request[0], 20000);
	if (!waitForAcknowledge(500)) goto done;

	/* Get controller type from upper nibble, length from lower nibble */
	int type = response[0] >> 4;
	int halfwords = response[0] & 0x0F;  /* Response length in 16-bit units */
	int responseLen = (halfwords + 1) * 2;  /* Convert to bytes */
	if (responseLen > 8) responseLen = 8;

	/* Read remaining bytes */
	for (int i = 1; i < responseLen; i++) {
		response[i] = exchangeByteWithTimeout(request[i], 20000);
		if (i < responseLen - 1 && !waitForAcknowledge(500))
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

	/* Upload SPU sound effect (guitar) - triggered by button press */
	uint32_t spuSoundAddr = 0;
	if (musicData_size > 0) {
		spuSoundAddr = uploadVAG(musicData, musicData_size);
		printf("SPU: Sound uploaded to 0x%05lX\n", (unsigned long)spuSoundAddr);
	}

	/* Initialize CD-DA for background music */
	/* NOTE: Do this BEFORE spuUnmute() since initCDDA() touches SPU_CTRL */
	initCDDA();
	puts("CD-DA initialized - music playing from disc");

	/* Unmute SPU AFTER CD-DA init (CD-DA init modifies SPU_CTRL) */
	spuUnmute();
	puts("SPU unmuted - press X for sound effect");

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
	puts("Press X button to play sound effect");

	/* Track previous button state for edge detection */
	uint16_t prevButtons = 0;

	/* Initialize starfield */
	initStarfield();
	puts("Starfield initialized");

	/* Background flash effect (0 = purple, 255 = yellow) */
	int bgFlash = 0;

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

		/* X button triggers SPU sound effect and flash (edge detection - only on press) */
		if ((pad.buttons & PAD_X) && !(prevButtons & PAD_X)) {
			if (spuSoundAddr != 0) {
				playSample(0, spuSoundAddr, 22050, 0x3FFF);
			}
			bgFlash = 255;  /* Trigger yellow flash */
		}
		prevButtons = pad.buttons;

		/* Fade flash back to purple */
		if (bgFlash > 0) {
			bgFlash -= 12;
			if (bgFlash < 0) bgFlash = 0;
		}

		/* Update starfield animation */
		updateStarfield();

		/* Update CD-DA looping */
		updateCDDA();

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

			/* Face buttons display */
			sprintf(hudText, "BTN: %s%s%s%s",
				(pad.buttons & PAD_X) ? "X " : "",
				(pad.buttons & PAD_CIRCLE) ? "O " : "",
				(pad.buttons & PAD_SQUARE) ? "[] " : "",
				(pad.buttons & PAD_TRIANGLE) ? "/\\ " : "");
			printString(chain, &font, 8, 32, hudText);

			/* Shoulder buttons */
			sprintf(hudText, "SH: %s%s%s%s",
				(pad.buttons & PAD_L1) ? "L1 " : "",
				(pad.buttons & PAD_R1) ? "R1 " : "",
				(pad.buttons & PAD_L2) ? "L2 " : "",
				(pad.buttons & PAD_R2) ? "R2 " : "");
			printString(chain, &font, 8, 44, hudText);

			/* Left analog stick values */
			sprintf(hudText, "L: X=%3d Y=%3d", pad.leftX, pad.leftY);
			printString(chain, &font, 8, SCREEN_HEIGHT - 30, hudText);

			/* Right analog stick values */
			sprintf(hudText, "R: X=%3d Y=%3d", pad.rightX, pad.rightY);
			printString(chain, &font, 8, SCREEN_HEIGHT - 18, hudText);
		}

		/* Calculate gradient colors based on flash state */
		/* Top: deep purple, Bottom: dark blue/black */
		/* Flash shifts toward yellow/orange */
		int topR = 60 + ((255 - 60) * bgFlash) / 255;
		int topG = 20 + ((220 - 20) * bgFlash) / 255;
		int topB = 90 + ((80 - 90) * bgFlash) / 255;
		int botR = 15 + ((180 - 15) * bgFlash) / 255;
		int botG = 5 + ((100 - 5) * bgFlash) / 255;
		int botB = 35 + ((40 - 35) * bgFlash) / 255;

		/* Draw gradient background as two Gouraud-shaded triangles (quad) */
		/* This creates a smooth vertical gradient with PSX hardware dithering */
		/* First triangle: top-left, top-right, bottom-right */
		ptr = allocatePacket(chain, ORDERING_TABLE_SIZE - 1, 6);
		ptr[0] = gp0_rgb(topR, topG, topB) | gp0_shadedTriangle(true, false, false);
		ptr[1] = gp0_xy(0, 0);                    /* Top-left */
		ptr[2] = gp0_rgb(topR, topG, topB);
		ptr[3] = gp0_xy(SCREEN_WIDTH, 0);         /* Top-right */
		ptr[4] = gp0_rgb(botR, botG, botB);
		ptr[5] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT); /* Bottom-right */

		/* Second triangle: top-left, bottom-right, bottom-left */
		ptr = allocatePacket(chain, ORDERING_TABLE_SIZE - 1, 6);
		ptr[0] = gp0_rgb(topR, topG, topB) | gp0_shadedTriangle(true, false, false);
		ptr[1] = gp0_xy(0, 0);                    /* Top-left */
		ptr[2] = gp0_rgb(botR, botG, botB);
		ptr[3] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT); /* Bottom-right */
		ptr[4] = gp0_rgb(botR, botG, botB);
		ptr[5] = gp0_xy(0, SCREEN_HEIGHT);        /* Bottom-left */

		/* Draw stars as flat shaded rectangles (right to left scrolling) */
		for (int i = 0; i < NUM_STARS; i++) {
			if (stars[i].x < 0 || stars[i].x >= SCREEN_WIDTH ||
			    stars[i].y < 0 || stars[i].y >= SCREEN_HEIGHT)
				continue;
			ptr = allocatePacket(chain, ORDERING_TABLE_SIZE - 2, 3);
			ptr[0] = gp0_rgb(stars[i].brightness, stars[i].brightness, stars[i].brightness) | gp0_rectangle(false, false, false);
			ptr[1] = gp0_xy(stars[i].x, stars[i].y);
			ptr[2] = gp0_xy(stars[i].size, stars[i].size);
		}

		/* Draw 3D shapes in background */
		/* Shapes should appear BEHIND the main model (which is at z=300) */
		/* We use ordering table indices: higher = drawn first (further back) */
		for (int s = 0; s < NUM_SHAPES; s++) {
			Shape3D *sh = &shapes[s];

			/* Only cull if completely behind camera */
			if (sh->z < 50) continue;

			/* Set up GTE for this shape */
			gte_setControlReg(GTE_TRX, sh->x);
			gte_setControlReg(GTE_TRY, sh->y);
			gte_setControlReg(GTE_TRZ, sh->z);
			gte_setRotationMatrix(ONE, 0, 0, 0, ONE, 0, 0, 0, ONE);
			rotateCurrentMatrix(sh->rotY, sh->rotX, sh->rotZ);

			int sz = 20;  /* Shape size */
			GTEVector16 v[8];  /* Vertex buffer */

			if (sh->type == SHAPE_CUBE) {
				/* Cube vertices */
				v[0] = (GTEVector16){-sz, -sz, -sz};  /* Back bottom left */
				v[1] = (GTEVector16){ sz, -sz, -sz};  /* Back bottom right */
				v[2] = (GTEVector16){ sz,  sz, -sz};  /* Back top right */
				v[3] = (GTEVector16){-sz,  sz, -sz};  /* Back top left */
				v[4] = (GTEVector16){-sz, -sz,  sz};  /* Front bottom left */
				v[5] = (GTEVector16){ sz, -sz,  sz};  /* Front bottom right */
				v[6] = (GTEVector16){ sz,  sz,  sz};  /* Front top right */
				v[7] = (GTEVector16){-sz,  sz,  sz};  /* Front top left */

				/* 6 faces, 2 triangles each - different brightness per face */
				int faces[6][4] = {
					{4, 5, 6, 7},  /* Front */
					{1, 0, 3, 2},  /* Back */
					{0, 4, 7, 3},  /* Left */
					{5, 1, 2, 6},  /* Right */
					{7, 6, 2, 3},  /* Top */
					{0, 1, 5, 4},  /* Bottom */
				};
				int bright[6] = {100, 60, 80, 80, 100, 50};

				for (int f = 0; f < 6; f++) {
					int br = bright[f];
					uint8_t fr = (sh->r * br) / 100;
					uint8_t fg = (sh->g * br) / 100;
					uint8_t fb = (sh->b * br) / 100;

					/* First triangle of quad */
					gte_loadV0(&v[faces[f][0]]);
					gte_loadV1(&v[faces[f][1]]);
					gte_loadV2(&v[faces[f][2]]);
					gte_command(GTE_CMD_RTPT | GTE_SF);
					gte_command(GTE_CMD_NCLIP);
					if (gte_getDataReg(GTE_MAC0) > 0) {
						gte_command(GTE_CMD_AVSZ3 | GTE_SF);
						int zIdx = gte_getDataReg(GTE_OTZ);
						drawBgTriangle(chain, fr, fg, fb, zIdx);
					}

					/* Second triangle of quad */
					gte_loadV0(&v[faces[f][0]]);
					gte_loadV1(&v[faces[f][2]]);
					gte_loadV2(&v[faces[f][3]]);
					gte_command(GTE_CMD_RTPT | GTE_SF);
					gte_command(GTE_CMD_NCLIP);
					if (gte_getDataReg(GTE_MAC0) > 0) {
						gte_command(GTE_CMD_AVSZ3 | GTE_SF);
						int zIdx = gte_getDataReg(GTE_OTZ);
						drawBgTriangle(chain, fr, fg, fb, zIdx);
					}
				}
			}
			else if (sh->type == SHAPE_PYRAMID) {
				/* Pyramid: apex at top, square base */
				GTEVector16 apex = {0, -sz, 0};
				v[0] = (GTEVector16){-sz, sz, -sz};  /* Base back left */
				v[1] = (GTEVector16){ sz, sz, -sz};  /* Base back right */
				v[2] = (GTEVector16){ sz, sz,  sz};  /* Base front right */
				v[3] = (GTEVector16){-sz, sz,  sz};  /* Base front left */

				/* 4 side faces */
				int sideFaces[4][2] = {{3, 2}, {2, 1}, {1, 0}, {0, 3}};
				int sideBright[4] = {100, 80, 60, 80};

				for (int f = 0; f < 4; f++) {
					int br = sideBright[f];
					uint8_t fr = (sh->r * br) / 100;
					uint8_t fg = (sh->g * br) / 100;
					uint8_t fb = (sh->b * br) / 100;

					gte_loadV0(&apex);
					gte_loadV1(&v[sideFaces[f][0]]);
					gte_loadV2(&v[sideFaces[f][1]]);
					gte_command(GTE_CMD_RTPT | GTE_SF);
					gte_command(GTE_CMD_NCLIP);
					if (gte_getDataReg(GTE_MAC0) > 0) {
						gte_command(GTE_CMD_AVSZ3 | GTE_SF);
						int zIdx = gte_getDataReg(GTE_OTZ);
						drawBgTriangle(chain, fr, fg, fb, zIdx);
					}
				}

				/* Base (2 triangles) */
				uint8_t baseBr = (sh->r * 40) / 100;
				uint8_t baseBg = (sh->g * 40) / 100;
				uint8_t baseBb = (sh->b * 40) / 100;

				gte_loadV0(&v[0]);
				gte_loadV1(&v[1]);
				gte_loadV2(&v[2]);
				gte_command(GTE_CMD_RTPT | GTE_SF);
				gte_command(GTE_CMD_NCLIP);
				if (gte_getDataReg(GTE_MAC0) > 0) {
					gte_command(GTE_CMD_AVSZ3 | GTE_SF);
					drawBgTriangle(chain, baseBr, baseBg, baseBb, gte_getDataReg(GTE_OTZ));
				}

				gte_loadV0(&v[0]);
				gte_loadV1(&v[2]);
				gte_loadV2(&v[3]);
				gte_command(GTE_CMD_RTPT | GTE_SF);
				gte_command(GTE_CMD_NCLIP);
				if (gte_getDataReg(GTE_MAC0) > 0) {
					gte_command(GTE_CMD_AVSZ3 | GTE_SF);
					drawBgTriangle(chain, baseBr, baseBg, baseBb, gte_getDataReg(GTE_OTZ));
				}
			}
			else { /* SHAPE_SPHERE - Octahedron (8 faces) */
				/* 6 vertices: top, bottom, front, back, left, right */
				GTEVector16 top   = {0, -sz, 0};
				GTEVector16 bot   = {0,  sz, 0};
				GTEVector16 front = {0, 0,  sz};
				GTEVector16 back  = {0, 0, -sz};
				GTEVector16 left  = {-sz, 0, 0};
				GTEVector16 right = { sz, 0, 0};

				/* 8 triangular faces */
				GTEVector16 *octaFaces[8][3] = {
					{&top, &front, &right}, {&top, &right, &back},
					{&top, &back, &left},   {&top, &left, &front},
					{&bot, &right, &front}, {&bot, &back, &right},
					{&bot, &left, &back},   {&bot, &front, &left},
				};
				int octaBright[8] = {100, 80, 60, 80, 90, 70, 50, 70};

				for (int f = 0; f < 8; f++) {
					int br = octaBright[f];
					uint8_t fr = (sh->r * br) / 100;
					uint8_t fg = (sh->g * br) / 100;
					uint8_t fb = (sh->b * br) / 100;

					gte_loadV0(octaFaces[f][0]);
					gte_loadV1(octaFaces[f][1]);
					gte_loadV2(octaFaces[f][2]);
					gte_command(GTE_CMD_RTPT | GTE_SF);
					gte_command(GTE_CMD_NCLIP);
					if (gte_getDataReg(GTE_MAC0) > 0) {
						gte_command(GTE_CMD_AVSZ3 | GTE_SF);
						int zIdx = gte_getDataReg(GTE_OTZ);
						drawBgTriangle(chain, fr, fg, fb, zIdx);
					}
				}
			}
		}

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
