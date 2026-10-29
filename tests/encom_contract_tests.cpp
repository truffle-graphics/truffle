#include "test_support.hpp"
#include "truffle/encom/world.hpp"

#include <vector>

namespace {

struct Position {
    float x = 0.0F;
};

struct Velocity {
    float x = 0.0F;
};

} // namespace

int main() {
    truffle::encom::World world;
    const auto movingEntity = world.create();
    const auto staticEntity = world.create();

    world.emplace<Position>(movingEntity, Position{2.0F});
    world.emplace<Velocity>(movingEntity, Velocity{3.0F});
    world.emplace<Position>(staticEntity, Position{9.0F});

    std::size_t queryCount = 0;
    world.each<Position, Velocity>(
        [&queryCount](truffle::encom::Entity, Position& position, Velocity& velocity) {
            position.x += velocity.x;
            ++queryCount;
        });

    TRUFFLE_CHECK(queryCount == 1);
    TRUFFLE_CHECK(world.get<Position>(movingEntity)->x == 5.0F);
    TRUFFLE_CHECK(world.get<Position>(staticEntity)->x == 9.0F);

    std::vector<truffle::encom::World::System> systems{
        [](truffle::encom::World& systemWorld, double deltaSeconds) {
            systemWorld.each<Position>(
                [deltaSeconds](truffle::encom::Entity, Position& position) {
                    position.x += static_cast<float>(deltaSeconds);
                });
        },
    };
    world.run(systems, 0.5);
    TRUFFLE_CHECK(world.get<Position>(movingEntity)->x == 5.5F);

    TRUFFLE_CHECK(world.destroy(movingEntity).ok());
    TRUFFLE_CHECK(!world.alive(movingEntity));
    TRUFFLE_CHECK(world.get<Position>(movingEntity) == nullptr);

    const auto recycledEntity = world.create();
    TRUFFLE_CHECK(recycledEntity.index == movingEntity.index);
    TRUFFLE_CHECK(!world.has<Position>(recycledEntity));
    return 0;
}
