#pragma once

struct SDL_Window;

class Application
{
	SDL_Window* window = nullptr;
	bool running = false;

public:
	void initialize();
	void shutdown();
	void start();
};