#include <SDL.h>
#include <vector>
#include <cmath>
#include <map>
#include <algorithm>

// ==========================================
// CONFIGURATION & CONSTANTS
// ==========================================

const int SCREEN_WIDTH = 1280;
const int SCREEN_HEIGHT = 720;

// Physics Constants
const float GRAVITY = 2200.0f;
const float JUMP_FORCE = -950.0f;
const float MOVE_SPEED = 450.0f;
const float ACCELERATION = 3000.0f;
const float FRICTION = 2500.0f;
const float AIR_FRICTION = 500.0f;
const float MAX_FALL_SPEED = 1000.0f;
const float TIME_STEP = 0.016f;

// ==========================================
// STRUCTS
// ==========================================

struct Vec2 { float x, y; };

struct Player {
    Vec2 pos;
    Vec2 vel;
    Vec2 size;
    bool onGround;
};

struct Obstacle {
    SDL_Rect rect;
    int type;
};

// Input Abstraction (Decouples Hardware from Logic)
struct InputState {
    bool left = false;
    bool right = false;
    bool jumpPressed = false; // True only on the frame pressed
    bool jumpHeld = false;    // True while holding button
};

// Touch Button Definition
struct TouchButton {
    SDL_Rect rect;
    std::string name; // For debugging
    bool active;      // Visual feedback
};

// ==========================================
// GLOBALS
// ==========================================

SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
bool isRunning = true;
std::vector<Obstacle> obstacles;

// Touch State Tracking
std::map<SDL_FingerID, Vec2> activeFingers;
TouchButton btnLeft, btnRight, btnJump;

// ==========================================
// SETUP & LEVELS
// ==========================================

void InitControls() {
    // Bottom Left Corner
    btnLeft =  { {50, 550, 150, 150}, "Left", false };
    btnRight = { {250, 550, 150, 150}, "Right", false };
    
    // Bottom Right Corner
    btnJump =  { {1030, 550, 200, 150}, "Jump", false };
}

void LoadLevel() {
    obstacles.clear();
    // 1. Floor
    obstacles.push_back({ {0, 600, 1280, 120}, 0 });
    // 2. Steps
    obstacles.push_back({ {300, 500, 200, 20}, 0 });
    obstacles.push_back({ {600, 400, 200, 20}, 0 });
    obstacles.push_back({ {900, 250, 300, 20}, 0 });
    // 3. Walls & Ceiling
    obstacles.push_back({ {-50, 0, 50, 720}, 0 });
    obstacles.push_back({ {1280, 0, 50, 720}, 0 });
    obstacles.push_back({ {400, 200, 100, 20}, 0 });
}

bool CheckCollision(const SDL_Rect& a, const SDL_Rect& b) {
    return (a.x < b.x + b.w && a.x + a.w > b.x &&
            a.y < b.y + b.h && a.y + a.h > b.y);
}

bool IsPointInRect(float x, float y, const SDL_Rect& r) {
    return (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h);
}

// ==========================================
// INPUT HANDLING (KEYBOARD + TOUCH)
// ==========================================

void HandleInput(InputState& input) {
    SDL_Event e;
    
    // Reset "Frame Perfect" inputs
    input.jumpPressed = false;

    // 1. Event Polling
    while (SDL_PollEvent(&e) != 0) {
        if (e.type == SDL_QUIT) {
            isRunning = false;
        }
        // --- KEYBOARD (PC Testing) ---
        else if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_ESCAPE) isRunning = false;
            if (e.key.keysym.sym == SDLK_SPACE && !e.key.repeat) {
                input.jumpPressed = true;
                input.jumpHeld = true;
            }
        }
        else if (e.type == SDL_KEYUP) {
            if (e.key.keysym.sym == SDLK_SPACE) input.jumpHeld = false;
        }
        
        // --- TOUCH (Android) ---
        // We track fingers manually to support multi-touch (e.g., Run + Jump)
        else if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERMOTION) {
            // Convert normalized (0..1) to Logical (0..1280)
            float x = e.tfinger.x * SCREEN_WIDTH;
            float y = e.tfinger.y * SCREEN_HEIGHT;
            activeFingers[e.tfinger.fingerId] = { x, y };

            // Check if this specific touch *just* hit the jump button (Tap event)
            if (e.type == SDL_FINGERDOWN && IsPointInRect(x, y, btnJump.rect)) {
                input.jumpPressed = true;
            }
        }
        else if (e.type == SDL_FINGERUP) {
            activeFingers.erase(e.tfinger.fingerId);
        }
    }

    // 2. Continuous State Checking
    // Reset continuous flags
    input.left = false;
    input.right = false;
    
    // Keyboard Continuous
    const Uint8* keys = SDL_GetKeyboardState(NULL);
    if (keys[SDL_SCANCODE_LEFT]) input.left = true;
    if (keys[SDL_SCANCODE_RIGHT]) input.right = true;

    // Touch Continuous (Loop through all active fingers)
    bool touchJumpHeld = false;
    
    // Reset visual feedback
    btnLeft.active = false;
    btnRight.active = false;
    btnJump.active = false;

    for (auto const& [id, pos] : activeFingers) {
        if (IsPointInRect(pos.x, pos.y, btnLeft.rect)) {
            input.left = true;
            btnLeft.active = true;
        }
        if (IsPointInRect(pos.x, pos.y, btnRight.rect)) {
            input.right = true;
            btnRight.active = true;
        }
        if (IsPointInRect(pos.x, pos.y, btnJump.rect)) {
            touchJumpHeld = true;
            btnJump.active = true;
        }
    }
    
    // Merge Keyboard and Touch for Jump Hold
    input.jumpHeld = (touchJumpHeld || keys[SDL_SCANCODE_SPACE]);
}

