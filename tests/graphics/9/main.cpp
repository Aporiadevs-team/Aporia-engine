// Aporia render test 9 — a real, playable scene.
// SDL3 owns the window; Aporia owns render, physics, input and text. The "car"
// is just a Body X (chassis) plus host code: the player applies a forward force
// and a steering torque, four wheel meshes follow the chassis, the mouse orbits
// the chase camera (RMB) and drops crates (LMB). No vehicle type exists in the
// engine — the whole machine is this file.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <SDL3/SDL.h>

#include "Aporia/Body.hpp"
#include "Aporia/Game.hpp"
#include "Aporia/Graphics/MeshPrimitives.hpp"
#include "Aporia/Math/Matrix4.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string firstExistingFont() {
    const char* candidates[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/calibri.ttf",
    };
    for (const char* path : candidates) {
        std::ifstream file(path);
        if (file.good()) {
            return path;
        }
    }
    return {};
}

Aporia::Mat4 bodyModel(Aporia::Game& game, const Aporia::Body& body) {
    const Aporia::Transform* transform = game.scene().findComponent<Aporia::Transform>(
        body.object(), game.scene().componentTypeId("Transform"));
    if (transform == nullptr) {
        return Aporia::Mat4::identity();
    }
    return Aporia::Mat4::translation(transform->position) * Aporia::Mat4::fromQuaternion(transform->orientation);
}

// Headless proof that a force moves the chassis, so the game logic itself is
// covered even without a window.
bool headlessSelfCheck() {
    Aporia::Game game;
    Aporia::GraphicsConfig config;
    config.headless = true;
    config.quality = Aporia::GraphicsQuality::Low;
    if (!game.initGraphics({}, config)) {
        std::cerr << "[selfcheck] initGraphics failed: " << game.graphics().lastError() << '\n';
        return false;
    }

    Aporia::TerrainDesc terrain;
    terrain.seed = 11;
    terrain.gridResolution = 48;
    terrain.worldSize = 40.0f;
    terrain.heightScale = 0.25f;
    terrain.heightOffset = -0.5f;
    game.createTerrain(terrain);

    Aporia::BodyDesc chassis;
    chassis.name = "car";
    chassis.meshKind = Aporia::MeshKind::Box;
    chassis.halfExtents = Aporia::Vec3{0.8f, 1.4f, 0.3f};
    chassis.transform.position = Aporia::Vec3{0.0, 0.0, 1.0};
    chassis.mass = 900.0;
    chassis.friction = 1.0;
    chassis.restitution = 0.0;
    const Aporia::Body car = game.createBody(chassis);
    if (!car.valid()) {
        std::cerr << "[selfcheck] car creation failed\n";
        return false;
    }

    const Aporia::Vec3 start = game.physics().bodyPosition(car.physicsBody());
    // The host owns the loop: apply the force on every fixed substep so the
    // accumulator inside step() does not dilute it.
    const double fixedStep = 1.0 / game.timestepHz();
    for (int step = 0; step < 600; ++step) {
        game.physics().applyForce(car.physicsBody(), Aporia::Vec3{0.0, 12000.0, 0.0});
        game.physics().step(fixedStep);
    }
    const Aporia::Vec3 end = game.physics().bodyPosition(car.physicsBody());
    const double moved = std::sqrt(
        (end.x - start.x) * (end.x - start.x) +
        (end.y - start.y) * (end.y - start.y));
    std::cout << "[selfcheck] car moved " << moved << " m under forward force\n";
    if (moved < 1.0) {
        std::cerr << "[selfcheck] FAIL: car did not move under force\n";
        return false;
    }
    return true;
}

