#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

// Standard Library Headers for Asset Loading
#include <vector>
#include <string>
#include <fstream>
#include <sstream>

// Include ImGui Headers
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#define MAP_BOX_SCALE 16
#define MAX_PLAYER_COUNT 4
#define CIRCLE_DRAW_SIDES 32
#define CIRCLE_DRAW_SIDES_LEN (CIRCLE_DRAW_SIDES + 1)

// Unified Struct representing 3D Wireframe line paths
struct Edge {
    float ax, ay, az;
    float bx, by, bz;
};

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
    bool noclip; // <-- Noclip flag added
};

struct AppState {
    SDL_Window* window;
    SDL_Renderer* renderer;
    int player_count;
    Player players[MAX_PLAYER_COUNT];

    // Dynamic Asset Pipeline Storage
    std::vector<Edge> map_edges;
    std::vector<Edge> player_model_edges;

    // UI Path buffers and loading state diagnostics
    char map_path[256];
    char model_path[256];
    std::string map_status;
    std::string model_status;

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

// Lightweight Wavefront OBJ wireframe asset loader
static bool LoadOBJLines(const std::string& filename, std::vector<Edge>& out_edges, float scale) {
    std::ifstream file(filename);
    if (!file.is_open()) return false;

    struct Vertex { float x, y, z; };
    std::vector<Vertex> vertices;
    out_edges.clear();

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v") {
            Vertex v;
            iss >> v.x >> v.y >> v.z;
            v.x *= scale; v.y *= scale; v.z *= scale;
            vertices.push_back(v);
        }
        else if (prefix == "f") {
            std::vector<int> face_indices;
            std::string vertex_token;
            while (iss >> vertex_token) {
                std::istringstream token_stream(vertex_token);
                std::string index_str;
                std::getline(token_stream, index_str, '/'); // Skip UVs/Normals definitions if present
                if (!index_str.empty()) {
                    int idx = std::stoi(index_str);
                    // Handle standard 1-based indexing or negative relative indexing
                    if (idx > 0) face_indices.push_back(idx - 1);
                    else if (idx < 0) face_indices.push_back(static_cast<int>(vertices.size()) + idx);
                }
            }
            // Generate perimeter outline wireframe loops out of polygon faces
            for (size_t i = 0; i < face_indices.size(); ++i) {
                size_t idx1 = face_indices[i];
                size_t idx2 = face_indices[(i + 1) % face_indices.size()];
                if (idx1 < vertices.size() && idx2 < vertices.size()) {
                    out_edges.push_back({
                        vertices[idx1].x, vertices[idx1].y, vertices[idx1].z,
                        vertices[idx2].x, vertices[idx2].y, vertices[idx2].z
                        });
                }
            }
        }
    }
    return !out_edges.empty();
}

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

        // Zero out gravity if noclip is enabled so the player hovers
        double grav = player->noclip ? 0.0 : 25.0;

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

        if (player->noclip) {
            // Add upward/downward vertical flight capability while noclipping
            if (wasd & 16) player->vel[1] = 8.4375; // Fly up (Space)
            else player->vel[1] -= velY * diff; // Drag so they don't drift vertically forever
        }

        player->vel[0] += diff * accX / rate;
        player->vel[2] += diff * accZ / rate;
        player->pos[0] += (time - diff / rate) * accX / rate + diff * velX / rate;
        player->pos[1] += -0.5 * grav * time * time + velY * time;
        player->pos[2] += (time - diff / rate) * accZ / rate + diff * velZ / rate;

        // Skip boundary clamping if noclip is active
        if (!player->noclip) {
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

static void draw(SDL_Renderer* renderer, const std::vector<Edge>& map_edges, const std::vector<Edge>& player_model_edges, const Player players[], int players_len, ImVec4 bg_color) {
    int w, h;
    if (!SDL_GetRenderOutputSize(renderer, &w, &h)) {
        return;
    }
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

            // 1. Render Environment Map Wireframe (Dynamic Vector Loop)
            SDL_SetRenderDrawColor(renderer, 64, 64, 64, 255);
            for (const auto& edge : map_edges) {
                float ax = static_cast<float>(mat[0] * (edge.ax - x0) + mat[1] * (edge.ay - y0) + mat[2] * (edge.az - z0));
                float ay = static_cast<float>(mat[3] * (edge.ax - x0) + mat[4] * (edge.ay - y0) + mat[5] * (edge.az - z0));
                float az = static_cast<float>(mat[6] * (edge.ax - x0) + mat[7] * (edge.ay - y0) + mat[8] * (edge.az - z0));
                float bx = static_cast<float>(mat[0] * (edge.bx - x0) + mat[1] * (edge.by - y0) + mat[2] * (edge.bz - z0));
                float by = static_cast<float>(mat[3] * (edge.bx - x0) + mat[4] * (edge.by - y0) + mat[5] * (edge.bz - z0));
                float bz = static_cast<float>(mat[6] * (edge.bx - x0) + mat[7] * (edge.by - y0) + mat[8] * (edge.bz - z0));
                drawClippedSegment(renderer, ax, ay, az, bx, by, bz, hor_origin, ver_origin, cam_origin, 1);
            }

            // 2. Render Enemy Target entities
            for (int j = 0; j < players_len; j++) {
                if (i == j) continue;
                const Player* target = &players[j];
                SDL_SetRenderDrawColor(renderer, target->color[0], target->color[1], target->color[2], 255);

                if (!player_model_edges.empty()) {
                    // Injecting relative 3D Model projections instead of flat billboards
                    for (const auto& edge : player_model_edges) {
                        double rx1 = target->pos[0] + edge.ax - x0;
                        double ry1 = target->pos[1] + edge.ay - y0;
                        double rz1 = target->pos[2] + edge.az - z0;

                        double rx2 = target->pos[0] + edge.bx - x0;
                        double ry2 = target->pos[1] + edge.by - y0;
                        double rz2 = target->pos[2] + edge.bz - z0;

                        float ax = static_cast<float>(mat[0] * rx1 + mat[1] * ry1 + mat[2] * rz1);
                        float ay = static_cast<float>(mat[3] * rx1 + mat[4] * ry1 + mat[5] * rz1);
                        float az = static_cast<float>(mat[6] * rx1 + mat[7] * ry1 + mat[8] * rz1);
                        float bx = static_cast<float>(mat[0] * rx2 + mat[1] * rx2 + mat[2] * rz2); // Fixed matrix mult error from previous
                        float by = static_cast<float>(mat[3] * rx2 + mat[4] * ry2 + mat[5] * rz2);
                        float bz = static_cast<float>(mat[6] * rx2 + mat[7] * ry2 + mat[8] * rz2);

                        drawClippedSegment(renderer, ax, ay, az, bx, by, bz, hor_origin, ver_origin, cam_origin, 1);
                    }
                }
                else {
                    // Fallback to basic circles if no 3D asset model is explicitly loaded
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
            }
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderLine(renderer, hor_origin, ver_origin - 10, hor_origin, ver_origin + 10);
            SDL_RenderLine(renderer, hor_origin - 10, ver_origin, hor_origin + 10, ver_origin);
        }
    }
    SDL_SetRenderClipRect(renderer, nullptr);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderDebugText(renderer, 0, 0, debug_string);
}

