#include "test_framework.h"

int main() {
  int failed_tests = 0;
  for (const auto& tc : tf::registry()) {
    const int before = tf::failures();
    std::printf("[ RUN  ] %s\n", tc.name);
    tc.fn();
    if (tf::failures() == before) {
      std::printf("[  OK  ] %s\n", tc.name);
    } else {
      std::printf("[ FAIL ] %s\n", tc.name);
      ++failed_tests;
    }
  }
  std::printf("\n%d checks, %d failures across %zu tests (%d failed)\n",
              tf::checks(), tf::failures(), tf::registry().size(), failed_tests);
  return tf::failures() == 0 ? 0 : 1;
}
