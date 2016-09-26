#include <stdio.h>

int main(int argc, char *argv[]) {
    int i;

    printf("Program arguments:\n");
    for (i = 0; i < argc; i++) {
        printf("args[%d]: %s\n", i, argv[i]);
    }

    return 0;
}
