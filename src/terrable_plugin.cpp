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
    : SOP_Node(net, name, op), width(-1), height(-1), cellSize(0.f)
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

static PRM_Name lightningChanceName("lightning_chance", "Lightning Chance");
static PRM_Default lightningChanceDefault(0.005f);
static PRM_Range lightningChanceRange(PRM_RANGE_RESTRICTED, 0.f, PRM_RANGE_RESTRICTED, 1.f);

PRM_Template SOP_Terrable::myTemplateList[] = {
    PRM_Template(PRM_INT, PRM_Template::PRM_EXPORT_MIN, 1, &simTimeName, &simTimeDefault, 0, &simTimeRange),
    PRM_Template(PRM_INT, PRM_Template::PRM_EXPORT_MIN, 1, &seedName, &seedDefault, 0, &seedRange),
    PRM_Template(PRM_FLT, PRM_Template::PRM_EXPORT_MIN, 1, &lightningChanceName, &lightningChanceDefault, 0, &lightningChanceRange),

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

float SOP_Terrable::calculateElevation(int x, int y, TerrainLayer topLayer) const
{
    float elevation = 0.f;
    for (int terrainLayerIdx = (int)TerrainLayer::BEDROCK; terrainLayerIdx <= (int)topLayer; ++terrainLayerIdx)
    {
        elevation += terrainLayers[posToIndex(x, y, (TerrainLayer)terrainLayerIdx)];
    }
    return elevation;
}

float SOP_Terrable::calculateSlope(int x, int y, TerrainLayer topLayer) const
{
    float hLeft = calculateElevation(std::max(x - 1, 0), y, topLayer);
    float hRight = calculateElevation(std::min(x + 1, width - 1), y, topLayer);
    float hDown = calculateElevation(x, std::max(y - 1, 0), topLayer);
    float hUp = calculateElevation(x, std::min(y + 1, height - 1), topLayer);

    float slopeX = (hRight - hLeft) / (2.f * cellSize);
    float slopeY = (hUp - hDown) / (2.f * cellSize);

    return sqrt(slopeX * slopeX + slopeY * slopeY);
}

float SOP_Terrable::calculateSlope(const UT_Vector2i& pos1, const UT_Vector2i& pos2, TerrainLayer topLayer) const
{
    float h1 = calculateElevation(pos1, topLayer);
    float h2 = calculateElevation(pos2, topLayer);

    float dx = pos2.x() - pos1.x();
    float dy = pos2.y() - pos1.y();
    float d = sqrtf(dx * dx + dy * dy) * cellSize;

    return (h2 - h1) / d;
}

float SOP_Terrable::calculateCuravature(int x, int y) const
{
    return calculateSlope(x, y); // TODO: pretty sure curvature calculation is more complicated than this
}

void SOP_Terrable::setTerrainSize(int newWidth, int newHeight)
{
    width = newWidth;
    height = newHeight;
    terrainLayers.clear();
    terrainLayers.resize(numTerrainLayers * width * height);

    UT_Matrix4R xform;
    xform.identity();
    gdp->getBBox(bbox, xform); // not sure if providing identity matrix here does anything
    cellSize = bbox.sizeX() / width;
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
                //float humusHeight = expf(-(bedrockSlope * bedrockSlope) / 0.480898346962988f);
                float humusHeight = expf(7.f * -(bedrockSlope * bedrockSlope));
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

    return true;
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
        simulateRunoffEvent(context, x, y);
        break;
    case Event::TEMPERATURE:
        simulateTemperatureEvent(context, x, y);
        break;
    case Event::LIGHTNING:
        simulateLightningEvent(context, x, y);
        break;
    case Event::GRAVITY:
        simulateGravityEvent(context, x, y);
        break;
    case Event::FIRE:
        simulateFireEvent(context, x, y);
        break;
    }
}

// TODO: make these into editable node parameters
constexpr float bedrockSoftness = 0.004f; // higher = more erosion
constexpr float bedrockSedimentShieldingFactor = 1.2f; // higher = more shielding
constexpr float rockSoftness = 0.008f;

constexpr float rockDepositionConstant = 0.8f; // higher = more deposition
constexpr float sandDepositionConstant = 0.7f;
constexpr float humusDepositionConstant = 0.6f;

