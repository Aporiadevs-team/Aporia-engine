// Aporia Engine - base project.
// The engine itself lives in engine\, everything here is yours.
#include "Aporia/Game.hpp"

#include <cstdio>

int main() {
    Aporia::Game game;
    const Aporia::ObjectHandle object = game.scene().createObject("player");
    game.step();

    std::printf("smak, engine %s\n", game.scene().contains(object) ? "runs" : "sleeps");
    return 0;
}
