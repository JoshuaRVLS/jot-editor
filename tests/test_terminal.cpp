#include "test_framework.h"
#include "tools/terminal/integrated.h"

TEST(IntegratedTerminalDefaultColorsResolveToTheme) {
  IntegratedTerminal::StyledCell cell{"x", 7, 0, true, true, false};
  auto colors = IntegratedTerminal::resolve_cell_colors(cell, 189, 235);

  ASSERT_EQ(colors.fg, 189);
  ASSERT_EQ(colors.bg, 235);
}

TEST(IntegratedTerminalExplicitColorsStayExplicit) {
  IntegratedTerminal::StyledCell cell{"x", 196, 22, false, false, false};
  auto colors = IntegratedTerminal::resolve_cell_colors(cell, 189, 235);

  ASSERT_EQ(colors.fg, 196);
  ASSERT_EQ(colors.bg, 22);
}

TEST(IntegratedTerminalReverseSwapsAfterDefaultResolution) {
  IntegratedTerminal::StyledCell cell{"x", 7, 22, true, false, true};
  auto colors = IntegratedTerminal::resolve_cell_colors(cell, 189, 235);

  ASSERT_EQ(colors.fg, 22);
  ASSERT_EQ(colors.bg, 189);
}
