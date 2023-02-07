#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "caesar.h"

int main(int argc, char** argv)
{

    if (argc == 3) {
        FILE* f = fopen(argv[1], "r");
        caesar(f, atoi(argv[2]));
        fclose(f);
    } else {
        caesar(stdin, atoi(argv[1]));
    }

    return 0;
}
