#include <UT/UT_DSOVersion.h>
#include <UT/UT_Math.h>
#include <UT/UT_Interrupt.h>
#include <UT/UT_MxNoise.h>

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

constexpr float layerColorThreshold = 0.05f;

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

// TODO: add parameter to allow considering layers below the given layer (calculate cumulative height for each point and calculate slope based on that)
float SOP_Terrable::calculateSlope(int x, int y, TerrainLayer layer) const
{
    float hLeft = terrainLayers[posToIndex(std::max(x - 1, 0), y, layer)];
    float hRight = terrainLayers[posToIndex(std::min(x + 1, width - 1), y, layer)];
    float hDown = terrainLayers[posToIndex(x, std::max(y - 1, 0), layer)];
    float hUp = terrainLayers[posToIndex(x, std::min(y + 1, height - 1), layer)];

    float slopeX = (hRight - hLeft) / (2.f * xCellSize);
    float slopeY = (hUp - hDown) / (2.f * yCellSize);

    return sqrt(slopeX * slopeX + slopeY * slopeY);
}

void SOP_Terrable::setTerrainSize(int newWidth, int newHeight)
{
    width = newWidth;
    height = newHeight;
    terrainLayers.resize(numTerrainLayers * width * height);

    UT_Matrix4R xform;
    xform.identity();
    gdp->getBBox(bbox, xform); // not sure if providing identity matrix here does anything
    xCellSize = bbox.sizeX() / width;
    yCellSize = bbox.sizeY() / height;
}

bool SOP_Terrable::readTerrainLayer(GEO_PrimVolume** volume, const std::string& layerName)
{
    GEO_Primitive* prim = gdp->findPrimitiveByName(layerName.c_str());

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

        setTerrainSize(bedrockWriteHandle->getXRes(), bedrockWriteHandle->getYRes());

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
                        terrainLayers[posToIndex(x, y, (TerrainLayer)terrainLayerIdx)] = 0.f;
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

        setTerrainSize(writeHandle->getXRes(), writeHandle->getYRes());

        // set bedrock = input height
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                float bedrockHeight = writeHandle->getValue(x, y, 0);
                terrainLayers[posToIndex(x, y, TerrainLayer::BEDROCK)] = bedrockHeight;
            }
        }

        // set rock and sand = 0
        for (int terrainLayerIdx = (int)TerrainLayer::ROCK; terrainLayerIdx <= (int)TerrainLayer::SAND; ++terrainLayerIdx)
        {
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    terrainLayers[posToIndex(x, y, (TerrainLayer)terrainLayerIdx)] = 0.f;
                }
            }
        }

        // set humus based on bedrock slope
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                float bedrockSlope = calculateSlope(x, y, TerrainLayer::BEDROCK);
                float humusHeight = expf(-(bedrockSlope * bedrockSlope) / 0.480898346962988f);
                terrainLayers[posToIndex(x, y, TerrainLayer::HUMUS)] = humusHeight;
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

UT_VoxelArrayWriteHandleF SOP_Terrable::createOrReadLayerAndGetWriteHandle(const std::string& layerName, const GEO_PrimVolume* heightPrim)
{
    GEO_PrimVolume* primVolume;

    if (!readTerrainLayer(&primVolume, layerName))
    {
        UT_VoxelArrayF voxelArray;
        voxelArray.size(width, height, 1);

        primVolume = GU_PrimVolume::build(gdp);
        primVolume->setVoxels(&voxelArray);

        primVolume->setTransform(heightPrim->getTransform());

        GA_RWHandleS nameAttribHandle(gdp->addStringTuple(GA_ATTRIB_PRIMITIVE, "name", 1));
        nameAttribHandle.set(primVolume->getMapOffset(), layerName);
    }

    return primVolume->getVoxelWriteHandle();
}

bool SOP_Terrable::writeOutputLayers()
{
    GEO_PrimVolume* heightPrim;
    if (!readTerrainLayer(&heightPrim, "height"))
    {
        return false;
    }

    // set individual layer values, creating layers if necessary
    for (int terrainLayerIdx = 0; terrainLayerIdx < numTerrainLayers; ++terrainLayerIdx)
    {
        const auto& layerName = terrainLayerNames[terrainLayerIdx];
        auto& layerWriteHandle = createOrReadLayerAndGetWriteHandle(layerName, heightPrim);

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                layerWriteHandle->setValue(x, y, 0, terrainLayers[posToIndex(x, y, (TerrainLayer)(terrainLayerIdx))]);
            }
        }
    }

    // set height (combined height of bedrock and granular materials)
    auto& heightWriteHandle = heightPrim->getVoxelWriteHandle();
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            float height = 0.f;
            for (int terrainLayerIdx = (int)TerrainLayer::BEDROCK; terrainLayerIdx <= (int)TerrainLayer::HUMUS; ++terrainLayerIdx)
            {
                height += terrainLayers[posToIndex(x, y, (TerrainLayer)terrainLayerIdx)];
            }

            heightWriteHandle->setValue(x, y, 0, height);
        }
    }

    // set color
    const std::string suffixes[] = {"x", "y", "z"};
    for (int i = 0; i < 3; ++i)
    {
        const auto& suffix = suffixes[i];
        const std::string colorLayerName = "color." + suffix;
        auto& colorWriteHandle = createOrReadLayerAndGetWriteHandle(colorLayerName, heightPrim);

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                for (int terrainLayerIdx = (int)TerrainLayer::HUMUS; terrainLayerIdx >= (int)TerrainLayer::BEDROCK; --terrainLayerIdx)
                {
                    TerrainLayer terrainLayer = (TerrainLayer)terrainLayerIdx;
                    if (terrainLayers[posToIndex(x, y, terrainLayer)] > layerColorThreshold || terrainLayer == TerrainLayer::BEDROCK)
                    {
                        const auto& col = terrainLayerColors[terrainLayerIdx];
                        colorWriteHandle->setValue(x, y, 0, col[i]);
                        break;
                    }
                }
            }
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
            UT_Vector3 noise;
            UT_MxNoise::perlin(noise, { x * 0.01f, y * 0.01f });

            terrainLayers[posToIndex(x, y, TerrainLayer::ROCK)] += simTimeYears * std::max(0.f, noise.x());
            terrainLayers[posToIndex(x, y, TerrainLayer::SAND)] += simTimeYears * std::max(0.f, noise.y());
            terrainLayers[posToIndex(x, y, TerrainLayer::HUMUS)] += simTimeYears * std::max(0.f, noise.z());
        }
    }
}

void SOP_Terrable::stepSimulation(OP_Context& context)
{
    int numEventsToSimulate = width * height * numEvents;
    for (int i = 0; i < numEventsToSimulate; ++i)
    {

    }
}

void SOP_Terrable::simulateEvent(OP_Context& context, int x, int y, Event event)
{

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

    //increaseHeightfieldHeight(context);

    fpreal now = context.getTime();

    int simTimeYears = getSimTime(now);

    for (int step = 0; step < simTimeYears; ++step)
    {
        stepSimulation(context);
    }

    if (!writeOutputLayers())
    {
        addWarning(SOP_MESSAGE, "failed writing output layers");
        return error();
    }

    return error();
}
