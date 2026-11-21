// Sample for the C/C++ compiler driver tests (#14): C++ with the standard library.
#include <iostream>
#include <string>

int main() {
    const std::string greeting = "hello from C++";
    std::cout << greeting << '\n';
    return 3;
}
