#include <fstream>
#include <filesystem>
#include <string>
int wmain(int argc,wchar_t** argv) {
    if(argc!=3 || std::wstring(argv[1])!=L"--data-dir")return 2;
    std::ofstream("restarted")<<std::filesystem::path(argv[2]).u8string();
    return 0;
}
