#ifndef __LSYSTEM_PLUGIN_h__
#define __LSYSTEM_PLUGIN_h__

//#include <GEO/GEO_Point.h>
#include <SOP/SOP_Node.h>
#include "LSystem.h"

namespace HDK_Sample 
{

class SOP_Terrable : public SOP_Node
{
public:
    static OP_Node* myConstructor(OP_Network*, const char*, OP_Operator*);

    // Stores the description of the interface of the SOP in Houdini.
    // Each parm template refers to a parameter.
    static PRM_Template myTemplateList[];

    // This optional data stores the list of local variables.
    static CH_LocalVariable myVariables[];

protected:
    SOP_Terrable(OP_Network *net, const char *name, OP_Operator *op);
    virtual ~SOP_Terrable();

    // Disable parameters according to other parameters.
    virtual unsigned disableParms();

    // cookMySop does the actual work of the SOP computing
    virtual OP_ERROR cookMySop(OP_Context &context);

    virtual bool evalVariableValue(fpreal &val, int index, int thread);
    virtual bool evalVariableValue(UT_String &v, int i, int thread)
    {
        return evalVariableValue(v, i, thread);
    }

private:
    int SIM_TIME(fpreal t) { return evalInt("sim_time", 0, t); }

    // Member variables are stored in the actual SOP, not with the geometry
    // In this case these are just used to transfer data to the local 
    // variable callback.
    // Another use for local data is a cache to store expensive calculations.

    // NOTE: You can declare local variables here like this  
    int myCurrPoint;
};

} // End HDK_Sample namespace

#endif
