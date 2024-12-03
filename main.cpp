
#include "my_shell.h"

int main(int argc, char** argv) {
    my_shell shell{argc, argv};
    shell.run();
    return 0;
}