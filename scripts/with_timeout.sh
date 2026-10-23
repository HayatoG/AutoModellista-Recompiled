#!/bin/sh
# Roda um comando com tempo limite (macOS não tem timeout(1)): with_timeout.sh <segundos> <comando...>
# Sai com 124 se o tempo estourou, como o timeout do coreutils.
secs=$1; shift
exec perl -e '
  my $s = shift; my $pid = fork();
  if ($pid == 0) { exec @ARGV or exit 127 }
  local $SIG{ALRM} = sub { kill "TERM", $pid; sleep 2; kill "KILL", $pid; print STDERR "with_timeout: $s s esgotados\n"; exit 124 };
  alarm $s; waitpid($pid, 0); exit($? >> 8);
' "$secs" "$@"
