//Copyright (c) 2011-2020 <>< Charles Lohr - Under the MIT/x11 or NewBSD License you choose.
// NO WARRANTY! NO GUARANTEE OF SUPPORT! USE AT YOUR OWN RISK

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "os_generic.h"
#include <GLES3/gl3.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include "android_native_app_glue.h"
#include <android/sensor.h>
#include "CNFGAndroid.h"
#include <pthread.h>

#define CNFG_IMPLEMENTATION
#define CNFG3D

#define ANDROID

#include "CNFG.h"

void Log(const char* fmt, ...);

//read image
#include <stdint.h>
#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb/stb_image.h"

float mountainangle;
float mountainoffsetx;
float mountainoffsety;

ASensorManager* sm;
const ASensor* as;
bool no_sensor_for_gyro = false;
ASensorEventQueue* aeq;
ALooper* l;

unsigned frames = 0;
unsigned long iframeno = 0;

void AndroidDisplayKeyboard(int pShow);

int lastbuttonx = 0;
int lastbuttony = 0;
int lastmotionx = 0;
int lastmotiony = 0;
int lastbid = 0;
int lastmask = 0;
int lastkey, lastkeydown;

static int keyboard_up;

void HandleKey(int keycode, int bDown)
{
	lastkey = keycode;
	lastkeydown = bDown;
	if (keycode == 10 && !bDown) { keyboard_up = 0; AndroidDisplayKeyboard(keyboard_up); }

	if (keycode == 4) { AndroidSendToBack(1); } //Handle Physical Back Button.
}

void HandleButton(int x, int y, int button, int bDown)
{
	lastbid = button;
	lastbuttonx = x;
	lastbuttony = y;

	if (bDown) { keyboard_up = !keyboard_up; AndroidDisplayKeyboard(keyboard_up); }
}

void HandleMotion(int x, int y, int mask)
{
	lastmask = mask;
	lastmotionx = x;
	lastmotiony = y;
}

short DisplaySizeX, DisplaySizeY;
short GetX, GetY;

extern struct android_app* gapp;

void HandleDestroy()
{
	Log("Destroying");
	exit(10);
}

volatile int suspended;

void HandleSuspend()
{
	suspended = 1;
}

void HandleResume()
{
	suspended = 0;
}

typedef struct {
	int w;
	int h;
	int c;
	uint32_t* rdimg;
	unsigned int tex;
} image;

void rdimg(image* img, unsigned char* data) {
	uint32_t* rd;
	rd = malloc(sizeof(int) * img->w * img->h);
	uint32_t current = 0;

	for (int y = 0; y < img->h; y += 1) {
		for (int x = 0; x < img->w; x += 1) {
			current = 0;
			for (int i = 0; i < img->c; i++) {
				current = current << 8;
				current += (uint32_t)data[(y * img->w + x) * img->c + i];
			}
			for (int i = 0; i < 4 - img->c; i++) {
				if (current == 1) {
					current = 0x00 | current << 8;
					continue;
				}
				current = 0xff | current << 8;
			}
			rd[(y * img->w + x)] = current;
		}
	}
	img->rdimg = rd;
}

//test load image
image* loadimage(char* path)
{
	int w, h, c;

	unsigned char* data = stbi_load(path, &w, &h, &c, 0);

	image* img;
	img = malloc(sizeof(image));
	img->w = w;
	img->h = h;
	img->c = c;

	if (data == NULL)
	{
		//char buff[256];
		//sprintf(buff, "could not find image at path %s", path);
		img->rdimg = NULL;
		return img;
	}

	// Faster way, but it doesn't seem to work. TODO FIXME
	/*if (c == 4) {
		img->rdimg = (unsigned int *)data;
		return img;
	}*/

	rdimg(img, data);
	stbi_image_free(data);

	return img;
}

void blittex(unsigned int tex, int x, int y, int w, int h) {
	if (w == 0 || h == 0) return;

	CNFGFlushRender();

	glUseProgram(gRDBlitProg);
	glUniform4f(gRDBlitProgUX,
		1.f / gRDLastResizeW, -1.f / gRDLastResizeH,
		-0.5f + x / (float)gRDLastResizeW, 0.5f - y / (float)gRDLastResizeH);
	glUniform1i(gRDBlitProgUT, 0);

	glBindTexture(GL_TEXTURE_2D, tex);

	float cx = w / 2;
	float cy = h / 2;

	float zrotx = 0;
	float zroty = 0;
	float brotx = w;
	float broty = h;
	float wrotx = w;
	float wroty = 0;
	float hrotx = 0;
	float hroty = h;

	const float verts[] = {
		zrotx, zroty, wrotx, wroty, brotx, broty,
		zrotx, zroty, brotx, broty, hrotx, hroty };
	static const uint8_t colors[] = {
		0,0,   255,0,  255,255,
		0,0,  255,255, 0,255 };

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(1, 2, GL_UNSIGNED_BYTE, GL_TRUE, 0, colors);

	glDrawArrays(GL_TRIANGLES, 0, 6);
}

