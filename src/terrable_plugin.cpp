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

// TODO: re-add cell size into the calculation
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

    float dx = (pos1.x() - pos2.x()) * xCellSize;
    float dy = (pos1.y() - pos2.y()) * yCellSize;
    float d = sqrtf(dx * dx + dy * dy);

    return (h2 - h1) / d;
}

UT_Vector3F SOP_Terrable::calculateNormal(int x, int y) const
{
    UT_Vector3F output = { 1, 1, 1 };

    UT_Vector3F L = { -1, calculateElevation(std::max(x - 1, 0), y), 0 };
    UT_Vector3F R = { 1, calculateElevation(std::min(x + 1, width - 1), y), 0 };
    UT_Vector3F D = { 0, calculateElevation(x, std::max(y - 1, 0)), -1 };
    UT_Vector3F U = { 0, calculateElevation(x, std::min(y + 1, height - 1)), 1 };

    output = cross(R - L, U - D);

    output.normalize();

    return output;
}

bool SOP_Terrable::rayCast(int x, int y, UT_Vector3F rayDirection) const
{
    int ceilY = calculateElevation(x, y);
    //std::cout << "starting val of point: " << ceilY << "\n";
    UT_Vector3i currCell = { x, ceilY, y };
    //std::cout << "starting pos: " << currCell << "\n";
    UT_Vector3F rayOrigin = currCell;
    UT_Vector3F rayDir = rayDirection;
    rayDir.normalize();
    //std::cout << "rayDir: " << rayDir << std::endl;

    //height test

    //= ray length so far, times ray height, compared to calculateElevation!
    //if calculateElevation is greater, then we have reached an intersection!
    //otherwise, keep going!

    //Based on minecraft gridMarch
    int itr = 0;
    float curr_t = 0.f;
    while (itr < std::sqrt(width * width + height * height)) {
        itr++;
        //update currCell!
        //UT_Vector2i signVec = UT_Vector2i(1, 1);
        float min_t = std::sqrt(3.f);
        int interfaceAxis = -1; // Track axis for which t is smallest
        for (int i = 0; i < 3; ++i) { // Iterate over the three axes
            if (rayDir[i] != 0) { // Is ray parallel to axis i?
                int sign_i = 1;
                if (std::signbit(rayDir[i])) {
                    sign_i = -1;
                }
                //std::cout << "sign of rayDir[" << i << "] is: " << sign_i << "\n";
                float offset = std::max(0.f, (float)sign_i);
                //std::cout << "offset =" << offset << "\n";

                // If the player is *exactly* on an interface then
                // they'll never move if they're looking in a negative direction
                if (currCell[i] == rayOrigin[i] && offset == 0.f) {
                    offset = -1.f;
                }

                int nextIntercept = currCell[i] + offset;
                //std::cout << nextIntercept << std::endl;
                float axis_t = (nextIntercept - rayOrigin[i]) / rayDir[i];

                if (axis_t <= min_t) {
                    //std::cout << axis_t << std::endl;
                    min_t = axis_t;
                    interfaceAxis = i;
                }
            }
        }

        if (interfaceAxis == -1) {
            throw std::out_of_range("interfaceAxis was -1 after the for loop in gridMarch!");
        }

        curr_t += min_t; // min_t is declared in slide 7 algorithm

        rayOrigin += rayDir * min_t;

        UT_Vector3i offset = { 0, 0, 0 };

        int sign_ia = 1;
        if (std::signbit(rayDir[interfaceAxis])) {
            sign_ia = -1;
        }
        offset[interfaceAxis] = std::min(0.f, (float)sign_ia);

        currCell = UT_Vector3i(floor(rayOrigin[0] + offset[0]), floor(rayOrigin[1] + offset[1]), floor(rayOrigin[2] + offset[2]));
        //std::cout << "next currCell: " << currCell << "\n";
        //basecase 1
        //outside of grid
        if (currCell[0] < 0 || currCell[0] >= width || currCell[2] < 0 || currCell[2] >= height) {
            //std::cout << "false case- exited grid!\n";
            return false;
        }
        //basecase 2
        //if the actual elevation is greater than the raycast found height! (at some x, z)
        if (currCell[1] < calculateElevation(currCell[0], currCell[2])) {
            //std::cout << "actual elevation =" << calculateElevation(currCell[0], currCell[2]) << ", raymarch height = " << currCell[1] << "\n";
            //std::cout << "true case- occlusion occured!\n";
            return true;
        }
    }
    return false;
}


