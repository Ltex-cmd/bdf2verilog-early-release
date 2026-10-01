#include "bdf.hpp"
#include "project_context.hpp"
namespace bdf {
std::string emit_verilog_project(const std::string& source,const std::string& module,
                                const std::vector<std::string>& libraries) {
    return experimental::emit_lazy_project(source,module,libraries);
}
}
