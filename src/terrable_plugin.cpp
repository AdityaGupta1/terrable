#include <UT/UT_DSOVersion.h>
#include <UT/UT_Math.h>
#include <UT/UT_Interrupt.h>

#include <GU/GU_Detail.h>
#include <GU/GU_PrimPoly.h>
#include <GU/GU_PrimVolume.h>

#include <CH/CH_LocalVariable.h>

#include <PRM/PRM_Include.h>
#include <PRM/PRM_SpareData.h>

#include <OP/OP_Operator.h>
#include <OP/OP_OperatorTable.h>
#include <OP/OP_AutoLockInputs.h>


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
    GEO_Primitive* prim = gdp->findPrimitiveByName(name.c_str());

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
        // read existing layers and populate nonexistent layers with default values

        if (!readTerrainLayer(&primVolume, "bedrock"))
        {
            return false;
        }

        auto& bedrockWriteHandle = primVolume->getVoxelWriteHandle();

        width = bedrockWriteHandle->getXRes();
        height = bedrockWriteHandle->getYRes();
        resizeTerrainLayersVector();

        for (int terrainLayerIdx = 0; terrainLayerIdx < numTerrainLayers; ++terrainLayerIdx)
        {
            if (readTerrainLayer(&primVolume, terrainLayerNames[terrainLayerIdx]))
            {
                auto& layerWriteHandle = primVolume->getVoxelWriteHandle();

                for (int y = 0; y < height; ++y)
                {
                    for (int x = 0; x < width; ++x)
                    {
                        float layerValue = layerWriteHandle->getValue(x, y, 0);
                        terrainLayers[posToIndex(x, y, (TerrainLayer)terrainLayerIdx)] = layerValue;
                    }
                }
            }
            else
            {
                for (int y = 0; y < height; ++y)
                {
                    for (int x = 0; x < width; ++x)
                    {
                        // bedrock layer must exist so there should be no issues with negative indices
                        terrainLayers[posToIndex(x, y, (TerrainLayer)terrainLayerIdx)] =
                            (terrainLayerIdx < numStackedTerrainLayers) ? terrainLayers[posToIndex(x, y, (TerrainLayer)(terrainLayerIdx - 1))] : 0;
                    }
                }
            }
        }
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
            }
        }

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                float sandHeight = terrainLayers[posToIndex(x, y, TerrainLayer::SAND)];

                // TODO: set humus based on approach at end of section 3.2 in paper
                float humusHeight = 0.f;

                terrainLayers[posToIndex(x, y, TerrainLayer::HUMUS)] = sandHeight + humusHeight;
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

bool SOP_Terrable::writeOutputLayers()
{
    GEO_PrimVolume* primVolume;

    for (int terrainLayerIdx = 0; terrainLayerIdx < numTerrainLayers; ++terrainLayerIdx)
    {
        const auto& layerName = terrainLayerNames[terrainLayerIdx];
        if (!readTerrainLayer(&primVolume, layerName))
        {
            UT_VoxelArrayF voxelArray;
            voxelArray.size(width, height, 1);

            primVolume = GU_PrimVolume::build(gdp);
            primVolume->setVoxels(&voxelArray);

            GA_RWHandleS nameAttribHandle(gdp->addStringTuple(GA_ATTRIB_PRIMITIVE, "name", 1));
            if (nameAttribHandle.isValid())
            {
                nameAttribHandle.set(primVolume->getMapOffset(), terrainLayerNames[terrainLayerIdx]);
            }
        }

        auto& layerWriteHandle = primVolume->getVoxelWriteHandle();

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                layerWriteHandle->setValue(x, y, 0, terrainLayers[posToIndex(x, y, (TerrainLayer)(terrainLayerIdx))]);
            }
        }
    }

    if (!readTerrainLayer(&primVolume, "height"))
    {
        return false;
    }

    auto& heightWriteHandle = primVolume->getVoxelWriteHandle();
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            //heightWriteHandle->setValue(x, y, 0, terrainLayers[posToIndex(x, y, (TerrainLayer)(numStackedTerrainLayers - 1))]); // write height of top stacked layer to "height"
            heightWriteHandle->setValue(x, y, 0, terrainLayers[posToIndex(x, y, TerrainLayer::MOISTURE)]); // TEMP: used for testnig
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
            // TEMP: outputting moisture for testing
            terrainLayers[posToIndex(x, y, TerrainLayer::MOISTURE)] += simTimeYears;
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
