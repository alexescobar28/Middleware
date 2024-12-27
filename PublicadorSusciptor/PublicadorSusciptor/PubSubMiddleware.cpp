#include "PubSubMiddleware.h"
#include <iostream>

int main() {
    PubSubMiddleware middleware;

    if (!middleware.initialize(1234)) {
        std::cerr << "Failed to initialize middleware." << std::endl;
        return 1;
    }

    std::cout << "Middleware started on port 1234" << std::endl;
    middleware.run();

    return 0;
}