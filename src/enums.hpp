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
        VEGETATION,
        DEAD_VEGETATION
    };
    static constexpr int numStackedTerrainLayers = (int)TerrainLayer::HUMUS + 1;
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
