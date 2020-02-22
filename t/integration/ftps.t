#!/usr/bin/env perl

use strict;

use Carp;
use Cwd qw(abs_path realpath);
use File::Path qw(mkpath rmtree);
use File::Spec;
use Test::Simple tests => 2;

my $proftpd = $ENV{PROFTPD_TEST_BIN};
my $proftpd_opts = "-t";
my $tracing = "false";
if ($ENV{TEST_VERBOSE}) {
  $proftpd_opts = "-td10";
  $tracing = "true";
}

my ($cmd, $ex, $res);
my $enoent_url = "ftps://demo:password\@test.rebex.nt/foo/bar/baz?tracing=$tracing";
$cmd = "$proftpd $proftpd_opts -c '$enoent_url'";
$ex = undef;
eval { $res = run_cmd($cmd, 1) };
$ex = $@ if $@;
ok(defined($ex), "handled FTPS URL with no such file");

# See:
#  https://stackoverflow.com/questions/7968703/is-there-a-public-ftp-server-to-test-upload-and-download
my $rebex_user = 'demo';
my $rebex_passwd = 'password';
my $simple_url = "ftps://$rebex_user:$rebex_passwd\@test.rebex.net/readme.txt?ssl_verify=false&tracing=$tracing";
$cmd = "$proftpd $proftpd_opts -c '$simple_url'";
$ex = undef;
eval { $res = run_cmd($cmd, 1) };
$ex = $@ if $@;
ok(defined($ex), "handled FTPS URL with simple text file");

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
