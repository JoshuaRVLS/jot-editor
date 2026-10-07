#ifndef JOT_MODEL_POPUP_H
#define JOT_MODEL_POPUP_H

#include <string>
#include <vector>
enum PopupPresentation
{
  POPUP_MODAL,
  POPUP_HOVER,
};

struct Popup
{
  bool visible = false;
  PopupPresentation presentation = POPUP_MODAL;
  std::string title;
  std::vector<std::string> lines;
  int x = 0, y = 0;
  int w = 0, h = 0;
  int scroll = 0;
  int content_lines = 0;
};

#endif
