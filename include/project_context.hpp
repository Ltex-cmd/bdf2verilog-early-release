#pragma once
#include "bdf.hpp"
#include <filesystem>
#include <set>
#include <utility>

namespace bdf::experimental {
enum class RefreshPolicy { explicit_refresh, before_conversion };
struct ContextStats {
    std::size_t directory_scans=0,file_reads=0,files_parsed=0,output_hits=0,outputs_invalidated=0;
};
// Input paths are regular BDF entries from a single search directory.
std::map<std::string,std::filesystem::path> index_component_names(
        const std::vector<std::filesystem::path>& paths);
class ProjectContext {
    struct File {
        std::string content;
        Schematic schematic;
        std::set<std::string> dependencies;
    };
    std::vector<std::filesystem::path> directories;
    std::map<std::string,std::filesystem::path> definitions;
    std::map<std::string,File> files;
    std::map<std::string,std::set<std::string>> parents;
    std::map<std::pair<std::string,std::string>,std::string> outputs;
    RefreshPolicy policy;
    bool indexed=false,blocked=false;
    ContextStats counters;
    std::map<std::string,std::filesystem::path> scan();
    File& load(const std::filesystem::path& path);
    void dependencies(const std::string& owner,const std::set<std::string>& children);
    void invalidate(const std::string& path);
public:
    // Explicit-refresh mode is an immutable snapshot between refresh calls.
    // Live mode performs a complete refresh before every conversion.
    // The context is single-threaded and owns all cached source/design data.
    explicit ProjectContext(const std::string& source_directory,
            const std::vector<std::string>& libraries={},
            RefreshPolicy refresh=RefreshPolicy::explicit_refresh);
    void refresh();
    std::string convert(const std::string& source,const std::string& module);
    const ContextStats& stats() const {return counters;}
};
std::string emit_lazy_project(const std::string& path,const std::string& module,
                             const std::vector<std::string>& libraries={});
} // namespace bdf::experimental