/*
* OGGetAbsoluteTime() dont work from os_generic.h
* i dont understand :(
* if try use OGGetAbsoluteTime then app is segmentation fault :(
*/
double OGGetAbsoluteTime1()
{
	struct timeval tv;
	gettimeofday(&tv, 0);
	return ((double)tv.tv_usec) / 1000000. + (tv.tv_sec);
}


int main()
{
	double ThisTime;
	double LastFPSTime = OGGetAbsoluteTime1();
	int linesegs = 0;

	//CNFGBGColor = 0x00FFD000;
	CNFGBGColor = 0x00FFD000;
	CNFGSetupFullscreen("RawDraw", 0);

	Log("main started!");

	//pthread_t threadLoadLogotype;
	//pthread_create(&threadLoadLogotype, 0, LoadLogotype, 0);

	// Creating random image data and texture
	//load logotype
	unsigned char* buffer, * pixels;
	int width_logo, height_logo;


	AAsset* file = AAssetManager_open(gapp->activity->assetManager, "test.png", AASSET_MODE_BUFFER);
	if (file)
	{
		buffer = AAsset_getBuffer(file);
		pixels = stbi_loadf_from_memory(buffer, AAsset_getLength(file), &width_logo, &height_logo, NULL, 4);
	}
	else
	{
		Log("Where start_logo.png?");
		exit(1);
	}

	const char* getpath = AndroidGetExternalFilesDir();
	char bu[0xFF];
	sprintf(bu, "%s/start_logo.png", getpath);

	image* img;
	img = loadimage(bu);
	img->tex = CNFGTexImage(img->rdimg, img->w, img->h);

	///int width, height, channels;
	///unsigned char *img = stbi_load(bu,&width, &height,&channels,0);

	///unsigned int tex = CNFGTexImage(img, 80, 80);	

	//unsigned int tex = CNFGTexImage(pixels, 256,256);

	//main thread app
	while (1)
	{
		iframeno++;

		CNFGHandleInput();

		//if( suspended ) { usleep(50000); continue; }

		CNFGClearFrame();
		CNFGColor(0xFFFFFFFF);
		CNFGGetDimensions(&DisplaySizeX, &DisplaySizeY);

		//get real size
		//vertical position your phone
		GetX = DisplaySizeX * 0.00092592592f;	// 1/1080
		GetY = DisplaySizeY * 0.00052083333f;	// 1/1920

		float SizeLogoX = DisplaySizeX / 2.16;
		float SizeLogoY = DisplaySizeY / 19.2;

		//CNFGBlitTex(tex, 120, 120, 256, 256);
		int x = 120;
		int y = 120;
		int s = 1;
		int scaling = 1;
		blittex(img->tex, DisplaySizeX / 2 - SizeLogoX / 2, DisplaySizeY / 2 - SizeLogoY / 2, SizeLogoX, SizeLogoY);


		frames++;
		//On Android, CNFGSwapBuffers must be called, and CNFGUpdateScreenWithBitmap does not have an implied framebuffer swap.
		CNFGSwapBuffers();

		ThisTime = OGGetAbsoluteTime1();
		if (ThisTime > LastFPSTime + 1)
		{
			printf("FPS: %d\n", frames);
			frames = 0;
			linesegs = 0;
			LastFPSTime += 1;
		}

	}

	///CNFGDeleteTex(tex);

	return(0);
}

void Log(const char* fmt, ...)
{
	const char* getpath = AndroidGetExternalFilesDir();
	char buffer[0xFF];
	static FILE* flLog = NULL;

	if (flLog == NULL)
	{
		sprintf(buffer, "%s/log.txt", getpath);
		flLog = fopen(buffer, "a");
	}

	memset(buffer, 0, sizeof(buffer));

	va_list arg;
	va_start(arg, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, arg);
	va_end(arg);

	char buf[80];
	time_t seconds = time(NULL);
	struct tm* timeinfo = localtime(&seconds);
	//const char* format = BOEV("%d.%m.%y %H:%M:%S");
	const char* format = "%H:%M:%S";
	strftime(buf, 80, format, timeinfo);

	if (flLog == NULL) return;
	fprintf(flLog, "[%s] %s\n", buf, buffer);
	fflush(flLog);

	return;
}