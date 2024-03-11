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
    : SOP_Node(net, name, op), width(-1), height(-1), xCellSize(0), yCellSize(0)
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

static PRM_Name seedName("seed", "Random Seed");
static PRM_Default seedDefault(0);
static PRM_Range seedRange(PRM_RANGE_UI, 0, PRM_RANGE_UI, 100);

PRM_Template SOP_Terrable::myTemplateList[] = {
    PRM_Template(PRM_INT, PRM_Template::PRM_EXPORT_MIN, 1, &simTimeName, &simTimeDefault, 0, &simTimeRange),
    PRM_Template(PRM_INT, PRM_Template::PRM_EXPORT_MIN, 1, &seedName, &seedDefault, 0, &seedRange),
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

float SOP_Terrable::calculateElevation(int x, int y) const
{
    float elevation = 0.f;
    for (int terrainLayerIdx = (int)TerrainLayer::BEDROCK; terrainLayerIdx <= (int)TerrainLayer::HUMUS; ++terrainLayerIdx)
    {
        elevation += terrainLayers[posToIndex(x, y, (TerrainLayer)terrainLayerIdx)];
    }
    return elevation;
}

float SOP_Terrable::calculateSlope(int x, int y) const
{
    float hLeft = calculateElevation(std::max(x - 1, 0), y);
    float hRight = calculateElevation(std::min(x + 1, width - 1), y);
    float hDown = calculateElevation(x, std::max(y - 1, 0));
    float hUp = calculateElevation(x, std::min(y + 1, height - 1));

    float slopeX = (hRight - hLeft) / (2.f * xCellSize);
    float slopeY = (hUp - hDown) / (2.f * yCellSize);

    return sqrt(slopeX * slopeX + slopeY * slopeY);
}

float SOP_Terrable::calculateSlope(UT_Vector2i pos1, UT_Vector2i pos2) const
{
    float h1 = calculateElevation(pos1.x(), pos1.y());
    float h2 = calculateElevation(pos2.x(), pos2.y());

    float dx = pos1.x() - pos2.x();
    float dy = pos1.y() - pos2.y();
    float d = sqrtf(dx * dx + dy * dy);

    return (h2 - h1) / d;
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
                float bedrockSlope = calculateSlope(x, y);
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
            heightWriteHandle->setValue(x, y, 0, calculateElevation(x, y));
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

void SOP_Terrable::stepSimulation(OP_Context& context)
{
    int numEventsToSimulate = width * height * numEvents;
    for (int i = 0; i < numEventsToSimulate; ++i)
    {
        int x = SYSdrand48() * width;
        int y = SYSdrand48() * height;
        Event event = (Event)(SYSdrand48() * numEvents);
        simulateEvent(context, x, y, event);
    }
}

void SOP_Terrable::simulateEvent(OP_Context& context, int x, int y, Event event)
{
    switch (event)
    {
    case Event::RUNOFF:
        simulateRunoffEvent(x, y);
        break;
    case Event::TEMPERATURE:
        // TODO
        break;
    case Event::LIGHTNING:
        simulateLightningEvent(x, y);
        break;
    case Event::GRAVITY:
        // TODO
        break;
    case Event::FIRE:
        // TODO
        break;
    }
}

void SOP_Terrable::simulateRunoffEvent(int x, int y)
{
    // TODO: calculate initial water amount

    UT_Vector2i sourcePos = { x, y };

    UT_Vector2i currentPos = sourcePos;
    UT_Vector2i nextPos;
    while (true)
    {
        std::vector<std::pair<UT_Vector2i, float>> nextPosCandidates;
        float totalSlope = 0.f;
        for (const auto& cardinalDirection : cardinalDirections)
        {
            UT_Vector2i nextPosCandidate = currentPos + cardinalDirection;
            if (nextPosCandidate.x() < 0 || nextPosCandidate.x() >= width || nextPosCandidate.y() < 0 || nextPosCandidate.y() >= height)
            {
                continue;
            }

            float slope = calculateSlope(currentPos, nextPosCandidate);
            if (slope >= 0.f)
            {
                continue;
            }

            slope = -slope; // make it positive
            nextPosCandidates.emplace_back(nextPosCandidate, slope);
            totalSlope += slope;
        }

        if (totalSlope == 0.f) // reached terrain local minimum
        {
            // TODO: what happens to excess water?
            break;
        }

        float rand = SYSdrand48() * totalSlope;
        for (const auto& [nextPosCandidate, slope] : nextPosCandidates)
        {
            if (rand < slope)
            {
                nextPos = nextPosCandidate;
                break;
            }

            rand -= slope;
        }

        // TEMP: testing
        terrainLayers[posToIndex(currentPos.x(), currentPos.y(), TerrainLayer::BEDROCK)] -= 0.1f;
        terrainLayers[posToIndex(nextPos.x(), nextPos.y(), TerrainLayer::BEDROCK)] += 0.1f;

        // TODO: actual erosion
        // probably scale sediment capacity by cell size?
    }
}

void SOP_Terrable::simulateLightningEvent(int x, int y)
{
    // TODO
}

OP_ERROR SOP_Terrable::cookMySop(OP_Context& context)
{
    OP_AutoLockInputs inputs(this);
    if (inputs.lock(context) >= UT_ERROR_ABORT)
    {
        return error();
    }

    UT_Interrupt* boss = UTgetInterrupt();
    if (!boss->opStart("running Terrable simulation"))
    {
        boss->opEnd();
        return error();
    }

    duplicateSource(0, context); // duplicate input geometry

    if (!readInputLayers())
    {
        addWarning(SOP_MESSAGE, "failed reading input layers");
        boss->opEnd();
        return error();
    }

    fpreal now = context.getTime();

    int simTimeYears = getSimTime(now);
    int seed = getSeed(now);

    SYSsrand48(seed);

    for (int step = 0; step < simTimeYears; ++step)
    {
        if (boss->opInterrupt((int)((step * 100.f) / simTimeYears)))
        {
            break;
        }

        stepSimulation(context);
    }

    if (!writeOutputLayers())
    {
        addWarning(SOP_MESSAGE, "failed writing output layers");
        boss->opEnd();
        return error();
    }

    boss->opEnd();
    return error();
}