// Headless drive across the real hilly terrain; fails if the chassis sinks
// through the heightfield (fall-through regression check).
bool hillDriveCheck() {
    Aporia::Game game;
    Aporia::GraphicsConfig config;
    config.headless = true;
    config.quality = Aporia::GraphicsQuality::Low;
    if (!game.initGraphics({}, config)) {
        return false;
    }

    Aporia::TerrainDesc terrain;
    terrain.seed = 11;
    terrain.gridResolution = 192;
    terrain.worldSize = 200.0f;
    terrain.heightScale = 3.0f;
    terrain.heightOffset = -0.5f;
    terrain.noiseFrequency = 1.6f;
    terrain.noiseOctaves = 4;
    const Aporia::Body terrainBody = game.createTerrain(terrain);

    Aporia::BodyDesc chassis;
    chassis.name = "car";
    chassis.meshKind = Aporia::MeshKind::Box;
    chassis.halfExtents = Aporia::Vec3{0.8f, 1.4f, 0.3f};
    chassis.transform.position = Aporia::Vec3{0.0, 0.0, 2.5};
    chassis.mass = 900.0;
    chassis.friction = 1.0;
    chassis.restitution = 0.0;
    const Aporia::Body car = game.createBody(chassis);
    if (!car.valid() || !terrainBody.physicsBody().valid()) {
        return false;
    }

    const double fixed = 1.0 / game.timestepHz();
    double minimumClearance = 1.0e9;
    for (int step = 0; step < 300 * 20; ++step) {
        const Aporia::Vec3 heading = game.physics()
            .bodyOrientation(car.physicsBody()).rotate({0.0, 1.0, 0.0});
        game.physics().applyForce(car.physicsBody(), heading * 12000.0);
        Aporia::Vec3 angular = game.physics().bodyAngularVelocity(car.physicsBody());
        angular.z = 1.2 * std::sin(static_cast<double>(step) * fixed * 0.6);
        game.physics().setBodyAngularVelocity(car.physicsBody(), angular);
        game.physics().step(fixed);

        const Aporia::Vec3 position = game.physics().bodyPosition(car.physicsBody());
        const double surface = game.physics().heightfieldHeightAt(terrainBody.physicsBody(), position);
        const double clearance = position.z - surface;
        minimumClearance = std::min(minimumClearance, clearance);
        if (clearance < -1.0) {
            std::cerr << "[hillcheck] FAIL: sank through terrain, clearance " << clearance
                      << " at step " << step << '\n';
            return false;
        }
    }
    std::cout << "[hillcheck] minimum chassis clearance " << minimumClearance << " m over 20 s\n";
    return true;
}

double headingOf(const Aporia::Quat& orientation) {
    const Aporia::Vec3 forward = orientation.rotate({0.0, 1.0, 0.0});
    return std::atan2(forward.y, forward.x);
}

// Checks that resting bodies actually feel surface friction: a crate given a
// sideways shove must slow down, not glide as if the ground were ice.
bool frictionCheck() {
    Aporia::Game game;
    Aporia::GraphicsConfig config;
    config.headless = true;
    config.quality = Aporia::GraphicsQuality::Low;
    if (!game.initGraphics({}, config)) {
        return false;
    }

    Aporia::TerrainDesc terrain;
    terrain.seed = 1;
    terrain.gridResolution = 16;
    terrain.worldSize = 40.0f;
    terrain.heightScale = 0.0f;
    terrain.heightOffset = 0.0f;
    game.createTerrain(terrain);

    Aporia::BodyDesc crate;
    crate.name = "crate";
    crate.meshKind = Aporia::MeshKind::Box;
    crate.halfExtents = Aporia::Vec3{0.4f, 0.4f, 0.4f};
    crate.transform.position = Aporia::Vec3{0.0, 0.0, 0.5};
    crate.mass = 40.0;
    crate.friction = 0.8;
    crate.restitution = 0.0;
    const Aporia::Body body = game.createBody(crate);
    if (!body.valid()) {
        return false;
    }

    const double fixed = 1.0 / game.timestepHz();
    for (int step = 0; step < 300; ++step) {
        game.physics().step(fixed); // settle
    }

    game.physics().setBodyVelocity(body.physicsBody(), {5.0, 0.0, 0.0});
    for (int step = 0; step < 120; ++step) { // 0.4 s
        game.physics().step(fixed);
    }
    const double slidingSpeed = std::abs(game.physics().bodyVelocity(body.physicsBody()).x);
    std::cout << "[frictioncheck] sideways speed after 0.4 s: " << slidingSpeed << " m/s\n";
    if (slidingSpeed > 3.0) {
        std::cerr << "[frictioncheck] FAIL: surface behaves like ice\n";
        return false;
    }
    return true;
}