// Generate fallback boundaries if no structural map is parsed
static void initEdgesDefault(int scale, std::vector<Edge>& out_edges) {
    out_edges.clear();
    const float r = static_cast<float>(scale);
    const int map[24] = {
        0,1 , 1,3 , 3,2 , 2,0 ,
        7,6 , 6,4 , 4,5 , 5,7 ,
        6,2 , 3,7 , 0,4 , 5,1
    };
    for (int i = 0; i < 12; i++) {
        float edge[6];
        for (int j = 0; j < 3; j++) {
            edge[j + 0] = (map[i * 2 + 0] & (1 << j) ? r : -r);
            edge[j + 3] = (map[i * 2 + 1] & (1 << j) ? r : -r);
        }
        out_edges.push_back({ edge[0], edge[1], edge[2], edge[3], edge[4], edge[5] });
    }
    for (int i = 0; i < scale; i++) {
        float d = static_cast<float>(i * 2);
        out_edges.push_back({ -r, -r, d - r, r, -r, d - r });
        out_edges.push_back({ d - r, -r, -r, d - r, -r, r });
    }
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
        players[i].noclip = false; // <-- Initialize Noclip state
        players[i].color[0] = (1 << (i / 2)) & 2 ? 0 : 0xff;
        players[i].color[1] = (1 << (i / 2)) & 1 ? 0 : 0xff;
        players[i].color[2] = (1 << (i / 2)) & 4 ? 0 : 0xff;
        players[i].color[0] = (i & 1) ? players[i].color[0] : ~players[i].color[0];
        players[i].color[1] = (i & 1) ? players[i].color[1] : ~players[i].color[1];
        players[i].color[2] = (i & 1) ? players[i].color[2] : ~players[i].color[2];
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

    // CRITICAL FIX: Since AppState uses std::vector, object construction must run via global new
    AppState* as = new AppState();
    if (!as) return SDL_APP_FAILURE;
    else *appstate = as;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        return SDL_APP_FAILURE;
    }

    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!SDL_CreateWindowAndRenderer("Asset Ingestion Engine Wireframe Shooter", 1280, 800, window_flags, &as->window, &as->renderer)) {
        return SDL_APP_FAILURE;
    }

    // Setup ImGui Context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForSDLRenderer(as->window, as->renderer);
    ImGui_ImplSDLRenderer3_Init(as->renderer);

    // Init status buffers
    SDL_strlcpy(as->map_path, "map.obj", sizeof(as->map_path));
    SDL_strlcpy(as->model_path, "model.obj", sizeof(as->model_path));
    as->map_status = "Default box map loaded.";
    as->model_status = "No model loaded (Using 2D fallback circle indicator).";

    as->player_count = 1;
    as->menu_active = false;
    as->show_demo_window = false;
    as->clear_color = ImVec4(0.05f, 0.05f, 0.08f, 1.0f);

    initPlayers(as->players, MAX_PLAYER_COUNT);
    initEdgesDefault(MAP_BOX_SCALE, as->map_edges);

    debug_string[0] = 0;
    SDL_SetRenderVSync(as->renderer, false);
    SDL_SetWindowRelativeMouseMode(as->window, true);
    SDL_SetHintWithPriority(SDL_HINT_WINDOWS_RAW_KEYBOARD, "1", SDL_HINT_OVERRIDE);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    AppState* as = static_cast<AppState*>(appstate);
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
        if (as->menu_active) break;

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
        if (as->menu_active || ImGui::GetIO().WantCaptureMouse) break;

        SDL_MouseID id = event->button.which;
        int index = whoseMouse(id, players, player_count);
        if (index >= 0) {
            shoot(index, players, player_count);
        }
        break;
    }
    case SDL_EVENT_KEY_DOWN: {
        SDL_Keycode sym = event->key.key;

        if (sym == SDLK_TAB) {
            as->menu_active = !as->menu_active;
            SDL_SetWindowRelativeMouseMode(as->window, !as->menu_active);
            if (as->menu_active) {
                for (int i = 0; i < MAX_PLAYER_COUNT; i++) as->players[i].wasd = 0;
            }
            break;
        }

        if (as->menu_active || ImGui::GetIO().WantCaptureKeyboard) break;
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
    if (!as->menu_active) {
        update(as->players, as->player_count, dt_ns);
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    if (as->menu_active) {
        if (as->show_demo_window) {
            ImGui::ShowDemoWindow(&as->show_demo_window);
        }

        ImGui::Begin("Shooter Settings & Asset Pipeline");
        ImGui::Text("Game Paused!");
        ImGui::Separator();

        // --- 3D MAP LOADING PIPELINE CONTROLS ---
        ImGui::Text("Environment Map Asset (.obj Wireframe)");
        ImGui::InputText("Map OBJ File Path", as->map_path, sizeof(as->map_path));
        static float map_import_scale = 1.0f;
        ImGui::SliderFloat("Map Spatial Scale", &map_import_scale, 0.1f, 10.0f);
        if (ImGui::Button("Load Custom Map")) {
            if (LoadOBJLines(as->map_path, as->map_edges, map_import_scale)) {
                as->map_status = "Successfully loaded map wireframe mesh.";
            }
            else {
                as->map_status = "Failed to load/find map file layout. Resetting...";
                initEdgesDefault(MAP_BOX_SCALE, as->map_edges);
            }
        }
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "%s", as->map_status.c_str());
        ImGui::Separator();

        // --- 3D PLAYER MODEL LOADING PIPELINE CONTROLS ---
        ImGui::Text("Player Entity Model Asset (.obj Wireframe)");
        ImGui::InputText("Player OBJ File Path", as->model_path, sizeof(as->model_path));
        static float model_import_scale = 1.0f;
        ImGui::SliderFloat("Model Entity Scale", &model_import_scale, 0.1f, 5.0f);
        if (ImGui::Button("Load Custom Player Model")) {
            if (LoadOBJLines(as->model_path, as->player_model_edges, model_import_scale)) {
                as->model_status = "Successfully loaded custom 3D entity wireframe.";
            }
            else {
                as->model_status = "Failed to load file. Reverting to default circles.";
                as->player_model_edges.clear();
            }
        }
        if (ImGui::Button("Reset Model Entity Buffer")) {
            as->player_model_edges.clear();
            as->model_status = "Cleared. Defaulting back to flat vector loops.";
        }
        ImGui::TextColored(ImVec4(0.2f, 0.7f, 0.9f, 1.0f), "%s", as->model_status.c_str());

        // --- DEBUG TOOLS ---
        ImGui::Separator();
        ImGui::Text("Debug Tools (Noclip & Flight)");
        for (int i = 0; i < as->player_count; i++) {
            char label[32];
            SDL_snprintf(label, sizeof(label), "Player %d Noclip", i + 1);
            ImGui::Checkbox(label, &as->players[i].noclip);
        }

        ImGui::Separator();
        ImGui::ColorEdit3("Arena Space Clear Color", (float*)&as->clear_color);
        ImGui::Checkbox("Dear ImGui Reference Guide", &as->show_demo_window);
        ImGui::Text("Active Local Players: %d", as->player_count);
        ImGui::Text("Application speed average: %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::End();
    }
    else {
        ImGui::SetNextWindowPos(ImVec2(10, 40));
        ImGui::SetNextWindowBgAlpha(0.35f);
        ImGui::Begin("Overlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        ImGui::Text("Press TAB to pause and open Settings / Asset Loader Menu");
        ImGui::End();
    }

    ImGui::Render();

    // Execute Native Scene Projections using modified dynamic parameters
    draw(as->renderer, as->map_edges, as->player_model_edges, as->players, as->player_count, as->clear_color);

    ImGuiIO& io = ImGui::GetIO();
    SDL_SetRenderScale(as->renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), as->renderer);
    SDL_SetRenderScale(as->renderer, 1.0f, 1.0f);

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
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    AppState* as = static_cast<AppState*>(appstate);
    delete as; // Safely runs vector destructors before dropping application space
}