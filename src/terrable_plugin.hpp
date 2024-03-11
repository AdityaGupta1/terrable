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

    void resizeTerrainLayersVector();
    bool readTerrainLayer(GEO_PrimVolume** volume, const std::string& name);
    bool readInputLayers();
    bool writeOutputLayers();

    void increaseHeightfieldHeight(OP_Context& context);

protected:
    OP_ERROR cookMySop(OP_Context& context) override;
};

} // namespace Terrable
