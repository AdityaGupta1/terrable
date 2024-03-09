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
#include "terrable_plugin.h"
using namespace HDK_Sample;

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

OP_Node* SOP_Terrable::myConstructor(OP_Network *net, const char *name, OP_Operator *op)
{
    return new SOP_Terrable(net, name, op);
}

SOP_Terrable::SOP_Terrable(OP_Network *net, const char *name, OP_Operator *op)
    : SOP_Node(net, name, op)
{}

SOP_Terrable::~SOP_Terrable() {}

unsigned SOP_Terrable::disableParms()
{
    return 0;
}

void SOP_Terrable::increaseHeightfieldHeight(OP_Context& context)
{
    if (!gdp || !gdp->hasVolumePrimitives())
    {
        return;
    }

    GEO_Primitive* prim = gdp->findPrimitiveByName("height");

    if (prim->getTypeId() != GEO_PRIMVOLUME)
    {
        return;
    }

    fpreal now = context.getTime();

    int simTimeYears = getSimTime(now);

    GEO_PrimVolume* volume = static_cast<GEO_PrimVolume*>(prim);

    UT_VoxelArrayIteratorF vit;
    vit.setArray(volume->getVoxelWriteHandle().get());
    for (vit.rewind(); !vit.atEnd(); vit.advance())
    {
        float currentValue = vit.getValue();

        float newValue = currentValue + simTimeYears;

        vit.setValue(newValue);
    }
}

OP_ERROR SOP_Terrable::cookMySop(OP_Context &context)
{
    OP_AutoLockInputs inputs(this);
    if (inputs.lock(context) >= UT_ERROR_ABORT)
    {
        return error();
    }

    duplicateSource(0, context); // duplicate input geometry

    increaseHeightfieldHeight(context);

    return error();
}
