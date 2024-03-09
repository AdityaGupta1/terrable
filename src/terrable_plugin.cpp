#include <UT/UT_DSOVersion.h>
#include <UT/UT_Math.h>
#include <UT/UT_Interrupt.h>

#include <GU/GU_Detail.h>
#include <GU/GU_PrimPoly.h>

#include <CH/CH_LocalVariable.h>

#include <PRM/PRM_Include.h>
#include <PRM/PRM_SpareData.h>

#include <OP/OP_Operator.h>
#include <OP/OP_OperatorTable.h>
#include <OP/OP_AutoLockInputs.h>

#include <GEO/GEO_PrimVolume.h>

#include <limits.h>
#include "terrable_plugin.hpp"

using namespace Terrable;

SOP_Terrable::SOP_Terrable(OP_Network* net, const char* name, OP_Operator* op)
    : SOP_Node(net, name, op), width(-1), height(-1)
{}

SOP_Terrable::~SOP_Terrable() {}

void newSopOperator(OP_OperatorTable* table)
{
    table->addOperator(
        new OP_Operator(
            "TerrableMain", // internal name (also used for name of newly created node)
            "Terrable", // UI name (shows up in node search)
            SOP_Terrable::myConstructor, // constructor
            SOP_Terrable::myTemplateList, // parameters
            1, // min # of sources
            1, // max # of sources
            nullptr, // local variables
            OP_FLAG_GENERATOR // flag it as generator
        )
    );
}

static PRM_Name simTimeName("sim_time", "Simulation Time (years)");
static PRM_Default simTimeDefault(1);
static PRM_Range simTimeRange(PRM_RANGE_RESTRICTED, 0, PRM_RANGE_UI, 100);

PRM_Template SOP_Terrable::myTemplateList[] = {
    PRM_Template(PRM_INT, PRM_Template::PRM_EXPORT_MIN, 1, &simTimeName, &simTimeDefault, 0, &simTimeRange),
    PRM_Template()
};

OP_Node* SOP_Terrable::myConstructor(OP_Network* net, const char* name, OP_Operator* op)
{
    return new SOP_Terrable(net, name, op);
}

unsigned SOP_Terrable::disableParms()
{
    return 0;
}

size_t SOP_Terrable::posToIndex(int x, int y, TerrainLayer layer) const
{
    return ((int)layer * height * width) + (y * width) + x;
}

void SOP_Terrable::resizeTerrainLayersVector()
{
    terrainLayers.resize(numTerrainLayers * height * width);
}

bool SOP_Terrable::readTerrainLayer(GEO_PrimVolume** volume, const std::string& name)
{
    GEO_Primitive* prim = gdp->findPrimitiveByName("height");

    if (prim == nullptr || prim->getTypeId() != GEO_PRIMVOLUME)
    {
        return false;
    }

    GEO_PrimVolume* primVolumePtr = static_cast<GEO_PrimVolume*>(prim);

    if (primVolumePtr == nullptr)
    {
        return false;
    }

    *volume = primVolumePtr;
    return true;
}

bool SOP_Terrable::readInputLayers()
{
    if (!gdp || !gdp->hasVolumePrimitives())
    {
        return false;
    }

    GEO_PrimVolume* primVolume;

    bool hasBedrock = gdp->findPrimitiveByName("bedrock") != nullptr;
    bool hasHeight = gdp->findPrimitiveByName("height") != nullptr;

    if (!hasBedrock && !hasHeight)
    {
        return false;
    }

    if (hasBedrock)
    {
        // read existing layers and populate all others with some default value (probably 0)

        if (!readTerrainLayer(&primVolume, "bedrock"))
        {
            return false;
        }

        auto& writeHandle = primVolume->getVoxelWriteHandle();

        width = writeHandle->getXRes();
        height = writeHandle->getYRes();
        resizeTerrainLayersVector();

        // TODO: read all terrain layers (probably using do-while to use already read bedrock voxel array)
    }
    else // hasHeight
    {
        // assume we want to populate layers from scratch using height as bedrock

        if (!readTerrainLayer(&primVolume, "height"))
        {
            return false;
        }

        auto& writeHandle = primVolume->getVoxelWriteHandle();

        width = writeHandle->getXRes();
        height = writeHandle->getYRes();
        resizeTerrainLayersVector();

        // set bedrock = input height and rock/sand = 0
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                float bedrockHeight = writeHandle->getValue(x, y, 0);
                terrainLayers[posToIndex(x, y, TerrainLayer::BEDROCK)] = bedrockHeight;
                terrainLayers[posToIndex(x, y, TerrainLayer::ROCK)] = bedrockHeight;
                terrainLayers[posToIndex(x, y, TerrainLayer::SAND)] = bedrockHeight;

                // TODO: set humus based on approach at end of section 3.2 in paper
                terrainLayers[posToIndex(x, y, TerrainLayer::HUMUS)] = bedrockHeight;
            }
        }

        // set moisture, vegetation, and dead vegetation = 0
        for (int terrainLayerIdx = (int)TerrainLayer::MOISTURE; terrainLayerIdx <= (int)TerrainLayer::DEAD_VEGETATION; ++terrainLayerIdx)
        {
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    terrainLayers[posToIndex(x, y, (TerrainLayer)terrainLayerIdx)] = 0.f;
                }
            }
        }
    }

    return true;
}

// for actual terrain layers (bedrock, sand, etc.), should this write height or thickness?
bool SOP_Terrable::writeOutputLayers()
{
    // TODO: output layers to heightfield and create new VolumePrims if necessary
    //       also calculate combined height (bedrock + granular materials) and output that into default height layer

    // TEMP: output bedrock directly to height
    GEO_PrimVolume* primVolume;
    if (!readTerrainLayer(&primVolume, "height"))
    {
        return false;
    }

    auto& writeHandle = primVolume->getVoxelWriteHandle();
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            writeHandle->setValue(x, y, 0, terrainLayers[posToIndex(x, y, TerrainLayer::BEDROCK)]);
        }
    }
}

// TEMP: used for basic testing
void SOP_Terrable::increaseHeightfieldHeight(OP_Context& context)
{
    fpreal now = context.getTime();

    int simTimeYears = getSimTime(now);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            terrainLayers[posToIndex(x, y, TerrainLayer::BEDROCK)] += simTimeYears;
        }
    }
}

OP_ERROR SOP_Terrable::cookMySop(OP_Context& context)
{
    OP_AutoLockInputs inputs(this);
    if (inputs.lock(context) >= UT_ERROR_ABORT)
    {
        return error();
    }

    duplicateSource(0, context); // duplicate input geometry

    if (!readInputLayers())
    {
        addWarning(SOP_MESSAGE, "failed reading input layers");
        return error();
    }

    increaseHeightfieldHeight(context);

    if (!writeOutputLayers())
    {
        addWarning(SOP_MESSAGE, "failed writing output layers");
        return error();
    }

    return error();
}
