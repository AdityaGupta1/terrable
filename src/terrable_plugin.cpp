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

OP_ERROR SOP_Terrable::cookMySop(OP_Context &context)
{
    OP_AutoLockInputs inputs(this);
    if (inputs.lock(context) >= UT_ERROR_ABORT)
    {
        return error();
    }

    fpreal now = context.getTime();

    int simTimeYears = getSimTime(now);

    duplicateSource(0, context); // duplicate input geometry

    fpreal frame = simTimeYears * 0.03;

    GA_RWHandleV3 Phandle(gdp->findAttribute(GA_ATTRIB_POINT, "P"));
    GA_Offset ptoff;
    GA_FOR_ALL_PTOFF(gdp, ptoff)
    {
        UT_Vector3 Pvalue = Phandle.get(ptoff);
        Pvalue.y() = sin(Pvalue.x() * .2 + Pvalue.z() * .3 + frame);
        Phandle.set(ptoff, Pvalue);
    }

    Phandle.bumpDataId();

    return error();
}
