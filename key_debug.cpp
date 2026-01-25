#include <iostream>
#include <termios.h>
#include <unistd.h>

int main() {
  struct termios oldt, newt;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);

  std::cout << "Press keys to see their codes (q to quit):" << std::endl;
  char c;
  while (read(STDIN_FILENO, &c, 1) == 1) {
    if (c == 'q')
      break;
    std::cout << "Code: " << (int)c << std::endl;
  }

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  return 0;
}
