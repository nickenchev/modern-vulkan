#include <SDL3/SDL_main.h>
#include "application.h"

int main(int argc, char *argv[])
{
	Application app;
	app.initialize();
	app.start();
	app.shutdown();

	return 0;
}
