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
    static constexpr int numStackedTerrainLayers = (int)TerrainLayer::HUMUS + 1; // stacked terrain layers store cumulative height including current layer
    static constexpr int numTerrainLayers = (int)TerrainLayer::DEAD_VEGETATION + 1; // non-stacked terrain layers store only self height/value

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
