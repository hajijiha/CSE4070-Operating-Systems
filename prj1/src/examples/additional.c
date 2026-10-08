#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>

int main (int argc, char **argv)
{
  int n, a, b, c, d;
  int fib_result, max_result;

  if (argc != 5)
    {
      printf ("Usage: additional <num1> <num2> <num3> <num4>\n");
      return EXIT_FAILURE;
    }

  n = atoi (argv[1]);
  a = atoi (argv[1]);
  b = atoi (argv[2]);
  c = atoi (argv[3]);
  d = atoi (argv[4]);

  fib_result = fibonacci (n);
  max_result = max_of_four_int (a, b, c, d);

  printf ("%d %d\n", fib_result, max_result);

  return EXIT_SUCCESS;
}
