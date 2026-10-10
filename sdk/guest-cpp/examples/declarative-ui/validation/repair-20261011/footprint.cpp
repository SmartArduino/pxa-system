#include <pxa/app.hpp>
#include <pxa/ui_component.hpp>
#include <pxa/ui_layout.hpp>
#include <pxa/list.hpp>
#include <pxa/ui_refresh.hpp>
#include <cstdio>
using namespace pxa; using namespace pxa::ui; using namespace pxa::ui::literals;
struct Model { int n{}; auto view() { return Text("A"); } };
struct Local { State<int> n{0}; auto view(){return Text(n);} };
int main(){
 State<DisplayMetrics> display{DisplayMetrics{}}; State<bool> flag{true}; State<unsigned> rev{0}; ListState items{4};
 auto area=SafeArea(display,Text("A")); auto fixed=SafeArea(DisplayMetrics{},Text("A"));
 auto component=Component(Model{}); auto local=Component<Local>();
 auto when=When(flag,[]{return Text("A");},[]{return Text("B");});
 auto list=KeyedList<4>(items,[](unsigned i){return i;},[](unsigned){return Text("A");});
 auto refresh=Refresh(rev,[]{return Text("A");});
 auto repeated=Button("A").on_click([]{}).enabled(flag).radius(2_dp).enabled(false);
 std::printf("{\"safe_bound\":%zu,\"safe_static\":%zu,\"component\":%zu,\"component_in_place\":%zu,\"when\":%zu,\"list\":%zu,\"refresh\":%zu,\"repeated_modifier_view\":%zu,\"repeated_modifier_page\":%zu,\"repeated_bindings\":%zu}\n", sizeof(area),sizeof(fixed),sizeof(component),sizeof(local),sizeof(when),sizeof(list),sizeof(refresh),sizeof(repeated),sizeof(Page<decltype(repeated)>),capacity_of<decltype(repeated)>.bindings);
}
