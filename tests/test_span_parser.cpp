#include "bdf_tape.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <random>
#include <clocale>
using namespace bdf::experimental; using namespace std::string_literals;
bool tape_equal(const std::string& s) {
 TapeDocument a,b;std::string ae,be;
 try{a=parse_tape(s);}catch(std::exception const&e){ae=e.what();}
 try{b=parse_span_tape(s);}catch(std::exception const&e){be=e.what();}
 if(ae!=be)return false;if(!ae.empty())return true;
 if(a.source!=b.source || a.escaped!=b.escaped || a.nodes.size()!=b.nodes.size())return false;
 for(size_t i=0;i<a.nodes.size();++i){auto x=a.nodes[i],y=b.nodes[i];if(x.offset!=y.offset || x.length!=y.length || x.next!=y.next || x.child_count!=y.child_count || x.line!=y.line || x.column!=y.column || x.kind!=y.kind)return false;}
 return true;
}
std::string decode(const std::string& s,bool projection) {
 try {auto d=projection?parse_span_tape(s):parse_tape(s);auto r=bdf::experimental::decode_tape(d);std::string locations;
 auto loc=[&](const bdf::Location& l){locations+='|'+std::to_string(l.line)+':'+std::to_string(l.column);};
 for(auto const& p:r.pins){loc(p.location);locations+=p.unused?'1':'0';}
 for(auto const& y:r.symbols){loc(y.location);for(auto const&p:y.ports){loc(p.location);locations+=p.unused?'1':'0';}for(auto const&p:y.parameters)loc(p.location);}
 for(auto const& c:r.connectors)loc(c.location);
 return "ok\n"+bdf::inspect_json(r)+locations;}
 catch(std::exception const&e){return "error\n"+std::string(e.what());}
}
int main(int argc,char**argv){
 size_t files=0, mutations=0, mismatch=0, full=0,selected=0,full_capacity=0,selected_capacity=0;std::mt19937 rng(84259);
 const std::string pad="/*"+std::string(1030,' ')+"*/\n";
 std::vector<std::string> edge={"", "()", ")", "(a)", "(a/b)", "(a//tail", "/*oops", "(font \"unfinished)", "(drawing(a \"q\\\" r\\\\s\\n\"))", "(a \"x\ny\")", "(a)/*ok*/", "(a)/*ok*", std::string(257,'(')+std::string(257,')'), std::string(258,'(')+std::string(258,')'), std::string("(a\0b)",5), "(a\v\f\rb)"};
 for(auto const& value:edge) if(!tape_equal(pad+value)){++mismatch;std::cerr<<"edge mismatch\n";}
 size_t threshold_cases=0;
 for(size_t target:{1023,1024,1025}) for(const auto& value:edge) {
  if(value.size()+4>target) continue;
  auto padded=std::string("/*")+std::string(target-value.size()-4,' ')+"*/"+value;
  ++threshold_cases;if(!tape_equal(padded) || decode(padded,false)!=decode(padded,true)) ++mismatch;
 }
 std::cerr<<"threshold_cases "<<threshold_cases<<'\n';
 size_t byte_cases=0;
 for(const char* locale:{"C","en_US.UTF-8","ru_RU.UTF-8","tr_TR.UTF-8"}) {
  if(!std::setlocale(LC_CTYPE,locale)) {std::cerr<<"skipped unavailable locale "<<locale<<'\n';continue;}
  for(int byte=0;byte<256;++byte) for(int context=0;context<3;++context) {
   const auto c=std::string(1,static_cast<char>(byte));
   const auto test=pad+(context==0?"(a"+c+"b)":context==1?"(a \"x"+c+"y\")":"/*x"+c+"y*/(a)");
   ++byte_cases;if(!tape_equal(test)){++mismatch;std::cerr<<"byte mismatch "<<byte<<" locale "<<locale<<'\n';}
  }
 }
 std::setlocale(LC_CTYPE,"C");
 for(int n=0;n<17;++n) for(const std::string tail:{"\")", "\"x\ny\")", ""}) {
  ++byte_cases;if(!tape_equal(pad+"(a \""+std::string(n,'\\')+tail))++mismatch;
 }
 for(int a=1;a<argc;++a)for(auto const&e:std::filesystem::recursive_directory_iterator(argv[a])){
  if(e.path().extension()!=".bdf")continue;
  auto source=bdf::read_file(e.path().string());auto check=[&](const std::string&s){auto f=decode(s,false),p=decode(s,true);if(f!=p || !tape_equal(s)){if(mismatch++<5)std::cerr<<e.path()<<"\n"<<f<<"\n"<<p<<'\n';}};
  check(source);++files;
  auto d=parse_tape(source),p=parse_span_tape(source);full+=d.nodes.size();selected+=p.nodes.size();full_capacity+=d.nodes.capacity();selected_capacity+=p.nodes.capacity();
  // Target structure, quoting, NUL, comments and nesting at real corpus offsets.
  const std::string mutationset="()\"\\/\n*\0"s;
  for(int j=0;j<12;++j){auto s=source;auto at=rng()%(s.size()+1);s.insert(at,1,mutationset[rng()%mutationset.size()]);check(s);++mutations;}
 }
 std::cout<<"{\"byte_cases\":"<<byte_cases<<",\"edge_cases\":"<<edge.size()<<",\"files\":"<<files<<",\"mutations\":"<<mutations<<",\"mismatch\":"<<mismatch<<",\"full_nodes\":"<<full<<",\"selective_nodes\":"<<selected<<",\"full_capacity\":"<<full_capacity<<",\"selective_capacity\":"<<selected_capacity<<"}\n";
 return mismatch?1:0;
}
