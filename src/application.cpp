#include "application.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

void Application::initialize()
{
	SDL_InitSubSystem(SDL_INIT_VIDEO);
	window = SDL_CreateWindow("Vulkan Learning", 1280, 720, SDL_WINDOW_VULKAN);
}

void Application::shutdown()
{
	if (window)
	{
		SDL_DestroyWindow(window);
	}
}

void Application::start()
{
	running = true;

	while (running)
	{
		SDL_Event event{ 0 };
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT)
			{
				running = false;
			}
		}
	}
}