constexpr float sedimentCapacityConstant = 0.01f; // higher = more sediment transported

constexpr float rockMoistureCapacity = 0.02f;
constexpr float sandMoistureCapacity = 0.05f;
constexpr float humusMoistureCapacity = 0.20f;

constexpr float soilMoistureAbsorptionRate = 0.12f;

constexpr float sourceMoistureReduction = 0.5f;

void SOP_Terrable::applyTerrainLayerChanges(const std::vector<TerrainLayerChange>& terrainLayerChanges)
{
    for (const auto& change : terrainLayerChanges)
    {
        terrainLayers[posToIndex(change.pos, change.layer)] += change.change;
    }
}

bool SOP_Terrable::calculateNextPosFromSlope(const UT_Vector2i& thisPos, UT_Vector2i* nextPos, float* slope, TerrainLayer topLayer)
{
    std::vector<std::pair<UT_Vector2i, float>> nextPosCandidates;

    float totalSlope = 0.f;
    for (const auto& cardinalDirection : cardinalDirections)
    {
        UT_Vector2i nextPosCandidate = thisPos + cardinalDirection;
        if (nextPosCandidate.x() < 0 || nextPosCandidate.x() >= width ||
            nextPosCandidate.y() < 0 || nextPosCandidate.y() >= height)
        {
            continue;
        }

        float slope = calculateSlope(thisPos, nextPosCandidate, topLayer);
        if (slope >= 0.f)
        {
            continue;
        }

        slope = -slope; // make it positive
        nextPosCandidates.emplace_back(nextPosCandidate, slope);
        totalSlope += slope;
    }

    if (nextPosCandidates.empty())
    {
        return false;
    }

    float rand = SYSdrand48() * totalSlope;
    for (const auto& [nextPosCandidate, nextPosSlope] : nextPosCandidates)
    {
        if (rand < nextPosSlope)
        {
            *nextPos = nextPosCandidate;
            *slope = nextPosSlope;
            return true;
        }

        rand -= nextPosSlope;
    }

    return false;
}

