#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
// Include ImGui Headers
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#define MAP_BOX_SCALE 16
#define MAP_BOX_EDGES_LEN (12 + MAP_BOX_SCALE * 2)
#define MAX_PLAYER_COUNT 4
#define CIRCLE_DRAW_SIDES 32
#define CIRCLE_DRAW_SIDES_LEN (CIRCLE_DRAW_SIDES + 1)
struct Player {
    SDL_MouseID mouse;
    SDL_KeyboardID keyboard;
    double pos[3];
    double vel[3];
    unsigned int yaw;
    int pitch;
    float radius, height;
    unsigned char color[3];
    unsigned char wasd;
};
struct AppState {
    SDL_Window* window;
    SDL_Renderer* renderer;
    int player_count;
    Player players[MAX_PLAYER_COUNT];
    float edges[MAP_BOX_EDGES_LEN][6];
    // ImGui Integration State
    bool menu_active;
    bool show_demo_window;
    ImVec4 clear_color;
};
struct AppMetadata {
    const char* key;
    const char* value;
};
static const AppMetadata extended_metadata[] = {
    { SDL_PROP_APP_METADATA_URL_STRING, "[examples.libsdl.org](https://examples.libsdl.org/SDL3/demo/02-woodeneye-008/)" },
    { SDL_PROP_APP_METADATA_CREATOR_STRING, "SDL team" },
    { SDL_PROP_APP_METADATA_COPYRIGHT_STRING, "Placed in the public domain" },
    { SDL_PROP_APP_METADATA_TYPE_STRING, "game" }
};
static int whoseMouse(SDL_MouseID mouse, const Player players[], int players_len) {
    for (int i = 0; i < players_len; i++) {
        if (players[i].mouse == mouse) return i;
    }
    return -1;
}
static int whoseKeyboard(SDL_KeyboardID keyboard, const Player players[], int players_len) {
    for (int i = 0; i < players_len; i++) {
        if (players[i].keyboard == keyboard) return i;
    }
    return -1;
}
static void shoot(int shooter, Player players[], int players_len) {
    double x0 = players[shooter].pos[0];
    double y0 = players[shooter].pos[1];
    double z0 = players[shooter].pos[2];
    double bin_rad = SDL_PI_D / 2147483648.0;
    double yaw_rad = bin_rad * players[shooter].yaw;
    double pitch_rad = bin_rad * players[shooter].pitch;
    double cos_yaw = SDL_cos(yaw_rad);
    double sin_yaw = SDL_sin(yaw_rad);
    double cos_pitch = SDL_cos(pitch_rad);
    double sin_pitch = SDL_sin(pitch_rad);
    double vx = -sin_yaw * cos_pitch;
    double vy = sin_pitch;
    double vz = -cos_yaw * cos_pitch;
    for (int i = 0; i < players_len; i++) {
        if (i == shooter) continue;
        Player* target = &(players[i]);
        int hit = 0;
        for (int j = 0; j < 2; j++) {
            double r = target->radius;
            double h = target->height;
            double dx = target->pos[0] - x0;
            double dy = target->pos[1] - y0 + (j == 0 ? 0 : r - h);
            double dz = target->pos[2] - z0;
            double vd = vx * dx + vy * dy + vz * dz;
            double dd = dx * dx + dy * dy + dz * dz;
            double vv = vx * vx + vy * vy + vz * vz;
            double rr = r * r;
            if (vd < 0) continue;
            if (vd * vd >= vv * (dd - rr)) hit += 1;
        }
        if (hit) {
            target->pos[0] = static_cast<double>(MAP_BOX_SCALE * (SDL_rand(256) - 128)) / 256.0;
            target->pos[1] = static_cast<double>(MAP_BOX_SCALE * (SDL_rand(256) - 128)) / 256.0;
            target->pos[2] = static_cast<double>(MAP_BOX_SCALE * (SDL_rand(256) - 128)) / 256.0;
        }
    }
}
static void update(Player* players, int players_len, Uint64 dt_ns) {
    for (int i = 0; i < players_len; i++) {
        Player* player = &players[i];
        double rate = 6.0;
        double time = static_cast<double>(dt_ns) * 1e-9;
        double drag = SDL_exp(-time * rate);
        double diff = 1.0 - drag;
        double mult = 60.0;
        double grav = 25.0;
        double yaw = static_cast<double>(player->yaw);
        double rad = yaw * SDL_PI_D / 2147483648.0;
        double cos = SDL_cos(rad);
        double sin = SDL_sin(rad);
        unsigned char wasd = player->wasd;
        double dirX = (wasd & 8 ? 1.0 : 0.0) - (wasd & 2 ? 1.0 : 0.0);
        double dirZ = (wasd & 4 ? 1.0 : 0.0) - (wasd & 1 ? 1.0 : 0.0);
        double norm = dirX * dirX + dirZ * dirZ;
        double accX = mult * (norm == 0 ? 0 : (cos * dirX + sin * dirZ) / SDL_sqrt(norm));
        double accZ = mult * (norm == 0 ? 0 : (-sin * dirX + cos * dirZ) / SDL_sqrt(norm));
        double velX = player->vel[0];
        double velY = player->vel[1];
        double velZ = player->vel[2];

        player->vel[0] -= velX * diff;
        player->vel[1] -= grav * time;
        player->vel[2] -= velZ * diff;
        player->vel[0] += diff * accX / rate;
        player->vel[2] += diff * accZ / rate;
        player->pos[0] += (time - diff / rate) * accX / rate + diff * velX / rate;
        player->pos[1] += -0.5 * grav * time * time + velY * time;
        player->pos[2] += (time - diff / rate) * accZ / rate + diff * velZ / rate;

        double scale = static_cast<double>(MAP_BOX_SCALE);
        double bound = scale - player->radius;
        double posX = SDL_max(SDL_min(bound, player->pos[0]), -bound);
        double posY = SDL_max(SDL_min(bound, player->pos[1]), player->height - scale);
        double posZ = SDL_max(SDL_min(bound, player->pos[2]), -bound);

        if (player->pos[0] != posX) player->vel[0] = 0;
        if (player->pos[1] != posY) player->vel[1] = (wasd & 16) ? 8.4375 : 0;
        if (player->pos[2] != posZ) player->vel[2] = 0;

        player->pos[0] = posX;
        player->pos[1] = posY;
        player->pos[2] = posZ;
    }
}
static void drawCircle(SDL_Renderer* renderer, float r, float x, float y) {
    float ang;
    SDL_FPoint points[CIRCLE_DRAW_SIDES_LEN];
    for (int i = 0; i < CIRCLE_DRAW_SIDES_LEN; i++) {
        ang = 2.0f * SDL_PI_F * static_cast<float>(i) / static_cast<float>(CIRCLE_DRAW_SIDES);
        points[i].x = x + r * SDL_cosf(ang);
        points[i].y = y + r * SDL_sinf(ang);
    }
    SDL_RenderLines(renderer, points, CIRCLE_DRAW_SIDES_LEN);
}
static void drawClippedSegment(
    SDL_Renderer* renderer,
    float ax, float ay, float az,
    float bx, float by, float bz,
    float x, float y, float z, float w)
{
    if (az >= -w && bz >= -w) return;
    float dx = ax - bx;
    float dy = ay - by;
    if (az > -w) {
        float t = (-w - bz) / (az - bz);
        ax = bx + dx * t;
        ay = by + dy * t;
        az = -w;
    }
    else if (bz > -w) {
        float t = (-w - az) / (bz - az);
        bx = ax - dx * t;
        by = ay - dy * t;
        bz = -w;
    }
    ax = -z * ax / az;
    ay = -z * ay / az;
    bx = -z * bx / bz;
    by = -z * by / bz;
    SDL_RenderLine(renderer, x + ax, y - ay, x + bx, y - by);
}
static char debug_string[32];
// Added ImVec4 parameter to clear the screen dynamically from ImGui properties
static void draw(SDL_Renderer* renderer, const float edges[][6], const Player players[], int players_len, ImVec4 bg_color) {
    int w, h;
    if (!SDL_GetRenderOutputSize(renderer, &w, &h)) {
        return;
    }
    // Apply specific color picked by Dear ImGui
    SDL_SetRenderDrawColorFloat(renderer, bg_color.x, bg_color.y, bg_color.z, bg_color.w);
    SDL_RenderClear(renderer);

    if (players_len > 0) {
        float wf = static_cast<float>(w);
        float hf = static_cast<float>(h);
        int part_hor = players_len > 2 ? 2 : 1;
        int part_ver = players_len > 1 ? 2 : 1;
        float size_hor = wf / static_cast<float>(part_hor);
        float size_ver = hf / static_cast<float>(part_ver);

        for (int i = 0; i < players_len; i++) {
            const Player* player = &players[i];
            float mod_x = static_cast<float>(i % part_hor);
            float mod_y = static_cast<float>(i / part_hor);
            float hor_origin = (mod_x + 0.5f) * size_hor;
            float ver_origin = (mod_y + 0.5f) * size_ver;
            float cam_origin = static_cast<float>(0.5 * SDL_sqrt(size_hor * size_hor + size_ver * size_ver));
            float hor_offset = mod_x * size_hor;
            float ver_offset = mod_y * size_ver;

            SDL_Rect rect;
            rect.x = static_cast<int>(hor_offset);
            rect.y = static_cast<int>(ver_offset);
            rect.w = static_cast<int>(size_hor);
            rect.h = static_cast<int>(size_ver);
            SDL_SetRenderClipRect(renderer, &rect);

            double x0 = player->pos[0];
            double y0 = player->pos[1];
            double z0 = player->pos[2];
            double bin_rad = SDL_PI_D / 2147483648.0;
            double yaw_rad = bin_rad * player->yaw;
            double pitch_rad = bin_rad * player->pitch;
            double cos_yaw = SDL_cos(yaw_rad);
            double sin_yaw = SDL_sin(yaw_rad);
            double cos_pitch = SDL_cos(pitch_rad);
            double sin_pitch = SDL_sin(pitch_rad);
            double mat[9] = {
                cos_yaw          ,          0, -sin_yaw          ,
                sin_yaw * sin_pitch,  cos_pitch,  cos_yaw * sin_pitch,
                sin_yaw * cos_pitch, -sin_pitch,  cos_yaw * cos_pitch
            };

            SDL_SetRenderDrawColor(renderer, 64, 64, 64, 255);
            for (int k = 0; k < MAP_BOX_EDGES_LEN; k++) {
                const float* line = edges[k];
                float ax = static_cast<float>(mat[0] * (line[0] - x0) + mat[1] * (line[1] - y0) + mat[2] * (line[2] - z0));
                float ay = static_cast<float>(mat[3] * (line[0] - x0) + mat[4] * (line[1] - y0) + mat[5] * (line[2] - z0));
                float az = static_cast<float>(mat[6] * (line[0] - x0) + mat[7] * (line[1] - y0) + mat[8] * (line[2] - z0));
                float bx = static_cast<float>(mat[0] * (line[3] - x0) + mat[1] * (line[4] - y0) + mat[2] * (line[5] - z0));
                float by = static_cast<float>(mat[3] * (line[3] - x0) + mat[4] * (line[4] - y0) + mat[5] * (line[5] - z0));
                float bz = static_cast<float>(mat[6] * (line[3] - x0) + mat[7] * (line[4] - y0) + mat[8] * (line[5] - z0));
                drawClippedSegment(renderer, ax, ay, az, bx, by, bz, hor_origin, ver_origin, cam_origin, 1);
            }

            for (int j = 0; j < players_len; j++) {
                if (i == j) continue;
                const Player* target = &players[j];
                SDL_SetRenderDrawColor(renderer, target->color[0], target->color[1], target->color[2], 255);
                for (int k = 0; k < 2; k++) {
                    double rx = target->pos[0] - player->pos[0];
                    double ry = target->pos[1] - player->pos[1] + (target->radius - target->height) * static_cast<float>(k);
                    double rz = target->pos[2] - player->pos[2];
                    double dx = mat[0] * rx + mat[1] * ry + mat[2] * rz;
                    double dy = mat[3] * rx + mat[4] * ry + mat[5] * rz;
                    double dz = mat[6] * rx + mat[7] * ry + mat[8] * rz;
                    double r_eff = target->radius * cam_origin / dz;
                    if (!(dz < 0)) continue;
                    drawCircle(renderer, static_cast<float>(r_eff),
                        static_cast<float>(hor_origin - cam_origin * dx / dz),
                        static_cast<float>(ver_origin + cam_origin * dy / dz));
                }
            }
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderLine(renderer, hor_origin, ver_origin - 10, hor_origin, ver_origin + 10);
            SDL_RenderLine(renderer, hor_origin - 10, ver_origin, hor_origin + 10, ver_origin);
        }
    }
    SDL_SetRenderClipRect(renderer, nullptr);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderDebugText(renderer, 0, 0, debug_string);
    // SDL_RenderPresent has been moved into the main Loop architecture so ImGui can paint on top before pushing it to monitor
}
static void initPlayers(Player* players, int len) {
    for (int i = 0; i < len; i++) {
        players[i].pos[0] = 8.0 * (i & 1 ? -1.0 : 1.0);
        players[i].pos[1] = 0;
        players[i].pos[2] = 8.0 * (i & 1 ? -1.0 : 1.0) * (i & 2 ? -1.0 : 1.0);
        players[i].vel[0] = 0;
        players[i].vel[1] = 0;
        players[i].vel[2] = 0;
        players[i].yaw = 0x20000000 + (i & 1 ? 0x80000000 : 0) + (i & 2 ? 0x40000000 : 0);
        players[i].pitch = -0x08000000;
        players[i].radius = 0.5f;
        players[i].height = 1.5f;
        players[i].wasd = 0;
        players[i].mouse = 0;
        players[i].keyboard = 0;
        players[i].color[0] = (1 << (i / 2)) & 2 ? 0 : 0xff;
        players[i].color[1] = (1 << (i / 2)) & 1 ? 0 : 0xff;
        players[i].color[2] = (1 << (i / 2)) & 4 ? 0 : 0xff;
        players[i].color[0] = (i & 1) ? players[i].color[0] : ~players[i].color[0];
        players[i].color[1] = (i & 1) ? players[i].color[1] : ~players[i].color[1];
        players[i].color[2] = (i & 1) ? players[i].color[2] : ~players[i].color[2];
    }
}
static void initEdges(int scale, float edges[][6], int edges_len) {
    const float r = static_cast<float>(scale);
    const int map[24] = {
        0,1 , 1,3 , 3,2 , 2,0 ,
        7,6 , 6,4 , 4,5 , 5,7 ,
        6,2 , 3,7 , 0,4 , 5,1
    };
    for (int i = 0; i < 12; i++) {
        for (int j = 0; j < 3; j++) {
            edges[i][j + 0] = (map[i * 2 + 0] & (1 << j) ? r : -r);
            edges[i][j + 3] = (map[i * 2 + 1] & (1 << j) ? r : -r);
        }
    }
    for (int i = 0; i < scale; i++) {
        float d = static_cast<float>(i * 2);
        for (int j = 0; j < 2; j++) {
            edges[i + 12][3 * j + 0] = j ? r : -r;
            edges[i + 12][3 * j + 1] = -r;
            edges[i + 12][3 * j + 2] = d - r;
            edges[i + 12 + scale][3 * j + 0] = d - r;
            edges[i + 12 + scale][3 * j + 1] = -r;
            edges[i + 12 + scale][3 * j + 2] = j ? r : -r;
        }
    }
}
SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    if (!SDL_SetAppMetadata("Example splitscreen shooter game", "1.0", "com.example.woodeneye-008")) {
        return SDL_APP_FAILURE;
    }

    for (int i = 0; i < SDL_arraysize(extended_metadata); i++) {
        if (!SDL_SetAppMetadataProperty(extended_metadata[i].key, extended_metadata[i].value)) {
            return SDL_APP_FAILURE;
        }
    }
    AppState* as = static_cast<AppState*>(SDL_calloc(1, sizeof(AppState)));
    if (!as) return SDL_APP_FAILURE;
    else *appstate = as;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        return SDL_APP_FAILURE;
    }

    // Create scaling and render flags accommodating ImGui example bounds
    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!SDL_CreateWindowAndRenderer("ImGui Integrated Wireframe Shooter", 1280, 800, window_flags, &as->window, &as->renderer)) {
        return SDL_APP_FAILURE;
    }
    // Setup ImGui Context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    // Bind Platform and Renderers
    ImGui_ImplSDL3_InitForSDLRenderer(as->window, as->renderer);
    ImGui_ImplSDLRenderer3_Init(as->renderer);
    as->player_count = 1;
    as->menu_active = false;
    as->show_demo_window = false;
    as->clear_color = ImVec4(0.00f, 0.00f, 0.00f, 1.0f); // Default to total black
    initPlayers(as->players, MAX_PLAYER_COUNT);
    initEdges(MAP_BOX_SCALE, as->edges, MAP_BOX_EDGES_LEN);
    debug_string[0] = 0;
    SDL_SetRenderVSync(as->renderer, false);
    SDL_SetWindowRelativeMouseMode(as->window, true);
    SDL_SetHintWithPriority(SDL_HINT_WINDOWS_RAW_KEYBOARD, "1", SDL_HINT_OVERRIDE);
    return SDL_APP_CONTINUE;
}
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    AppState* as = static_cast<AppState*>(appstate);

    // Always dispatch events to ImGui first
    ImGui_ImplSDL3_ProcessEvent(event);
    Player* players = as->players;
    int player_count = as->player_count;

    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;

    case SDL_EVENT_MOUSE_REMOVED:
        for (int i = 0; i < player_count; i++)
            if (players[i].mouse == event->mdevice.which) players[i].mouse = 0;
        break;

    case SDL_EVENT_KEYBOARD_REMOVED:
        for (int i = 0; i < player_count; i++)
            if (players[i].keyboard == event->kdevice.which) players[i].keyboard = 0;
        break;

    case SDL_EVENT_MOUSE_MOTION: {
        if (as->menu_active) break; // Disable aiming while in UI Menu

        SDL_MouseID id = event->motion.which;
        int index = whoseMouse(id, players, player_count);
        if (index >= 0) {
            players[index].yaw -= static_cast<int>(event->motion.xrel) * 0x00080000;
            players[index].pitch = SDL_max(-0x40000000, SDL_min(0x40000000, players[index].pitch - static_cast<int>(event->motion.yrel) * 0x00080000));
        }
        else if (id) {
            for (int i = 0; i < MAX_PLAYER_COUNT; i++) {
                if (players[i].mouse == 0) {
                    players[i].mouse = id;
                    as->player_count = SDL_max(as->player_count, i + 1);
                    break;
                }
            }
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (as->menu_active || ImGui::GetIO().WantCaptureMouse) break; // Block shooting if clicking interface

        SDL_MouseID id = event->button.which;
        int index = whoseMouse(id, players, player_count);
        if (index >= 0) {
            shoot(index, players, player_count);
        }
        break;
    }
    case SDL_EVENT_KEY_DOWN: {
        SDL_Keycode sym = event->key.key;

        // Allow hotkey logic to override the game state directly
        if (sym == SDLK_TAB) {
            as->menu_active = !as->menu_active;
            SDL_SetWindowRelativeMouseMode(as->window, !as->menu_active);
            if (as->menu_active) {
                for (int i = 0; i < MAX_PLAYER_COUNT; i++) as->players[i].wasd = 0; // Wipe stuck movement keys
            }
            break;
        }

        if (as->menu_active || ImGui::GetIO().WantCaptureKeyboard) break; // Block character movement if typing
        SDL_KeyboardID id = event->key.which;
        int index = whoseKeyboard(id, players, player_count);
        if (index >= 0) {
            if (sym == SDLK_W) players[index].wasd |= 1;
            if (sym == SDLK_A) players[index].wasd |= 2;
            if (sym == SDLK_S) players[index].wasd |= 4;
            if (sym == SDLK_D) players[index].wasd |= 8;
            if (sym == SDLK_SPACE) players[index].wasd |= 16;
        }
        else if (id) {
            for (int i = 0; i < MAX_PLAYER_COUNT; i++) {
                if (players[i].keyboard == 0) {
                    players[i].keyboard = id;
                    as->player_count = SDL_max(as->player_count, i + 1);
                    break;
                }
            }
        }
        break;
    }
    case SDL_EVENT_KEY_UP: {
        SDL_Keycode sym = event->key.key;
        SDL_KeyboardID id = event->key.which;
        if (sym == SDLK_ESCAPE) return SDL_APP_SUCCESS;

        if (as->menu_active || ImGui::GetIO().WantCaptureKeyboard) break;

        int index = whoseKeyboard(id, players, player_count);
        if (index >= 0) {
            if (sym == SDLK_W) players[index].wasd &= 30;
            if (sym == SDLK_A) players[index].wasd &= 29;
            if (sym == SDLK_S) players[index].wasd &= 27;
            if (sym == SDLK_D) players[index].wasd &= 23;
            if (sym == SDLK_SPACE) players[index].wasd &= 15;
        }
        break;
    }
    }
    return SDL_APP_CONTINUE;
}
SDL_AppResult SDL_AppIterate(void* appstate) {
    AppState* as = static_cast<AppState*>(appstate);
    static Uint64 accu = 0;
    static Uint64 last = 0;
    static Uint64 past = 0;

    Uint64 now = SDL_GetTicksNS();
    Uint64 dt_ns = now - past;
    // Pause physics bounds handling when game is paused in the UI 
    if (!as->menu_active) {
        update(as->players, as->player_count, dt_ns);
    }
    // Prepare ImGui structure and variables
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    // Configure GUI drawing logic
    if (as->menu_active) {
        if (as->show_demo_window) {
            ImGui::ShowDemoWindow(&as->show_demo_window);
        }

        ImGui::Begin("Shooter Settings");
        ImGui::Text("Game Paused!");
        ImGui::Separator();
        ImGui::Checkbox("Dear ImGui Reference Guide", &as->show_demo_window);
        ImGui::ColorEdit3("Arena Background Color", (float*)&as->clear_color);
        ImGui::Text("Active Local Players: %d", as->player_count);
        ImGui::Separator();
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::End();
    }
    else {
        // Small helper Overlay to remind the player how to open the menu
        ImGui::SetNextWindowPos(ImVec2(10, 40));
        ImGui::SetNextWindowBgAlpha(0.35f);
        ImGui::Begin("Overlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        ImGui::Text("Press TAB to pause and open Settings Menu");
        ImGui::End();
    }

    ImGui::Render();
    // Render underlying Game Frame natively passing the ImGui color vector
    draw(as->renderer, as->edges, as->players, as->player_count, as->clear_color);

    // Render ImGui Overlays onto the GPU frame buffer securely
    ImGuiIO& io = ImGui::GetIO();
    SDL_SetRenderScale(as->renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), as->renderer);
    SDL_SetRenderScale(as->renderer, 1.0f, 1.0f); // Release scale bindings gracefully 

    // Issue the absolute screen commit sending merged renderers directly to screen monitor
    SDL_RenderPresent(as->renderer);
    if (now - last > 999999999) {
        last = now;
        SDL_snprintf(debug_string, sizeof(debug_string), "%" SDL_PRIu64 " fps", accu);
        accu = 0;
    }
    past = now;
    accu += 1;

    Uint64 elapsed = SDL_GetTicksNS() - now;
    if (elapsed < 999999) {
        SDL_DelayNS(999999 - elapsed);
    }
    return SDL_APP_CONTINUE;
}
void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    // Tear down ImGui contexts gracefully releasing data back to OS
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_free(appstate);
}