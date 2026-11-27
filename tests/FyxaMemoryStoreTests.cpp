#include "editor/FyxaMemoryStore.h"
#include <chrono>
#include <filesystem>
#include <iostream>
namespace fs=std::filesystem;using json=nlohmann::json;
int failures=0;void check(bool ok,const char*msg){std::cout<<(ok?"ok    ":"FAIL  ")<<msg<<"\n";if(!ok)++failures;}
int main(){const auto root=fs::temp_directory_path()/("fyxa-memory-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));const auto file=root/"mem.json";std::string error;FyxaMemoryStore store(file);
auto a=store.save("City project","Style","Keep roads grid-aligned; use stone and glass buildings.",error);check(error.empty()&&a.is_object(),"saves explicit project memory");auto b=store.save("Space project","Controls","Use WASD movement.",error);check(error.empty()&&b.is_object(),"saves a different project's note");auto list=store.list("City project",error);check(error.empty()&&list.size()==1,"lists only matching project memories");auto search=store.search("City project","roads glass",error);check(error.empty()&&search.size()==1,"search finds relevant note");check(store.search("City project","WASD",error).empty(),"does not leak notes across project scopes");check(store.remove("City project",a.value("id",""),error)&&error.empty(),"deletes a memory by exact ID and project");check(store.list("City project",error).empty()&&store.list("Space project",error).size()==1,"delete preserves other project memory");
fs::remove_all(root);std::cout<<(failures?"Fyxa memory tests FAILED\n":"Fyxa memory tests passed\n");return failures?1:0;}