void SOP_Terrable::simulateRunoffEvent(OP_Context& context, int x, int y)
{
    std::vector<TerrainLayerChange> terrainLayerChanges;

    // TODO: set initial water based on rainfall
    // TODO: reduce initial water amount proportionally to plant density (water intercepted by plants and released to the atmosphere through evaporation)
    float currentWater = 1.6f;
    float carriedRock = 0.f;
    float carriedSand = 0.f;
    float carriedHumus = 0.f;

    UT_Vector2i sourcePos(x, y);

    UT_Vector2i thisPos = sourcePos;
    UT_Vector2i nextPos;
    float nextPosSlope;
    while (true)
    {
        bool foundNextPos = calculateNextPosFromSlope(thisPos, &nextPos, &nextPosSlope);

        if (!foundNextPos || currentWater <= 0.f) // reached terrain local minimum or ran out of water
        {
            // TODO: what happens to excess water?
            terrainLayerChanges.emplace_back(thisPos, TerrainLayer::ROCK, carriedRock);
            terrainLayerChanges.emplace_back(thisPos, TerrainLayer::SAND, carriedSand);
            terrainLayerChanges.emplace_back(thisPos, TerrainLayer::HUMUS, carriedHumus);
            break;
        }

        float thisRock = terrainLayers[posToIndex(thisPos, TerrainLayer::ROCK)];
        float thisSand = terrainLayers[posToIndex(thisPos, TerrainLayer::SAND)];
        float thisHumus = terrainLayers[posToIndex(thisPos, TerrainLayer::HUMUS)];

        float thisMoistureCapacity =
            thisRock * rockMoistureCapacity +
            thisSand * sandMoistureCapacity +
            thisHumus * humusMoistureCapacity;
        float thisMoisture = terrainLayers[posToIndex(thisPos, TerrainLayer::MOISTURE)];

        float soilAbsorption = fmin(soilMoistureAbsorptionRate / nextPosSlope, thisMoistureCapacity - thisMoisture);
        soilAbsorption = fmin(soilAbsorption, currentWater);
        currentWater -= soilAbsorption;
        terrainLayerChanges.emplace_back(thisPos, TerrainLayer::MOISTURE, +soilAbsorption);

        float currentSedimentCapacity = currentWater * sedimentCapacityConstant;

        float currentSediment = carriedRock + carriedSand + carriedHumus;
        if (currentSediment > currentSedimentCapacity)
        {
            float excessSedimentRatio = (currentSediment - currentSedimentCapacity) / currentSediment;

            if (carriedRock > 0.f)
            {
                float rockDeposition = carriedRock * excessSedimentRatio * rockDepositionConstant;
                carriedRock -= rockDeposition;
                terrainLayerChanges.emplace_back(thisPos, TerrainLayer::ROCK, rockDeposition);
            }

            if (carriedSand > 0.f)
            {
                float sandDeposition = carriedSand * excessSedimentRatio * sandDepositionConstant;
                carriedSand -= sandDeposition;
                terrainLayerChanges.emplace_back(thisPos, TerrainLayer::SAND, sandDeposition);
            }

            if (carriedHumus > 0.f)
            {
                float humusDeposition = carriedHumus * excessSedimentRatio * humusDepositionConstant;
                carriedHumus -= humusDeposition;
                terrainLayerChanges.emplace_back(thisPos, TerrainLayer::HUMUS, humusDeposition);
            }
        }
        else
        {
            // TODO: dampen by vegetation amount
            float excessSedimentCapacity = currentSedimentCapacity - currentSediment;

            if (thisRock > 0.f)
            {
                float rockErosion = fmin(thisRock, excessSedimentCapacity) * rockSoftness;
                terrainLayerChanges.emplace_back(thisPos, TerrainLayer::ROCK, -rockErosion);
                carriedSand += rockErosion;
                excessSedimentCapacity = fmax(0.f, excessSedimentCapacity - rockErosion);
            }

            float thisSediment = thisRock + thisSand + thisHumus;
            float bedrockErosionFactor = 1.f / (1.f + bedrockSedimentShieldingFactor * thisSediment);
            float bedrockErosion = excessSedimentCapacity * bedrockErosionFactor * bedrockSoftness;
            terrainLayerChanges.emplace_back(thisPos, TerrainLayer::BEDROCK, -bedrockErosion);
            carriedRock += bedrockErosion;
        }

        thisPos = nextPos;
    }

    applyTerrainLayerChanges(terrainLayerChanges);

    // "Once the runoff sequence terminates we approximate the effects of plant transpiration and seepage into groundwater
    // by reducing the moisture at the source p0 by a constant amount."
    float& sourceMoisture = terrainLayers[posToIndex(sourcePos, TerrainLayer::MOISTURE)];
    sourceMoisture = fmax(sourceMoisture - sourceMoistureReduction, 0.f);
}

void SOP_Terrable::simulateTemperatureEvent(OP_Context& context, int x, int y)
{
    // TODO
}

// TODO: make these into editable node parameters
constexpr float k_l_c = 1.2f; // curvature scaling factor
constexpr float k_l_s = 2.0f; // minimum curvature for which maximum lightning chance is achieved
constexpr float lightningBedrockToRemove = 0.4f;

