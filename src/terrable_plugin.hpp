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
    float xCellSize;
    float yCellSize;

protected:
    SOP_Terrable(OP_Network* net, const char* name, OP_Operator* op);
    virtual ~SOP_Terrable();

public:
    static OP_Node* myConstructor(OP_Network*, const char*, OP_Operator*);

    static PRM_Template myTemplateList[];

protected:
    unsigned disableParms() override;

private:
    int getSimTime(fpreal t) { return evalInt("sim_time", 0, t); }

    size_t posToIndex(int x, int y, TerrainLayer layer) const;

    float calculateSlope(int x, int y, TerrainLayer layer) const;

    void setTerrainSize(int newWidth, int newHeight);

    bool readTerrainLayer(GEO_PrimVolume** volume, const std::string& layerName);
    bool readInputLayers();

    UT_VoxelArrayWriteHandleF createOrReadLayerAndGetWriteHandle(const std::string& layerName, const GEO_PrimVolume* heightPrim);
    bool writeOutputLayers();

    // TEMP: used for basic testing
    void increaseHeightfieldHeight(OP_Context& context);

    void stepSimulation(OP_Context& context);
    void simulateEvent(OP_Context& context, int x, int y, Event event);

protected:
    OP_ERROR cookMySop(OP_Context& context) override;
};

} // namespace Terrable
