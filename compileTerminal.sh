#!/bin/bash
gcc -Wall -Wextra -std=c11 terminal.c -o terminal \
    $(pkg-config --cflags --libs gtk+-3.0 vte-2.91)