// Validates the engine's turn predictor: no rotation from a standstill, a real
// turn once moving, and a bounded yaw rate.
bool steeringCheck() {
    Aporia::Game game;
    Aporia::GraphicsConfig config;
    config.headless = true;
    config.quality = Aporia::GraphicsQuality::Low;
    if (!game.initGraphics({}, config)) {
        return false;
    }

    Aporia::TerrainDesc terrain;
    terrain.seed = 3;
    terrain.gridResolution = 32;
    terrain.worldSize = 40.0f;
    terrain.heightScale = 0.0f;
    terrain.heightOffset = 0.0f;
    game.createTerrain(terrain);

    Aporia::BodyDesc chassis;
    chassis.name = "car";
    chassis.meshKind = Aporia::MeshKind::Box;
    chassis.halfExtents = Aporia::Vec3{0.8f, 1.4f, 0.3f};
    chassis.transform.position = Aporia::Vec3{0.0, 0.0, 0.6};
    chassis.mass = 900.0;
    chassis.friction = 1.0;
    chassis.restitution = 0.0;
    const Aporia::Body car = game.createBody(chassis);
    if (!car.valid()) {
        return false;
    }

    const double fixed = 1.0 / game.timestepHz();
    for (int step = 0; step < 300; ++step) {
        game.physics().step(fixed); // settle on the ground
    }

    // Full lock at a standstill must not spin the chassis.
    const double before = headingOf(game.physics().bodyOrientation(car.physicsBody()));
    for (int step = 0; step < 300; ++step) {
        game.physics().setBodySteering(car.physicsBody(), 1.0);
        game.physics().step(fixed);
    }
    const double stillTurn = std::abs(headingOf(game.physics().bodyOrientation(car.physicsBody())) - before);
    std::cout << "[steercheck] standstill heading change " << stillTurn << " rad\n";
    if (stillTurn > 0.05) {
        std::cerr << "[steercheck] FAIL: car turned in place\n";
        return false;
    }

    // Once driving, full lock must actually turn the car, but within grip.
    for (int step = 0; step < 300; ++step) { // build up speed first
        const Aporia::Vec3 heading = game.physics()
            .bodyOrientation(car.physicsBody()).rotate({0.0, 1.0, 0.0});
        game.physics().applyForce(car.physicsBody(), heading * 12000.0);
        game.physics().step(fixed);
    }
    const double beforeDrive = headingOf(game.physics().bodyOrientation(car.physicsBody()));
    double peakYaw = 0.0;
    double peakSlip = 0.0;
    for (int step = 0; step < 300; ++step) {
        const Aporia::Vec3 heading = game.physics()
            .bodyOrientation(car.physicsBody()).rotate({0.0, 1.0, 0.0});
        game.physics().applyForce(car.physicsBody(), heading * 12000.0);
        game.physics().setBodySteering(car.physicsBody(), 1.0);
        game.physics().step(fixed);
        peakYaw = std::max(peakYaw, std::abs(game.physics().bodyYawRate(car.physicsBody())));

        const Aporia::Vec3 velocity = game.physics().bodyVelocity(car.physicsBody());
        const Aporia::Vec3 planarVelocity{velocity.x, velocity.y, 0.0};
        const Aporia::Vec3 planarHeading{heading.x, heading.y, 0.0};
        if (planarVelocity.length() > 1.0 && planarHeading.length() > 1.0e-3) {
            const double cosine = std::clamp(
                dot(planarVelocity.normalized(), planarHeading.normalized()), -1.0, 1.0);
            peakSlip = std::max(peakSlip, std::acos(cosine));
        }
    }
    const double driveTurn = std::abs(headingOf(game.physics().bodyOrientation(car.physicsBody())) - beforeDrive);
    std::cout << "[steercheck] driving heading change " << driveTurn
              << " rad, peak yaw rate " << peakYaw << " rad/s, peak slip " << peakSlip << " rad\n";
    if (driveTurn < 0.3) {
        std::cerr << "[steercheck] FAIL: car did not turn while driving\n";
        return false;
    }
    if (peakYaw > 6.0) {
        std::cerr << "[steercheck] FAIL: yaw rate not bounded\n";
        return false;
    }
    if (peakSlip > 0.7) {
        std::cerr << "[steercheck] FAIL: car slides like on ice\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    bool headlessOnly = false;
    double durationSeconds = 30.0;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--headless") == 0) {
            headlessOnly = true;
        } else {
            durationSeconds = std::max(1.0, std::atof(argv[index]));
        }
    }

    if (!headlessSelfCheck()) {
        return 1;
    }
    if (!hillDriveCheck()) {
        return 1;
    }
    if (!steeringCheck()) {
        return 1;
    }
    if (!frictionCheck()) {
        return 1;
    }
    if (headlessOnly) {
        std::cout << "Aporia render test 9 headless self-check passed\n";
        return 0;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("Aporia Render Test 9 - Drive", 1280, 720, 0);
    if (window == nullptr) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
    const HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));

    Aporia::Game game;
    Aporia::GraphicsContext context;
    context.nativeWindow = reinterpret_cast<std::uintptr_t>(hwnd);
    Aporia::GraphicsConfig config;
    config.headless = false;
    config.enableValidation = false;
    Aporia::GraphicsQuality quality = Aporia::GraphicsQuality::High;
    config.quality = quality;
    config.sunDirection[0] = 0.2f;
    config.sunDirection[1] = -0.6f;
    config.sunDirection[2] = 0.78f;
    if (!game.initGraphics(context, config)) {
        std::cerr << "initGraphics failed: " << game.graphics().lastError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    game.physics().setSolverIterations(16);

    const Aporia::FontHandle font = [&]() {
        const std::string path = firstExistingFont();
        return path.empty() ? Aporia::FontHandle{} : game.graphics().createFontFromTtf(path, 22.0f);
    }();

    // Hills to drive over; heightfield collider + render mesh in one call.
    Aporia::TerrainDesc terrain;
    terrain.seed = 11;
    terrain.gridResolution = 192;
    terrain.worldSize = 200.0f;
    terrain.heightScale = 3.0f;
    terrain.heightOffset = -0.5f;
    terrain.noiseFrequency = 1.6f;
    terrain.noiseOctaves = 4;
    const Aporia::Body terrainBody = game.createTerrain(terrain);
    // Asphalt-ish surface: the heightfield default is a fairly slippery 0.5.
    game.physics().setBodyFriction(terrainBody.physicsBody(), 0.95);

    // The whole car: one box chassis (physics) plus host-side wheel visuals.
    const Aporia::Vec3 spawn{0.0, 0.0, 2.5};
    Aporia::BodyDesc chassis;
    chassis.name = "car";
    chassis.meshKind = Aporia::MeshKind::Box;
    chassis.halfExtents = Aporia::Vec3{0.8f, 1.4f, 0.3f};
    chassis.transform.position = spawn;
    chassis.mass = 900.0;
    chassis.friction = 1.0;
    chassis.restitution = 0.0;
    const Aporia::Body car = game.createBody(chassis);
    if (!car.valid()) {
        std::cerr << "car creation failed\n";
        game.shutdownGraphics();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // A couple of crates to bump into, so the scene is not empty.
    std::vector<Aporia::Body> crates;
    const Aporia::Vec3 crateSpots[4] = {
        {6.0, 10.0, 1.5}, {-7.0, 16.0, 1.5}, {12.0, -5.0, 1.5}, {-13.0, -9.0, 1.5}};
    for (const Aporia::Vec3& spot : crateSpots) {
        Aporia::BodyDesc crate;
        crate.name = "crate";
        crate.meshKind = Aporia::MeshKind::Box;
        crate.halfExtents = Aporia::Vec3{0.6f, 0.6f, 0.6f};
        crate.transform.position = spot;
        crate.mass = 80.0;
        crate.friction = 0.9;
        crate.restitution = 0.0;
        crates.push_back(game.createBody(crate));
    }

    const Aporia::MeshHandle wheelMesh = game.graphics().createMesh(Aporia::apr::makeSphereMesh(0.30f, 16, 10));

    Aporia::RenderItem terrainItem;
    terrainItem.meshId = terrainBody.mesh().value();
    {
        const Aporia::Mat4 model = Aporia::Mat4::identity();
        std::memcpy(terrainItem.modelMatrix, &model, sizeof(model));
    }

    const double wheelOffsets[4][2] = {
        {-0.8, -1.1}, {0.8, -1.1}, {-0.8, 1.1}, {0.8, 1.1}};

    // Third-person camera: follows the chassis heading, mouse (RMB) orbits it.
    double camYaw = 0.0;   // offset from the chassis heading (rad)
    double camPitch = 0.35; // elevation (rad)
    double camDistance = 9.0;
    double frameAccumulator = 0.0;

    LARGE_INTEGER frequency{}, previous{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&previous);
    const double startSeconds = static_cast<double>(SDL_GetTicks()) / 1000.0;

    bool running = true;
    bool settingsOpen = false;
    bool playerUltra = false;
    int speedUnits = 1; // 0 = m/s, 1 = km/h, 2 = mph
    double speed = 0.0;
    double odometer = 0.0;
    Aporia::Vec3 previousPosition = spawn;
    Aporia::MouseState previousMouse = game.input().mouse();

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
            }
        }
        if (!running) {
            break;
        }
        if (static_cast<double>(SDL_GetTicks()) / 1000.0 - startSeconds >= durationSeconds) {
            break;
        }

        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const double frameDelta = static_cast<double>(now.QuadPart - previous.QuadPart) /
                                  static_cast<double>(frequency.QuadPart);
        previous = now;

        game.updateInput();
        Aporia::Input& input = game.input();

        // Mouse look: hold RMB and move to orbit the chase camera.
        const Aporia::MouseState mouse = input.mouse();
        if (input.mouseButton(Aporia::MouseButton::Right)) {
            camYaw += (mouse.x - previousMouse.x) * 0.006;
            camPitch -= (mouse.y - previousMouse.y) * 0.006;
            camPitch = std::clamp(camPitch, -0.15, 1.15);
        }
        previousMouse = mouse;

        // Graphics settings panel (X), same controls as render test 7.
        if (input.keyPressed(Aporia::KeyCode::X)) {
            settingsOpen = !settingsOpen;
        }
        if (settingsOpen) {
            if (input.keyPressed(Aporia::KeyCode::Digit1)) {
                quality = Aporia::GraphicsQuality::Low;
                game.graphics().setQuality(quality);
            }
            if (input.keyPressed(Aporia::KeyCode::Digit2)) {
                quality = Aporia::GraphicsQuality::Medium;
                game.graphics().setQuality(quality);
            }
            if (input.keyPressed(Aporia::KeyCode::Digit3)) {
                quality = Aporia::GraphicsQuality::High;
                game.graphics().setQuality(quality);
            }
            if (input.keyPressed(Aporia::KeyCode::U)) {
                playerUltra = !playerUltra;
                game.graphics().enablePlayerUltraShadows(playerUltra);
            }
        }

        // Car heading: local +Y rotated by the chassis orientation. Read it from
        // physics (not the cached scene pointer) so scene mutations are safe.
        const Aporia::Vec3 forwardBefore = game.physics()
            .bodyOrientation(car.physicsBody()).rotate({0.0, 1.0, 0.0});

        const double engineForce = 16000.0;
        const double dragCoefficient = 180.0; // N per (m/s): shortens flights, caps top speed
        const bool drive = input.key(Aporia::KeyCode::W);
        const bool reverse = input.key(Aporia::KeyCode::S);
        const bool steerLeft = input.key(Aporia::KeyCode::A);
        const bool steerRight = input.key(Aporia::KeyCode::D);
        const bool brake = input.key(Aporia::KeyCode::Space);
        const double steerInput = (steerLeft ? 1.0 : 0.0) - (steerRight ? 1.0 : 0.0);

        // The engine owns turning: the host only reports the driver's request and
        // PhysicsWorld smooths the wheel and predicts a mass/speed/slope/shape
        // aware yaw rate.
        game.physics().setBodySteering(car.physicsBody(), steerInput);

        // Speed unit cycle: m/s -> km/h -> mph.
        if (input.keyPressed(Aporia::KeyCode::M)) {
            speedUnits = (speedUnits + 1) % 3;
        }

        if (input.keyPressed(Aporia::KeyCode::R)) {
            game.physics().setBodyPosition(car.physicsBody(), spawn);
            game.physics().setBodyOrientation(car.physicsBody(), {});
            game.physics().setBodyVelocity(car.physicsBody(), {});
            game.physics().setBodyAngularVelocity(car.physicsBody(), {});
            camYaw = 0.0;
            camPitch = 0.35;
            frameAccumulator = 0.0;
        }

        // LMB drops a crate just ahead of the chassis ("тыкать").
        if (input.mouseButtonPressed(Aporia::MouseButton::Left) && crates.size() < 40) {
            Aporia::BodyDesc crate;
            crate.name = "crate";
            crate.meshKind = Aporia::MeshKind::Box;
            crate.halfExtents = Aporia::Vec3{0.6f, 0.6f, 0.6f};
            crate.transform.position = {
                forwardBefore.x * 7.0, forwardBefore.y * 7.0, 2.5};
            crate.mass = 80.0;
            crate.friction = 0.9;
            crate.restitution = 0.0;
            crates.push_back(game.createBody(crate));
        }

        // Host-owned fixed loop: the drive force is applied on every substep so
        // the engine accumulator does not dilute it.
        const double fixedStep = 1.0 / game.timestepHz();
        frameAccumulator += frameDelta;
        std::uint32_t substeps = 0;
        while (frameAccumulator >= fixedStep && substeps < game.maxSubsteps()) {
            const Aporia::Vec3 heading = game.physics()
                .bodyOrientation(car.physicsBody()).rotate({0.0, 1.0, 0.0});
            Aporia::Vec3 force{};
            if (drive) {
                force += heading * engineForce;
            }
            if (reverse) {
                force -= heading * engineForce * 0.6;
            }
            if (force.lengthSquared() > 0.0) {
                game.physics().applyForce(car.physicsBody(), force);
            }
            // Air/rolling drag: bleeds off speed so the chassis does not glide.
            game.physics().applyForce(car.physicsBody(),
                game.physics().bodyVelocity(car.physicsBody()) * -dragCoefficient);

            if (brake) {
                game.physics().setBodyVelocity(car.physicsBody(), {});
                game.physics().setBodyAngularVelocity(car.physicsBody(), {});
            }
            game.step(fixedStep);
            frameAccumulator -= fixedStep;
            ++substeps;
        }
        if (substeps == game.maxSubsteps() && frameAccumulator >= fixedStep) {
            frameAccumulator = 0.0;
        }

        speed = game.physics().bodyVelocity(car.physicsBody()).length();

        const Aporia::Vec3 forward = game.physics()
            .bodyOrientation(car.physicsBody()).rotate({0.0, 1.0, 0.0});
        const Aporia::Vec3 carPosition = game.physics().bodyPosition(car.physicsBody());
        odometer += (carPosition - previousPosition).length();
        previousPosition = carPosition;

        const Aporia::Mat4 carMatrix = bodyModel(game, car);
        std::vector<Aporia::RenderItem> items;
        items.push_back(terrainItem);

        Aporia::RenderItem chassisItem;
        chassisItem.meshId = car.mesh().value();
        std::memcpy(chassisItem.modelMatrix, &carMatrix, sizeof(carMatrix));
        chassisItem.color[0] = 0.2f;
        chassisItem.color[1] = 0.55f;
        chassisItem.color[2] = 0.95f;
        items.push_back(chassisItem);

        for (const auto& offset : wheelOffsets) {
            Aporia::RenderItem wheelItem;
            wheelItem.meshId = wheelMesh.value();
            const Aporia::Mat4 wheelModel = carMatrix * Aporia::Mat4::translation({offset[0], offset[1], 0.0});
            std::memcpy(wheelItem.modelMatrix, &wheelModel, sizeof(wheelModel));
            wheelItem.color[0] = 0.12f;
            wheelItem.color[1] = 0.12f;
            wheelItem.color[2] = 0.14f;
            items.push_back(wheelItem);
        }

        for (const Aporia::Body& crate : crates) {
            const Aporia::Transform* crateTransform = game.scene().findComponent<Aporia::Transform>(
                crate.object(), game.scene().componentTypeId("Transform"));
            if (crateTransform == nullptr) {
                continue;
            }
            Aporia::RenderItem crateItem;
            crateItem.meshId = crate.mesh().value();
            const Aporia::Mat4 crateModel = Aporia::Mat4::translation(crateTransform->position) *
                                           Aporia::Mat4::fromQuaternion(crateTransform->orientation);
            std::memcpy(crateItem.modelMatrix, &crateModel, sizeof(crateModel));
            crateItem.color[0] = 0.65f;
            crateItem.color[1] = 0.45f;
            crateItem.color[2] = 0.25f;
            items.push_back(crateItem);
        }

        // Chase camera behind the chassis heading, then the mouse yaw/pitch.
        const double carYaw = std::atan2(forward.y, forward.x);
        const double yaw = carYaw + camYaw;
        const double horizontal = std::cos(camPitch) * camDistance;
        const double vertical = std::sin(camPitch) * camDistance + 1.2;
        const Aporia::Vec3 eye{
            carPosition.x - std::cos(yaw) * horizontal,
            carPosition.y - std::sin(yaw) * horizontal,
            carPosition.z + vertical};
        const Aporia::Vec3 target{carPosition.x, carPosition.y, carPosition.z + 0.7};
        const Aporia::Mat4 view = Aporia::Mat4::lookAt(eye, target, {0.0, 0.0, 1.0});
        const Aporia::Mat4 projection = Aporia::Mat4::perspectiveVulkan(
            1.05, 1280.0 / 720.0, 0.1, 400.0);
        Aporia::CameraState camera;
        std::memcpy(camera.viewMatrix, &view, sizeof(view));
        std::memcpy(camera.projectionMatrix, &projection, sizeof(projection));
        camera.position[0] = eye.x;
        camera.position[1] = eye.y;
        camera.position[2] = eye.z;

        Aporia::UiDrawList ui;
        ui.font = font;
        double displaySpeed = speed;
        const char* unitName = "m/s";
        if (speedUnits == 1) {
            displaySpeed = speed * 3.6; // km/h
            unitName = "km/h";
        } else if (speedUnits == 2) {
            displaySpeed = speed * 2.2369362920544; // mph
            unitName = "mph";
        }
        char hud[208];
        std::snprintf(hud, sizeof(hud),
                      "SPEED %5.1f %s   DIST %5.1f m   W/S drive  A/D steer  SPACE brake  RMB look  LMB crate  R reset  M units  X settings  ESC quit",
                      displaySpeed, unitName, odometer);
        Aporia::UiTextRun run;
        run.text = hud;
        run.x = 16.0f;
        run.y = 16.0f;
        run.size = 18.0f;
        run.color = {1.0f, 0.92f, 0.4f, 1.0f};
        ui.textRuns.push_back(run);

        if (settingsOpen) {
            const std::string qualityLine = std::string("Current preset: ") +
                (quality == Aporia::GraphicsQuality::Low ? "Low"
                 : quality == Aporia::GraphicsQuality::Medium ? "Medium" : "High");
            const std::string ultraLine = std::string("U Player ultra shadows: ") +
                (playerUltra ? "ON" : "OFF");
            ui.cornerRadius = 12.0f;
            ui.blur.mode = Aporia::BlurMode::Kawase;
            ui.blur.radius = 5.0f;
            ui.blur.intensity = 0.9f;
            ui.roundedRects.push_back({{24.0f, 50.0f, 370.0f, 185.0f}, {0.04f, 0.06f, 0.10f, 0.94f}});
            ui.textRuns.push_back({"GRAPHICS SETTINGS", 48.0f, 72.0f, 24.0f, {0.95f, 0.97f, 1.0f, 1.0f}});
            ui.textRuns.push_back({"1 Low    2 Medium    3 High", 48.0f, 112.0f, 16.0f, {0.85f, 0.88f, 0.94f, 1.0f}});
            ui.textRuns.push_back({ultraLine, 48.0f, 140.0f, 16.0f, {0.85f, 0.88f, 0.94f, 1.0f}});
            ui.textRuns.push_back({qualityLine, 48.0f, 168.0f, 16.0f, {0.85f, 0.88f, 0.94f, 1.0f}});
            ui.textRuns.push_back({"X close settings", 48.0f, 198.0f, 14.0f, {0.65f, 0.70f, 0.78f, 1.0f}});
        }
        game.graphics().setUiDrawList(std::move(ui));

        if (!game.graphics().render(camera, items)) {
            const std::string& error = game.graphics().lastError();
            if (error.find("out of date") == std::string::npos &&
                error.find("recreated") == std::string::npos) {
                std::cerr << "render failed: " << error << '\n';
                break;
            }
        }
    }

    game.shutdownGraphics();
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::cout << "Aporia render test 9 (drive) finished, odometer " << odometer << " m\n";
    return 0;
}
