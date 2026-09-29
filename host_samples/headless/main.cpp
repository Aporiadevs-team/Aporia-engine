#include "Aporia/Game.hpp"

int main() {
    Aporia::Game game;
    const Aporia::ObjectHandle object = game.scene().createObject("host");
    game.step();
    return game.scene().contains(object) && game.lastSubstepCount() == 1 ? 0 : 1;
}
