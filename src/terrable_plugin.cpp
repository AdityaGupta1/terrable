#include <UT/UT_DSOVersion.h>
//#include <RE/RE_EGLServer.h>

#include <UT/UT_Math.h>
#include <UT/UT_Interrupt.h>
#include <GU/GU_Detail.h>
#include <GU/GU_PrimPoly.h>
#include <CH/CH_LocalVariable.h>
#include <PRM/PRM_Include.h>
#include <PRM/PRM_SpareData.h>
#include <OP/OP_Operator.h>
#include <OP/OP_OperatorTable.h>

#include <limits.h>
#include "terrable_plugin.h"
using namespace HDK_Sample;

//
// newSopOperator is the hook that Houdini grabs from this dll
// and invokes to register the SOP. In this case we add ourselves
// to the specified operator table.
//
void newSopOperator(OP_OperatorTable* table)
{
    table->addOperator(
        new OP_Operator(
            "TerrableMain", // Internal name
            "Terrable", // UI name
            SOP_Terrable::myConstructor, // How to build the SOP
            SOP_Terrable::myTemplateList, // My parameters
            0, // Min # of sources
            0, // Max # of sources
            SOP_Terrable::myVariables, // Local variables
            OP_FLAG_GENERATOR // Flag it as generator
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

// Here's how we define local variables for the SOP.
enum {
    VAR_PT, // Point number of the star
};

CH_LocalVariable
SOP_Terrable::myVariables[] = {
    { "PT", VAR_PT, 0 }, // The table provides a mapping
    { 0, 0, 0 }
};

bool SOP_Terrable::evalVariableValue(fpreal &val, int index, int thread)
{
    // myCurrPoint will be negative when we're not cooking so only try to
    // handle the local variables when we have a valid myCurrPoint index.
    if (myCurrPoint >= 0)
    {
        // Note that "gdp" may be null here, so we do the safe thing
        // and cache values we are interested in.
        switch (index)
        {
        case VAR_PT:
            val = (fpreal) myCurrPoint;
            return true;
        default:
            /* do nothing */;
        }
    }
    // Not one of our variables, must delegate to the base class.
    return SOP_Node::evalVariableValue(val, index, thread);
}

OP_Node* SOP_Terrable::myConstructor(OP_Network *net, const char *name, OP_Operator *op)
{
    return new SOP_Terrable(net, name, op);
}

SOP_Terrable::SOP_Terrable(OP_Network *net, const char *name, OP_Operator *op)
    : SOP_Node(net, name, op)
{
    myCurrPoint = -1; // To prevent garbage values from being returned
}

SOP_Terrable::~SOP_Terrable() {}

unsigned SOP_Terrable::disableParms()
{
    return 0;
}

OP_ERROR SOP_Terrable::cookMySop(OP_Context &context)
{
    fpreal now = context.getTime();

    int simTimeYears = SIM_TIME(now);

    UT_Interrupt* boss;

    // Since we don't have inputs, we don't need to lock them.
    myCurrPoint = 0; // Initialize the PT local variable

    // Check to see that there hasn't been a critical error in cooking the SOP.
    if (error() < UT_ERROR_ABORT)
    {
        boss = UTgetInterrupt();
        gdp->clearAndDestroy();

        if (boss->opStart("Building LSYSTEM"))
        {
            // TODO: terrain stuff goes here

            vec3 start = { 0, 0, 0 };
            vec3 end = { 0, (double)simTimeYears, 0 };

            GU_PrimPoly* poly = GU_PrimPoly::build(gdp, 2);

            GA_Offset startPtoff = poly->getPointOffset(0);
            GA_Offset endPtoff = poly->getPointOffset(1);

            gdp->setPos3(startPtoff, UT_Vector3(start[0], start[1], start[2]));
            gdp->setPos3(endPtoff, UT_Vector3(end[0], end[1], end[2]));

            ////////////////////////////////////////////////////////////////////////////////////////////

            // Highlight the star which we have just generated. This routine
            // call clears any currently highlighted geometry, and then it
            // highlights every primitive for this SOP. 
            select(GU_SPrimitive);
        }

        boss->opEnd();
    }

    myCurrPoint = -1;
    return error();
}
