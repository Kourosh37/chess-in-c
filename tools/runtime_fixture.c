/* Real PE imports for testing system and application DLL dependency traversal. */
#if defined(CHESS_RUNTIME_OPTIONAL)
__declspec(dllexport) int chess_runtime_optional(void) { return 1; }
#elif defined(CHESS_RUNTIME_SYSTEM)
__declspec(dllimport) int chess_runtime_optional(void);
__declspec(dllexport) int chess_runtime_system(void) { return chess_runtime_optional(); }
#elif defined(CHESS_RUNTIME_APP)
__declspec(dllexport) int chess_runtime_app(void) { return 2; }
#else
__declspec(dllimport) int chess_runtime_system(void);
__declspec(dllimport) int chess_runtime_app(void);
int main(void) { return chess_runtime_system() + chess_runtime_app() == 3 ? 0 : 1; }
#endif
