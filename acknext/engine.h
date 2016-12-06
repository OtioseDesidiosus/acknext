#pragma once

#include <acknext.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#define HANDLE_TASK   1
#define HANDLE_SCRIPT 2

void engine_seterror(ERROR code, char const * message, ...);

void engine_setsdlerror();

// Renderer API
void render_frame();
void initialize_renderer();

// Scheduler API
void scheduler_update();
void scheduler_initialize();
void scheduler_shutdown();

// Compiler API

bool compiler_init();
bool compiler_add(char const * fileName);
bool compiler_start();

// Handle System
HANDLE handle_getnew(int type);

struct engine
{
    SDL_Window * window;
    SDL_GLContext context;
};

extern struct engine engine;
