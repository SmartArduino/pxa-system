"""Check common UI mistakes fail at the API boundary, plus the valid alternatives."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SDK = Path(__file__).resolve().parents[1]
PREFIX = """#include <pxa/ui.hpp>
#include <pxa/ui_action.hpp>
#include <pxa/ui_component.hpp>
#include <pxa/ui_layout.hpp>
#include <pxa/ui_widgets.hpp>
#include <pxa/list.hpp>
#include <pxa/ui_refresh.hpp>
#include <pxa/ui_environment.hpp>
using namespace pxa; using namespace pxa::ui; using namespace pxa::ui::literals;
"""


class UiDiagnostics(unittest.TestCase):
    def compile(self, body):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "probe.cpp"
            path.write_text(PREFIX + body)
            return subprocess.run(
                [os.environ.get("CXX", "clang++"), "-std=c++2c", "-fsyntax-only",
                 "-fno-exceptions", "-fno-rtti", "-Wno-attributes",
                 "-I", str(SDK / "include"), str(path)],
                capture_output=True, text=True, check=False)

    def test_unfinished_button(self):
        result = self.compile('void test(){auto view=Column(Button("Missing action"));}')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ViewLike", result.stderr)

    def test_model_needs_component(self):
        result = self.compile('struct Model{auto view(){return Text("Hi");}}; '
                              'void test(){auto view=Column(Model{});}')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ViewLike", result.stderr)

    def test_result_cannot_be_discarded(self):
        for button in ('Button("Save")', 'Button("Save",WidgetStyle{{0,0,80,30}})'):
            with self.subTest(button=button):
                result = self.compile('void test(){auto view=' + button +
                    '.on_click([]()->Result<void>{return std::unexpected(Error::io_error);});}')
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("ClickCallback", result.stderr)

    def test_valid_composition(self):
        result = self.compile('''
struct Model { State<int> count{0}; auto view(){return Text(count);} };
void test(TaskScope& scope,State<int>& count,State<DisplayMetrics>& display){
    auto view=SafeArea(display,Column(
        Text(Computed([](int n){return n*2;},count)).font(Font::headline),
        Button("Save").on_click(Action(scope,[]()->Result<void>{return {};}))
            .height(36_dp).background(0x123456ff).border_color(Color::border),
        Component<Model>()).padding(Padding{1_dp,2_dp,3_dp,4_dp}));
    Environment<DisplayMetrics,UiAppearance> environment;
    auto& metrics=environment.display();
    auto& theme=environment.appearance();
}
''')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_factory_boundaries(self):
        for body, constraint in (
            ('void test(State<bool>& state){auto v=When(state,[]{return 1;},[]{return Text("Hi");});}', 'ViewFactory'),
            ('void test(State<std::uint32_t>& state){auto v=Refresh(state,[]{return 1;});}', 'ViewFactory'),
            ('void test(ListState& state){auto v=KeyedList<4>(state,[](unsigned i){return i;},[](unsigned){return 1;});}', 'ListFactories'),
        ):
            result = self.compile(body)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(constraint, result.stderr)

    def test_noncopyable_lvalue_modifier_is_constrained(self):
        result = self.compile('void test(State<std::uint32_t>& state){auto v=Refresh(state,[]{return Text("Hi");}); auto x=v.width(100_dp);}')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('constructible_from', result.stderr)

    def test_dp_literal_cannot_wrap(self):
        result = self.compile('constexpr auto invalid=4294967296_dp;')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('constant expression', result.stderr)

    def test_environment_selection_is_constrained(self):
        for body in ('Environment<DisplayMetrics,DisplayMetrics> duplicate;',
                     'void test(){Environment<> value; value.appearance();}'):
            result = self.compile(body)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('constraints not satisfied', result.stderr)


if __name__ == "__main__":
    unittest.main()
