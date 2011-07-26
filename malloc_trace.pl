# malloc_trace.pl,v 1.2 2009/04/05 18:04:47 lacos Exp

# Representation of NULL (%p).
$nil = $ARGV[0];

# Allocation map.
%allocmap = ();

# Amount of memory never freed by a specific PID.
%never_freed = ();

# Maximum of memory allocation for a specific PID.
%peak = ();

# Exit code.
$exit = 0;

sub alloc
{
  my ($pid, $blksiz, $newptr) = @_;

  if (0 != ($newptr cmp $nil)) {
    my $new_not_freed = $never_freed{$pid};
    my $old_peak = $peak{$pid};

    $allocmap{$pid . ": " . $newptr} = $blksiz;

    $new_not_freed += $blksiz;
    $never_freed{$pid} = $new_not_freed;

    if (!defined($old_peak)) {
      $old_peak = 0;
    }
    if ($old_peak < $new_not_freed) {
      $peak{$pid} = $new_not_freed;
    }
  }
}


sub free
{
  my ($pid, $oldptr) = @_;

  if (0 != ($oldptr cmp $nil)) {
    my $pid_addr = $pid . ": " . $oldptr;
    my $blksiz = delete($allocmap{$pid_addr});

    if (defined($blksiz)) {
      my $new_not_freed = $never_freed{$pid} - $blksiz;

      if (0 == $new_not_freed) {
        delete($never_freed{$pid});
      }
      else {
        $never_freed{$pid} = $new_not_freed;
      }
    }
    else {
      warn($pid_addr . ": invalid free\n");
      $exit = 1;
    }
  }
}


sub realloc
{
  my ($pid, $oldptr, $blksiz, $newptr) = @_;

  if (0 == $blksiz) {
    free($pid, $oldptr);
  }
  elsif (0 != ($newptr cmp $nil)) {
    free($pid, $oldptr);
    alloc($pid, $blksiz, $newptr);
  }
}


# MAIN

if (!defined($nil) || 0 == ("" cmp $nil)) {
  die("You must specify the %p representation of NULL as the first argument.");
}

while (<STDIN>) {
  my $pid;
  my $blknum;
  my $blksiz;
  my $newptr;
  my $oldptr;

  if (($pid, $blknum, $blksiz, $newptr)
      = (/^(\d+): calloc\((\d+), (\d+)\) == (\S+)$/o)) {
    alloc($pid, $blknum * $blksiz, $newptr);
  }
  elsif (($pid, $blksiz, $newptr)
      = (/^(\d+): [mv]alloc\((\d+)\) == (\S+)$/o)) {
    alloc($pid, $blksiz, $newptr);
  }
  elsif (($pid, $oldptr)
      = (/^(\d+): free\((\S+)\)$/o)) {
    free($pid, $oldptr);
  }
  elsif (($pid, $oldptr, $blksiz, $newptr)
      = (/^(\d+): realloc\((\S+), (\d+)\) == (\S+)$/o)) {
    realloc($pid, $oldptr, $blksiz, $newptr);
  }
  else {
    chomp();
    warn("skipping unkown format \"" . $_ . "\"\n");
  }
}

while (my ($pid_addr, $blksiz) = each(%allocmap)) {
  warn($pid_addr . ": " . $blksiz . "\n");
  $exit = 1;
}

while (my ($pid, $never_freed) = each(%never_freed)) {
  warn($pid . ": leaked: " . $never_freed . "\n");
}

while (my ($pid, $peak1) = each(%peak)) {
  print($pid . ": peak: " . $peak1 . "\n");
}

exit($exit);
