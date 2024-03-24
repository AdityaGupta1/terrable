#pragma once

#include <vector>

#include <SOP/SOP_Node.h>

#include "enums.hpp"

namespace Terrable
{

class SOP_Terrable : public SOP_Node
{
private:
    int width;
    int height;
    std::vector<float> terrainLayers;

    UT_BoundingBox bbox;
    float cellSize; // assuming square cells

protected:
    SOP_Terrable(OP_Network* net, const char* name, OP_Operator* op);
    virtual ~SOP_Terrable();

public:
    static OP_Node* myConstructor(OP_Network*, const char*, OP_Operator*);

    static PRM_Template myTemplateList[];

protected:
    unsigned disableParms() override;

private:
    int getIntParam(PRM_Name& name, OP_Context& context) { return evalInt(name.getTokenRef(), 0, context.getTime()); }
    float getFloatParam(PRM_Name& name, OP_Context& context) { return evalFloat(name.getTokenRef(), 0, context.getTime()); }

    size_t posToIndex(int x, int y, TerrainLayer layer) const;
    inline size_t posToIndex(const UT_Vector2i& pos, TerrainLayer layer) const
    {
        return posToIndex(pos.x(), pos.y(), layer);
    }

    float calculateElevation(int x, int y, TerrainLayer topLayer = TerrainLayer::HUMUS) const;
    inline float calculateElevation(const UT_Vector2i& pos, TerrainLayer topLayer = TerrainLayer::HUMUS) const
    {
        return calculateElevation(pos.x(), pos.y(), topLayer);
    }
    float calculateSlope(int x, int y, TerrainLayer topLayer = TerrainLayer::HUMUS) const;
    float calculateSlope(const UT_Vector2i& pos1, const UT_Vector2i& pos2, TerrainLayer topLayer = TerrainLayer::HUMUS) const;
    float calculateCuravature(int x, int y) const;

    void setTerrainSize(int newWidth, int newHeight);

    bool readTerrainLayer(GEO_PrimVolume** volume, const std::string& layerName);
    bool readInputLayers();

    UT_VoxelArrayWriteHandleF createOrReadLayerAndGetWriteHandle(const std::string& layerName, const GEO_PrimVolume* heightPrim);
    bool writeOutputLayers();

    void stepSimulation(OP_Context& context);
    void simulateEvent(OP_Context& context, int x, int y, Event event);

    struct TerrainLayerChange
    {
        UT_Vector2i pos;
        TerrainLayer layer;
        float change;

        TerrainLayerChange(UT_Vector2i pos, TerrainLayer layer, float change)
            : pos(pos), layer(layer), change(change)
        {
        }
    };
    void applyTerrainLayerChanges(const std::vector<TerrainLayerChange>& terrainLayerChanges);

    bool calculateNextPosFromSlope(const UT_Vector2i& thisPos, UT_Vector2i* nextPos, float* slope, TerrainLayer topLayer = TerrainLayer::HUMUS);

    void simulateRunoffEvent(OP_Context& context, int x, int y);
    void simulateTemperatureEvent(OP_Context& context, int x, int y);
    void simulateLightningEvent(OP_Context& context, int x, int y);
    void simulateGravityEvent(OP_Context& context, int x, int y);
    void simulateFireEvent(OP_Context& context, int x, int y);

protected:
    OP_ERROR cookMySop(OP_Context& context) override;
};

} // namespace Terrable
