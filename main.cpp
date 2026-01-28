/*
   Uphill Both Ways - Prototype Chunk 1 (Celeste Physics Version)
   Target: Android (SDL2)
*/

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>

// --- Celeste Constants (Scaled x4 for 720p) ---
const int LOGICAL_WIDTH = 1280;
const int LOGICAL_HEIGHT = 720;
const float SCALE = 4.0f;

// Values taken directly from Player.cs and scaled
const float GRAVITY = 900.0f * SCALE;
const float MAX_FALL = 160.0f * SCALE;
const float FAST_MAX_FALL = 240.0f * SCALE;
const float MAX_RUN = 90.0f * SCALE;
const float RUN_ACCEL = 1000.0f * SCALE;
const float RUN_REDUCE = 400.0f * SCALE;
const float JUMP_SPEED = -105.0f * SCALE;
const float VAR_JUMP_TIME = 0.2f; // Time you can hold jump for higher jump
const float AIR_MULT = 0.65f;

// --- Helper Math (Monocle/Celeste logic) ---
float Approach(float val, float target, float maxMove) {
    if (val > target) {
        return std::max(val - maxMove, target);
    } else {
        return std::min(val + maxMove, target);
    }
}

// --- Structures ---
struct Rect { float x, y, w, h; };

struct Player {
    Rect rect;
    float vx, vy;
    bool onGround;
    float varJumpTimer; // For variable jump height
    bool autoJump;      // Buffer
};

struct TouchButton {
    SDL_Rect rect;
    bool active;
    long long fingerId;
    Uint8 r, g, b, a;
};

// --- Globals ---
SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
bool isRunning = true;
Player player;
std::vector<Rect> platforms;

// Input State
int moveInput = 0; // -1, 0, 1
bool jumpPressed = false;
bool jumpHeld = false;

// UI
TouchButton btnLeft, btnRight, btnJump;

bool CheckCollision(Rect a, Rect b) {
    return (a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y);
}

void InitGame() {
    // Player Setup (Red Madeline Box)
    player.rect = {100, 400, 8.0f * SCALE, 11.0f * SCALE}; // 32x44 approx
    player.vx = 0;
    player.vy = 0;
    player.onGround = false;
    player.varJumpTimer = 0;

    // Platforms (Green)
    platforms.push_back({0, 650, 1280, 70}); // Floor
    platforms.push_back({400, 550, 200, 30});
    platforms.push_back({700, 450, 200, 30});
    platforms.push_back({1000, 350, 200, 30});
    platforms.push_back({1250, 0, 30, 720}); // Wall

    // UI Buttons
    btnLeft.rect = {50, 550, 150, 150};
    btnLeft.active = false; btnLeft.fingerId = -1;
    btnLeft.r=255; btnLeft.g=255; btnLeft.b=255; btnLeft.a=100;

    btnRight.rect = {220, 550, 150, 150};
    btnRight.active = false; btnRight.fingerId = -1;
    btnRight.r=255; btnRight.g=255; btnRight.b=255; btnRight.a=100;

    btnJump.rect = {1050, 500, 180, 180};
    btnJump.active = false; btnJump.fingerId = -1;
    btnJump.r=0; btnJump.g=255; btnJump.b=0; btnJump.a=100;
}

void HandleTouch(int type, long long fingerId, float x, float y) {
    int lx = (int)(x * LOGICAL_WIDTH);
    int ly = (int)(y * LOGICAL_HEIGHT);
    SDL_Point p = {lx, ly};

    if (type == SDL_FINGERDOWN) {
        if (SDL_PointInRect(&p, &btnLeft.rect)) { btnLeft.active = true; btnLeft.fingerId = fingerId; }
        if (SDL_PointInRect(&p, &btnRight.rect)) { btnRight.active = true; btnRight.fingerId = fingerId; }
        if (SDL_PointInRect(&p, &btnJump.rect)) { 
            btnJump.active = true; 
            btnJump.fingerId = fingerId; 
            jumpPressed = true; 
            jumpHeld = true; 
        }
    }
    else if (type == SDL_FINGERUP) {
        if (fingerId == btnLeft.fingerId) { btnLeft.active = false; btnLeft.fingerId = -1; }
        if (fingerId == btnRight.fingerId) { btnRight.active = false; btnRight.fingerId = -1; }
        if (fingerId == btnJump.fingerId) { 
            btnJump.active = false; 
            btnJump.fingerId = -1; 
            jumpHeld = false; 
        }
    }
}

