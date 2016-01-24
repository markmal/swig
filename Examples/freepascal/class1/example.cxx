/* File : example.c */

#include "example.h"
#include <stdio.h>

Simple::Simple(void) {
  x = 0;
  y = 0;
}

Simple::~Simple(void) {
 x = 0;
}

void Simple::move(double dx, double dy) {
  x += dx;
  y += dy;
}

int main( )
{
   Simple simple;

   simple.x = 20;
   simple.y = 30;

   printf(" Simple (%6.2f, %6.2f)\n", simple.x, simple.y);

   simple.move(5.5, 2.2);
   printf(" Simple (%6.2f, %6.2f)\n", simple.x, simple.y);

   return 0;
}
