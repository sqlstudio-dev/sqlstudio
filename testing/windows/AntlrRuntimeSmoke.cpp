#include <antlr4-runtime.h>
#include <windows.h>
#include <crtdbg.h>
#include <cstdlib>
#include <iostream>

#ifdef _DEBUG
int __cdecl failOnCrtError(int kind, char *message, int *) {
  if (kind == _CRT_ERROR || kind == _CRT_ASSERT) {
    fputs(message, stderr);
    // Fail on the first invalid free instead of continuing with a corrupt heap.
    std::_Exit(5);
  }
  return FALSE;
}
#endif

int main() {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
  static_assert(_ITERATOR_DEBUG_LEVEL == 2, "Match the Debug parser ABI");
  _CrtSetReportHook(failOnCrtError);
  for (int kind : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
    _CrtSetReportMode(kind, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(kind, _CRTDBG_FILE_STDERR);
  }
#else
  static_assert(_ITERATOR_DEBUG_LEVEL == 0, "Match the Release parser ABI");
#endif

  // The numeric fallback allocates a string inside the runtime DLL. Even a
  // short string owns a debug-iterator proxy which the caller must free.
  for (size_t token = 0; token < 1000; ++token) {
    std::string name = antlr4::dfa::Vocabulary::EMPTY_VOCABULARY.getDisplayName(token);
    if (name != std::to_string(token)) return 1;
  }

  // Exercise allocations in both directions, including non-SSO strings.
  const std::string longName(256, 'x');
  antlr4::dfa::Vocabulary vocabulary({"", "'literal'"}, {"", "LITERAL", longName});
  for (int iteration = 0; iteration < 1000; ++iteration) {
    if (vocabulary.getDisplayName(1) != "'literal'" || vocabulary.getDisplayName(2) != longName)
      return 2;
    antlr4::misc::IntervalSet intervals;
    intervals.add(1, 3);
    if (intervals.toList().size() != 3) return 3;
  }
#ifdef _DEBUG
  if (!_CrtCheckMemory()) return 4;
#endif
  std::cout << "PASS: ANTLR strings, vectors and iterator proxies cross the DLL boundary safely.\n";
}