void Update(float dt) {
    // Input Processing
    moveInput = 0;
    if (btnLeft.active) moveInput = -1;
    if (btnRight.active) moveInput = 1;

    // --- Celeste Physics Implementation ---

    // 1. Horizontal Movement (RunAccel / RunReduce)
    float targetSpeed = moveInput * MAX_RUN;
    float accel = RUN_ACCEL;
    
    // Air Multiplier
    if (!player.onGround) accel *= AIR_MULT;

    // Friction vs Acceleration logic
    if (std::abs(player.vx) > MAX_RUN && std::signbit(player.vx) == std::signbit(moveInput)) {
        // Reduce back from beyond max speed
        player.vx = Approach(player.vx, targetSpeed, RUN_REDUCE * dt); 
    } else {
        // Approach max speed
        player.vx = Approach(player.vx, targetSpeed, accel * dt);
    }

    // 2. Jumping
    if (player.varJumpTimer > 0) {
        player.varJumpTimer -= dt;
    }

    if (jumpPressed) {
        jumpPressed = false; // Consume buffer
        if (player.onGround) {
            player.vy = JUMP_SPEED;
            player.varJumpTimer = VAR_JUMP_TIME;
            player.onGround = false;
        }
    }

    // Variable Jump Height (holding button keeps velocity up)
    if (player.varJumpTimer > 0) {
        if (jumpHeld) {
            player.vy = std::min(player.vy, JUMP_SPEED); // Keep going up
        } else {
            player.varJumpTimer = 0; // Cut jump short
        }
    }

    // 3. Gravity
    float maxFall = MAX_FALL;
    // Fast fall if holding down (optional, keeping simple for now) or apex
    if (std::abs(player.vy) < 40.0f * SCALE && !jumpHeld) {
        player.vy = Approach(player.vy, maxFall, GRAVITY * 0.5f * dt); // Apex floatiness
    } else {
        player.vy = Approach(player.vy, maxFall, GRAVITY * dt);
    }

    // 4. Move X
    player.rect.x += player.vx * dt;
    Rect pRect = player.rect;
    for (const auto& plat : platforms) {
        if (CheckCollision(pRect, plat)) {
            if (player.vx > 0) player.rect.x = plat.x - player.rect.w;
            else if (player.vx < 0) player.rect.x = plat.x + plat.w;
            player.vx = 0;
        }
    }

    // 5. Move Y
    player.rect.y += player.vy * dt;
    player.onGround = false;
    pRect = player.rect;
    for (const auto& plat : platforms) {
        if (CheckCollision(pRect, plat)) {
            if (player.vy > 0) { // Landing
                player.rect.y = plat.y - player.rect.h;
                player.onGround = true;
                player.vy = 0;
            } else if (player.vy < 0) { // Head bonk
                player.rect.y = plat.y + plat.h;
                player.vy = 0;
                player.varJumpTimer = 0;
            }
        }
    }

    // Bounds Reset
    if (player.rect.y > LOGICAL_HEIGHT + 100) {
        player.rect.x = 100;
        player.rect.y = 400;
        player.vx = 0;
        player.vy = 0;
    }
}

void Render() {
    SDL_SetRenderDrawColor(renderer, 135, 206, 235, 255);
    SDL_RenderClear(renderer);

    // Platforms
    SDL_SetRenderDrawColor(renderer, 34, 139, 34, 255);
    for (const auto& plat : platforms) {
        SDL_Rect r = {(int)plat.x, (int)plat.y, (int)plat.w, (int)plat.h};
        SDL_RenderFillRect(renderer, &r);
    }

    // Player
    SDL_SetRenderDrawColor(renderer, 172, 50, 50, 255); // Madeline Hair Color
    SDL_Rect rPlayer = {(int)player.rect.x, (int)player.rect.y, (int)player.rect.w, (int)player.rect.h};
    SDL_RenderFillRect(renderer, &rPlayer);

    // UI
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 100);
    SDL_RenderFillRect(renderer, &btnLeft.rect);
    SDL_RenderFillRect(renderer, &btnRight.rect);
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 100);
    SDL_RenderFillRect(renderer, &btnJump.rect);

    SDL_RenderPresent(renderer);
}

int main(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("Uphill", 0, 0, LOGICAL_WIDTH, LOGICAL_HEIGHT, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(renderer, LOGICAL_WIDTH, LOGICAL_HEIGHT);

    InitGame();

    Uint32 lastTime = SDL_GetTicks();
    while (isRunning) {
        Uint32 curTime = SDL_GetTicks();
        float dt = (curTime - lastTime) / 1000.0f;
        lastTime = curTime;
        if (dt > 0.05f) dt = 0.05f;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) isRunning = false;
            else if (e.type >= SDL_FINGERDOWN && e.type <= SDL_FINGERMOTION) {
                HandleTouch(e.type, e.tf.fingerId, e.tf.x, e.tf.y);
            }
        }
        Update(dt);
        Render();
    }
    SDL_Quit();
    return 0;
}
