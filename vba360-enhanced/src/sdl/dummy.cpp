#include <stdio.h>
#include <SDL.h>

//I hate hacks

int realsystemRedShift = 0;
bool linkenable = false;
int lspeed = 0;

// Stub for SDL_AppActiveInit - workaround for missing symbol in libSDLx360
extern "C" int SDL_AppActiveInit(void)
{
	// Simple stub implementation
	return 0;
}



void LinkSStop()
{
 
}

void StartLink(unsigned short)
{
 
}

void LinkSSend(unsigned short)
{
 
}

void StartGPLink(unsigned short)
{
 
}

void StartJOYLink(unsigned short)
{
 
}

void LinkUpdate(int)
{
 
}

