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
my $bad_dns_url = "https://test.example.com?tracing=$tracing";
$cmd = "$proftpd $proftpd_opts -c '$bad_dns_url'";
$ex = undef;
eval { $res = run_cmd($cmd, 1) };
$ex = $@ if $@;
ok(defined($ex), "handled HTTPS URL with bad hostname");

my $bad_port_url = "https://www.google.com:45678?tracing=$tracing";
$cmd = "$proftpd $proftpd_opts -c '$bad_port_url'";
$ex = undef;
eval { $res = run_cmd($cmd, 1) };
$ex = $@ if $@;
ok(defined($ex), "handled HTTPS URL with bad port");

my $simple_url = "https://www.google.com/foo/bar/baz?tracing=$tracing";
$cmd = "$proftpd $proftpd_opts -c '$simple_url'";
$ex = undef;
eval { $res = run_cmd($cmd, 1) };
$ex = $@ if $@;
ok(defined($ex), "handled HTTPS URL with no such file");

my $good_url = "https://raw.githubusercontent.com/proftpd/proftpd/master/sample-configurations/basic.conf?tracing=$tracing&ssl_verify=false";
$cmd = "$proftpd $proftpd_opts -c '$good_url'";
$ex = undef;
eval { $res = run_cmd($cmd, 1) };
$ex = $@ if $@;
ok(defined($ex), "handled HTTPS URL with good file");

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
