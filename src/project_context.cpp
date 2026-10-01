#include "project_context.hpp"
#ifdef BDF_CONTEXT_TAPE
#include "bdf_tape.hpp"
#endif
#include <cctype>
#include <functional>
#include <stdexcept>

namespace bdf::experimental {
namespace fs=std::filesystem;
namespace {
std::string folded(std::string value) {
    for(auto& c:value) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
[[noreturn]] void unsupported(const std::string& message) {
    throw std::runtime_error("unsupported HDL conversion: "+message);
}
fs::path absolute(const std::string& source) {return fs::absolute(source).lexically_normal();}
Schematic decode_source(std::string content) {
#ifdef BDF_CONTEXT_TAPE
    return decode_tape(parse_span_tape(std::move(content)));
#else
    return decode(parse(content));
#endif
}
}
std::map<std::string,fs::path> index_component_names(const std::vector<fs::path>& paths) {
    std::map<std::string,fs::path> result;
    for(const auto& path:paths) {
        const auto key=folded(path.stem().string());
        if(!result.emplace(key,path).second)
            unsupported("ambiguous BDF component name in "+path.parent_path().string()+": "+key);
    }
    return result;
}
ProjectContext::ProjectContext(const std::string& root,const std::vector<std::string>& libraries,
                              RefreshPolicy refresh):policy(refresh) {
    directories.push_back(absolute(root));
    for(const auto& dir:libraries) directories.push_back(absolute(dir));
    // Validate explicit search directories even when the design is flat.
    for(const auto& dir:directories)
        if(!fs::is_directory(dir)) unsupported("BDF library directory does not exist: "+dir.string());
}
std::map<std::string,fs::path> ProjectContext::scan() {
    std::map<std::string,fs::path> result;
    for(const auto& directory:directories) {
        if(!fs::is_directory(directory)) unsupported("BDF library directory does not exist: "+directory.string());
        ++counters.directory_scans;
        std::vector<fs::path> local_paths;
        for(const auto& entry:fs::directory_iterator(directory)) {
            if(folded(entry.path().extension().string())!=".bdf" || !entry.is_regular_file()) continue;
            local_paths.push_back(entry.path());
        }
        const auto local=index_component_names(local_paths);
        result.insert(local.begin(),local.end());
    }
    return result;
}
ProjectContext::File& ProjectContext::load(const fs::path& path) {
    const auto key=path.string();auto found=files.find(key);
    if(found!=files.end()) return found->second;
    ++counters.file_reads;auto content=read_file(key);
    auto schematic=decode_source(content);++counters.files_parsed;
    schematic.source_directory=path.parent_path().string();
    return files.emplace(key,File{std::move(content),std::move(schematic),{}}).first->second;
}
void ProjectContext::dependencies(const std::string& owner,const std::set<std::string>& children) {
    auto& previous=files.at(owner).dependencies;
    for(const auto& child:previous) parents[child].erase(owner);
    previous=children;
    for(const auto& child:children) parents[child].insert(owner);
}
void ProjectContext::invalidate(const std::string& path) {
    std::set<std::string> dirty={path};std::vector<std::string> pending={path};
    for(std::size_t i=0;i<pending.size();++i) {
        const auto found=parents.find(pending[i]);
        if(found!=parents.end()) for(const auto& parent:found->second)
            if(dirty.insert(parent).second) pending.push_back(parent);
    }
    for(auto it=outputs.begin();it!=outputs.end();) {
        if(dirty.count(it->first.first)) {++counters.outputs_invalidated;it=outputs.erase(it);}
        else ++it;
    }
}
void ProjectContext::refresh() {
    try {
        auto fresh=scan();
        if(!indexed || fresh!=definitions) {
            counters.outputs_invalidated+=outputs.size();outputs.clear();
            definitions=std::move(fresh);indexed=true;
        }
        std::vector<std::string> changed;
        for(const auto& [path,file]:files) {
            if(!fs::is_regular_file(path)) {changed.push_back(path);continue;}
            ++counters.file_reads;
            if(read_file(path)!=file.content) changed.push_back(path);
        }
        for(const auto& path:changed) {
            invalidate(path);
            for(const auto& child:files.at(path).dependencies) parents[child].erase(path);
            files.erase(path);
        }
        // Cached HDL must not hide deleted memory files or changed path types.
        for(const auto& [path,file]:files) {
            (void)path;
            for(const auto& symbol:file.schematic.symbols)
                if(is_lpm(symbol.type)) (void)prepare_lpm(symbol,file.schematic.source_directory);
        }
        blocked=false;
    } catch(...) {
        counters.outputs_invalidated+=outputs.size();outputs.clear();blocked=true;
        throw;
    }
}
std::string ProjectContext::convert(const std::string& source,const std::string& module) {
    if(policy==RefreshPolicy::before_conversion) refresh();
    if(blocked) unsupported("project refresh failed; refresh successfully before converting");
    const auto path=absolute(source);
    if(path.parent_path()!=directories[0]) unsupported("source is outside this project context");
    const auto cache_key=std::make_pair(path.string(),module);
    auto saved=outputs.find(cache_key);
    if(saved!=outputs.end()) {++counters.output_hits;return saved->second;}
    auto& top=load(path);
    ComponentLibrary components;
    std::set<std::string> visiting={folded(module)};
    std::vector<std::string> order;
    std::function<void(const fs::path&,std::size_t)> resolve;
    resolve=[&](const fs::path& current,std::size_t depth) {
        if(depth>64 || components.size()>512) unsupported("BDF hierarchy limit exceeded");
        const auto& schematic=load(current).schematic;
        std::set<std::string> referenced;
        for(const auto& symbol:schematic.symbols) {
            if(symbol.parameterized && !is_lpm(symbol.type)) unsupported("parameters on "+symbol.instance);
            if(is_supported_primitive(symbol.type)) continue;
            const auto key=folded(symbol.type);
            if(visiting.count(key)) unsupported("recursive BDF component "+symbol.type);
            if(!indexed) {definitions=scan();indexed=true;}
            const auto found=definitions.find(key);
            if(found==definitions.end()) unsupported("missing BDF component "+symbol.type);
            referenced.insert(found->second.string());
            if(components.count(symbol.type)) continue;
            visiting.insert(key);
            auto& child=load(found->second);
            resolve(found->second,depth+1);visiting.erase(key);
            components.emplace(symbol.type,child.schematic);order.push_back(symbol.type);
        }
        dependencies(current.string(),referenced);
    };
    resolve(path,0);
    if(components.count(module)) unsupported("top module name conflicts with component "+module);
    std::string result=emit_verilog(top.schematic,module,components);
    for(const auto& name:order) result+='\n'+emit_verilog(components.at(name),name,components);
    outputs.emplace(cache_key,result);
    return result;
}
std::string emit_lazy_project(const std::string& source,const std::string& module,
                             const std::vector<std::string>& libraries) {
    const auto path=absolute(source);
    ProjectContext context(path.parent_path().string(),libraries);
    return context.convert(path.string(),module);
}
} // namespace bdf::experimental