void SOP_Terrable::simulateLightningEvent(OP_Context& context, int x, int y)
{
    fpreal now = context.getTime();

    std::vector<TerrainLayerChange> terrainLayerChanges;

    UT_Vector2i sourcePos = { x, y };

    UT_Vector2i thisPos = sourcePos;

    // float thisBedrock = terrainLayers[posToIndex(thisPos, TerrainLayer::BEDROCK)];
    float thisRock = terrainLayers[posToIndex(thisPos, TerrainLayer::ROCK)];
    float thisSand = terrainLayers[posToIndex(thisPos, TerrainLayer::SAND)];
    float thisVegetation = terrainLayers[posToIndex(thisPos, TerrainLayer::VEGETATION)];
    float thisDeadVegetation = terrainLayers[posToIndex(thisPos, TerrainLayer::DEAD_VEGETATION)];

    std::vector<std::pair<UT_Vector2i, float>> nextPosCandidates;

    float localCurvature = calculateCuravature(x, y);

    // k_L = maximum probability that lightning strikes at that cell
    float k_L = getFloatParam(lightningChanceName, context);

    // lp = probability of damage
    float lp = k_L * fmin(1.f, expf(k_l_c * (localCurvature - k_l_s)));

    float r = SYSdrand48(); // get a random float between 0 and 1

    // damage done
    if (r < lp) {
        // TODO: destroy vegetation if present and exit early accordingly
        //       based on the paper's wording, it seems like no damage is done to bedrock if vegetation is destroyed by lightning

        // obtain 4 directly surrounding coords
        std::vector<UT_Vector2i> nextPosCandidates;
        nextPosCandidates.emplace_back(thisPos);

        for (const auto& cardinalDirection : cardinalDirections)
        {
            UT_Vector2i nextPosCandidate = thisPos + cardinalDirection;
            if (nextPosCandidate.x() < 0 || nextPosCandidate.x() >= width ||
                nextPosCandidate.y() < 0 || nextPosCandidate.y() >= height)
            {
                continue;
            }
            nextPosCandidates.emplace_back(nextPosCandidate);
        }

        // spread granular materials to 4 directly surrounding coords
        for (UT_Vector2i candidate : nextPosCandidates)
        {
            float r2 = SYSdrand48(); // get a random float between 0 and 1
            if (r2 > 0.3f) {
                terrainLayerChanges.emplace_back(candidate, TerrainLayer::ROCK, lightningBedrockToRemove * 0.25f);
            }
            else {
                terrainLayerChanges.emplace_back(candidate, TerrainLayer::SAND, lightningBedrockToRemove * 0.25f);
            }
            break;
        }

        // remove bedrock in current coord
        terrainLayerChanges.emplace_back(thisPos, TerrainLayer::BEDROCK, -lightningBedrockToRemove);
    }

    // make all changes
    applyTerrainLayerChanges(terrainLayerChanges);
}

// TODO: make these into editable node parameters
constexpr float rockFrictionAngleDegrees = 22.f;
constexpr float sandFrictionAngleDegrees = 18.f;
constexpr float humusFrictionAngleDegrees = 16.f;

void SOP_Terrable::simulateGravityEvent(OP_Context& context, int x, int y)
{
    float rand = SYSdrand48();
    TerrainLayer terrainLayer;
    float frictionAngleDegrees;
    if (rand < 0.333333333333333f)
    {
        terrainLayer = TerrainLayer::ROCK;
        frictionAngleDegrees = rockFrictionAngleDegrees;
    }
    else if (rand < 0.666666666666666f)
    {
        terrainLayer = TerrainLayer::SAND;
        frictionAngleDegrees = sandFrictionAngleDegrees;
    }
    else
    {
        terrainLayer = TerrainLayer::HUMUS;
        frictionAngleDegrees = humusFrictionAngleDegrees;
    }

    std::vector<TerrainLayerChange> terrainLayerChanges;

    UT_Vector2i thisPos(x, y);
    UT_Vector2i nextPos;
    float nextPosSlope;
    while (true)
    {
        float thisSediment = terrainLayers[posToIndex(thisPos, terrainLayer)];
        if (thisSediment <= 0.f || !calculateNextPosFromSlope(thisPos, &nextPos, &nextPosSlope, terrainLayer))
        {
            break;
        }

        // TODO: increase friction angle based on vegetation
        float frictionHeight = tanf(SYSdegToRad(frictionAngleDegrees)) * cellSize;

        float thisElevation = calculateElevation(thisPos, terrainLayer);
        float nextElevation = calculateElevation(nextPos, terrainLayer);
        float heightGap = thisElevation - nextElevation;
        if (heightGap < frictionHeight)
        {
            break;
        }

        // TODO: additional contribution proportional to curvature
        float sedimentToMove = fmin(heightGap - frictionHeight, thisSediment) * SYSdrand48();

        terrainLayerChanges.emplace_back(thisPos, terrainLayer, -sedimentToMove);
        terrainLayerChanges.emplace_back(nextPos, terrainLayer, sedimentToMove);

        // TODO: destroy vegetation

        thisPos = nextPos;
    }

    applyTerrainLayerChanges(terrainLayerChanges);
}

void SOP_Terrable::simulateFireEvent(OP_Context& context, int x, int y)
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

    int simTimeYears = getIntParam(simTimeName, context);
    int seed = getIntParam(seedName, context);

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
