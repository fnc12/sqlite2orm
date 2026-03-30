#include <sqlite2orm/ast.h>

namespace sqlite2orm {

    bool FunctionCallNode::operator==(const AstNode& other) const {
        auto* o = dynamic_cast<const FunctionCallNode*>(&other);
        if(!o || this->name != o->name || this->distinct != o->distinct || this->star != o->star) {
            return false;
        }
        if(this->arguments.size() != o->arguments.size()) return false;
        for(size_t i = 0; i < this->arguments.size(); ++i) {
            if(!astNodesEqual(this->arguments.at(i), o->arguments.at(i))) return false;
        }
        if(static_cast<bool>(this->filterWhere) != static_cast<bool>(o->filterWhere)) return false;
        if(this->filterWhere && o->filterWhere && *this->filterWhere != *o->filterWhere) return false;
        if(static_cast<bool>(this->over) != static_cast<bool>(o->over)) return false;
        if(this->over && o->over && *this->over != *o->over) return false;
        return true;
    }

}  // namespace sqlite2orm