// ==========================================
// PHYSICS
// ==========================================

void UpdatePhysics(Player& player, const InputState& input, float dt) {
    // 1. Horizontal
    float targetSpeed = 0.0f;
    if (input.left) targetSpeed = -MOVE_SPEED;
    if (input.right) targetSpeed = MOVE_SPEED;

    float friction = player.onGround ? FRICTION : AIR_FRICTION;

    if (player.vel.x < targetSpeed) {
        player.vel.x += ACCELERATION * dt;
        if (player.vel.x > targetSpeed) player.vel.x = targetSpeed;
    }
    else if (player.vel.x > targetSpeed) {
        player.vel.x -= ACCELERATION * dt;
        if (player.vel.x < targetSpeed) player.vel.x = targetSpeed;
    }

    if (targetSpeed == 0.0f) {
        if (player.vel.x > 0) {
            player.vel.x -= friction * dt;
            if (player.vel.x < 0) player.vel.x = 0;
        } else if (player.vel.x < 0) {
            player.vel.x += friction * dt;
            if (player.vel.x > 0) player.vel.x = 0;
        }
    }

    // 2. Jumping
    // Initial Jump
    if (input.jumpPressed && player.onGround) {
        player.vel.y = JUMP_FORCE;
        player.onGround = false;
    }
    // Variable Jump Height (Celeste Mechanic)
    // If we release the button while moving up, cut the speed
    if (!input.jumpHeld && player.vel.y < 0) {
        player.vel.y *= 0.5f; 
    }

    // 3. Gravity
    player.vel.y += GRAVITY * dt;
    if (player.vel.y > MAX_FALL_SPEED) player.vel.y = MAX_FALL_SPEED;

    // 4. Movement & Collision (Axis Separated)
    // X Axis
    player.pos.x += player.vel.x * dt;
    SDL_Rect pRect = { (int)player.pos.x, (int)player.pos.y, (int)player.size.x, (int)player.size.y };
    for (const auto& obs : obstacles) {
        if (CheckCollision(pRect, obs.rect)) {
            if (player.vel.x > 0) player.pos.x = (float)(obs.rect.x - player.size.x);
            else if (player.vel.x < 0) player.pos.x = (float)(obs.rect.x + obs.rect.w);
            player.vel.x = 0;
        }
    }

    // Y Axis
    player.pos.y += player.vel.y * dt;
    player.onGround = false;
    pRect.x = (int)player.pos.x;
    pRect.y = (int)player.pos.y;
    for (const auto& obs : obstacles) {
        if (CheckCollision(pRect, obs.rect)) {
            if (player.vel.y > 0) {
                player.pos.y = (float)(obs.rect.y - player.size.y);
                player.onGround = true;
                player.vel.y = 0;
            } else if (player.vel.y < 0) {
                player.pos.y = (float)(obs.rect.y + obs.rect.h);
                player.vel.y = 0;
            }
        }
    }

    // World Bounds
    if (player.pos.y > SCREEN_HEIGHT + 100) {
        player.pos = { 100, 500 };
        player.vel = { 0, 0 };
    }
}

// ==========================================
// RENDERING
// ==========================================

void RenderButton(const TouchButton& btn) {
    // Draw Outline
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 150);
    SDL_RenderDrawRect(renderer, &btn.rect);

    // Draw Fill (Visual feedback when pressed)
    if (btn.active) {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 100);
        SDL_RenderFillRect(renderer, &btn.rect);
    }
}

void Render(const Player& player) {
    SDL_SetRenderDrawColor(renderer, 135, 206, 235, 255); // Sky Blue
    SDL_RenderClear(renderer);

    // Obstacles
    SDL_SetRenderDrawColor(renderer, 34, 139, 34, 255); // Forest Green
    for (const auto& obs : obstacles) {
        SDL_RenderFillRect(renderer, &obs.rect);
        SDL_SetRenderDrawColor(renderer, 0, 100, 0, 255); // Border
        SDL_RenderDrawRect(renderer, &obs.rect);
        SDL_SetRenderDrawColor(renderer, 34, 139, 34, 255);
    }

    // Player
    SDL_Rect pRect = { (int)player.pos.x, (int)player.pos.y, (int)player.size.x, (int)player.size.y };
    SDL_SetRenderDrawColor(renderer, 255, 69, 0, 255); // Red-Orange
    SDL_RenderFillRect(renderer, &pRect);

    // UI (On-Screen Controls)
    RenderButton(btnLeft);
    RenderButton(btnRight);
    RenderButton(btnJump);

    SDL_RenderPresent(renderer);
}

// ==========================================
// MAIN
// ==========================================

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) return -1;

    window = SDL_CreateWindow("Uphill Proto", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    
    // Critical for Android: Scale 1280x720 logic to whatever the phone screen is
    SDL_RenderSetLogicalSize(renderer, SCREEN_WIDTH, SCREEN_HEIGHT);

    InitControls();
    LoadLevel();

    Player player;
    player.pos = { 100, 500 };
    player.vel = { 0, 0 };
    player.size = { 32, 64 };
    player.onGround = false;

    InputState inputState;

    Uint64 lastTime = SDL_GetTicks64();
    float accumulator = 0.0f;

    while (isRunning) {
        Uint64 currentTime = SDL_GetTicks64();
        float frameTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;
        if (frameTime > 0.25f) frameTime = 0.25f;

        accumulator += frameTime;

        HandleInput(inputState);

        while (accumulator >= TIME_STEP) {
            UpdatePhysics(player, inputState, TIME_STEP);
            accumulator -= TIME_STEP;
        }

        Render(player);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
