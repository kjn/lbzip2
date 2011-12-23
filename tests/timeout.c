/*
  timeout.c - execute a command with one minute timeout.
  Copyright (C) 2011 Mikolaj Izdebski

  This program basically does the same thing as the following one-liner:

     perl -e'alarm 60; exec @ARGV or die $!'

  It's written in C because we don't want to depend on perl.
*/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <unistd.h>  /* execv() */
#include <stdio.h>   /* perror() */

int
main(int argc, char **argv)
{
  alarm(60);

  argv++;
  execv(*argv, argv);

  perror("execv");
  return 1;
}
