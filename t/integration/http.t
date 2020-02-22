#!/usr/bin/env perl

use strict;

use Carp;
use Cwd qw(abs_path realpath);
use File::Path qw(mkpath rmtree);
use File::Spec;
use Test::Simple tests => 4;

my $proftpd = $ENV{PROFTPD_TEST_BIN};
my $proftpd_opts = "-t";
my $tracing = "false";
if ($ENV{TEST_VERBOSE}) {
  $proftpd_opts = "-td10";
  $tracing = "true";
}

my ($cmd, $ex, $res);
my $bad_dns_url = "http://test.example.com?tracing=$tracing";
$cmd = "$proftpd $proftpd_opts -c '$bad_dns_url'";
$ex = undef;
eval { $res = run_cmd($cmd, 1) };
$ex = $@ if $@;
ok(defined($ex), "handled HTTP URL with bad hostname");

my $bad_port_url = "http://www.google.com:45678?tracing=$tracing";
$cmd = "$proftpd $proftpd_opts -c '$bad_port_url'";
$ex = undef;
eval { $res = run_cmd($cmd, 1) };
$ex = $@ if $@;
ok(defined($ex), "handled HTTP URL with bad port");

my $simple_url = "http://www.google.com/foo/bar/baz?tracing=$tracing";
$cmd = "$proftpd $proftpd_opts -c '$simple_url'";
$ex = undef;
eval { $res = run_cmd($cmd, 1) };
$ex = $@ if $@;
ok(defined($ex), "handled HTTP URL with no such file");

my $bad_scheme_url = "foo://www.google.com?tracing=$tracing";
$cmd = "$proftpd $proftpd_opts -c '$bad_scheme_url'";
$ex = undef;
eval { $res = run_cmd($cmd, 1) };
$ex = $@ if $@;
ok(defined($ex), "handled URL with unsupported scheme");

sub run_cmd {
  my $cmd = shift;
  my $check_exit_status = shift;
  $check_exit_status = 0 unless defined $check_exit_status;

  if ($ENV{TEST_VERBOSE}) {
    print STDOUT "# Executing: $cmd\n";
  }

  my @output = `$cmd > /dev/null`;
  my $exit_status = $?;

  if ($ENV{TEST_VERBOSE}) {
    print STDOUT "# Output: ", join('', @output), "\n";
  }

  if ($check_exit_status) {
    if ($? != 0) {
      croak("'$cmd' failed with exit code $?");
    }
  }

  return 1;
}