float SOP_Terrable::calculateIllumination(int x, int y) const
{
    //1. Initialize
    //Hard-coded Sun Direction for now.
    UT_Vector3F sunPos = { 1, 1, 1 };
    //UT_Vector3F mainLightDirection = -sunPos;
    //mainLightDirection.normalize();
    UT_Vector3F rayToSun = sunPos;
    rayToSun.normalize();

    
    float currentElevation = calculateElevation(x, y);

    //2. Normal Test
    UT_Vector3F normal = calculateNormal(x, y);

    //std::cout << "normal: " << normal << "\n";

    // if normal and sun are pointed away from each other ... ignore

    if (dot(-normal, rayToSun) < 0) {
        //std::cout << "dot with sun = " << dot(normal, rayToSun) << "\n";
        //std::cout << "normal is: " << -normal << ", and rayToSun is: " << rayToSun << "\n";
        return 0;
    }
    //else {
    //    return 1;
    //}
    //

    UT_Vector2i sourcePos = { x, y };

    UT_Vector2i thisPos = sourcePos;
    UT_Vector2i nextPos(0, 0);
    //std::vector<std::pair<UT_Vector2i, float>> nextPosCandidates;

    //3. RayCast Occlusion Check
    if (rayCast(x, y, rayToSun)) {
        //Occlusion occurs!
        return 0;
    }

    //4. Calculate Illumination Value

    //Lambert Term * Current Elevation

    //float sunIntensity = 10.0f;
    //float lambertTerm = std::abs(dot(normal, rayToSun));

    //float illumination = sunIntensity * lambertTerm * currentElevation;
    //std::cout << "We got to this stage\n";
    //return illumination;
    return std::abs(dot(normal, rayToSun));
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
    else // hasHeight (base case)
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
        simulateTemperatureEvent(x, y);

        break;
    case Event::LIGHTNING:
        simulateLightningEvent(x, y);
        break;
    case Event::GRAVITY:
        // TODO
        simulateGravityEvent(x, y);

        break;
    case Event::FIRE:
        // TODO
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

struct TerrainLayerChange
{
    UT_Vector2i pos;
    TerrainLayer layer;
    float change;

    TerrainLayerChange(UT_Vector2i pos, TerrainLayer layer, float change)
        : pos(pos), layer(layer), change(change)
    {}
};

void SOP_Terrable::simulateRunoffEvent(int x, int y)
{
    std::vector<TerrainLayerChange> terrainLayerChanges;

    // TODO: set initial water based on rainfall
    // TODO: reduce initial water amount proportionally to plant density (water intercepted by plants and released to the atmosphere through evaporation)
    float currentWater = 1.6f;
    float carriedRock = 0.f;
    float carriedSand = 0.f;
    float carriedHumus = 0.f;

    UT_Vector2i sourcePos = { x, y };

    UT_Vector2i thisPos = sourcePos;
    UT_Vector2i nextPos(0, 0);
    std::vector<std::pair<UT_Vector2i, float>> nextPosCandidates;
    while (true)
    {
        nextPosCandidates.clear();
        float totalSlope = 0.f;
        //Check each nei
        for (const auto& cardinalDirection : cardinalDirections)
        {
            UT_Vector2i nextPosCandidate = thisPos + cardinalDirection;
            if (nextPosCandidate.x() < 0 || nextPosCandidate.x() >= width || nextPosCandidate.y() < 0 || nextPosCandidate.y() >= height)
            {
                continue;
            }

            float slope = calculateSlope(thisPos, nextPosCandidate);
            if (slope >= 0.f)
            {
                continue;
            }

            slope = -slope; // make it positive
            nextPosCandidates.emplace_back(nextPosCandidate, slope);
            totalSlope += slope;
        }

        if (nextPosCandidates.empty() || currentWater <= 0.f) // reached terrain local minimum or ran out of water
        {
            // TODO: what happens to excess water?
            terrainLayerChanges.emplace_back(thisPos, TerrainLayer::ROCK, carriedRock);
            terrainLayerChanges.emplace_back(thisPos, TerrainLayer::SAND, carriedSand);
            terrainLayerChanges.emplace_back(thisPos, TerrainLayer::HUMUS, carriedHumus);
            break;
        }

        float rand = SYSdrand48() * totalSlope;
        float thisSlope = 0.f;
        bool didSetNextPos = false;
        for (const auto& [nextPosCandidate, slope] : nextPosCandidates)
        {
            if (rand < slope)
            {
                nextPos = nextPosCandidate;
                thisSlope = slope;
                didSetNextPos = true;
                break;
            }

            rand -= slope;
        }

        // corrects for floating point precision errors in the above random sampling logic
        if (!didSetNextPos)
        {
            auto& [nextPosCandidate, slope] = nextPosCandidates.back();
            nextPos = nextPosCandidate;
            thisSlope = slope;
        }

        float thisRock = terrainLayers[posToIndex(thisPos, TerrainLayer::ROCK)];
        float thisSand = terrainLayers[posToIndex(thisPos, TerrainLayer::SAND)];
        float thisHumus = terrainLayers[posToIndex(thisPos, TerrainLayer::HUMUS)];

        float thisMoistureCapacity =
            thisRock * rockMoistureCapacity +
            thisSand * sandMoistureCapacity +
            thisHumus * humusMoistureCapacity;
        float thisMoisture = terrainLayers[posToIndex(thisPos, TerrainLayer::MOISTURE)];

        float soilAbsorption = fmin(soilMoistureAbsorptionRate / thisSlope, thisMoistureCapacity - thisMoisture);
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

    for (const auto& change : terrainLayerChanges)
    {
        terrainLayers[posToIndex(change.pos, change.layer)] += change.change;
    }

    // "Once the runoff sequence terminates we approximate the effects of plant transpiration and seepage into groundwater by reducing the moisture at the source p0 by a constant amount."
    float& sourceMoisture = terrainLayers[posToIndex(sourcePos, TerrainLayer::MOISTURE)];
    sourceMoisture = fmax(sourceMoisture - sourceMoistureReduction, 0.f);
}

void SOP_Terrable::simulateTemperatureEvent(int x, int y)
{
    // TODO
    // Given a random position p(x, y), we first find delta_t, which is the estimated difference in night and day temp.
    // Using delta_t, we dampen it in proportion to the local density of vegetation V(p) and thickness of sand and humus G(p).
    // Finally, this is the probability used to determine whether the current tile fractures and becomes rocks or not.

    //1. Find illumination
    float illumination_p = calculateIllumination(x, y);
    if (illumination_p != 0) {
        //std::cout << "0 illumination case!\n";

        //here, we should color or something

        //terrainLayers[posToIndex({x, y}, TerrainLayer::HUMUS)] = 100.0f;
        //std::cout << "Occluded!!!\n";
        //terrainLayers[posToIndex({ x, y }, TerrainLayer::HUMUS)] = 100.0f;
        //White = illuminated
        terrainLayers[posToIndex({ x, y }, TerrainLayer::VEGETATION)] = 1.0f;
    }

    //4. elevation calculation

    float elevation = std::max(0.f, calculateElevation(x, y));
    float elevationWeight = 2.0f;
    //don't need elevation above 0.f

    //5. delta T calculation

    float deltaTemperature = illumination_p * (1.0f + elevation / elevationWeight);

    //6. Rest of the formula!

    float k = 10.0f;
    float s_p = 1.0f;
    float k_g = 1.0f;
    float G_p = 1.0f;
    float k_v = 1.0f;
    float V_p = 1.0f;

    float probability = k * deltaTemperature * s_p / (1 + k_g * G_p + k_v * V_p);
    //std::cout << "p = " <<  probability << "\n";
    float rand = SYSdrand48();

    if (probability > rand) {
        //Trigger event!

        //Turn bedrock into rock
        //Amount converted = difference between local and critical slope
        //critical slope = some threshold

        float criticalSlope = 0.5f;

        float rockAmount = std::max(0.f, calculateSlope(x, y) - criticalSlope);

        //reduce bedrock amount
        //increase rock amount
        //complete!
        //std::cout << "probability: " << probability << "\n";
    }
}

void SOP_Terrable::simulateLightningEvent(int x, int y)
{
    // TODO
}

void SOP_Terrable::simulateGravityEvent(int x, int y)
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
