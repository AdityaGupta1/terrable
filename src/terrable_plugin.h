#ifndef __LSYSTEM_PLUGIN_h__
#define __LSYSTEM_PLUGIN_h__

#include <SOP/SOP_Node.h>
#include "LSystem.h"

namespace HDK_Sample 
{

class SOP_Terrable : public SOP_Node
{
public:
    static OP_Node* myConstructor(OP_Network*, const char*, OP_Operator*);

    static PRM_Template myTemplateList[];

protected:
    SOP_Terrable(OP_Network *net, const char *name, OP_Operator *op);
    virtual ~SOP_Terrable();

    unsigned disableParms() override;

    OP_ERROR cookMySop(OP_Context &context) override;

private:
    int getSimTime(fpreal t) { return evalInt("sim_time", 0, t); }
};

} // End HDK_Sample namespace

#endif
