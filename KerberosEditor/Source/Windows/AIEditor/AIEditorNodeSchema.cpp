#include "AIEditorNodeSchema.hpp"

namespace Kerberos {

AI::AINodeDesc MakeDefaultNodeDesc(const AI::AINodeTypeInfo& info)
{
    AI::AINodeDesc desc;
    desc.Type = info.Type;
    for (const AI::AINodeParamDef& param : info.Params)
        desc.Params.emplace(param.Name, param.Default);
    return desc;
}

bool IsBlackboardKeyParam(const std::string_view name)
{
    return name == "Key" || name == "TargetKey" || name.ends_with("Key");
}

}
