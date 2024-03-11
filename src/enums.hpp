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
}
