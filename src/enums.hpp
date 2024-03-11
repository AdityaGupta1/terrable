#pragma once

#include <string>
#include <array>

namespace Terrable
{
    enum class TerrainLayer
    {
        BEDROCK,
        ROCK,
        SAND,
        HUMUS,
        MOISTURE,
        VEGETATION, // TODO: split into different vegetation types
        DEAD_VEGETATION // TODO: also split this one? unsure if necessary
    };
    static constexpr int numTerrainLayers = (int)TerrainLayer::DEAD_VEGETATION + 1;

    static std::array<std::string, numTerrainLayers> terrainLayerNames = {
        "bedrock",
        "rock",
        "sand",
        "humus",
        "moisture",
        "vegetation",
        "dead_vegetation"
    };

    static std::array<UT_Vector3, numTerrainLayers> terrainLayerColors = {
        UT_Vector3(0.2f, 0.2f, 0.2f), // bedrock
        UT_Vector3(0.4f, 0.4f, 0.4f), // rock
        UT_Vector3(255, 230, 128) / 255.f, // sand
        UT_Vector3(135, 97, 32) / 255.f, // humus
        UT_Vector3(1.f, 0.f, 1.f), // moisture
        UT_Vector3(1.f, 0.f, 1.f), // vegetation
        UT_Vector3(1.f, 0.f, 1.f), // dead_vegetation
    };

    enum class Event
    {
        RUNOFF,
        TEMPERATURE,
        LIGHTNING,
        GRAVITY,
        FIRE
    };
    static constexpr int numEvents = (int)Event::FIRE + 1;

    static std::array<UT_Vector2i, 4> cardinalDirections = {
        UT_Vector2i(1, 0),
        UT_Vector2i(0, 1),
        UT_Vector2i(-1, 0),
        UT_Vector2i(0, -1)
    };
}
