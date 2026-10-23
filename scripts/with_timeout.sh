#!/bin/sh
# Roda um comando com tempo limite (macOS não tem timeout(1)): with_timeout.sh <segundos> <comando...>
# Sai com 124 se o tempo estourou, como o timeout do coreutils. O comando roda num grupo de processos próprio e o
# grupo inteiro recebe TERM (e KILL 5 s depois): filhos como o Dolphin do oráculo não ficam órfãos.
secs=$1; shift
exec perl -e '
  use POSIX ();
  my $s = shift; my $pid = fork();
  if ($pid == 0) { POSIX::setpgid(0, 0); exec @ARGV or exit 127 }
  my $kill = sub { kill "TERM", -$pid; sleep 5; kill "KILL", -$pid; };
  local $SIG{ALRM} = sub { $kill->(); print STDERR "with_timeout: $s s esgotados\n"; exit 124 };
  local $SIG{INT} = sub { $kill->(); exit 130 };
  local $SIG{TERM} = sub { $kill->(); exit 143 };
  alarm $s; waitpid($pid, 0); my $st = $?; kill "KILL", -$pid; exit($st >> 8);
' "$secs" "$@"
