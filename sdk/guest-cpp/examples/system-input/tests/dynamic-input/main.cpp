#include <pxa/app.hpp>
#include <cstdio>
using namespace pxa::ui;
using namespace pxa::ui::literals;
struct DynamicInput {
 State<std::string> text{""}; State<std::string> status{"逐个样本删除最后一字，提交"};
 TextInputRef editor; pxa::Context* context=nullptr; unsigned sample=0;
 static std::string preset(unsigned which) {
  if (which==0) {std::string s; for(unsigned i=0;i<80;++i)s+="中文";s+="01234567890123456789";return s;}
  if (which==1) {std::string s="https://example.org/source.json?q=";s.append(2048-s.size(),'a');return s;}
  if (which==2) return std::string(4052,'x');
  return "a";
 }
 void reset(){text.set(preset(sample));status.set("删除一字后，按输入法提交");}
 auto view(){return Column(Text("动态文本验收"),
  TextInput(text,editor).max_bytes().single_line().on_submit([this]{
   (void)editor.hide_keyboard();auto expected=preset(sample);expected.pop_back();
   std::uint64_t hash=1469598103934665603ULL;
   for(unsigned char c:text.get()){hash^=c;hash*=1099511628211ULL;}
   char msg[96];std::snprintf(msg,sizeof(msg),"DYNAMIC sample=%u bytes=%zu hash=%016llx %s",sample,text.get().size(),(unsigned long long)hash,text.get()==expected?"PASS":"FAIL");
   status.set(msg);(void)context->log().write(pxa::LogLevel::info,msg);
  }), Row(Button("打开").on_click([this]{(void)editor.show_keyboard();}),
   Button("下一个").on_click([this]{sample=(sample+1)%4;reset();}),
   Button("重置").on_click([this]{reset();})).gap(4_dp),Text(status)).gap(4_dp).padding(12_dp);}
 pxa::Result<void> on_start(pxa::Context&ctx,std::span<const std::byte>){context=&ctx;reset();return{};}
 void on_background(pxa::Context&){(void)editor.hide_keyboard();}
};
PXA_APPLICATION(DynamicInput)
